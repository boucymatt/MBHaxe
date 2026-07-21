#include <vitaGL.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vita_boot_log.h"
#include "vitagl_alloc_vita.h"

/*
 * The installed vitaGL archive tries newlib before any of its private pools
 * for vglMalloc/vglCalloc.  That puts short-lived shader and buffer metadata
 * in the same heap as HashLink's audio decoders.  Keep graphics allocations
 * in graphics-owned memory while a pool has room, with newlib retained as a
 * final compatibility fallback.
 *
 * GNU ld --wrap redirects calls made by the archive and by the vendored GLSL
 * override to these entry points.  vglFree and vglMallocUsableSize are already
 * pool-aware, so they intentionally continue to use vitaGL's real versions.
 */

static void *mbhaxe_vitagl_pool_malloc(uint32_t size) {
	static const vglMemType pools[] = {
		VGL_MEM_RAM,
		VGL_MEM_BUDGET,
		VGL_MEM_VRAM,
		VGL_MEM_SLOW,
	};
	uint32_t index;
	void *result;

	for (index = 0; index < sizeof(pools) / sizeof(pools[0]); ++index) {
		/* An uninitialized or unavailable pool has a total size of zero.
		 * Avoid entering its allocator, particularly PHYCONT_ON_DEMAND. */
		if (vglMemTotal(pools[index]) == 0)
			continue;
		result = vglAlloc(size, pools[index]);
		if (result != NULL)
			return result;
	}

	return vglAlloc(size, VGL_MEM_EXTERNAL);
}

void *__wrap_vglMalloc(uint32_t size) {
	return mbhaxe_vitagl_pool_malloc(size);
}

void *__wrap_vglCalloc(uint32_t count, uint32_t size) {
	uint32_t total;
	void *result;

	if (size != 0 && count > UINT32_MAX / size)
		return NULL;
	total = count * size;
	result = mbhaxe_vitagl_pool_malloc(total);
	if (result != NULL)
		memset(result, 0, total);
	return result;
}

void *__wrap_vglRealloc(void *ptr, uint32_t size) {
	size_t old_size;
	void *result;

	if (ptr == NULL)
		return mbhaxe_vitagl_pool_malloc(size);
	if (size == 0) {
		vglFree(ptr);
		return NULL;
	}

	old_size = vglMallocUsableSize(ptr);
	if (old_size >= size)
		return ptr;

	result = mbhaxe_vitagl_pool_malloc(size);
	if (result == NULL)
		return NULL;
	if (old_size != 0)
		memcpy(result, ptr, old_size);
	vglFree(ptr);
	return result;
}

void mbhaxe_vitagl_allocator_log_state(void) {
	char line[224];

	(void)snprintf(line, sizeof(line),
		"vitaGL allocator: pool-first active; "
		"RAM=%lu/%lu BUDGET=%lu/%lu VRAM=%lu/%lu SLOW=%lu/%lu free/total",
		(unsigned long)vglMemFree(VGL_MEM_RAM),
		(unsigned long)vglMemTotal(VGL_MEM_RAM),
		(unsigned long)vglMemFree(VGL_MEM_BUDGET),
		(unsigned long)vglMemTotal(VGL_MEM_BUDGET),
		(unsigned long)vglMemFree(VGL_MEM_VRAM),
		(unsigned long)vglMemTotal(VGL_MEM_VRAM),
		(unsigned long)vglMemFree(VGL_MEM_SLOW),
		(unsigned long)vglMemTotal(VGL_MEM_SLOW));
	mbhaxe_vita_boot_log_write(line);
}
