// Peanuts Native for Linux: ANativeWindow is the game window (an SDL3 window made by peanuts-run). The replay's present
// (vka_present.c) makes its VkSurfaceKHR through pnl_window_create_surface instead of vkCreateAndroidSurfaceKHR.
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ANativeWindow ANativeWindow;
void ANativeWindow_acquire(ANativeWindow *w);
void ANativeWindow_release(ANativeWindow *w);
int32_t ANativeWindow_getWidth(ANativeWindow *w);
int32_t ANativeWindow_getHeight(ANativeWindow *w);
int32_t ANativeWindow_getFormat(ANativeWindow *w);
int32_t ANativeWindow_setBuffersGeometry(ANativeWindow *w, int32_t width, int32_t height, int32_t format);
int32_t ANativeWindow_setFrameRate(ANativeWindow *w, float rate, int8_t compat);
enum { ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT = 0, ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_FIXED_SOURCE = 1 };
enum { WINDOW_FORMAT_RGBA_8888 = 1, WINDOW_FORMAT_RGBX_8888 = 2, WINDOW_FORMAT_RGB_565 = 4 };

// ---- Linux additions (pnl_window.c) ----
// The Vulkan instance extensions the window system needs (SDL_Vulkan_GetInstanceExtensions); NULL with *n = 0 without a window system
const char *const *pnl_window_instance_extensions(uint32_t *n);
// A VkSurfaceKHR for the window (VkInstance and VkSurfaceKHR passed as their handle values); 0 = made
int pnl_window_create_surface(ANativeWindow *w, void *instance, uint64_t *surface);
// The window's refresh rate in Hz (0 unknown)
float pnl_window_refresh_hz(ANativeWindow *w);
#ifdef __cplusplus
}
#endif
