#include <SDL2/SDL.h>
#include <vitaGL.h>
#include <hl.h>

#include <stdio.h>
#include <stdlib.h>

#include "gl_vita.h"
#include "vita_boot_log.h"
#include "vitagl_alloc_vita.h"

typedef struct {
	int width;
	int height;
	int x;
	int y;
	float opacity;
	SDL_bool grabbed;
} vita_sdl_window;

static int vita_gl_ready;
static int vita_cursor_visible;

static int vita_sdl_init(Uint32 flags) {
	int result;
	/* VitaGL owns GXM and the display. SDL remains responsible for controller,
	 * event, timer and audio-device support. */
	mbhaxe_vita_boot_log_write("SDL: initialization begin");
	result = SDL_Init(flags & ~SDL_INIT_VIDEO);
	mbhaxe_vita_boot_log_write(result == 0
		? "SDL: initialization complete"
		: "SDL: initialization failed");
	return result;
}

static int vita_sdl_gl_set_attribute(SDL_GLattr attribute, int value) {
	(void)attribute;
	(void)value;
	return 0;
}

static SDL_Window *vita_sdl_create_window(const char *title, int x, int y,
	int width, int height, Uint32 flags) {
	vita_sdl_window *window;
	(void)title;
	(void)flags;
	mbhaxe_vita_boot_log_write("Window: creation begin");
	if (!vita_gl_ready) {
		/* Dynamic vertices, uniforms, and textures are written immediately before
		 * GPU submission. Keep vitaGL's mapped RAM uncached so those writes cannot
		 * remain in the CPU cache while GXM consumes the command stream. */
		vglUseCachedMem(GL_FALSE);
		vglUseTripleBuffering(GL_FALSE);
		/* The 01.18 core reaches SceShaccCg through glLinkProgram while the
		 * compiler is optimizing the first-frame sprite vertex shader. Avoid
		 * the compiler's aggressive O3 transforms and unsafe arithmetic
		 * shortcuts; the generated GXM program remains fully optimized enough
		 * for this fixed-function-style UI workload. */
		vglSetupRuntimeShaderCompiler(SHARK_OPT_SAFE, SHARK_DISABLE,
			SHARK_DISABLE, SHARK_DISABLE);
		/* Heaps can return to the display target several times while composing
		 * its offscreen passes.  Two scenes let the first frame finish but leave
		 * GXM with no valid display scene for the following bitmap draw.  Reserve
		 * vitaGL's documented maximum; the opt-in glFinish diagnostic also needs
		 * this capacity because every finish ends and restarts a scene. */
		vglSetupDisplayRenderTarget(8);
		/* vitaGL returns whether it had to fall back to a smaller display
		 * resolution, not whether initialization succeeded.  At the Vita's
		 * native 960x544 resolution the normal return value is GL_FALSE. */
		mbhaxe_vita_boot_log_write("vitaGL: initialization begin");
		/* Keep enough USER_RW memory outside vitaGL for HashLink to grow while
		 * indexing missions and constructing the preview world. vitaGL can spill
		 * graphics allocations into its large CDRAM and PHYCONT pools. */
		(void)vglInitExtended(8 * 1024 * 1024, 960, 544,
			40 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);
		mbhaxe_vita_boot_log_write("vitaGL: initialization complete");
		mbhaxe_vita_boot_log_write(
			"vitaGL: display render target supports 8 scenes/frame");
		mbhaxe_vita_boot_log_write(
			"vitaGL: first/second-frame scene tracing active");
		mbhaxe_vita_boot_log_write(
			"vitaGL: boot splash disabled; single GXM context active");
		mbhaxe_vita_boot_log_write(
			"vitaGL: shader compiler safe mode O1; fast math/precision/int disabled");
		mbhaxe_vita_boot_log_write(
			"vitaGL: scalar uniform copies active; CLIB uniform memcpy disabled");
		mbhaxe_vita_boot_log_write(
			"vitaGL: sampler-size uniforms resolved and range-checked");
		mbhaxe_vita_boot_log_write(
			"vitaGL: uniform bindings validated before GXM parameter queries");
		mbhaxe_vita_boot_log_write(
			"vitaGL: uniform uploads clamped to linked buffer bounds");
		mbhaxe_vita_boot_log_write(
			"vitaGL: failed O1 shaders retried at O0; null links rejected");
		mbhaxe_vita_boot_log_write(
			"vitaGL: display queue uses front-to-back GXM sync order");
		mbhaxe_vita_boot_log_write(
			"vitaGL: per-frame persistent logging throttled");
		mbhaxe_vita_boot_log_write(
			"vitaGL: Vita mesh batches capped to 256 uniform vectors");
		mbhaxe_vita_boot_log_write(
			"vitaGL: empty pre-draw finishes deferred within GXM scene budget");
		mbhaxe_vita_boot_log_write(
			"vitaGL: first-two-frame framebuffer fingerprint active");
		mbhaxe_vitagl_allocator_log_state();
		vita_gl_ready = 1;
	}
	window = calloc(1, sizeof(*window));
	if (window == NULL) {
		mbhaxe_vita_boot_log_write("Window: allocation failed");
		return NULL;
	}
	window->width = 960;
	window->height = 544;
	window->x = x;
	window->y = y;
	window->opacity = 1.0f;
	(void)width;
	(void)height;
	mbhaxe_vita_boot_log_write("Window: creation complete");
	return (SDL_Window *)window;
}

static SDL_GLContext vita_sdl_gl_create_context(SDL_Window *window) {
	mbhaxe_vita_boot_log_write("Window: GL context requested");
	return (SDL_GLContext)window;
}

static int vita_sdl_display_mode(int display, SDL_DisplayMode *mode) {
	(void)display;
	if (mode != NULL) {
		SDL_zero(*mode);
		mode->format = SDL_PIXELFORMAT_ABGR8888;
		mode->w = 960;
		mode->h = 544;
		mode->refresh_rate = 60;
	}
	return 0;
}

static int vita_sdl_get_display_mode(int display, int index,
	SDL_DisplayMode *mode) {
	if (index != 0)
		return -1;
	return vita_sdl_display_mode(display, mode);
}

static void vita_sdl_get_window_size(SDL_Window *window, int *width,
	int *height) {
	vita_sdl_window *vita_window = (vita_sdl_window *)window;
	if (width != NULL)
		*width = vita_window == NULL ? 960 : vita_window->width;
	if (height != NULL)
		*height = vita_window == NULL ? 544 : vita_window->height;
}

static void vita_sdl_get_window_position(SDL_Window *window, int *x, int *y) {
	vita_sdl_window *vita_window = (vita_sdl_window *)window;
	if (x != NULL)
		*x = vita_window == NULL ? 0 : vita_window->x;
	if (y != NULL)
		*y = vita_window == NULL ? 0 : vita_window->y;
}

static void vita_sdl_set_window_position(SDL_Window *window, int x, int y) {
	vita_sdl_window *vita_window = (vita_sdl_window *)window;
	if (vita_window != NULL) {
		vita_window->x = x;
		vita_window->y = y;
	}
}

static void vita_sdl_set_window_size(SDL_Window *window, int width, int height) {
	(void)window;
	(void)width;
	(void)height;
}

static int vita_sdl_get_window_opacity(SDL_Window *window, float *opacity) {
	vita_sdl_window *vita_window = (vita_sdl_window *)window;
	if (opacity != NULL)
		*opacity = vita_window == NULL ? 1.0f : vita_window->opacity;
	return 0;
}

static int vita_sdl_set_window_opacity(SDL_Window *window, float opacity) {
	vita_sdl_window *vita_window = (vita_sdl_window *)window;
	if (vita_window != NULL)
		vita_window->opacity = opacity;
	return 0;
}

static void vita_sdl_set_window_grab(SDL_Window *window, SDL_bool grabbed) {
	vita_sdl_window *vita_window = (vita_sdl_window *)window;
	if (vita_window != NULL)
		vita_window->grabbed = grabbed;
}

static SDL_bool vita_sdl_get_window_grab(SDL_Window *window) {
	vita_sdl_window *vita_window = (vita_sdl_window *)window;
	return vita_window == NULL ? SDL_FALSE : vita_window->grabbed;
}

static void vita_sdl_swap_window(SDL_Window *window) {
	static uint32_t present_sequence;
	char line[96];
	int trace_present;

	(void)window;
	++present_sequence;
	mbhaxe_vita_gl_present_begin(present_sequence);
	/* The persistent logger opens, syncs, and closes the file for each line so
	 * it remains useful after a crash. Logging twice per frame indefinitely,
	 * however, turns storage latency into frame and audio stalls. */
	trace_present = present_sequence <= 8 || present_sequence % 300 == 0;
	if (trace_present) {
		(void)snprintf(line, sizeof(line), "GLFRAME present begin seq=%lu",
			(unsigned long)present_sequence);
		mbhaxe_vita_boot_log_write(line);
	}
	vglSwapBuffers(GL_FALSE);
	if (trace_present) {
		(void)snprintf(line, sizeof(line), "GLFRAME present complete seq=%lu",
			(unsigned long)present_sequence);
		mbhaxe_vita_boot_log_write(line);
	}
}

static void vita_sdl_destroy_window(SDL_Window *window) {
	free(window);
}

static int vita_sdl_show_cursor(int toggle) {
	if (toggle == SDL_QUERY)
		return vita_cursor_visible ? SDL_ENABLE : SDL_DISABLE;
	vita_cursor_visible = toggle == SDL_ENABLE;
	return vita_cursor_visible ? SDL_ENABLE : SDL_DISABLE;
}

/* Heaps enumerates controllers during Pad.wait(), while SDL also leaves an
 * initial SDL_CONTROLLERDEVICEADDED event queued for the same device.  If
 * that event is forwarded, Heaps closes the already-open controller and
 * immediately opens it again.  Consume only add events whose instance is
 * already open; genuine hot-plug events continue through unchanged. */
static int vita_sdl_poll_event(SDL_Event *event) {
	if (event == NULL)
		return SDL_PollEvent(NULL);

	while (SDL_PollEvent(event) != 0) {
		if (event->type == SDL_CONTROLLERDEVICEADDED) {
			int device_index = event->cdevice.which;
			SDL_JoystickID instance_id =
				SDL_JoystickGetDeviceInstanceID(device_index);

			if (instance_id >= 0 &&
				SDL_GameControllerFromInstanceID(instance_id) != NULL) {
				mbhaxe_vita_boot_log_write(
					"SDL: ignored duplicate controller-added event");
				continue;
			}
		}
		return 1;
	}
	return 0;
}

static int vita_sdl_message_box(Uint32 flags, const char *title,
	const char *message, SDL_Window *window) {
	(void)flags;
	(void)window;
	printf("%s: %s\n", title == NULL ? "MBHaxe" : title,
		message == NULL ? "" : message);
	return 0;
}

static int vita_sdl_display_bounds(int display, SDL_Rect *bounds) {
	(void)display;
	if (bounds != NULL) {
		bounds->x = 0;
		bounds->y = 0;
		bounds->w = 960;
		bounds->h = 544;
	}
	return 0;
}

#define SDL_Init vita_sdl_init
#define SDL_PollEvent vita_sdl_poll_event
#define SDL_GL_SetAttribute vita_sdl_gl_set_attribute
#define SDL_CreateWindow vita_sdl_create_window
#define SDL_GL_CreateContext vita_sdl_gl_create_context
#define SDL_GL_GetDrawableSize vita_sdl_get_window_size
#define SDL_GL_SetSwapInterval(interval) (0)
#define SDL_GL_SwapWindow vita_sdl_swap_window
#define SDL_GL_MakeCurrent(window, context) (0)
#define SDL_GL_DeleteContext(context) ((void)0)
#define SDL_GetCurrentDisplayMode vita_sdl_display_mode
#define SDL_GetDesktopDisplayMode vita_sdl_display_mode
#define SDL_GetDisplayMode vita_sdl_get_display_mode
#define SDL_GetNumDisplayModes(display) (1)
#define SDL_GetNumVideoDisplays() (1)
#define SDL_GetWindowDisplayIndex(window) (0)
#define SDL_SetWindowDisplayMode(window, mode) (0)
#define SDL_SetWindowFullscreen(window, flags) (0)
#define SDL_SetWindowTitle(window, title) ((void)0)
#define SDL_SetWindowPosition vita_sdl_set_window_position
#define SDL_GetWindowPosition vita_sdl_get_window_position
#define SDL_SetWindowSize vita_sdl_set_window_size
#define SDL_GetWindowSize vita_sdl_get_window_size
#define SDL_SetWindowMinimumSize(window, width, height) ((void)0)
#define SDL_SetWindowMaximumSize(window, width, height) ((void)0)
#define SDL_GetWindowMinimumSize vita_sdl_get_window_size
#define SDL_GetWindowMaximumSize vita_sdl_get_window_size
#define SDL_GetWindowOpacity vita_sdl_get_window_opacity
#define SDL_SetWindowOpacity vita_sdl_set_window_opacity
#define SDL_MaximizeWindow(window) ((void)0)
#define SDL_MinimizeWindow(window) ((void)0)
#define SDL_RestoreWindow(window) ((void)0)
#define SDL_ShowWindow(window) ((void)0)
#define SDL_HideWindow(window) ((void)0)
#define SDL_DestroyWindow vita_sdl_destroy_window
#define SDL_SetRelativeMouseMode(enabled) (0)
#define SDL_GetRelativeMouseMode() SDL_FALSE
#define SDL_WarpMouseGlobal(x, y) (0)
#define SDL_WarpMouseInWindow(window, x, y) ((void)0)
#define SDL_SetWindowGrab vita_sdl_set_window_grab
#define SDL_GetWindowGrab vita_sdl_get_window_grab
#define SDL_ShowSimpleMessageBox vita_sdl_message_box
#define SDL_GetDisplayBounds vita_sdl_display_bounds
#define SDL_GetDisplayName(display) "PlayStation Vita"
#define SDL_ShowCursor vita_sdl_show_cursor
#define SDL_CreateColorCursor(surface, x, y) ((SDL_Cursor *)1)
#define SDL_CreateSystemCursor(id) ((SDL_Cursor *)1)
#define SDL_FreeCursor(cursor) ((void)0)
#define SDL_SetCursor(cursor) ((void)0)
#define SDL_SetClipboardText(text) (0)
#define SDL_GetClipboardText() SDL_strdup("")
#define hl_to_utf16(text) ((const char *)(hl_to_utf16)(text))

#undef HL_NAME
#ifndef MBHAXE_HASHLINK_SDL_SOURCE
#error "MBHAXE_HASHLINK_SDL_SOURCE must name the upstream HashLink SDL binding"
#endif
#include MBHAXE_HASHLINK_SDL_SOURCE
#undef SDL_PollEvent
