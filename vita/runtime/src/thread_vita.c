#include <pthread.h>
#include <sched.h>
#include <psp2/kernel/threadmgr.h>

static int vita_pthread_setname(pthread_t thread, const char *name) {
	(void)thread;
	(void)name;
	return 0;
}

#define hl_thread_id hl_thread_id_posix
#define pthread_yield sched_yield
#define pthread_setname_np vita_pthread_setname

#ifndef MBHAXE_HASHLINK_THREAD_SOURCE
#error "MBHAXE_HASHLINK_THREAD_SOURCE must name the upstream HashLink thread binding"
#endif
#include MBHAXE_HASHLINK_THREAD_SOURCE

#undef hl_thread_id

HL_PRIM int hl_thread_id(void) {
	return sceKernelGetThreadId();
}
