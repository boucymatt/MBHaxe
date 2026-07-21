#define HL_BSD
#define PATH_MAX 1024
#define CLOCK_THREAD_CPUTIME_ID 0
#define hl_sys_string hl_sys_string_posix
#define hl_sys_exe_path hl_sys_exe_path_posix
#define hl_sys_full_path hl_sys_full_path_posix

#ifndef MBHAXE_HASHLINK_SYS_SOURCE
#error "MBHAXE_HASHLINK_SYS_SOURCE must name the upstream HashLink sys binding"
#endif
#include MBHAXE_HASHLINK_SYS_SOURCE

#undef hl_sys_string
#undef hl_sys_exe_path
#undef hl_sys_full_path

/* Vita mount paths (for example app0:/data) are not POSIX paths.  newlib's
 * realpath() rejects them on-device, while HashLink's default fullPath
 * implementation is built entirely around realpath().  Heaps constructs its
 * asset filesystem before Main.main() and asks for app0:/.; returning NULL
 * there throws during static initialization, before Haxe diagnostics exist.
 * Canonicalize mount paths lexically and keep the upstream implementation for
 * ordinary POSIX paths. */
HL_PRIM vbyte *hl_sys_full_path(vbyte *path) {
	const char *source = (const char *)path;
	const char *colon;
	const char *cursor;
	char result[PATH_MAX];
	size_t prefix_length;
	size_t length;
	size_t floor;

	if (source == NULL)
		return NULL;
	colon = strchr(source, ':');
	if (colon == NULL || colon == source || colon - source > 15)
		return hl_sys_full_path_posix(path);

	prefix_length = (size_t)(colon - source) + 1;
	memcpy(result, source, prefix_length);
	length = prefix_length;
	cursor = colon + 1;
	if (*cursor == '/') {
		result[length++] = '/';
		while (*cursor == '/')
			cursor++;
	}
	floor = length;

	while (*cursor != '\0') {
		const char *segment = cursor;
		size_t segment_length;

		while (*cursor != '\0' && *cursor != '/')
			cursor++;
		segment_length = (size_t)(cursor - segment);

		if (segment_length == 1 && segment[0] == '.') {
			/* Skip current-directory components. */
		} else if (segment_length == 2 && segment[0] == '.' &&
			segment[1] == '.') {
			/* Pop one component without escaping the Vita mount. */
			if (length > floor) {
				if (result[length - 1] == '/')
					length--;
				while (length > floor && result[length - 1] != '/')
					length--;
			}
		} else if (segment_length != 0) {
			if (length > prefix_length && result[length - 1] != '/')
				result[length++] = '/';
			if (length + segment_length >= sizeof(result))
				return NULL;
			memcpy(result + length, segment, segment_length);
			length += segment_length;
		}

		while (*cursor == '/')
			cursor++;
	}

	result[length] = '\0';
	return (vbyte *)pstrdup(result, -1);
}

HL_PRIM vbyte *hl_sys_string(void) {
	return (vbyte *)USTR("PS Vita");
}

HL_PRIM vbyte *hl_sys_exe_path(void) {
	return (vbyte *)pstrdup("app0:/eboot.bin", -1);
}
