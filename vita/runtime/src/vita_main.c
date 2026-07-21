#include <hl.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/types.h>

#include <stdio.h>
#include <string.h>

#include "vita_boot_log.h"

#define MBHAXE_LOG_DIRECTORY "ux0:data/MBHaxe-MBU"
#define MBHAXE_LOG_PATH "ux0:data/MBHaxe-MBU/startup.log"
#define MBHAXE_FALLBACK_LOG_PATH "ux0:data/MBHaxe-startup.log"

#ifndef MBHAXE_APP_VERSION
#define MBHAXE_APP_VERSION "unknown"
#endif

/* Generated HLC root setup has a sizeable spill frame, and the game has deep
 * renderer/resource call chains.  Do not rely on the Vita loader's small
 * default main-thread stack. */
const SceSize sceUserMainThreadStackSize = 1024 * 1024;

/* Vita newlib otherwise reserves 128 MiB.  HashLink now owns a separate
 * 64 MiB kernel-backed GC arena, so keep the combined reservation unchanged
 * while preventing collector pages from entering newlib's malloc bins. */
unsigned int _newlib_heap_size_user = 64u * 1024u * 1024u;

extern int mbhaxe_hashlink_main(int argc, char **argv);
extern void hl_entry_point(void);
extern void hl_init_types(hl_module_context *context);
extern void hl_init_hashes(void);
extern void hl_init_roots(void);
extern void fun$init(void);

static const char *mbhaxe_log_path = MBHAXE_LOG_PATH;

static SceUID mbhaxe_open_log(int truncate) {
	int flags = SCE_O_WRONLY | SCE_O_CREAT;
	SceUID file;

	(void)sceIoMkdir(MBHAXE_LOG_DIRECTORY, 0777);
	flags |= truncate ? SCE_O_TRUNC : SCE_O_APPEND;
	file = sceIoOpen(mbhaxe_log_path, flags, 0666);
	if (file >= 0)
		return file;

	/* A bad/stale directory must not make the diagnostic mechanism silent. */
	mbhaxe_log_path = MBHAXE_FALLBACK_LOG_PATH;
	return sceIoOpen(mbhaxe_log_path, flags, 0666);
}

static void mbhaxe_write_all(SceUID file, const char *data, size_t size) {
	while (size != 0) {
		SceSSize written = sceIoWrite(file, data, size);
		if (written <= 0)
			return;
		data += written;
		size -= (size_t)written;
	}
}

void mbhaxe_vita_boot_log_reset(void) {
	static const char header[] =
		"MBHaxe Vita native startup " MBHAXE_APP_VERSION "\n";
	SceUID file;

	mbhaxe_log_path = MBHAXE_LOG_PATH;
	file = mbhaxe_open_log(1);
	if (file < 0)
		return;
	mbhaxe_write_all(file, header, sizeof(header) - 1);
	(void)sceIoSyncByFd(file, 0);
	(void)sceIoClose(file);
}

void mbhaxe_vita_boot_log_write(const char *message) {
	SceUID file;
	size_t length;

	if (message == NULL)
		message = "(null)";
	length = strlen(message);
	file = mbhaxe_open_log(0);
	if (file < 0)
		return;
	mbhaxe_write_all(file, message, length);
	if (length == 0 || message[length - 1] != '\n')
		mbhaxe_write_all(file, "\n", 1);
	(void)sceIoSyncByFd(file, 0);
	(void)sceIoClose(file);
}

/* HashLink's HLC main reports its safe-call exception through uprintf.  Route
 * those two messages (the exception and each stack frame) to persistent
 * storage as well as retaining the upstream stdout behavior. */
void mbhaxe_vita_uprintf(const uchar *format, const uchar *value) {
	char utf8_format[128];
	char utf8_value[1792];
	char line[2048];

	utostr(utf8_format, sizeof(utf8_format), format);
	utostr(utf8_value, sizeof(utf8_value), value);
	(void)snprintf(line, sizeof(line), utf8_format, utf8_value);
	mbhaxe_vita_boot_log_write(line);
	uprintf(format, value);
}

void mbhaxe_vita_logged_alloc_init(hl_alloc *allocator) {
	mbhaxe_vita_boot_log_write("Checkpoint: hl_alloc_init begin");
	hl_alloc_init(allocator);
	mbhaxe_vita_boot_log_write("Checkpoint: hl_alloc_init complete");
}

void mbhaxe_vita_logged_init_types(hl_module_context *context) {
	mbhaxe_vita_boot_log_write("Checkpoint: hl_init_types begin");
	hl_init_types(context);
	mbhaxe_vita_boot_log_write("Checkpoint: hl_init_types complete");
}

void mbhaxe_vita_logged_init_hashes(void) {
	mbhaxe_vita_boot_log_write("Checkpoint: hl_init_hashes begin");
	hl_init_hashes();
	mbhaxe_vita_boot_log_write("Checkpoint: hl_init_hashes complete");
}

void mbhaxe_vita_logged_init_roots(void) {
	mbhaxe_vita_boot_log_write("Checkpoint: hl_init_roots begin");
	hl_init_roots();
	mbhaxe_vita_boot_log_write("Checkpoint: hl_init_roots complete");
}

void mbhaxe_vita_logged_static_init(void) {
	mbhaxe_vita_boot_log_write("Checkpoint: fun$init begin");
	fun$init();
	mbhaxe_vita_boot_log_write("Checkpoint: fun$init complete");
}

/* This is the function selected by the wrapped HLC main.  An exception in
 * generated type/root/static initialization unwinds before the second line,
 * then HLC's safe-call reporter records the exact exception. */
void mbhaxe_vita_logged_entry_point(void) {
	mbhaxe_vita_boot_log_write("HashLink: entering generated entry point");
	hl_entry_point();
	mbhaxe_vita_boot_log_write("HashLink: generated entry point returned");
}

int main(int argc, char **argv) {
	int result;

	mbhaxe_vita_boot_log_reset();
	mbhaxe_vita_boot_log_write("Native main: entered");
	result = mbhaxe_hashlink_main(argc, argv);
	if (result == 0)
		mbhaxe_vita_boot_log_write("Native main: HashLink returned normally");
	else
		mbhaxe_vita_boot_log_write("Native main: HashLink returned an exception");
	return result;
}
