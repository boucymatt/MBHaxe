#ifndef MBHAXE_VITA_SYS_MMAN_H
#define MBHAXE_VITA_SYS_MMAN_H

#include <errno.h>
#include <stddef.h>
#include <malloc.h>

#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4

#define MAP_PRIVATE   2
#define MAP_ANONYMOUS 0x20
#define MAP_ANON      MAP_ANONYMOUS
#define MAP_FAILED    ((void *)-1)

/*
 * Keep HashLink GC mappings out of newlib's reusable free lists. Both observed
 * failures had graphics-shaped values in malloc bin metadata after pages had
 * crossed this ownership boundary; the next unrelated free then crashed in
 * _free_r.
 *
 * Keep released mappings owned by this shim and recycle only exact-size pages.
 * The bookkeeping lives in BSS instead of the pages themselves, so page data
 * can never corrupt the recycle table. 4096 entries is above the number of
 * 64 KiB pages that can fit in the Vita process heap.
 */
#define MBHAXE_VITA_MMAP_BLOCK_CAPACITY 4096u

typedef enum {
	MBHAXE_VITA_MMAP_UNUSED = 0,
	MBHAXE_VITA_MMAP_ACTIVE,
	MBHAXE_VITA_MMAP_QUARANTINED
} mbhaxe_vita_mmap_state;

typedef struct {
	void *address;
	size_t length;
	unsigned char state;
} mbhaxe_vita_mmap_block;

static mbhaxe_vita_mmap_block
	mbhaxe_vita_mmap_blocks[MBHAXE_VITA_MMAP_BLOCK_CAPACITY];
static volatile unsigned int mbhaxe_vita_mmap_lock_word;

static inline void mbhaxe_vita_mmap_lock(void) {
	while (__sync_lock_test_and_set(&mbhaxe_vita_mmap_lock_word, 1u) != 0u) {
	}
}

static inline void mbhaxe_vita_mmap_unlock(void) {
	__sync_lock_release(&mbhaxe_vita_mmap_lock_word);
}

/*
 * HashLink uses mmap to obtain 64 KiB-aligned GC pages. Vita user programs
 * do not have mmap, while newlib's aligned allocator provides the property
 * the collector actually requires for an AOT executable. Requested addresses
 * are only hints in HashLink and every returned page is 64 KiB aligned.
 */
static inline void *mmap(void *address, size_t length, int protection,
	int flags, int file, long offset) {
	void *result = NULL;
	unsigned int free_slot = MBHAXE_VITA_MMAP_BLOCK_CAPACITY;
	unsigned int i;
	(void)address;
	(void)protection;
	(void)flags;
	(void)file;
	(void)offset;

	if (length == 0u) {
		errno = EINVAL;
		return MAP_FAILED;
	}

	mbhaxe_vita_mmap_lock();
	for (i = 0u; i < MBHAXE_VITA_MMAP_BLOCK_CAPACITY; ++i) {
		mbhaxe_vita_mmap_block *block = &mbhaxe_vita_mmap_blocks[i];
		if (block->state == MBHAXE_VITA_MMAP_QUARANTINED &&
			block->length == length) {
			block->state = MBHAXE_VITA_MMAP_ACTIVE;
			result = block->address;
			break;
		}
		if (free_slot == MBHAXE_VITA_MMAP_BLOCK_CAPACITY &&
			block->state == MBHAXE_VITA_MMAP_UNUSED)
			free_slot = i;
	}

	if (result != NULL) {
		mbhaxe_vita_mmap_unlock();
		return result;
	}

	if (free_slot == MBHAXE_VITA_MMAP_BLOCK_CAPACITY) {
		mbhaxe_vita_mmap_unlock();
		errno = ENOMEM;
		return MAP_FAILED;
	}

	result = memalign(1u << 16, length);
	if (result == NULL) {
		mbhaxe_vita_mmap_unlock();
		return MAP_FAILED;
	}

	mbhaxe_vita_mmap_blocks[free_slot].address = result;
	mbhaxe_vita_mmap_blocks[free_slot].length = length;
	mbhaxe_vita_mmap_blocks[free_slot].state = MBHAXE_VITA_MMAP_ACTIVE;
	mbhaxe_vita_mmap_unlock();
	return result;
}

static inline int munmap(void *address, size_t length) {
	unsigned int i;

	/* Preserve free(NULL)-style behavior used by a few generic paths. */
	if (address == NULL && length == 0u)
		return 0;
	if (address == NULL || length == 0u) {
		errno = EINVAL;
		return -1;
	}

	mbhaxe_vita_mmap_lock();
	for (i = 0u; i < MBHAXE_VITA_MMAP_BLOCK_CAPACITY; ++i) {
		mbhaxe_vita_mmap_block *block = &mbhaxe_vita_mmap_blocks[i];
		if (block->address != address)
			continue;
		if (block->state != MBHAXE_VITA_MMAP_ACTIVE ||
			block->length != length) {
			mbhaxe_vita_mmap_unlock();
			errno = EINVAL;
			return -1;
		}
		block->state = MBHAXE_VITA_MMAP_QUARANTINED;
		mbhaxe_vita_mmap_unlock();
		return 0;
	}
	mbhaxe_vita_mmap_unlock();
	errno = EINVAL;
	return -1;
}

#endif
