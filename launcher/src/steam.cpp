// Peanuts Native for Linux — Steam shortcuts: a game added to Steam (shortcuts.vdf) with its own artwork, started straight into the game
// ("<AppImage> --launch <title id>"), so it can be played from Steam's Game Mode.
#include "core.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "stb_image_write.h"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

namespace pn {

// ---------- is Steam here / running ----------
static std::vector<std::string> steam_roots() {
    std::vector<std::string> r;
    for (auto d : {"/.local/share/Steam", "/.steam/steam", "/.steam/root", "/.var/app/com.valvesoftware.Steam/.local/share/Steam"}) {
        std::string p = home() + d; char real[4096];
        if (!is_dir(p + "/userdata") || !realpath(p.c_str(), real)) continue;
        if (std::find(r.begin(), r.end(), std::string(real)) == r.end()) r.push_back(real);
    }
    return r;
}
std::vector<std::string> steam_users() {
    std::vector<std::string> out;
    for (auto &root : steam_roots()) {
        DIR *d = opendir((root + "/userdata").c_str()); if (!d) continue;
        while (dirent *e = readdir(d)) {
            std::string n = e->d_name; if (n.empty() || n == "0" || n.find_first_not_of("0123456789") != std::string::npos) continue;
            out.push_back(root + "/userdata/" + n);
        }
        closedir(d);
    }
    return out;
}
bool steam_running() {
    DIR *d = opendir("/proc"); if (!d) return false;
    bool found = false; uid_t me = getuid();
    while (dirent *e = readdir(d)) {
        if (!isdigit((unsigned char)e->d_name[0])) continue;
        std::string dir = std::string("/proc/") + e->d_name; struct stat st;
        if (stat(dir.c_str(), &st) != 0 || st.st_uid != me) continue;
        std::string comm = trim(read_file(dir + "/comm", 64));
        if (comm == "steam" || comm == "steamwebhelper") { found = true; break; }
    }
    closedir(d);
    return found;
}
bool steam_game_mode() {
    const char *a = getenv("SteamGamepadUI"), *b = getenv("GAMESCOPE_WAYLAND_DISPLAY"), *c = getenv("XDG_CURRENT_DESKTOP");
    return (a && *a && strcmp(a, "0")) || (b && *b) || (c && strcasestr(c, "gamescope"));
}
std::string launch_command_exe() {
    const char *ai = getenv("APPIMAGE");
    if (ai && *ai && is_file(ai)) return ai;
    if (is_file(P.appdir + "/AppRun")) return P.appdir + "/AppRun";
    return P.bin + "/peanuts-native";
}

// ---------- binary KeyValues (shortcuts.vdf) ----------
struct KV {
    uint8_t type = 0;   // 0 map, 1 string, 2 int32, 3 float, 7 uint64, 10 int64 (the types Steam writes)
    std::string key, str; uint64_t num = 0; std::vector<KV> kids;
    KV *find(const std::string &k) { for (auto &c : kids) if (strcasecmp(c.key.c_str(), k.c_str()) == 0) return &c; return nullptr; }
};
static bool kv_parse(const std::string &b, size_t &i, std::vector<KV> &out, int depth) {
    if (depth > 32) return false;
    while (i < b.size()) {
        uint8_t t = (uint8_t)b[i++];
        if (t == 8) return true;
        KV n; n.type = t;
        size_t z = b.find('\0', i); if (z == std::string::npos) return false;
        n.key = b.substr(i, z - i); i = z + 1;
        switch (t) {
        case 0: if (!kv_parse(b, i, n.kids, depth + 1)) return false; break;
        case 1: { size_t e = b.find('\0', i); if (e == std::string::npos) return false; n.str = b.substr(i, e - i); i = e + 1; break; }
        case 2: case 3: case 4: case 6: if (i + 4 > b.size()) return false; { uint32_t v; memcpy(&v, b.data() + i, 4); n.num = v; } i += 4; break;
        case 7: case 10: if (i + 8 > b.size()) return false; memcpy(&n.num, b.data() + i, 8); i += 8; break;
        default: return false;   // wide strings and the like: Steam does not write them here
        }
        out.push_back(std::move(n));
    }
    return depth == 0;   // the top level may end at the end of the file
}
static void kv_write(std::string &o, const std::vector<KV> &v) {
    for (auto &n : v) {
        o += (char)n.type; o += n.key; o += '\0';
        switch (n.type) {
        case 0: kv_write(o, n.kids); o += (char)8; break;
        case 1: o += n.str; o += '\0'; break;
        case 2: case 3: case 4: case 6: { uint32_t x = (uint32_t)n.num; o.append((const char *)&x, 4); break; }
        default: o.append((const char *)&n.num, 8); break;
        }
    }
}
static KV kv_s(const char *k, const std::string &v) { KV n; n.type = 1; n.key = k; n.str = v; return n; }
static KV kv_i(const char *k, uint32_t v) { KV n; n.type = 2; n.key = k; n.num = v; return n; }

// The id Steam gives a non-Steam game: crc32 of exe + name, top bit set (its artwork files are named after it)
static uint32_t shortcut_id(const std::string &exe_quoted, const std::string &name) {
    std::string s = exe_quoted + name; uint32_t c = 0xFFFFFFFFu;   // crc32 (zlib's)
    for (unsigned char ch : s) { c ^= ch; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return ~c | 0x80000000u;
}
static std::string quoted(const std::string &p) { return "\"" + p + "\""; }

// ---------- artwork ----------
struct Img { int w = 0, h = 0; std::vector<float> px; /* RGB, linear-ish 0..1 */
    Img(int W, int H) : w(W), h(H), px((size_t)W * H * 3) {}
    void blend(int x, int y, float r, float g, float b, float a) {
        if (x < 0 || y < 0 || x >= w || y >= h || a <= 0) return;
        if (a > 1) a = 1;
        float *p = &px[((size_t)y * w + x) * 3]; p[0] += (r - p[0]) * a; p[1] += (g - p[1]) * a; p[2] += (b - p[2]) * a;
    }
};
static void rgb(uint32_t c, float &r, float &g, float &b) { r = ((c >> 16) & 255) / 255.f; g = ((c >> 8) & 255) / 255.f; b = (c & 255) / 255.f; }
static const uint32_t BG0 = 0x0D0F14, BG1 = 0x1A1E27, ACCENT = 0xE8B04B, TEXT = 0xF2F3F5, DIM = 0x9AA1AE;

static void background(Img &im, float gx, float gy, float glow_r) {
    float r0, g0, b0, r1, g1, b1, ar, ag, ab; rgb(BG0, r0, g0, b0); rgb(BG1, r1, g1, b1); rgb(ACCENT, ar, ag, ab);
    for (int y = 0; y < im.h; y++) for (int x = 0; x < im.w; x++) {
        float t = (float)y / im.h; float *p = &im.px[((size_t)y * im.w + x) * 3];
        p[0] = r1 + (r0 - r1) * t; p[1] = g1 + (g0 - g1) * t; p[2] = b1 + (b0 - b1) * t;
        float dx = x - gx, dy = y - gy, d = std::sqrt(dx * dx + dy * dy) / glow_r;
        float a = 0.22f * std::exp(-d * d * 2.2f);
        p[0] += (ar - p[0]) * a; p[1] += (ag - p[1]) * a; p[2] += (ab - p[2]) * a;
    }
}
// Coverage of a rounded rectangle at a pixel centre (1 px soft edge)
static float rrect_cov(float x, float y, float x0, float y0, float x1, float y1, float rad) {
    float cx = std::clamp(x, x0 + rad, x1 - rad), cy = std::clamp(y, y0 + rad, y1 - rad);
    float d = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy)) - rad;
    return std::clamp(0.5f - d, 0.f, 1.f);
}
static void shadow(Img &im, float x0, float y0, float x1, float y1, float rad, float blur, float strength) {
    for (int y = (int)(y0 - blur * 2); y < (int)(y1 + blur * 3); y++) for (int x = (int)(x0 - blur * 2); x < (int)(x1 + blur * 2); x++) {
        float cx = std::clamp((float)x, x0 + rad, x1 - rad), cy = std::clamp((float)y - blur * 0.4f, y0 + rad, y1 - rad);
        float d = std::sqrt((x - cx) * (x - cx) + (y - blur * 0.4f - cy) * (y - blur * 0.4f - cy)) - rad;
        float a = d <= 0 ? strength : strength * std::exp(-(d * d) / (2 * blur * blur * 0.35f));
        im.blend(x, y, 0, 0, 0, a);
    }
}
static void draw_icon(Img &im, const unsigned char *src, int sw, int sh, float x0, float y0, float size, float rad) {
    shadow(im, x0, y0, x0 + size, y0 + size, rad, size * 0.06f, 0.55f);
    for (int y = (int)y0; y < (int)std::ceil(y0 + size); y++) for (int x = (int)x0; x < (int)std::ceil(x0 + size); x++) {
        float cov = rrect_cov(x + 0.5f, y + 0.5f, x0, y0, x0 + size, y0 + size, rad); if (cov <= 0) continue;
        float u = (x + 0.5f - x0) / size * sw - 0.5f, v = (y + 0.5f - y0) / size * sh - 0.5f;   // bilinear
        int ux = std::clamp((int)std::floor(u), 0, sw - 1), vy = std::clamp((int)std::floor(v), 0, sh - 1);
        int ux1 = std::min(ux + 1, sw - 1), vy1 = std::min(vy + 1, sh - 1); float fu = std::clamp(u - ux, 0.f, 1.f), fv = std::clamp(v - vy, 0.f, 1.f);
        float c[4];
        for (int k = 0; k < 4; k++) {
            auto at = [&](int xx, int yy) { return src[((size_t)yy * sw + xx) * 4 + k] / 255.f; };
            c[k] = (at(ux, vy) * (1 - fu) + at(ux1, vy) * fu) * (1 - fv) + (at(ux, vy1) * (1 - fu) + at(ux1, vy1) * fu) * fv;
        }
        im.blend(x, y, c[0], c[1], c[2], cov * c[3]);
    }
    // a hairline of light on the edge
    for (int y = (int)y0 - 1; y < (int)(y0 + size) + 1; y++) for (int x = (int)x0 - 1; x < (int)(x0 + size) + 1; x++) {
        float o = rrect_cov(x + 0.5f, y + 0.5f, x0, y0, x0 + size, y0 + size, rad), i = rrect_cov(x + 0.5f, y + 0.5f, x0 + 1, y0 + 1, x0 + size - 1, y0 + size - 1, rad - 1);
        im.blend(x, y, 1, 1, 1, (o - i) * 0.12f);
    }
}

struct Font { std::string data; stbtt_fontinfo info; bool ok = false;
    explicit Font(const std::string &path) { data = read_file(path, 8 << 20); ok = !data.empty() && stbtt_InitFont(&info, (const unsigned char *)data.data(), stbtt_GetFontOffsetForIndex((const unsigned char *)data.data(), 0)); } };

static std::vector<uint32_t> utf8(const std::string &s) {
    std::vector<uint32_t> o;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i]; uint32_t cp; int n;
        if (c < 0x80) { cp = c; n = 1; } else if ((c >> 5) == 6) { cp = c & 31; n = 2; } else if ((c >> 4) == 14) { cp = c & 15; n = 3; } else { cp = c & 7; n = 4; }
        for (int k = 1; k < n && i + k < s.size(); k++) cp = (cp << 6) | (s[i + k] & 63);
        o.push_back(cp); i += n;
    }
    return o;
}
static float text_width(Font &f, float px, const std::vector<uint32_t> &t) {
    float sc = stbtt_ScaleForPixelHeight(&f.info, px), w = 0;
    for (size_t i = 0; i < t.size(); i++) { int adv, lsb; stbtt_GetCodepointHMetrics(&f.info, t[i], &adv, &lsb); w += adv * sc; if (i + 1 < t.size()) w += stbtt_GetCodepointKernAdvance(&f.info, t[i], t[i + 1]) * sc; }
    return w;
}
static void draw_text(Img &im, Font &f, float px, float x, float baseline, uint32_t col, const std::vector<uint32_t> &t, float alpha = 1) {
    if (!f.ok) return;
    float r, g, b; rgb(col, r, g, b);
    float sc = stbtt_ScaleForPixelHeight(&f.info, px);
    for (size_t i = 0; i < t.size(); i++) {
        int w = 0, h = 0, xo = 0, yo = 0; float fx = x - std::floor(x);
        unsigned char *bm = stbtt_GetCodepointBitmapSubpixel(&f.info, sc, sc, fx, 0, t[i], &w, &h, &xo, &yo);
        for (int yy = 0; yy < h; yy++) for (int xx = 0; xx < w; xx++) im.blend((int)std::floor(x) + xo + xx, (int)baseline + yo + yy, r, g, b, bm[yy * w + xx] / 255.f * alpha);
        stbtt_FreeBitmap(bm, nullptr);
        int adv, lsb; stbtt_GetCodepointHMetrics(&f.info, t[i], &adv, &lsb); x += adv * sc;
        if (i + 1 < t.size()) x += stbtt_GetCodepointKernAdvance(&f.info, t[i], t[i + 1]) * sc;
    }
}
// The name in up to `max_lines` centred lines (left-aligned at `left` when centre < 0), shrinking until it fits. Returns the height used
static float draw_title(Img &im, Font &f, const std::string &name, float centre, float left, float top, float maxw, float px, int max_lines, uint32_t col) {
    auto words = split(name, ' ');
    std::vector<std::string> lines;
    for (; px > 18; px *= 0.92f) {
        lines.clear(); std::string cur;
        for (auto &w : words) {
            std::string tryl = cur.empty() ? w : cur + " " + w;
            if (text_width(f, px, utf8(tryl)) <= maxw || cur.empty()) cur = tryl; else { lines.push_back(cur); cur = w; }
        }
        if (!cur.empty()) lines.push_back(cur);
        bool fits = (int)lines.size() <= max_lines; for (auto &l : lines) if (text_width(f, px, utf8(l)) > maxw) fits = false;
        if (fits) break;
    }
    float lh = px * 1.12f, y = top + px * 0.8f;
    for (auto &l : lines) { auto t = utf8(l); float w = text_width(f, px, t); draw_text(im, f, px, centre >= 0 ? centre - w / 2 : left, y, col, t); y += lh; }
    return lines.size() * lh;
}
static void peanut_mark(Img &im, float cx, float cy, float r) {   // the launcher's mark, small: two lobes with a waist
    float ar, ag, ab; rgb(ACCENT, ar, ag, ab);
    for (int y = (int)(cy - r * 2); y < (int)(cy + r * 2); y++) for (int x = (int)(cx - r * 2); x < (int)(cx + r * 2); x++) {
        float dx = (x + 0.5f - cx), dy = (y + 0.5f - cy);
        float rx = dx * 0.8f + dy * 0.6f, ry = -dx * 0.6f + dy * 0.8f;   // tilted
        float d1 = std::sqrt(rx * rx + (ry + r * 0.55f) * (ry + r * 0.55f)) - r * 0.62f, d2 = std::sqrt(rx * rx + (ry - r * 0.55f) * (ry - r * 0.55f)) - r * 0.7f;
        float d = std::min(d1, d2); im.blend(x, y, ar, ag, ab, std::clamp(0.5f - d, 0.f, 1.f));
    }
}
static bool save_png(const Img &im, const std::string &path) {
    std::vector<unsigned char> o((size_t)im.w * im.h * 3);
    for (size_t i = 0; i < o.size(); i++) o[i] = (unsigned char)std::lround(std::clamp(im.px[i], 0.f, 1.f) * 255.f);
    std::string tmp = path + ".tmp";
    if (!stbi_write_png(tmp.c_str(), im.w, im.h, 3, o.data(), im.w * 3)) return false;
    return rename(tmp.c_str(), path.c_str()) == 0;
}

// Portrait (library), landscape (recent games), hero (the game's page) and the icon
static bool make_art(const Game &g, const std::string &grid, uint32_t id, std::string &icon_out) {
    mkdirs(grid);
    int iw = 0, ih = 0, n = 0; unsigned char *icon = g.icon_path.empty() ? nullptr : stbi_load(g.icon_path.c_str(), &iw, &ih, &n, 4);
    Font title(P.share + "/fonts/InterDisplay-Bold.ttf"), semi(P.share + "/fonts/Inter-SemiBold.ttf");
    std::string base = grid + "/" + std::to_string(id);
    bool ok = true;
    { // portrait 600 x 900
        Img im(600, 900); background(im, 300, 330, 520);
        if (icon) draw_icon(im, icon, iw, ih, 90, 120, 420, 46); else peanut_mark(im, 300, 330, 120);
        draw_title(im, title, g.name, 300, 0, 600, 520, 52, 3, TEXT);
        peanut_mark(im, 300 - text_width(semi, 22, utf8("Peanuts Native")) / 2 - 18, 852, 9);
        auto t = utf8("Peanuts Native"); draw_text(im, semi, 22, 300 - text_width(semi, 22, t) / 2 + 6, 860, DIM, t);
        ok &= save_png(im, base + "p.png");
    }
    { // landscape 920 x 430
        Img measure(920, 430); float h = draw_title(measure, title, g.name, -1, 430, 0, 450, 50, 3, TEXT);   // its height, to centre it beside the icon
        Img im2(920, 430); background(im2, 215, 215, 420);
        if (icon) draw_icon(im2, icon, iw, ih, 50, 50, 330, 38); else peanut_mark(im2, 215, 215, 100);
        draw_title(im2, title, g.name, -1, 430, 215 - h / 2 - 22, 450, 50, 3, TEXT);
        auto t = utf8("Peanuts Native"); peanut_mark(im2, 440, 215 + h / 2 + 26, 8); draw_text(im2, semi, 22, 456, 215 + h / 2 + 34, DIM, t);
        ok &= save_png(im2, base + ".png");
    }
    { // hero 1920 x 620: the art on the right, Steam writes the name on the left
        Img im(1920, 620); background(im, 1480, 310, 900);
        if (icon) draw_icon(im, icon, iw, ih, 1260, 90, 440, 50); else peanut_mark(im, 1480, 310, 140);
        ok &= save_png(im, base + "_hero.png");
    }
    if (icon) {   // the icon, square, as a file of its own
        Img im(256, 256); for (size_t i = 0; i < im.px.size(); i += 3) rgb(BG0, im.px[i], im.px[i + 1], im.px[i + 2]);
        for (int y = 0; y < 256; y++) for (int x = 0; x < 256; x++) {
            float u = (x + 0.5f) / 256 * iw, v = (y + 0.5f) / 256 * ih; int ux = std::min((int)u, iw - 1), vy = std::min((int)v, ih - 1);
            const unsigned char *s = &icon[((size_t)vy * iw + ux) * 4]; im.blend(x, y, s[0] / 255.f, s[1] / 255.f, s[2] / 255.f, s[3] / 255.f);
        }
        icon_out = base + "_icon.png"; ok &= save_png(im, icon_out);
        stbi_image_free(icon);
    }
    return ok;
}

// ---------- add ----------
static bool shortcut_matches(KV &e, const std::string &tid) {
    KV *lo = e.find("LaunchOptions");
    return lo && lo->str.find("--launch " + tid) != std::string::npos;
}
bool steam_has(const Game &g) {
    for (auto &u : steam_users()) {
        std::string b = read_file(u + "/config/shortcuts.vdf", 16 << 20);
        if (b.find("--launch " + g.tid) != std::string::npos) return true;
    }
    return false;
}
bool steam_add(const Game &g, std::string &msg) {
    auto users = steam_users();
    if (users.empty()) { msg = "Steam has not been set up on this handheld yet (no Steam user folder was found). Open Steam once, sign in, then try again."; return false; }
    if (steam_running() && steam_game_mode()) { msg = "Steam is running Game Mode right now, and it rewrites its list of shortcuts when it closes, which would undo this.\n\nSwitch to Desktop Mode, make sure Steam is closed (Steam menu → Exit), open Peanuts Native there and press Add to Steam. Your games then appear in Game Mode."; return false; }
    if (steam_running()) { msg = "Steam is open. It saves its own list of shortcuts when it closes, which would undo this.\n\nClose Steam first (in Desktop Mode: Steam menu → Exit), then press Add to Steam again."; return false; }
    std::string exe = launch_command_exe(), exe_q = quoted(exe);
    uint32_t id = shortcut_id(exe_q, g.name);
    int added = 0, updated = 0;
    for (auto &u : users) {
        std::string path = u + "/config/shortcuts.vdf";
        mkdirs(u + "/config");
        std::string b = read_file(path, 16 << 20);
        std::vector<KV> root;
        if (!b.empty()) { size_t i = 0; if (!kv_parse(b, i, root, 0)) { msg = "Steam's shortcuts file could not be read (" + path + "). Nothing was changed."; return false; } }
        KV *sc = nullptr; for (auto &n : root) if (n.type == 0 && strcasecmp(n.key.c_str(), "shortcuts") == 0) sc = &n;
        if (!sc) { KV n; n.type = 0; n.key = "shortcuts"; root.push_back(n); sc = &root.back(); }
        std::string icon;
        make_art(g, u + "/config/grid", id, icon);
        KV e; e.type = 0;
        e.kids = {kv_i("appid", id), kv_s("AppName", g.name), kv_s("Exe", exe_q), kv_s("StartDir", quoted(dirname_of(exe))), kv_s("icon", icon), kv_s("ShortcutPath", ""),
                  kv_s("LaunchOptions", "--launch " + g.tid), kv_i("IsHidden", 0), kv_i("AllowDesktopConfig", 1), kv_i("AllowOverlay", 1), kv_i("OpenVR", 0), kv_i("Devkit", 0),
                  kv_s("DevkitGameID", ""), kv_i("DevkitOverrideAppID", 0), kv_i("LastPlayTime", 0), kv_s("FlatpakAppID", ""), kv_s("sortas", "")};
        KV tags; tags.type = 0; tags.key = "tags"; tags.kids.push_back(kv_s("0", "Peanuts Native")); e.kids.push_back(tags);
        bool replaced = false;
        for (auto &old : sc->kids) if (old.type == 0 && shortcut_matches(old, g.tid)) {
            if (KV *lp = old.find("LastPlayTime")) e.kids[14].num = lp->num;   // keep its play history
            if (KV *oid = old.find("appid"); oid && (uint32_t)oid->num != id)   // the AppImage moved: its old artwork goes
                for (auto sfx : {"p.png", ".png", "_hero.png", "_icon.png"}) unlink((u + "/config/grid/" + std::to_string((uint32_t)oid->num) + sfx).c_str());
            e.key = old.key; old = e; replaced = true; break;
        }
        if (!replaced) { int next = 0; for (auto &k : sc->kids) next = std::max(next, atoi(k.key.c_str()) + 1); e.key = std::to_string(next); sc->kids.push_back(e); }
        std::string o; kv_write(o, root); o += (char)8;
        if (is_file(path) && !is_file(path + ".peanuts-backup")) write_file(path + ".peanuts-backup", b);   // the first time only: Steam's own list as it was
        std::string tmp = path + ".tmp";
        if (!write_file(tmp, o) || rename(tmp.c_str(), path.c_str()) != 0) { msg = "Could not write " + path; return false; }
        (replaced ? updated : added)++;
    }
    msg = std::string(updated && !added ? "Updated" : "Added") + " \"" + g.name + "\" in Steam" + (users.size() > 1 ? " for " + std::to_string(users.size()) + " Steam accounts" : "") +
          ", with its artwork. It shows up the next time Steam starts: in Game Mode it is under Library → Non-Steam.\n\nThe shortcut starts this AppImage:\n" + exe +
          "\nIf you move or rename the AppImage, add the game to Steam again.";
    return true;
}

}  // namespace pn
