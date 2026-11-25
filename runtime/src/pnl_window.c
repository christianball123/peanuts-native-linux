// Peanuts Native for Linux: ANativeWindow over the SDL3 game window. peanuts-run makes the window on the main thread and keeps its pixel size
// here (pnl_window_set_size on every resize), so the replay's present thread never calls into SDL's window state except to make its surface.
#include "pnl.h"
#include <vulkan/vulkan_core.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <stdatomic.h>
#include <stdlib.h>

struct ANativeWindow { SDL_Window *win; _Atomic int refs; _Atomic int32_t w, h; _Atomic float hz; };

ANativeWindow *pnl_window_wrap(SDL_Window *win) {
    ANativeWindow *w = calloc(1, sizeof *w); if (!w) return NULL;
    w->win = win; atomic_store(&w->refs, 1);
    int pw = 0, ph = 0; SDL_GetWindowSizeInPixels(win, &pw, &ph); pnl_window_set_size(w, pw, ph);
    const SDL_DisplayMode *m = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(win));
    atomic_store(&w->hz, m ? m->refresh_rate : 0.f);
    return w;
}
void pnl_window_set_size(ANativeWindow *w, int pw, int ph) { if (!w) return; atomic_store(&w->w, pw > 0 ? pw : 1); atomic_store(&w->h, ph > 0 ? ph : 1); }
SDL_Window *pnl_window_sdl(ANativeWindow *w) { return w ? w->win : NULL; }

void ANativeWindow_acquire(ANativeWindow *w) { if (w) atomic_fetch_add(&w->refs, 1); }
void ANativeWindow_release(ANativeWindow *w) { if (w && atomic_fetch_sub(&w->refs, 1) == 1) free(w); }   // the SDL window itself is peanuts-run's
int32_t ANativeWindow_getWidth(ANativeWindow *w) { return w ? atomic_load(&w->w) : 0; }
int32_t ANativeWindow_getHeight(ANativeWindow *w) { return w ? atomic_load(&w->h) : 0; }
int32_t ANativeWindow_getFormat(ANativeWindow *w) { (void)w; return WINDOW_FORMAT_RGBA_8888; }
int32_t ANativeWindow_setBuffersGeometry(ANativeWindow *w, int32_t width, int32_t height, int32_t format) { (void)w; (void)width; (void)height; (void)format; return 0; }
int32_t ANativeWindow_setFrameRate(ANativeWindow *w, float rate, int8_t compat) { (void)w; (void)rate; (void)compat; return 0; }   // the compositor picks the rate
float pnl_window_refresh_hz(ANativeWindow *w) { return w ? atomic_load(&w->hz) : 0.f; }

const char *const *pnl_window_instance_extensions(uint32_t *n) {
    Uint32 c = 0; const char *const *e = SDL_Vulkan_GetInstanceExtensions(&c);
    *n = e ? c : 0; return e;
}
int pnl_window_create_surface(ANativeWindow *w, void *instance, uint64_t *surface) {
    if (!w || !w->win) return -1;
    VkSurfaceKHR s = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(w->win, (VkInstance)instance, NULL, &s)) { fprintf(stderr, "[RED] SDL_Vulkan_CreateSurface: %s\n", SDL_GetError()); return -1; }
    *surface = (uint64_t)s; return 0;
}
