#ifndef MBHAXE_VITA_BOOT_LOG_H
#define MBHAXE_VITA_BOOT_LOG_H

/* This logger deliberately bypasses HashLink and libc FILE buffering.  It is
 * safe to use before hl_global_init() and while reporting an uncaught HLC
 * exception. */
void mbhaxe_vita_boot_log_reset(void);
void mbhaxe_vita_boot_log_write(const char *message);

#endif
