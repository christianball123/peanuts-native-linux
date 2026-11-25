/* peanuts-bottom: shows the game's companion page on the AYN Thor's bottom screen.
 *
 * Started by peanuts-run once a companion package has loaded:
 *   ArmadaOS Game Mode:   armada-run-bottom -- peanuts-bottom <map>
 *   Desktop / elsewhere:  peanuts-bottom <map>   (opens on the second monitor)
 *
 * In Game Mode the bottom screen is a separate Xwayland display that ArmadaLive normally owns.
 * Like the Eden Duo (Fritchii) helper, this window loads /armada-bottom-env, opens on that display
 * with the X11 driver and marks itself as the bottom layer's Steam game (STEAM_GAME and
 * GAMESCOPECTRL_BASELAYER_APPID), raising itself above ArmadaLive.  It claims the screen again
 * every couple of seconds in case ArmadaLive comes back to the front.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <SDL3/SDL.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pnl_bottom.h"

#define LOG(...) do { fprintf(stderr, "peanuts-bottom: " __VA_ARGS__); fputc('\n', stderr); fflush(stderr); } while (0)

static volatile sig_atomic_t g_stop;
static void on_signal(int s) { (void)s; g_stop = 1; }

/* ---- ArmadaOS bottom display ---------------------------------------------------------------- */

static int armada_env_path(char *out, size_t n) {
    const char *o = getenv("PNL_BOTTOM_ENV");
    if (o && *o) { snprintf(out, n, "%s", o); return access(out, R_OK) == 0; }
    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (rt && *rt) { snprintf(out, n, "%s/armada-bottom-env", rt); if (access(out, R_OK) == 0) return 1; }
    snprintf(out, n, "/run/user/%u/armada-bottom-env", (unsigned)getuid());
    if (access(out, R_OK) == 0) return 1;
    snprintf(out, n, "/armada-bottom-env");
    return access(out, R_OK) == 0;
}

static int load_armada_env(void) {
    char path[512];
    if (!armada_env_path(path, sizeof path)) return 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[2048];
    while (fgets(line, sizeof line, f)) {
        size_t l = strlen(line);
        while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
        if (!l || line[0] == '#') continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = line, *val = eq + 1;
        if (!strncmp(key, "export ", 7)) key += 7;
        size_t vl = strlen(val);
        if (vl >= 2 && ((val[0] == '"' && val[vl - 1] == '"') || (val[0] == '\'' && val[vl - 1] == '\''))) {
            val[vl - 1] = 0; val++;
        }
        setenv(key, val, 1);
    }
    fclose(f);
    return 1;
}

/* libX11 is loaded at run time so the AppImage does not need it on Wayland-only setups. */
typedef struct _XDisplay XDisplay;
typedef unsigned long XID_t;
static struct {
    void *lib;
    XID_t (*InternAtom)(XDisplay *, const char *, int);
    int (*ChangeProperty)(XDisplay *, XID_t, XID_t, XID_t, int, int, const unsigned char *, int);
    int (*RaiseWindow)(XDisplay *, XID_t);
    int (*Flush)(XDisplay *);
} X;

static int load_x11(void) {
    X.lib = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
    if (!X.lib) { LOG("libX11.so.6 not found: %s", dlerror()); return 0; }
    X.InternAtom = (XID_t (*)(XDisplay *, const char *, int))dlsym(X.lib, "XInternAtom");
    X.ChangeProperty = (int (*)(XDisplay *, XID_t, XID_t, XID_t, int, int, const unsigned char *, int))dlsym(X.lib, "XChangeProperty");
    X.RaiseWindow = (int (*)(XDisplay *, XID_t))dlsym(X.lib, "XRaiseWindow");
    X.Flush = (int (*)(XDisplay *))dlsym(X.lib, "XFlush");
    return X.InternAtom && X.ChangeProperty && X.RaiseWindow && X.Flush;
}

/* The same Steam app id the Thor Eden Duo helper uses for the bottom layer. */
#define THOR_BOTTOM_APPID 852199u
#define XA_CARDINAL_ 6

static void claim_bottom(SDL_Window *win, int verbose) {
    if (!X.lib) return;
    SDL_PropertiesID p = SDL_GetWindowProperties(win);
    XDisplay *dpy = (XDisplay *)SDL_GetPointerProperty(p, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL);
    XID_t xid = (XID_t)SDL_GetNumberProperty(p, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    if (!dpy || !xid) return;
    uint32_t appid = THOR_BOTTOM_APPID;
    XID_t steam = X.InternAtom(dpy, "STEAM_GAME", 0);
    XID_t base = X.InternAtom(dpy, "GAMESCOPECTRL_BASELAYER_APPID", 0);
    X.ChangeProperty(dpy, xid, steam, XA_CARDINAL_, 32, 0, (const unsigned char *)&appid, 1);
    if (base) X.ChangeProperty(dpy, xid, base, XA_CARDINAL_, 32, 0, (const unsigned char *)&appid, 1);
    X.RaiseWindow(dpy, xid);
    X.Flush(dpy);
    if (verbose) LOG("claimed the bottom screen (X window 0x%lx)", (unsigned long)xid);
}

/* ---- main ------------------------------------------------------------------------------------- */

static int env_int(const char *k, int def) {
    const char *v = getenv(k);
    return (v && *v) ? atoi(v) : def;
}

static SDL_DisplayID pick_display(int armada) {
    int n = 0;
    SDL_DisplayID *ids = SDL_GetDisplays(&n);
    SDL_DisplayID pick = 0;
    if (!ids || n <= 0) { SDL_free(ids); return 0; }
    for (int i = 0; i < n; i++) {
        SDL_Rect b = {0};
        SDL_GetDisplayBounds(ids[i], &b);
        LOG("display %d: %s %dx%d at %d,%d", i, SDL_GetDisplayName(ids[i]) ? SDL_GetDisplayName(ids[i]) : "?", b.w, b.h, b.x, b.y);
    }
    int want = env_int("PNL_BOTTOM_DISPLAY", -1);
    if (want >= 0 && want < n) pick = ids[want];
    else if (armada) pick = ids[0];   /* the bottom Xwayland holds only the bottom screen */
    else if (n >= 2) {
        int game = env_int("PNL_GAME_DISPLAY", 0);
        if (game < 0 || game >= n) game = 0;
        /* Prefer the screen that is not the game's; among several, the smallest (the Thor's 3.9"). */
        long best = -1;
        for (int i = 0; i < n; i++) {
            if (i == game) continue;
            SDL_Rect b = {0};
            SDL_GetDisplayBounds(ids[i], &b);
            long area = (long)b.w * b.h;
            if (best < 0 || area < best) { best = area; pick = ids[i]; }
        }
    }
    SDL_free(ids);
    return pick;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <map>\n", argv[0]); return 2; }
    const char *map_path = argv[1];
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGHUP, on_signal);
    unsetenv("LD_PRELOAD");

    int fd = open(map_path, O_RDWR | O_CLOEXEC);
    if (fd < 0) { LOG("cannot open %s: %s", map_path, strerror(errno)); return 1; }
    void *map = mmap(NULL, PNB_MAP_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (map == MAP_FAILED) { LOG("mmap failed: %s", strerror(errno)); return 1; }
    pnb_header *h = (pnb_header *)map;
    if (h->magic != PNB_MAGIC || h->version != PNB_VERSION) { LOG("bad map header"); return 1; }
    PNB_STORE32(&h->helper_pid, (uint32_t)getpid());
    pid_t host = (pid_t)h->host_pid;

    /* Game Mode: armada-run-bottom may write the env file just as we start. */
    int armada = 0;
    int force_armada = env_int("PNL_BOTTOM_ARMADA", 0);
    for (int i = 0; i < (force_armada ? 50 : 1); i++) {
        if (load_armada_env()) { armada = 1; if (getenv("DISPLAY") && *getenv("DISPLAY")) break; }
        if (force_armada) usleep(100000);
    }
    if (force_armada) armada = 1;
    if (armada) {
        const char *d = getenv("DISPLAY");
        if (!d || !*d) { LOG("Game Mode bottom screen has no DISPLAY; giving up"); return PNB_EXIT_NO_SCREEN; }
        setenv("SDL_VIDEODRIVER", "x11", 1);
        load_x11();
        LOG("Game Mode bottom screen on DISPLAY=%s", d);
    }
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_APP_NAME, "Peanuts Native Companion");
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) { LOG("SDL_Init: %s", SDL_GetError()); return 1; }
    if (!armada && !strcmp(SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "", "x11")) load_x11();

    SDL_DisplayID disp = pick_display(armada);
    if (!disp) { LOG("no second screen found"); SDL_Quit(); return PNB_EXIT_NO_SCREEN; }
    SDL_Rect b = {0};
    SDL_GetDisplayBounds(disp, &b);
    if (b.w <= 0 || b.h <= 0) { b.w = 1240; b.h = 1080; }

    SDL_PropertiesID cp = SDL_CreateProperties();
    SDL_SetStringProperty(cp, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Peanuts Native Companion");
    SDL_SetNumberProperty(cp, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(disp));
    SDL_SetNumberProperty(cp, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(disp));
    SDL_SetNumberProperty(cp, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, b.w);
    SDL_SetNumberProperty(cp, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, b.h);
    SDL_SetBooleanProperty(cp, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, true);
    SDL_SetBooleanProperty(cp, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, !env_int("PNL_BOTTOM_WINDOWED", 0));
    SDL_SetBooleanProperty(cp, SDL_PROP_WINDOW_CREATE_FOCUSABLE_BOOLEAN, armada ? true : false);
    SDL_Window *win = SDL_CreateWindowWithProperties(cp);
    SDL_DestroyProperties(cp);
    if (!win) { LOG("window: %s", SDL_GetError()); SDL_Quit(); return 1; }
    SDL_Renderer *ren = SDL_CreateRenderer(win, NULL);
    if (!ren) { LOG("renderer: %s", SDL_GetError()); SDL_DestroyWindow(win); SDL_Quit(); return 1; }
    SDL_SetRenderVSync(ren, 1);
    if (armada) SDL_HideCursor();

    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_RenderPresent(ren);
    if (armada) claim_bottom(win, 1);

    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(win, &pw, &ph);
    PNB_STORE32(&h->panel_w, (uint32_t)pw);
    PNB_STORE32(&h->panel_h, (uint32_t)ph);
    snprintf(h->where, sizeof h->where, "%s %s%s", SDL_GetCurrentVideoDriver(),
             armada ? (getenv("DISPLAY") ? getenv("DISPLAY") : "?") : (SDL_GetDisplayName(disp) ? SDL_GetDisplayName(disp) : "?"),
             armada ? " (Game Mode)" : "");
    PNB_STORE32(&h->helper_ready, PNB_LOAD32(&h->helper_ready) + 1);
    LOG("window %dx%d on %s", pw, ph, h->where);

    SDL_Texture *tex = NULL;
    int tw = 0, th = 0;
    uint64_t shown_seq = 0;
    int need_draw = 0;
    unsigned tick = 0;
    int mouse_down = 0;

    while (!g_stop && !PNB_LOAD32(&h->quit)) {
        if (host > 0 && kill(host, 0) != 0 && errno == ESRCH) { LOG("game has exited"); break; }

        uint64_t seq = PNB_LOAD64(&h->frame_seq);
        if (seq && seq != shown_seq) {
            int w = (int)PNB_LOAD32(&h->frame_w), hh = (int)PNB_LOAD32(&h->frame_h);
            if (w > 0 && hh > 0 && w <= PNB_MAX_W && hh <= PNB_MAX_H) {
                if (!tex || tw != w || th != hh) {
                    if (tex) SDL_DestroyTexture(tex);
                    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, w, hh);
                    if (tex) SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
                    tw = w; th = hh;
                }
                if (tex) SDL_UpdateTexture(tex, NULL, pnb_pixels(map, seq), w * 4);
                shown_seq = seq;
                need_draw = 1;
            }
        }
        if (need_draw) {
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderClear(ren);
            if (tex) SDL_RenderTexture(ren, tex, NULL, NULL);
            SDL_RenderPresent(ren);
            PNB_STORE32(&h->shown_frames, PNB_LOAD32(&h->shown_frames) + 1);
            need_draw = 0;
        }

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            pnb_touch t = {0};
            int have = 0;
            switch (e.type) {
            case SDL_EVENT_QUIT: g_stop = 1; break;
            case SDL_EVENT_WINDOW_EXPOSED: need_draw = 1; break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                SDL_GetWindowSizeInPixels(win, &pw, &ph);
                PNB_STORE32(&h->panel_w, (uint32_t)pw);
                PNB_STORE32(&h->panel_h, (uint32_t)ph);
                need_draw = 1;
                break;
            case SDL_EVENT_FINGER_DOWN: case SDL_EVENT_FINGER_MOTION:
            case SDL_EVENT_FINGER_UP: case SDL_EVENT_FINGER_CANCELED:
                if (e.tfinger.windowID && e.tfinger.windowID != SDL_GetWindowID(win)) break;
                t.id = (int32_t)(e.tfinger.fingerID & 0x3fffffff) + 1;
                t.x = e.tfinger.x * (float)pw;
                t.y = e.tfinger.y * (float)ph;
                t.down = (e.type == SDL_EVENT_FINGER_DOWN || e.type == SDL_EVENT_FINGER_MOTION);
                have = 1;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP:
                if (e.button.which == SDL_TOUCH_MOUSEID || e.button.button != SDL_BUTTON_LEFT) break;
                mouse_down = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                { int ww = 0, wh = 0; SDL_GetWindowSize(win, &ww, &wh); if (ww <= 0 || wh <= 0) break;
                  t.id = 0; t.x = e.button.x * pw / ww; t.y = e.button.y * ph / wh; }
                t.down = mouse_down;
                have = 1;
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (e.motion.which == SDL_TOUCH_MOUSEID || !mouse_down) break;
                { int ww = 0, wh = 0; SDL_GetWindowSize(win, &ww, &wh); if (ww <= 0 || wh <= 0) break;
                  t.id = 0; t.x = e.motion.x * pw / ww; t.y = e.motion.y * ph / wh; t.down = 1; have = 1; }
                break;
            default: break;
            }
            if (have) {
                if (t.x < 0) t.x = 0;
                if (t.y < 0) t.y = 0;
                if (pw > 0 && t.x > pw - 1) t.x = (float)(pw - 1);
                if (ph > 0 && t.y > ph - 1) t.y = (float)(ph - 1);
                uint32_t head = h->touch_head;
                h->touch[head % PNB_TOUCH_RING] = t;
                PNB_STORE32(&h->touch_head, head + 1);
            }
        }

        if (armada && (++tick % 150) == 0) claim_bottom(win, 0);
        SDL_Delay(need_draw ? 1 : 8);
    }

    LOG("closing after %u frames", PNB_LOAD32(&h->shown_frames));
    if (tex) SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    PNB_STORE32(&h->helper_pid, 0);
    if (host > 0 && kill(host, 0) != 0) unlink(map_path);
    munmap(map, PNB_MAP_BYTES);
    return 0;
}
