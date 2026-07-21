#define HL_NAME(name) uv_##name
#include <hl.h>

typedef struct _uv_handle uv_handle;
typedef struct _uv_loop uv_loop;

static int vita_uv_loop;

HL_PRIM void HL_NAME(close_handle)(uv_handle *handle, vclosure *callback) {
	(void)handle;
	(void)callback;
}

HL_PRIM uv_loop *HL_NAME(default_loop)(void) {
	return (uv_loop *)&vita_uv_loop;
}

HL_PRIM uv_handle *HL_NAME(fs_start_wrap)(uv_loop *loop, vclosure *callback,
	vbyte *path) {
	(void)loop;
	(void)callback;
	(void)path;
	return NULL;
}

HL_PRIM int HL_NAME(run)(uv_loop *loop, int mode) {
	(void)loop;
	(void)mode;
	return 0;
}
