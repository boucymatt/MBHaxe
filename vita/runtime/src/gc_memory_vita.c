#include <psp2/kernel/sysmem.h>
#include <psp2/types.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vita_boot_log.h"

/*
 * HashLink's collector requires every page returned by sys_alloc_align() to
 * be 64 KiB aligned.  Keeping those pages in newlib's malloc arena makes the
 * old dlmalloc bin lists share ownership with the collector and with
 * vitaGL's C++ shader preprocessor.  Allocate GC storage directly from the
 * Vita kernel instead.
 *
 * The first arena replaces the 64 MiB removed from newlib's default 128 MiB
 * heap.  Later arenas are deliberately smaller so a growing collector can
 * use spare memory without consuming a kernel UID for every 64 KiB page.
 */
#define MBHAXE_GC_PAGE_SIZE          (64u * 1024u)
#define MBHAXE_GC_PRIMARY_ARENA_SIZE (64u * 1024u * 1024u)
#define MBHAXE_GC_EXTRA_ARENA_SIZE   (16u * 1024u * 1024u)
#define MBHAXE_GC_MAX_ARENA_SIZE     (64u * 1024u * 1024u)
#define MBHAXE_GC_TOTAL_ARENA_LIMIT  (96u * 1024u * 1024u)
#define MBHAXE_GC_MAX_ARENAS         4u
#define MBHAXE_GC_MAX_PAGES          \
	(MBHAXE_GC_MAX_ARENA_SIZE / MBHAXE_GC_PAGE_SIZE)
#define MBHAXE_GC_BITMAP_WORDS       (MBHAXE_GC_MAX_PAGES / 32u)
#define MBHAXE_GC_MAX_ALLOCATIONS    4096u

typedef struct {
	void *base;
	SceUID uid;
	uint32_t page_count;
	uint32_t used[MBHAXE_GC_BITMAP_WORDS];
} mbhaxe_gc_arena;

typedef struct {
	void *address;
	uint32_t page_count;
	uint16_t arena_index;
	uint8_t active;
} mbhaxe_gc_allocation;

static mbhaxe_gc_arena mbhaxe_gc_arenas[MBHAXE_GC_MAX_ARENAS];
static mbhaxe_gc_allocation
	mbhaxe_gc_allocations[MBHAXE_GC_MAX_ALLOCATIONS];
static uint32_t mbhaxe_gc_arena_count;
static uint32_t mbhaxe_gc_total_arena_pages;
static uint32_t mbhaxe_gc_pages_in_use;
static uint32_t mbhaxe_gc_peak_pages;
static uint32_t mbhaxe_gc_next_peak_log =
	(8u * 1024u * 1024u) / MBHAXE_GC_PAGE_SIZE;
static volatile uint32_t mbhaxe_gc_lock_word;

static void mbhaxe_gc_lock(void) {
	while (__sync_lock_test_and_set(&mbhaxe_gc_lock_word, 1u) != 0u) {
	}
}

static void mbhaxe_gc_unlock(void) {
	__sync_lock_release(&mbhaxe_gc_lock_word);
}

static uint32_t mbhaxe_gc_align_pages(size_t size) {
	if (size == 0u || size > MBHAXE_GC_MAX_ARENA_SIZE)
		return 0u;
	return (uint32_t)((size + MBHAXE_GC_PAGE_SIZE - 1u) /
		MBHAXE_GC_PAGE_SIZE);
}

static int mbhaxe_gc_page_used(const mbhaxe_gc_arena *arena,
	uint32_t page) {
	return (arena->used[page >> 5] & (1u << (page & 31u))) != 0u;
}

static void mbhaxe_gc_mark_pages(mbhaxe_gc_arena *arena, uint32_t first,
	uint32_t count, int used) {
	uint32_t page;
	for (page = first; page < first + count; ++page) {
		uint32_t mask = 1u << (page & 31u);
		if (used)
			arena->used[page >> 5] |= mask;
		else
			arena->used[page >> 5] &= ~mask;
	}
}

static int mbhaxe_gc_find_pages(const mbhaxe_gc_arena *arena,
	uint32_t needed, uint32_t *first_page) {
	uint32_t first;
	if (needed == 0u || needed > arena->page_count)
		return 0;

	for (first = 0u; first + needed <= arena->page_count; ++first) {
		uint32_t page;
		for (page = 0u; page < needed; ++page) {
			if (mbhaxe_gc_page_used(arena, first + page))
				break;
		}
		if (page == needed) {
			*first_page = first;
			return 1;
		}
		first += page;
	}
	return 0;
}

static int mbhaxe_gc_create_arena(uint32_t needed_pages) {
	SceKernelAllocMemBlockOpt options;
	mbhaxe_gc_arena *arena;
	uint32_t requested_size;
	uint32_t minimum_size;
	uint32_t remaining_size;
	SceUID uid;
	void *base = NULL;

	if (mbhaxe_gc_arena_count >= MBHAXE_GC_MAX_ARENAS)
		return 0;

	minimum_size = needed_pages * MBHAXE_GC_PAGE_SIZE;
	remaining_size = MBHAXE_GC_TOTAL_ARENA_LIMIT -
		mbhaxe_gc_total_arena_pages * MBHAXE_GC_PAGE_SIZE;
	if (minimum_size > remaining_size)
		return 0;
	requested_size = mbhaxe_gc_arena_count == 0u
		? MBHAXE_GC_PRIMARY_ARENA_SIZE
		: MBHAXE_GC_EXTRA_ARENA_SIZE;
	if (requested_size < minimum_size)
		requested_size = minimum_size;
	if (requested_size > remaining_size)
		requested_size = remaining_size;

	memset(&options, 0, sizeof(options));
	options.size = sizeof(options);
	options.attr = SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_ALIGNMENT;
	options.alignment = MBHAXE_GC_PAGE_SIZE;

	/* If the preferred arena does not fit, retain useful operation with the
	 * largest power-of-two fallback that can still hold this request. */
	for (;;) {
		uid = sceKernelAllocMemBlock("MBHaxe HashLink GC",
			SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, requested_size, &options);
		if (uid >= 0)
			break;
		if (requested_size <= minimum_size)
			return 0;
		requested_size >>= 1;
		requested_size &= ~(MBHAXE_GC_PAGE_SIZE - 1u);
		if (requested_size < minimum_size)
			requested_size = minimum_size;
	}

	if (sceKernelGetMemBlockBase(uid, &base) < 0 || base == NULL ||
		((uintptr_t)base & (MBHAXE_GC_PAGE_SIZE - 1u)) != 0u) {
		(void)sceKernelFreeMemBlock(uid);
		return 0;
	}

	arena = &mbhaxe_gc_arenas[mbhaxe_gc_arena_count++];
	memset(arena, 0, sizeof(*arena));
	arena->base = base;
	arena->uid = uid;
	arena->page_count = requested_size / MBHAXE_GC_PAGE_SIZE;
	mbhaxe_gc_total_arena_pages += arena->page_count;

	if (requested_size >= MBHAXE_GC_PRIMARY_ARENA_SIZE)
		mbhaxe_vita_boot_log_write(
			"HashLink GC: 64 MiB kernel arena initialized");
	else if (requested_size >= 48u * 1024u * 1024u)
		mbhaxe_vita_boot_log_write(
			"HashLink GC: 48 MiB kernel arena initialized");
	else if (requested_size >= 32u * 1024u * 1024u)
		mbhaxe_vita_boot_log_write(
			"HashLink GC: 32 MiB kernel arena initialized");
	else if (requested_size >= 16u * 1024u * 1024u)
		mbhaxe_vita_boot_log_write(
			"HashLink GC: 16 MiB kernel arena initialized");
	else
		mbhaxe_vita_boot_log_write(
			"HashLink GC: fallback kernel arena initialized");
	return 1;
}

void *sys_alloc_align(int size, int align) {
	mbhaxe_gc_allocation *record = NULL;
	uint32_t pages;
	uint32_t arena_index;
	uint32_t first_page = 0u;
	void *result;
	uint32_t allocation_index;
	uint32_t peak_to_log = 0u;

	if (size <= 0 || align <= 0 || (align & (align - 1)) != 0 ||
		(uint32_t)align > MBHAXE_GC_PAGE_SIZE)
		return NULL;
	pages = mbhaxe_gc_align_pages((size_t)size);
	if (pages == 0u)
		return NULL;

	mbhaxe_gc_lock();
	for (allocation_index = 0u;
		allocation_index < MBHAXE_GC_MAX_ALLOCATIONS;
		++allocation_index) {
		if (!mbhaxe_gc_allocations[allocation_index].active) {
			record = &mbhaxe_gc_allocations[allocation_index];
			break;
		}
	}
	if (record == NULL) {
		mbhaxe_gc_unlock();
		mbhaxe_vita_boot_log_write(
			"HashLink GC: allocation table exhausted");
		return NULL;
	}

	for (;;) {
		for (arena_index = 0u; arena_index < mbhaxe_gc_arena_count;
			++arena_index) {
			if (mbhaxe_gc_find_pages(&mbhaxe_gc_arenas[arena_index],
				pages, &first_page))
				goto found;
		}
		if (!mbhaxe_gc_create_arena(pages)) {
			mbhaxe_gc_unlock();
			mbhaxe_vita_boot_log_write(
				"HashLink GC: kernel arena allocation failed");
			return NULL;
		}
	}

found:
	mbhaxe_gc_mark_pages(&mbhaxe_gc_arenas[arena_index], first_page,
		pages, 1);
	result = (void *)((uintptr_t)mbhaxe_gc_arenas[arena_index].base +
		(uintptr_t)first_page * MBHAXE_GC_PAGE_SIZE);
	record->address = result;
	record->page_count = pages;
	record->arena_index = (uint16_t)arena_index;
	record->active = 1u;
	mbhaxe_gc_pages_in_use += pages;
	if (mbhaxe_gc_pages_in_use > mbhaxe_gc_peak_pages) {
		mbhaxe_gc_peak_pages = mbhaxe_gc_pages_in_use;
		if (mbhaxe_gc_peak_pages >= mbhaxe_gc_next_peak_log) {
			peak_to_log = mbhaxe_gc_peak_pages;
			while (mbhaxe_gc_next_peak_log <= mbhaxe_gc_peak_pages)
				mbhaxe_gc_next_peak_log +=
					(8u * 1024u * 1024u) / MBHAXE_GC_PAGE_SIZE;
		}
	}
	mbhaxe_gc_unlock();

	/* mmap-compatible pages are always zero-filled, including recycled
	 * ranges from an earlier collection. */
	memset(result, 0, (size_t)pages * MBHAXE_GC_PAGE_SIZE);
	if (peak_to_log != 0u) {
		char line[96];
		(void)snprintf(line, sizeof(line),
			"HashLink GC: peak arena use %u MiB",
			(peak_to_log * MBHAXE_GC_PAGE_SIZE) / (1024u * 1024u));
		mbhaxe_vita_boot_log_write(line);
	}
	return result;
}

void sys_free_align(void *ptr, int size) {
	uint32_t pages;
	uint32_t index;

	if (ptr == NULL || size <= 0)
		return;
	pages = mbhaxe_gc_align_pages((size_t)size);
	if (pages == 0u)
		return;

	mbhaxe_gc_lock();
	for (index = 0u; index < MBHAXE_GC_MAX_ALLOCATIONS; ++index) {
		mbhaxe_gc_allocation *record = &mbhaxe_gc_allocations[index];
		mbhaxe_gc_arena *arena;
		uint32_t first_page;
		if (!record->active || record->address != ptr)
			continue;
		if (record->page_count != pages ||
			record->arena_index >= mbhaxe_gc_arena_count) {
			mbhaxe_gc_unlock();
			mbhaxe_vita_boot_log_write(
				"HashLink GC: rejected mismatched page release");
			return;
		}
		arena = &mbhaxe_gc_arenas[record->arena_index];
		first_page = (uint32_t)(((uintptr_t)ptr -
			(uintptr_t)arena->base) / MBHAXE_GC_PAGE_SIZE);
		mbhaxe_gc_mark_pages(arena, first_page, pages, 0);
		if (mbhaxe_gc_pages_in_use >= pages)
			mbhaxe_gc_pages_in_use -= pages;
		else
			mbhaxe_gc_pages_in_use = 0u;
		record->active = 0u;
		record->address = NULL;
		record->page_count = 0u;
		mbhaxe_gc_unlock();
		return;
	}
	mbhaxe_gc_unlock();
	mbhaxe_vita_boot_log_write(
		"HashLink GC: rejected unknown page release");
}
