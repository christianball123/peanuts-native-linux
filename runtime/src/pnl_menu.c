// The in-game menu (Start + Select). peanuts-run draws it into an RGBA picture and hands it to the host's overlay (nxhost_overlay_set slot 0), which the
// present draws over every game frame. The game keeps running behind it; while the menu is open the controller's buttons go to the menu only.
//
//   Resume · Bottom screen Shown/Hidden · Frame rate cap (the game's / 30 / 60 ...) · Frame rate counter On/Off · Quit game
//
// Text: Inter (the launcher's font, in the AppImage's share/peanuts-native/fonts) through stb_truetype.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include "pnl_menu.h"
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "../third_party/stb_truetype.h"

int nxhost_overlay_set(int slot, const void *px, int w, int h, int stride);
int nxhost_fps(float *out, int n);
void nxhost_fps_cap_set(int fps);
int nxhost_fps_cap_status(char *buf, int n);
extern atomic_int pnl_bottom_blank;     // pnl_companion.c: 1 = the bottom screen shows black
int pnl_companion_active(void);

enum { IT_RESUME, IT_BOTTOM, IT_CAP, IT_FPS, IT_QUIT, IT_N };

static struct {
    int ok, open, sel, w, h, show_fps, cap, rates;
    stbtt_fontinfo bold, reg; unsigned char *bold_data, *reg_data;
    uint32_t *px; char title[128];
    uint64_t fps_t; int fps_shown; uint32_t *fpx; int fw, fh;
} M;

static unsigned char *slurp(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *d = n > 0 ? malloc((size_t)n) : NULL;
    if (d && fread(d, 1, (size_t)n, f) != (size_t)n) { free(d); d = NULL; }
    fclose(f); return d;
}
static int load_font(stbtt_fontinfo *fi, unsigned char **data, const char *dir, const char *const names[]) {
    char p[1200];
    for (int i = 0; names[i]; i++) {
        snprintf(p, sizeof p, "%s/%s", dir, names[i]);
        if ((*data = slurp(p)) && stbtt_InitFont(fi, *data, stbtt_GetFontOffsetForIndex(*data, 0))) return 1;
        free(*data); *data = NULL;
    }
    return 0;
}

void pnl_menu_init(int w, int h, const char *title) {
    char exe[1024]; ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) return;
    exe[n] = 0; char *sl = strrchr(exe, '/'); if (sl) *sl = 0;
    char dir[1100]; snprintf(dir, sizeof dir, "%s/../share/peanuts-native/fonts", exe);
    static const char *const bold[] = {"Inter-SemiBold.ttf", "InterDisplay-Bold.ttf", "Inter-Regular.ttf", "standard.ttf", NULL};
    static const char *const reg[] = {"Inter-Regular.ttf", "Inter-SemiBold.ttf", "standard.ttf", NULL};
    if (!load_font(&M.bold, &M.bold_data, dir, bold) || !load_font(&M.reg, &M.reg_data, dir, reg)) {
        fprintf(stderr, "[peanuts-run] in-game menu: no font in %s: the menu is off\n", dir); return;
    }
    snprintf(M.title, sizeof M.title, "%s", title && *title ? title : "Peanuts Native");
    M.w = w; M.h = h; M.ok = 1;
}
void pnl_menu_resize(int w, int h) { if (w > 0 && h > 0) { M.w = w; M.h = h; if (M.open) pnl_menu_redraw(); } }
int pnl_menu_is_open(void) { return M.open; }

// ---- Drawing (premultiplied RGBA, as the overlay takes it) ----
typedef struct { float r, g, b, a; } col;
static inline void blend(uint32_t *d, col c, float cov) {
    float a = c.a * cov; if (a <= 0) return;
    uint32_t v = *d; float dr = (v & 255) / 255.f, dg = (v >> 8 & 255) / 255.f, db = (v >> 16 & 255) / 255.f, da = (v >> 24) / 255.f;
    dr = c.r * a + dr * (1 - a); dg = c.g * a + dg * (1 - a); db = c.b * a + db * (1 - a); da = a + da * (1 - a);
    *d = (uint32_t)(dr * 255 + .5f) | (uint32_t)(dg * 255 + .5f) << 8 | (uint32_t)(db * 255 + .5f) << 16 | (uint32_t)(da * 255 + .5f) << 24;
}
static void rrect(uint32_t *px, int W, int H, float x, float y, float w, float h, float rad, col c) {
    int x0 = (int)floorf(x), y0 = (int)floorf(y), x1 = (int)ceilf(x + w), y1 = (int)ceilf(y + h);
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > W) x1 = W; if (y1 > H) y1 = H;
    for (int j = y0; j < y1; j++) for (int i = x0; i < x1; i++) {
        float cx = i + .5f, cy = j + .5f;
        float qx = fmaxf(fmaxf(x + rad - cx, cx - (x + w - rad)), 0), qy = fmaxf(fmaxf(y + rad - cy, cy - (y + h - rad)), 0);
        float d = sqrtf(qx * qx + qy * qy) - rad;   // < 0 inside
        float cov = d <= -.5f ? 1 : d >= .5f ? 0 : .5f - d;
        if (cx < x || cx > x + w || cy < y || cy > y + h) cov = fminf(cov, 0);
        if (cov > 0) blend(&px[(size_t)j * W + i], c, cov);
    }
}
// UTF-8: the next code point (é in Pokémon), advancing *s
static int utf8_next(const unsigned char **s) {
    const unsigned char *p = *s; int c = *p++;
    if (c >= 0xc0 && c < 0xe0 && (p[0] & 0xc0) == 0x80) { c = (c & 0x1f) << 6 | (p[0] & 0x3f); p += 1; }
    else if (c >= 0xe0 && c < 0xf0 && (p[0] & 0xc0) == 0x80 && (p[1] & 0xc0) == 0x80) { c = (c & 0x0f) << 12 | (p[0] & 0x3f) << 6 | (p[1] & 0x3f); p += 2; }
    else if (c >= 0x80) c = '?';
    *s = p; return c;
}
static float text_w(stbtt_fontinfo *f, float size, const char *s) {
    float sc = stbtt_ScaleForPixelHeight(f, size), x = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        int c = utf8_next(&p), adv, lsb; stbtt_GetCodepointHMetrics(f, c, &adv, &lsb); x += adv * sc;
        if (*p) { const unsigned char *q = p; x += sc * stbtt_GetCodepointKernAdvance(f, c, utf8_next(&q)); }
    }
    return x;
}
// baseline at y
static void text(uint32_t *px, int W, int H, stbtt_fontinfo *f, float size, float x, float y, const char *s, col c) {
    float sc = stbtt_ScaleForPixelHeight(f, size);
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        int cp = utf8_next(&p);
        int adv, lsb, x0, y0, x1, y1; stbtt_GetCodepointHMetrics(f, cp, &adv, &lsb);
        float fx = x - floorf(x);
        stbtt_GetCodepointBitmapBoxSubpixel(f, cp, sc, sc, fx, 0, &x0, &y0, &x1, &y1);
        int gw = x1 - x0, gh = y1 - y0;
        if (gw > 0 && gh > 0) {
            unsigned char *g = malloc((size_t)gw * gh);
            if (g) {
                stbtt_MakeCodepointBitmapSubpixel(f, g, gw, gh, gw, sc, sc, fx, 0, cp);
                int ox = (int)floorf(x) + x0, oy = (int)floorf(y) + y0;
                for (int j = 0; j < gh; j++) { int yy = oy + j; if (yy < 0 || yy >= H) continue;
                    for (int i = 0; i < gw; i++) { int xx = ox + i; if (xx < 0 || xx >= W) continue;
                        unsigned char v = g[j * gw + i]; if (v) blend(&px[(size_t)yy * W + xx], c, v / 255.f); } }
                free(g);
            }
        }
        x += adv * sc; if (*p) { const unsigned char *q = p; x += sc * stbtt_GetCodepointKernAdvance(f, cp, utf8_next(&q)); }
    }
}

static void cap_refresh(void) {
    char b[128] = ""; M.cap = nxhost_fps_cap_status(b, sizeof b);
    int cap = 0, rates = 0; if (sscanf(b, "%d %d", &cap, &rates) >= 2) M.rates = rates; else M.rates = 0;
}
static const int k_rates[6] = {15, 20, 30, 40, 45, 60};
static void cap_step(int dir) {
    // the choices: 0 (the game's own) and the rates this game's limiters take
    int ch[7], n = 0; ch[n++] = 0;
    for (int i = 0; i < 6; i++) if (M.rates >> i & 1) ch[n++] = k_rates[i];
    if (n < 2) return;
    int at = 0; for (int i = 0; i < n; i++) if (ch[i] == M.cap) at = i;
    at = (at + dir + n) % n; M.cap = ch[at]; nxhost_fps_cap_set(M.cap);
}
static int item_shown(int it) {
    if (it == IT_BOTTOM) return pnl_companion_active();
    return 1;
}

void pnl_menu_redraw(void) {
    if (!M.ok || !M.open) return;
    int W = M.w, H = M.h; size_t need = (size_t)W * H;
    uint32_t *px = realloc(M.px, need * 4); if (!px) return; M.px = px;
    for (size_t i = 0; i < need; i++) px[i] = 0x8c000000u;   // the game dimmed (black, 55 %)
    float s = H / 1080.f; if (W / 1920.f < s) s = W / 1920.f; if (s < .5f) s = .5f;
    int items[IT_N], n = 0; for (int i = 0; i < IT_N; i++) if (item_shown(i)) items[n++] = i;
    float row = 76 * s, pw = 820 * s, top = 150 * s, ph = top + n * row + 104 * s;
    float x = (W - pw) / 2, y = (H - ph) / 2;
    const col panel = {.105f, .114f, .141f, .97f}, line = {1, 1, 1, .08f}, white = {1, 1, 1, 1}, dim = {.70f, .72f, .78f, 1}, accent = {1.f, .80f, .20f, 1}, dark = {.08f, .08f, .10f, 1};
    rrect(px, W, H, x, y, pw, ph, 28 * s, panel);
    // header
    text(px, W, H, &M.reg, 24 * s, x + 44 * s, y + 58 * s, "IN-GAME MENU", dim);
    char t[128]; snprintf(t, sizeof t, "%s", M.title);
    if (text_w(&M.bold, 40 * s, t) > pw - 88 * s) {   // too long: cut with "..."
        size_t l = strlen(t);
        while (l > 1) { t[--l] = 0; while (l > 1 && ((unsigned char)t[l - 1] & 0xc0) == 0x80) t[--l] = 0; if (l > 1 && (unsigned char)t[l - 1] >= 0xc0) t[--l] = 0; char u[132]; snprintf(u, sizeof u, "%s...", t); if (text_w(&M.bold, 40 * s, u) <= pw - 88 * s) { snprintf(t, sizeof t, "%s", u); break; } }
    }
    text(px, W, H, &M.bold, 40 * s, x + 44 * s, y + 108 * s, t, white);
    rrect(px, W, H, x + 44 * s, y + top - 12 * s, pw - 88 * s, 2 * s, 0, line);
    // rows
    for (int r = 0; r < n; r++) {
        int it = items[r]; float ry = y + top + r * row; int on = M.sel == it;
        if (on) rrect(px, W, H, x + 24 * s, ry + 6 * s, pw - 48 * s, row - 12 * s, 16 * s, accent);
        const char *label = it == IT_RESUME ? "Resume" : it == IT_BOTTOM ? "Bottom screen" : it == IT_CAP ? "Frame rate cap" : it == IT_FPS ? "Frame rate counter" : "Quit game";
        char val[48] = "";
        if (it == IT_BOTTOM) snprintf(val, sizeof val, "%s", atomic_load(&pnl_bottom_blank) ? "Hidden" : "Shown");
        if (it == IT_CAP) { if (!M.rates) snprintf(val, sizeof val, "Not for this game"); else if (M.cap) snprintf(val, sizeof val, "%d fps", M.cap); else snprintf(val, sizeof val, "Game's own"); }
        if (it == IT_FPS) snprintf(val, sizeof val, "%s", M.show_fps ? "On" : "Off");
        col tc = on ? dark : white, vc = on ? dark : dim;
        float base = ry + row / 2 + 11 * s;
        text(px, W, H, &M.bold, 32 * s, x + 56 * s, base, label, tc);
        if (*val) {
            int arrows = on && (it == IT_BOTTOM || it == IT_FPS || (it == IT_CAP && M.rates));
            char v2[64]; snprintf(v2, sizeof v2, arrows ? "<  %s  >" : "%s", val);
            text(px, W, H, &M.reg, 30 * s, x + pw - 56 * s - text_w(&M.reg, 30 * s, v2), base, v2, vc);
        }
    }
    // footer
    const char *hint = "A  Select      B  Back      Left / Right  Change      Start + Select  Close";
    text(px, W, H, &M.reg, 22 * s, x + (pw - text_w(&M.reg, 22 * s, hint)) / 2, y + ph - 38 * s, hint, dim);
    nxhost_overlay_set(0, px, W, H, W * 4);
}

void pnl_menu_open(void) {
    if (!M.ok || M.open) return;
    M.open = 1; M.sel = IT_RESUME; cap_refresh();
    fprintf(stderr, "[peanuts-run] in-game menu opened\n");
    pnl_menu_redraw();
}
void pnl_menu_close(void) {
    if (!M.open) return;
    M.open = 0; nxhost_overlay_set(0, NULL, 0, 0, 0);
    fprintf(stderr, "[peanuts-run] in-game menu closed\n");
}
void pnl_menu_toggle(void) { if (M.open) pnl_menu_close(); else pnl_menu_open(); }

static void move(int dir) {
    int it = M.sel;
    for (int k = 0; k < IT_N; k++) { it = (it + dir + IT_N) % IT_N; if (item_shown(it)) break; }
    M.sel = it;
}
static int change(int dir) {
    switch (M.sel) {
    case IT_BOTTOM: atomic_store(&pnl_bottom_blank, !atomic_load(&pnl_bottom_blank)); return 1;
    case IT_CAP: cap_step(dir); return 1;
    case IT_FPS: M.show_fps = !M.show_fps; if (!M.show_fps) { nxhost_overlay_set(1, NULL, 0, 0, 0); M.fps_shown = -1; } return 1;
    default: return 0;
    }
}
int pnl_menu_input(int what) {
    if (!M.open) return PNL_MENU_NONE;
    switch (what) {
    case PNL_IN_UP: move(-1); break;
    case PNL_IN_DOWN: move(1); break;
    case PNL_IN_LEFT: if (!change(-1)) return PNL_MENU_NONE; break;
    case PNL_IN_RIGHT: if (!change(1)) return PNL_MENU_NONE; break;
    case PNL_IN_BACK: pnl_menu_close(); return PNL_MENU_NONE;
    case PNL_IN_OK:
        if (M.sel == IT_RESUME) { pnl_menu_close(); return PNL_MENU_NONE; }
        if (M.sel == IT_QUIT) { pnl_menu_close(); return PNL_MENU_QUIT; }
        change(1); break;
    default: return PNL_MENU_NONE;
    }
    pnl_menu_redraw();
    return PNL_MENU_NONE;
}

// The frame rate counter (top right), redrawn twice a second while it is on
void pnl_menu_tick(uint64_t now_ms) {
    if (!M.ok || !M.show_fps || now_ms - M.fps_t < 500) return;
    M.fps_t = now_ms;
    float v[3] = {0}; nxhost_fps(v, 3); int f = (int)(v[0] + .5f);
    if (f == M.fps_shown) return;
    M.fps_shown = f;
    float s = M.h / 1080.f; if (s < .5f) s = .5f;
    char t[32]; snprintf(t, sizeof t, "%d FPS", f);
    int w = (int)(text_w(&M.bold, 30 * s, t) + 40 * s), h = (int)(52 * s);
    uint32_t *px = realloc(M.fpx, (size_t)w * h * 4); if (!px) return; M.fpx = px;
    memset(px, 0, (size_t)w * h * 4);
    rrect(px, w, h, 4 * s, 4 * s, w - 8 * s, h - 8 * s, 12 * s, (col){.06f, .06f, .08f, .78f});
    col c = f >= 55 ? (col){.55f, .95f, .55f, 1} : f >= 28 ? (col){1, .85f, .35f, 1} : (col){1, .45f, .40f, 1};
    text(px, w, h, &M.bold, 30 * s, 20 * s, h / 2 + 11 * s, t, c);
    nxhost_overlay_set(1, px, w, h, w * 4);
}
