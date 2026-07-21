/*
 * HLC generates `#include <hlc_main.c>` into marblegame.c.  The Vita runtime
 * include directory precedes HashLink's source directory, so this small
 * wrapper can keep boot diagnostics in the game repository while still using
 * HashLink's upstream main implementation.
 */
#include <hlc.h>

void mbhaxe_vita_logged_entry_point(void);
void mbhaxe_vita_uprintf(const uchar *format, const uchar *value);
void mbhaxe_vita_logged_alloc_init(hl_alloc *allocator);
void mbhaxe_vita_logged_init_types(hl_module_context *context);
void mbhaxe_vita_logged_init_hashes(void);
void mbhaxe_vita_logged_init_roots(void);
void mbhaxe_vita_logged_static_init(void);

#define main mbhaxe_hashlink_main
#define hl_entry_point mbhaxe_vita_logged_entry_point
#define uprintf mbhaxe_vita_uprintf
#include_next <hlc_main.c>
#undef uprintf
#undef hl_entry_point
#undef main

/* The generated hl_entry_point is below this include.  Redirect only its
 * initialization calls; the wrappers live in vita_main.c, where these macros
 * are not active and can therefore call the real functions without recursion.
 * HLC's Vita build always uses HL_MAKE, so the individual generated .c files
 * containing the real definitions are separate translation units. */
#ifdef HL_MAKE
#define hl_alloc_init mbhaxe_vita_logged_alloc_init
#define hl_init_types mbhaxe_vita_logged_init_types
#define hl_init_hashes mbhaxe_vita_logged_init_hashes
#define hl_init_roots mbhaxe_vita_logged_init_roots
#define fun$init mbhaxe_vita_logged_static_init
#endif
