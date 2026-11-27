// Peanuts Native for Linux — the launcher: setup, the library, builds and play, made for the AYN Thor's top screen and its controller (touch and mouse work too).
#include "core.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <cstring>
#include <dirent.h>
#include <map>
#include <sys/stat.h>
#include <unistd.h>
#include <csignal>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb_image.h"

using namespace pn;

// ---------- look ----------
static float S = 1.f;   // scale: 1 at 1080 pixels of height
static ImFont *F_REG, *F_SEMI, *F_DISP;
static const ImU32 C_BG = IM_COL32(13, 15, 20, 255), C_PANEL = IM_COL32(22, 25, 32, 255), C_CARD = IM_COL32(28, 32, 41, 255), C_CARD_HI = IM_COL32(37, 42, 54, 255),
    C_ACCENT = IM_COL32(232, 176, 75, 255), C_ACCENT_SOFT = IM_COL32(243, 208, 138, 255), C_TEXT = IM_COL32(236, 239, 244, 255), C_DIM = IM_COL32(154, 163, 178, 255),
    C_FAINT = IM_COL32(98, 106, 122, 255), C_OK = IM_COL32(91, 209, 139, 255), C_WARN = IM_COL32(242, 166, 90, 255), C_ERR = IM_COL32(242, 109, 109, 255);
static ImVec4 v4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }
static float px(float v) { return std::round(v * S); }

static void apply_style() {
    ImGuiStyle &st = ImGui::GetStyle(); st = ImGuiStyle();
    st.WindowRounding = px(18); st.ChildRounding = px(16); st.FrameRounding = px(12); st.GrabRounding = px(12); st.PopupRounding = px(16); st.ScrollbarRounding = px(10);
    st.WindowPadding = ImVec2(px(28), px(24)); st.FramePadding = ImVec2(px(22), px(14)); st.ItemSpacing = ImVec2(px(16), px(14)); st.ItemInnerSpacing = ImVec2(px(12), px(10));
    st.ScrollbarSize = px(14); st.WindowBorderSize = 0; st.ChildBorderSize = 0; st.FrameBorderSize = 0; st.PopupBorderSize = 0;
    st.FontSizeBase = px(26); st.FontScaleMain = 1.f;
    ImVec4 *c = st.Colors;
    c[ImGuiCol_Text] = v4(C_TEXT); c[ImGuiCol_TextDisabled] = v4(C_FAINT); c[ImGuiCol_WindowBg] = v4(C_BG); c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = v4(C_PANEL); c[ImGuiCol_FrameBg] = v4(C_CARD); c[ImGuiCol_FrameBgHovered] = v4(C_CARD_HI); c[ImGuiCol_FrameBgActive] = v4(C_CARD_HI);
    c[ImGuiCol_Button] = v4(C_CARD); c[ImGuiCol_ButtonHovered] = v4(C_CARD_HI); c[ImGuiCol_ButtonActive] = v4(IM_COL32(48, 54, 68, 255));
    c[ImGuiCol_Header] = v4(C_CARD); c[ImGuiCol_HeaderHovered] = v4(C_CARD_HI); c[ImGuiCol_HeaderActive] = v4(IM_COL32(48, 54, 68, 255));
    c[ImGuiCol_CheckMark] = v4(C_ACCENT); c[ImGuiCol_SliderGrab] = v4(C_ACCENT); c[ImGuiCol_SliderGrabActive] = v4(C_ACCENT_SOFT);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0); c[ImGuiCol_ScrollbarGrab] = v4(IM_COL32(52, 58, 72, 255)); c[ImGuiCol_ScrollbarGrabHovered] = v4(IM_COL32(70, 77, 94, 255));
    c[ImGuiCol_NavCursor] = v4(C_ACCENT); c[ImGuiCol_Separator] = v4(IM_COL32(40, 45, 57, 255)); c[ImGuiCol_TextSelectedBg] = v4(IM_COL32(232, 176, 75, 90));
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.6f); c[ImGuiCol_PlotHistogram] = v4(C_ACCENT);
}

// ---------- textures ----------
static SDL_Renderer *R;
static std::map<std::string, SDL_Texture *> g_tex;
static SDL_Texture *texture(const std::string &path) {
    if (path.empty()) return nullptr;
    auto it = g_tex.find(path); if (it != g_tex.end()) return it->second;
    int w, h, n; unsigned char *px_ = stbi_load(path.c_str(), &w, &h, &n, 4); SDL_Texture *t = nullptr;
    if (px_) { t = SDL_CreateTexture(R, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h); if (t) { SDL_UpdateTexture(t, nullptr, px_, w * 4); SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND); SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR); } stbi_image_free(px_); }
    g_tex[path] = t; return t;
}

// ---------- drawing helpers ----------
static void text_at(ImDrawList *dl, ImFont *f, float size, ImVec2 p, ImU32 col, const char *s, float wrap = 0) { dl->AddText(f, px(size), p, col, s, nullptr, wrap); }
static ImVec2 text_size(ImFont *f, float size, const char *s, float wrap = -1) { return f->CalcTextSizeA(px(size), FLT_MAX, wrap < 0 ? 0 : wrap, s); }
static void draw_peanut(ImDrawList *dl, ImVec2 c, float r, ImU32 col) {   // the mark: a peanut shell, tilted, two lobes with a waist, a little texture
    float ang = -0.5f, ca = std::cos(ang), sa = std::sin(ang);
    auto P_ = [&](float x, float y) { return ImVec2(c.x + (x * ca - y * sa) * r, c.y + (x * sa + y * ca) * r); };
    dl->AddCircleFilled(P_(-0.42f, 0.f), r * 0.56f, col, 40);
    dl->AddCircleFilled(P_(0.44f, 0.f), r * 0.52f, col, 40);
    dl->AddCircleFilled(P_(0.0f, 0.f), r * 0.40f, col, 40);
    ImU32 dot = IM_COL32(13, 15, 20, 70);
    const float d[][2] = {{-0.62f, -0.18f}, {-0.40f, 0.16f}, {-0.22f, -0.22f}, {0.30f, 0.16f}, {0.52f, -0.20f}, {0.66f, 0.12f}};
    for (auto &q : d) dl->AddCircleFilled(P_(q[0], q[1]), r * 0.07f, dot, 12);
    dl->AddCircleFilled(P_(-0.52f, -0.30f), r * 0.12f, IM_COL32(255, 255, 255, 50), 16);
}
static void button_glyph(ImDrawList *dl, ImVec2 c, float r, const char *label, ImU32 bg) {
    dl->AddCircleFilled(c, r, bg, 32);
    ImVec2 ts = text_size(F_SEMI, 18, label); text_at(dl, F_SEMI, 18, ImVec2(c.x - ts.x / 2, c.y - ts.y / 2), IM_COL32(13, 15, 20, 255), label);
}
static void pill(ImDrawList *dl, ImVec2 p, const char *s, ImU32 fg, ImU32 bg) {
    ImVec2 ts = text_size(F_SEMI, 18, s); ImVec2 q(p.x + ts.x + px(24), p.y + ts.y + px(10));
    dl->AddRectFilled(p, q, bg, px(100)); text_at(dl, F_SEMI, 18, ImVec2(p.x + px(12), p.y + px(5)), fg, s);
}
static ImU32 alpha(ImU32 c, float a) { return (c & 0x00FFFFFF) | ((ImU32)(a * 255) << 24); }

// A big button: accent (primary) or plain. Returns true when pressed (click, tap, A on the controller)
static bool big_button(const char *label, float w, bool primary = false, bool enabled = true) {
    ImGui::PushFont(F_SEMI, px(26));
    ImGui::BeginDisabled(!enabled);
    if (primary) { ImGui::PushStyleColor(ImGuiCol_Button, v4(C_ACCENT)); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, v4(C_ACCENT_SOFT)); ImGui::PushStyleColor(ImGuiCol_ButtonActive, v4(C_ACCENT_SOFT)); ImGui::PushStyleColor(ImGuiCol_Text, v4(IM_COL32(20, 16, 8, 255))); }
    bool r = ImGui::Button(label, ImVec2(w, px(68)));
    if (primary) ImGui::PopStyleColor(4);
    ImGui::EndDisabled(); ImGui::PopFont();
    return r && enabled;
}

// ---------- state ----------
enum class Screen { Prepare, Setup, Library, Game, Build, Settings, GameSettings, Browser, Log };
static std::vector<Screen> g_stack;
static Screen cur() { return g_stack.empty() ? Screen::Library : g_stack.back(); }
static void go(Screen s) { g_stack.push_back(s); }
static void back() { if (g_stack.size() > 1) g_stack.pop_back(); }
static std::string g_sel_tid;              // the game the Game page shows
static std::shared_ptr<Job> g_job;         // the build or setup job on screen
static std::string g_job_tid;              // the game it builds

static std::string g_message, g_message_title; static bool g_message_open = false;
static std::string g_log_path, g_log_text;
static std::function<void()> g_steam_run;   // waiting for Steam to close (a job)
static pid_t g_play_pid = -1; static std::string g_play_log; static double g_play_start = 0; static std::string g_play_name;
static SDL_Window *W;
static bool g_focus_first = true;          // put the controller's cursor on the first item of a new screen

static std::string g_confirm_label; static std::function<void()> g_confirm;   // a second button on the message box
static void message(const std::string &title, const std::string &text) { g_message_title = title; g_message = text; g_message_open = true; g_confirm = nullptr; }
static void confirm(const std::string &title, const std::string &text, const std::string &yes, std::function<void()> on_yes) { message(title, text); g_confirm_label = yes; g_confirm = std::move(on_yes); }

// ---------- the file browser (a page of its own: works with the controller, in Game Mode and on the desktop alike) ----------
struct Browser { std::string title, cwd; bool dirs = false; std::vector<std::string> exts; std::function<void(const std::string &)> pick; } B;
static void browse(const std::string &title, const std::string &start, bool dirs, std::vector<std::string> exts, std::function<void(const std::string &)> pick) {
    B.title = title; B.dirs = dirs; B.exts = std::move(exts); B.pick = std::move(pick);
    B.cwd = is_dir(start) ? start : is_file(start) ? dirname_of(start) : home();
    go(Screen::Browser); g_focus_first = true;
}

// ---------- frame pieces ----------
static void topbar(const char *subtitle) {
    ImDrawList *dl = ImGui::GetWindowDrawList(); ImVec2 o = ImGui::GetWindowPos(); float w = ImGui::GetWindowWidth();
    draw_peanut(dl, ImVec2(o.x + px(78), o.y + px(62)), px(26), C_ACCENT);
    text_at(dl, F_DISP, 40, ImVec2(o.x + px(120), o.y + px(36)), C_TEXT, "Peanuts Native");
    if (subtitle && *subtitle) { ImVec2 t = text_size(F_DISP, 40, "Peanuts Native"); text_at(dl, F_REG, 26, ImVec2(o.x + px(136) + t.x, o.y + px(46)), C_DIM, subtitle); }
    char ver[64]; snprintf(ver, sizeof ver, "Linux · %s", P.version.c_str()); ImVec2 vs = text_size(F_REG, 20, ver);
    text_at(dl, F_REG, 20, ImVec2(o.x + w - px(56) - vs.x, o.y + px(50)), C_FAINT, ver);
    ImGui::SetCursorPosY(px(124)); ImGui::Dummy(ImVec2(0, 0)); ImGui::SetCursorPosY(px(124));
}
static void hints(std::initializer_list<std::pair<const char *, const char *>> items) {
    ImDrawList *dl = ImGui::GetForegroundDrawList(); ImVec2 ds = ImGui::GetIO().DisplaySize;
    float y = ds.y - px(48), x = ds.x - px(48);
    std::vector<std::pair<const char *, const char *>> v(items); std::reverse(v.begin(), v.end());
    dl->AddRectFilled(ImVec2(0, ds.y - px(88)), ds, alpha(C_BG, 0.92f));
    for (auto &it : v) {
        ImVec2 ts = text_size(F_REG, 22, it.second); x -= ts.x; text_at(dl, F_REG, 22, ImVec2(x, y - ts.y / 2), C_DIM, it.second);
        x -= px(30); ImU32 bg = !strcmp(it.first, "A") ? C_OK : !strcmp(it.first, "B") ? C_ERR : !strcmp(it.first, "X") ? IM_COL32(110, 160, 255, 255) : C_ACCENT;
        button_glyph(dl, ImVec2(x, y), px(18), it.first, bg); x -= px(40);
    }
}
static bool pad_pressed(ImGuiKey k) { return ImGui::IsKeyPressed(k, false); }
static bool back_pressed() { return pad_pressed(ImGuiKey_GamepadFaceRight) || pad_pressed(ImGuiKey_Escape) || (ImGui::IsMouseClicked(3)); }
// Called right after the widget that should hold the controller's cursor when a screen opens
static void focus_here() { if (g_focus_first) { ImGui::SetItemDefaultFocus(); ImGui::SetKeyboardFocusHere(-1); g_focus_first = false; } }

// ---------- Prepare: the build engine copy on first start ----------
static void screen_prepare() {
    topbar("");
    ImVec2 ds = ImGui::GetIO().DisplaySize; ImDrawList *dl = ImGui::GetWindowDrawList();
    if (!g_job) { g_job = prepare_engine_job(); }
    float t = (float)ImGui::GetTime();
    draw_peanut(dl, ImVec2(ds.x / 2, ds.y / 2 - px(80) + std::sin(t * 3) * px(6)), px(70), C_ACCENT);
    std::string ph = g_job->get_phase(); if (ph.empty()) ph = "Getting ready";
    ImVec2 ts = text_size(F_SEMI, 30, ph.c_str()); text_at(dl, F_SEMI, 30, ImVec2((ds.x - ts.x) / 2, ds.y / 2 + px(30)), C_TEXT, ph.c_str());
    const char *sub = "Peanuts Native is setting up its build tools. This happens once per release.";
    ts = text_size(F_REG, 22, sub); text_at(dl, F_REG, 22, ImVec2((ds.x - ts.x) / 2, ds.y / 2 + px(80)), C_DIM, sub);
    float bw = px(520), bx = (ds.x - bw) / 2, by = ds.y / 2 + px(140);
    dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + px(10)), C_CARD, px(5));
    float seg = std::fmod(t * 0.6f, 1.4f) - 0.4f; dl->AddRectFilled(ImVec2(bx + bw * std::max(0.f, seg), by), ImVec2(bx + bw * std::min(1.f, seg + 0.4f), by + px(10)), C_ACCENT, px(5));
    if (g_job->done) {
        if (g_job->rc == 0 && engine_ok()) { g_job.reset(); g_stack.clear(); go((keys_ok() && is_dir(C.get("games_dir"))) ? Screen::Library : Screen::Setup); if (cur() == Screen::Library) LIB.rescan(); }
        else { std::string err; { std::lock_guard<std::mutex> g(g_job->mu); for (auto &l : g_job->lines) err += l + "\n"; }
               message("The build tools could not be set up", "Peanuts Native needs about 1 GB free in your home folder.\n\n" + err + "\nLog: " + g_job->log_path); g_job.reset(); }
    }
}

// ---------- Setup: keys, firmware, games folder ----------
// One card per step, side by side: its number (a tick once done), what it is, where it stands, and its button(s)
static void setup_card_begin(int n, const char *title, const char *text, bool done, const std::string &status, float w, float h) {
    ImGui::BeginChild(title, ImVec2(w, h), 0, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
    ImDrawList *dl = ImGui::GetWindowDrawList(); ImVec2 p = ImGui::GetWindowPos();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), C_PANEL, px(20));
    if (done) dl->AddRect(p, ImVec2(p.x + w, p.y + h), alpha(C_OK, 0.5f), px(20), 0, px(2));
    ImVec2 c(p.x + px(52), p.y + px(56));
    dl->AddCircleFilled(c, px(26), done ? C_OK : C_ACCENT, 40);
    if (done) { dl->AddLine(ImVec2(c.x - px(11), c.y + px(1)), ImVec2(c.x - px(3), c.y + px(9)), IM_COL32(13, 15, 20, 255), px(4)); dl->AddLine(ImVec2(c.x - px(3), c.y + px(9)), ImVec2(c.x + px(12), c.y - px(8)), IM_COL32(13, 15, 20, 255), px(4)); }
    else { char t[4]; snprintf(t, sizeof t, "%d", n); ImVec2 ts = text_size(F_SEMI, 26, t); text_at(dl, F_SEMI, 26, ImVec2(c.x - ts.x / 2, c.y - ts.y / 2), IM_COL32(13, 15, 20, 255), t); }
    text_at(dl, F_SEMI, 30, ImVec2(p.x + px(96), p.y + px(36)), C_TEXT, title);
    text_at(dl, F_REG, 21, ImVec2(p.x + px(28), p.y + px(110)), C_DIM, text, w - px(56));
    if (!status.empty()) {
        dl->PushClipRect(ImVec2(p.x + px(28), p.y), ImVec2(p.x + w - px(28), p.y + h), true);
        text_at(dl, F_SEMI, 20, ImVec2(p.x + px(28), p.y + h - px(196)), done ? C_OK : C_WARN, status.c_str());
        dl->PopClipRect();
    }
    ImGui::SetCursorPosY(h - px(162)); ImGui::Indent(px(24));
}
static void setup_card_end() { ImGui::Unindent(px(24)); ImGui::EndChild(); }
static void screen_setup() {
    topbar("Setup");
    ImVec2 ds = ImGui::GetIO().DisplaySize; float m = px(72), gap = px(28), w = ds.x - 2 * m, cw = (w - 2 * gap) / 3, ch = std::min(px(450), ds.y - px(470));
    ImGui::SetCursorPos(ImVec2(m, px(132)));
    ImGui::PushFont(F_DISP, px(50)); ImGui::TextUnformatted("Welcome"); ImGui::PopFont();
    ImGui::SetCursorPosX(m); ImGui::PushFont(F_REG, px(25)); ImGui::PushStyleColor(ImGuiCol_Text, v4(C_DIM)); ImGui::PushTextWrapPos(m + w);
    ImGui::TextUnformatted("Peanuts Native turns Switch games you own into native apps for this handheld: each game is rebuilt for its own CPU and GPU, with no emulator in between. Set up these three things once.");
    ImGui::PopTextWrapPos(); ImGui::PopStyleColor(); ImGui::PopFont();
    float y = ImGui::GetCursorPosY() + px(26), bw = cw - px(48);

    bool k = keys_ok(); std::string ks = C.get("keys");
    ImGui::SetCursorPos(ImVec2(m, y));
    std::vector<std::string> found = k ? std::vector<std::string>{} : detect_keys();
    setup_card_begin(1, "Your keys", "prod.keys from your own console. Peanuts Native never downloads keys and never sends them anywhere.", k,
                     k ? basename_of(dirname_of(ks)) + "/" + basename_of(ks) : found.empty() ? "Not set" : "Found one on this system", cw, ch);
    if (!found.empty()) { std::string l = "Use " + basename_of(dirname_of(dirname_of(found[0]))) + "'s keys"; if (big_button(l.c_str(), bw, true)) C.set("keys", found[0]); focus_here(); }
    else ImGui::Dummy(ImVec2(bw, px(68)));
    if (big_button(k ? "Choose another file" : "Choose prod.keys", bw, !k && found.empty())) browse("Choose your prod.keys", ks.empty() ? home() : ks, false, {".keys"}, [](const std::string &p) { C.set("keys", p); });
    if (!k && found.empty()) focus_here();
    setup_card_end();

    std::string fw = C.get("firmware"); bool f = is_file(P.data + "/firmware/fonts/standard.ttf");
    ImGui::SetCursorPos(ImVec2(m + cw + gap, y));
    setup_card_begin(2, "Your firmware", "Optional. A firmware dump from your console, as a folder or a .zip: games then show their own fonts and Miis.", f,
                     f ? "Fonts read" : "Not set (games use stand-in fonts)", cw, ch);
    ImGui::Dummy(ImVec2(bw, px(68)));
    ImGui::BeginDisabled(!k);
    if (big_button(f ? "Read another firmware" : "Choose firmware", bw)) browse("Choose your firmware folder or .zip", fw.empty() ? home() : fw, false, {".zip", "/"}, [](const std::string &p) {
        C.set("firmware", p); g_job = extract_fonts_job(p); g_job_tid.clear(); go(Screen::Build); g_focus_first = true; });
    ImGui::EndDisabled();
    setup_card_end();

    std::string gd = C.get("games_dir"); bool g = is_dir(gd);
    ImGui::SetCursorPos(ImVec2(m + 2 * (cw + gap), y));
    setup_card_begin(3, "Your games", "The folder with your dumps: .nsp, .xci, .nsz, .xcz or .nup, with their updates and DLC. Sub-folders count too.", g,
                     g ? gd : "Not set", cw, ch);
    ImGui::Dummy(ImVec2(bw, px(68)));
    if (big_button(g ? "Choose another folder" : "Choose games folder", bw, k && !g)) browse("Choose your games folder", gd.empty() ? home() : gd, true, {}, [](const std::string &p) { C.set("games_dir", p); });
    if (k && !g) focus_here();
    setup_card_end();

    ImGui::SetCursorPos(ImVec2((ds.x - px(560)) / 2, y + ch + px(34)));
    if (big_button("Go to my games", px(560), true, k && g)) { C.set("setup_done", "1"); g_stack.clear(); go(Screen::Library); LIB.rescan(); g_focus_first = true; }
    if (k && g) focus_here();
    hints({{"A", "Select"}});
}

// ---------- Library ----------
static const char *state_text(const Game &g, ImU32 &fg, ImU32 &bg) {
    switch (g.state) {
    case GameState::Ready: fg = C_OK; bg = alpha(C_OK, 0.16f); return "Ready";
    case GameState::NeedsRebuild: fg = C_WARN; bg = alpha(C_WARN, 0.16f); return "Rebuild";
    case GameState::NeedsBuild: fg = C_ACCENT_SOFT; bg = alpha(C_ACCENT, 0.16f); return g.version_supported ? "Build" : "Build · update?";
    case GameState::Building: fg = C_ACCENT_SOFT; bg = alpha(C_ACCENT, 0.16f); return "Building";
    default: fg = C_DIM; bg = alpha(C_DIM, 0.14f); return "Not supported yet";
    }
}
static void draw_cover(ImDrawList *dl, const Game &g, ImVec2 a, ImVec2 b, float rounding) {
    SDL_Texture *t = texture(g.icon_path);
    if (t) dl->AddImageRounded((ImTextureID)(intptr_t)t, a, b, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, rounding);
    else {
        dl->AddRectFilledMultiColor(a, b, IM_COL32(44, 50, 64, 255), IM_COL32(34, 38, 50, 255), IM_COL32(26, 29, 38, 255), IM_COL32(34, 38, 50, 255));
        dl->AddRect(a, b, IM_COL32(0, 0, 0, 0), rounding);
        std::string ini; for (auto &w : split(g.name, ' ')) if (!w.empty() && isalnum((unsigned char)w[0]) && ini.size() < 2) ini += (char)toupper((unsigned char)w[0]);
        float sz = (b.y - a.y) * 0.32f / S; ImVec2 ts = text_size(F_DISP, sz, ini.c_str());
        text_at(dl, F_DISP, sz, ImVec2((a.x + b.x - ts.x) / 2, (a.y + b.y - ts.y) / 2), IM_COL32(120, 130, 150, 255), ini.c_str());
    }
}
// ---------- updates ----------
static std::atomic<int> g_upd_state{0};   // 0 not checked, 1 checking, 2 a newer one, 3 up to date, 4 could not check
static UpdateInfo g_upd; static std::string g_upd_why; static std::mutex g_upd_mu; static bool g_upd_offered = false;
static void update_check_async() {
    if (g_upd_state == 1) return;
    g_upd_state = 1;
    std::thread([] {
        UpdateInfo u; std::string why; bool ok = update_check(u, why);
        std::lock_guard<std::mutex> l(g_upd_mu);
        if (!ok) { g_upd_why = why; g_upd_state = 4; return; }
        g_upd = u; g_upd_state = version_newer(u.version, P.version) ? 2 : 3;
    }).detach();
}
static void restart_self() {
    const char *ai = getenv("APPIMAGE");
    if (!ai) return;
    fflush(nullptr);
    execl(ai, ai, (char *)nullptr);
    message("Restart Peanuts Native", std::string("The update is installed. Close Peanuts Native and open it again (") + strerror(errno) + ").");
}
static void update_start() {
    UpdateInfo u; { std::lock_guard<std::mutex> l(g_upd_mu); u = g_upd; }
    std::string why; if (!update_possible(why)) { message("Cannot update here", "Peanuts Native can only update itself when it runs from its AppImage file: " + why + ".\n\nDownload it from github.com/" + update_repo() + "/releases instead."); return; }
    g_job = update_job(u); g_job_tid.clear();
    std::string ver = u.version;
    g_steam_run = [ver] { confirm("Updated to " + ver, "Peanuts Native " + ver + " is installed. Restart now to use it? Your games, settings and Steam shortcuts stay as they are.", "Restart now", [] { restart_self(); }); };
    go(Screen::Build); g_focus_first = true;
}
static void update_offer() {
    UpdateInfo u; { std::lock_guard<std::mutex> l(g_upd_mu); u = g_upd; }
    std::string notes = u.notes.size() > 700 ? u.notes.substr(0, 700) + "…" : u.notes;
    confirm("Update available: " + u.version, "You have " + P.version + ". " + (notes.empty() ? std::string("") : "What's new:\n" + notes + "\n\n") + "The update downloads the new AppImage and puts it in place of this one.",
            "Update now", [] { update_start(); });
}

static void screen_library() {
    if (g_upd_state == 2 && !g_upd_offered && !g_message_open && !(g_job && g_job->running)) { g_upd_offered = true; update_offer(); }
    char sub[96]; auto games = LIB.snapshot(); int nsup = 0, nready = 0; for (auto &g : games) { if (g.cat) nsup++; if (g.state == GameState::Ready) nready++; }
    snprintf(sub, sizeof sub, "%d games · %d ready", (int)games.size(), nready); topbar(sub);
    ImDrawList *dl = ImGui::GetWindowDrawList(); ImVec2 ds = ImGui::GetIO().DisplaySize;
    if (LIB.scanning) {
        char s[96]; snprintf(s, sizeof s, "Looking through your games… %d / %d", LIB.scan_done.load(), LIB.scan_total.load());
        ImVec2 o = ImGui::GetWindowPos(); text_at(dl, F_REG, 22, ImVec2(o.x + px(56), o.y + px(96)), C_ACCENT_SOFT, s);
    } else if (LIB.unreadable > 0) {
        char s[200]; int n = LIB.unreadable; snprintf(s, sizeof s, "%d file%s could not be read: are your keys from the console these games were dumped on, and new enough for them?", n, n == 1 ? "" : "s");
        ImVec2 o = ImGui::GetWindowPos(); text_at(dl, F_REG, 22, ImVec2(o.x + px(56), o.y + px(96)), C_WARN, s);
    }
    if (games.empty() && !LIB.scanning) {
        const char *t = LIB.scan_error.empty() ? "No games found yet" : "Your games folder is missing";
        std::string s = LIB.scan_error.empty() ? "Put your .nsp, .xci, .nsz, .xcz or .nup files in\n" + C.get("games_dir") + "\nand rescan." : LIB.scan_error;
        draw_peanut(dl, ImVec2(ds.x / 2, ds.y / 2 - px(120)), px(60), IM_COL32(122, 94, 46, 255));
        ImVec2 ts = text_size(F_SEMI, 34, t); text_at(dl, F_SEMI, 34, ImVec2((ds.x - ts.x) / 2, ds.y / 2 - px(30)), C_TEXT, t);
        for (int i = 0; auto &l : split(s, '\n')) { ts = text_size(F_REG, 22, l.c_str()); text_at(dl, F_REG, 22, ImVec2((ds.x - ts.x) / 2, ds.y / 2 + px(24) + i++ * px(32)), C_DIM, l.c_str()); }
        ImGui::SetCursorPos(ImVec2((ds.x - px(640)) / 2, ds.y / 2 + px(140)));
        ImGui::BeginGroup();
        if (big_button("Rescan", px(310), true)) LIB.rescan();
        focus_here();
        ImGui::SameLine(); if (big_button("Change folder", px(310))) browse("Choose your games folder", C.get("games_dir"), true, {}, [](const std::string &p) { C.set("games_dir", p); LIB.rescan(); });
        ImGui::EndGroup();
    } else {
        float cw = px(264), ch = px(370), gap = px(30), avail = ImGui::GetContentRegionAvail().x - px(96);
        int cols = std::max(1, (int)((avail + gap) / (cw + gap)));
        float total = cols * cw + (cols - 1) * gap, x0 = (ImGui::GetWindowWidth() - total) / 2;
        ImGui::SetCursorPos(ImVec2(0, px(150)));
        ImGui::BeginChild("grid", ImVec2(ImGui::GetWindowWidth(), ds.y - px(150) - px(92)), 0, ImGuiWindowFlags_NoBackground);
        bool shown_unsupported_header = false; int col = 0; float y = px(8);
        for (size_t i = 0; i < games.size(); i++) {
            const Game &g = games[i];
            if (!g.cat && !shown_unsupported_header) {
                if (col) { y += ch + gap; col = 0; }
                shown_unsupported_header = true;
                ImGui::SetCursorPos(ImVec2(x0, y)); ImGui::PushFont(F_SEMI, px(26)); ImGui::PushStyleColor(ImGuiCol_Text, v4(C_DIM));
                ImGui::TextUnformatted(nsup ? "Not supported yet" : "None of these games is supported yet"); ImGui::PopStyleColor(); ImGui::PopFont();
                y += px(52);
            }
            ImGui::SetCursorPos(ImVec2(x0 + col * (cw + gap), y));
            ImGui::PushID(g.tid.c_str());
            bool pressed = ImGui::InvisibleButton("card", ImVec2(cw, ch));
            if (i == 0) focus_here();
            bool hov = ImGui::IsItemHovered() || ImGui::IsItemFocused();
            ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax(); ImDrawList *gdl = ImGui::GetWindowDrawList();
            float lift = hov ? px(6) : 0; a.y -= lift; b.y -= lift;
            if (hov) gdl->AddRect(ImVec2(a.x - px(5), a.y - px(5)), ImVec2(b.x + px(5), b.y + px(5)), C_ACCENT, px(22), 0, px(4));
            gdl->AddRectFilled(a, b, hov ? C_CARD_HI : C_CARD, px(18));
            draw_cover(gdl, g, ImVec2(a.x + px(12), a.y + px(12)), ImVec2(b.x - px(12), a.y + px(12) + (cw - px(24))), px(12));
            if (!g.cat) gdl->AddRectFilled(ImVec2(a.x + px(12), a.y + px(12)), ImVec2(b.x - px(12), a.y + px(12) + (cw - px(24))), IM_COL32(13, 15, 20, 130), px(12));
            float ty = a.y + cw + px(4);
            gdl->PushClipRect(ImVec2(a.x + px(14), ty), ImVec2(b.x - px(14), b.y), true);
            text_at(gdl, F_SEMI, 22, ImVec2(a.x + px(16), ty), C_TEXT, g.name.c_str(), b.x - a.x - px(32));
            gdl->PopClipRect();
            ImU32 fg, bg; const char *st = state_text(g, fg, bg);
            if (g_job && g_job->running && g_job_tid == g.tid) { st = "Building"; fg = C_ACCENT_SOFT; bg = alpha(C_ACCENT, 0.16f); }
            pill(gdl, ImVec2(a.x + px(14), b.y - px(48)), st, fg, bg);
            if (pressed) { g_sel_tid = g.tid; go(Screen::Game); g_focus_first = true; }
            ImGui::PopID();
            if (++col == cols) { col = 0; y += ch + gap; }
        }
        ImGui::SetCursorPos(ImVec2(0, y + (col ? ch + gap : 0) + px(40))); ImGui::Dummy(ImVec2(1, 1));
        ImGui::EndChild();
    }
    if (pad_pressed(ImGuiKey_GamepadFaceUp) || pad_pressed(ImGuiKey_F2)) { go(Screen::Settings); g_focus_first = true; }
    if (pad_pressed(ImGuiKey_GamepadFaceLeft) || pad_pressed(ImGuiKey_F5)) LIB.rescan();
    hints({{"A", "Open"}, {"X", "Rescan"}, {"Y", "Settings"}});
}

// ---------- Game page ----------
static std::string version_label(const Game &g, uint32_t v) {   // the catalog's own name for an update ("1.3.0"), else its number
    if (g.cat) for (auto &b : g.cat->builds) if (b.second == v) return v ? "update " + b.first : "base game " + b.first;
    return v ? "update v" + std::to_string(v) : "base game";
}
static bool find_game(const std::string &tid, Game &out) { for (auto &g : LIB.snapshot()) if (g.tid == tid) { out = g; return true; } return false; }
static void start_build(const Game &g, bool force) { g_job = build_job(g, force); g_job_tid = g.tid; go(Screen::Build); g_focus_first = true; }
static void start_play(const Game &g) {
    std::string err;
    pid_t pid = play_start(g, g_play_log, err);
    if (pid < 0) { message("The game could not start", err); return; }
    g_play_pid = pid; g_play_start = ImGui::GetTime(); g_play_name = g.name;
    SDL_HideWindow(W);   // the game takes the screen; the launcher comes back when it ends
}
// Add to Steam: straight away when Steam is closed; in Desktop Mode it can close Steam first (a job: Steam takes a few seconds to go)
static void add_to_steam(const std::vector<Game> &games) {
    auto run = [games] {
        std::string msg, all; int ok = 0;
        for (auto &g : games) { if (steam_add(g, msg)) ok++; else { message("Not added to Steam", msg); return; } all = msg; }
        if (games.size() == 1) message("Added to Steam", all);
        else message("Added to Steam", std::to_string(ok) + " games were added with their artwork. They show up the next time Steam starts: in Game Mode they are under Library → Non-Steam.");
    };
    if (steam_users().empty()) { message("Steam is not set up", "No Steam account was found on this handheld. Open Steam once and sign in, then try again."); return; }
    if (!steam_running()) { run(); return; }
    if (steam_game_mode()) { std::string m; steam_add(games[0], m); message("Switch to Desktop Mode first", m); return; }
    confirm("Close Steam first?", "Steam is open, and it rewrites its list of shortcuts when it closes, which would undo this. Peanuts Native can close Steam now, add the "
            + std::string(games.size() == 1 ? "game" : "games") + ", and you start Steam again afterwards (or switch back to Game Mode).", "Close Steam and add", [games, run] {
        g_job = start_job("Adding to Steam", P.logs + "/steam-" + now_stamp() + ".log", {[](Job &j) {
            j.set_phase("Closing Steam");
            std::string out; run_capture({"steam", "-shutdown"}, {}, out);
            if (steam_running()) run_capture({"flatpak", "run", "com.valvesoftware.Steam", "-shutdown"}, {}, out);
            for (int i = 0; i < 300 && steam_running(); i++) { if (j.cancelled) return 1; usleep(100000); }
            if (steam_running()) { j.add_line("[RED] Steam did not close within 30 seconds. Close it yourself (Steam menu → Exit) and try again."); return 1; }
            j.add_line("Steam is closed"); return 0; }});
        g_job_tid.clear(); g_steam_run = run; go(Screen::Build); g_focus_first = true; });
}
static void info_row(const char *k, const std::string &v, ImU32 col = C_TEXT) {
    ImGui::PushFont(F_REG, px(22)); ImGui::PushStyleColor(ImGuiCol_Text, v4(C_DIM)); ImGui::TextUnformatted(k); ImGui::PopStyleColor();
    ImGui::SameLine(px(260)); ImGui::PushStyleColor(ImGuiCol_Text, v4(col)); ImGui::TextWrapped("%s", v.c_str()); ImGui::PopStyleColor(); ImGui::PopFont();
}
static void screen_game() {
    Game g; if (!find_game(g_sel_tid, g)) { back(); return; }
    topbar("");
    ImVec2 ds = ImGui::GetIO().DisplaySize; float m = px(56), cover = std::min(px(420), ds.y - px(330));
    ImDrawList *dl = ImGui::GetWindowDrawList(); ImVec2 o = ImGui::GetWindowPos();
    // a soft wash of the accent behind the cover
    dl->AddRectFilledMultiColor(ImVec2(o.x, o.y + px(110)), ImVec2(o.x + ds.x, o.y + ds.y), alpha(C_ACCENT, 0.08f), alpha(C_ACCENT, 0.02f), alpha(C_ACCENT, 0.0f), alpha(C_ACCENT, 0.0f));
    draw_cover(dl, g, ImVec2(o.x + m, o.y + px(140)), ImVec2(o.x + m + cover, o.y + px(140) + cover), px(22));
    float x = m + cover + px(56), w = ds.x - x - m;
    ImGui::SetCursorPos(ImVec2(x, px(132)));
    ImGui::BeginChild("info", ImVec2(w, ds.y - px(132) - px(100)), 0, ImGuiWindowFlags_NoBackground);
    ImGui::PushFont(F_DISP, px(44)); ImGui::PushTextWrapPos(w); ImGui::TextUnformatted(g.name.c_str()); ImGui::PopTextWrapPos(); ImGui::PopFont();
    ImU32 fg, bg; const char *st = state_text(g, fg, bg);
    bool building = g_job && g_job->running && g_job_tid == g.tid;
    ImVec2 pp = ImGui::GetCursorScreenPos(); pill(ImGui::GetWindowDrawList(), pp, building ? "Building" : st, fg, bg); ImGui::Dummy(ImVec2(0, px(44)));
    std::string files = g.is_nup ? basename_of(g.base) : (g.base.empty() ? "no base game file" : "base game");
    if (!g.is_nup) { for (auto &u : g.updates) files += ", " + version_label(g, u.version); if (!g.dlcs.empty()) files += ", " + std::to_string(g.dlcs.size()) + " DLC"; }
    info_row("Title ID", g.tid);
    info_row("Your files", files);
    if (g.cat) {
        info_row("Supported", g.supported_versions().empty() ? "base game" : "update " + g.supported_versions());
        info_row("Builds with", version_label(g, g.version) + (g.version_supported ? "" : "  (not a supported update: it may not work)"), g.version_supported ? C_TEXT : C_WARN);
        { std::string lab = companion_label(g.tid);
          if (!lab.empty()) info_row("Bottom screen", C.game(g.tid, "bottom_screen", "1") == "1" ? lab : lab + " (off in Game settings)", C.game(g.tid, "bottom_screen", "1") == "1" ? C_OK : C_DIM);
          else if (g.cat->companion) info_row("Bottom screen", "Has an Eden Duo companion that is not bundled yet: import its .dsmod.zip in Game settings", C_DIM); }
    } else info_row("Status", "Peanuts Native has no profile for this game yet. Supported games are listed on the Peanuts Native page.", C_DIM);
    if (!g.pack_dir.empty()) info_row("Build", g.state == GameState::Ready ? "ready, made from your " + version_label(g, g.version) : "made by an older release or from another update: build again", g.state == GameState::Ready ? C_OK : C_WARN);
    ImGui::Dummy(ImVec2(0, px(18)));
    float bw = std::min(w, px(560));
    if (g.cat) {
        if (building) { if (big_button("Show the build", bw, true)) { go(Screen::Build); g_focus_first = true; } focus_here(); }
        else if (g.state == GameState::Ready) {
            if (big_button("Play", bw, true)) start_play(g);
            focus_here();
            if (big_button(steam_has(g) ? "Update Steam shortcut" : "Add to Steam", bw)) add_to_steam({g});
            if (big_button("Game settings", bw)) { go(Screen::GameSettings); g_focus_first = true; }
            if (big_button("Build again", bw)) start_build(g, false);
            { std::string q = C.get("quit_combo", "1");
              const char *qt = q == "1" ? "To quit: hold Select + Start" : q == "2" ? "To quit: hold L + R + Select + Start" : q == "3" ? "To quit: hold L3 + R3 (press both sticks)" : "";
              std::string ts = std::string("In-game menu: press Select + Start together") + (*qt ? std::string("\n") + qt : std::string()); const char *t = ts.c_str();
              { ImGui::PushFont(F_REG, px(20)); ImGui::PushStyleColor(ImGuiCol_Text, v4(C_DIM)); ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + bw); ImGui::TextWrapped("%s", t); ImGui::PopTextWrapPos(); ImGui::PopStyleColor(); ImGui::PopFont(); } }
        } else {
            bool can = !(g_job && g_job->running);
            if (big_button(g.state == GameState::NeedsRebuild ? "Build again" : "Build", bw, true, can && (g.is_nup || !g.base.empty()))) start_build(g, g.state == GameState::NeedsRebuild);
            focus_here();
            if (!can) { ImGui::PushStyleColor(ImGuiCol_Text, v4(C_DIM)); ImGui::TextUnformatted("Another build is running."); ImGui::PopStyleColor(); }
            if (!g.is_nup && g.base.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, v4(C_WARN)); ImGui::TextWrapped("The base game's file is not in your games folder. Add it and rescan."); ImGui::PopStyleColor(); }
            if (!g.pack_dir.empty() && big_button("Play the old build", bw)) start_play(g);
            if (big_button("Game settings", bw)) { go(Screen::GameSettings); g_focus_first = true; }
            ImGui::PushFont(F_REG, px(21)); ImGui::PushStyleColor(ImGuiCol_Text, v4(C_DIM));
            ImGui::TextWrapped("A build reads your game files, rewrites the game's code for this CPU and translates its shaders for this GPU. Most games take 5 to 40 minutes; you can keep using the handheld meanwhile.");
            ImGui::PopStyleColor(); ImGui::PopFont();
        }
    } else { if (big_button("Back to my games", bw)) back(); focus_here(); }
    ImGui::EndChild();
    if (back_pressed()) back();
    hints({{"A", "Select"}, {"B", "Back"}});
}

// ---------- Build progress (a game's build, or a setup job) ----------
static void screen_build() {
    if (!g_job) { back(); return; }
    topbar(g_job->title.c_str());
    ImVec2 ds = ImGui::GetIO().DisplaySize; float m = px(56), w = ds.x - 2 * m;
    ImDrawList *dl = ImGui::GetWindowDrawList(); ImVec2 o = ImGui::GetWindowPos();
    bool done = g_job->done, ok = done && g_job->rc == 0;
    if (done && g_steam_run) {   // Steam has closed (or not): add the games now, or leave the job's message on screen
        auto f = g_steam_run; g_steam_run = nullptr;
        if (ok) { g_job.reset(); back(); f(); return; }
    }
    std::string ph = done ? (ok ? (g_job_tid.empty() ? "Done" : "Ready to play") : (g_job->cancelled ? "Stopped" : "The build stopped")) : g_job->get_phase();
    double el = (done ? g_job->ended : std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count()) - g_job->started;
    char tm[64]; snprintf(tm, sizeof tm, "%d:%02d", (int)el / 60, (int)el % 60);
    text_at(dl, F_DISP, 40, ImVec2(o.x + m, o.y + px(136)), done ? (ok ? C_OK : C_ERR) : C_TEXT, ph.c_str());
    ImVec2 ts = text_size(F_REG, 26, tm); text_at(dl, F_REG, 26, ImVec2(o.x + ds.x - m - ts.x, o.y + px(148)), C_DIM, tm);
    float by = o.y + px(206), p = g_job->progress;
    dl->AddRectFilled(ImVec2(o.x + m, by), ImVec2(o.x + m + w, by + px(12)), C_CARD, px(6));
    if (done) dl->AddRectFilled(ImVec2(o.x + m, by), ImVec2(o.x + m + w, by + px(12)), ok ? C_OK : C_ERR, px(6));
    else if (p >= 0) dl->AddRectFilled(ImVec2(o.x + m, by), ImVec2(o.x + m + w * std::min(1.f, p), by + px(12)), C_ACCENT, px(6));
    else { float t = std::fmod((float)ImGui::GetTime() * 0.5f, 1.3f) - 0.3f; dl->AddRectFilled(ImVec2(o.x + m + w * std::max(0.f, t), by), ImVec2(o.x + m + w * std::min(1.f, t + 0.3f), by + px(12)), C_ACCENT, px(6)); }
    // the output, newest at the bottom
    ImGui::SetCursorPos(ImVec2(m, px(246)));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, v4(C_PANEL));
    ImGui::BeginChild("log", ImVec2(w, ds.y - px(246) - px(200)), 0, 0);
    ImGui::PushFont(F_REG, px(19));
    { std::lock_guard<std::mutex> g(g_job->mu);
      for (auto &l : g_job->lines) { bool red = l.find("[RED]") != std::string::npos; if (red) ImGui::PushStyleColor(ImGuiCol_Text, v4(C_ERR)); else ImGui::PushStyleColor(ImGuiCol_Text, v4(C_DIM)); ImGui::TextWrapped("%s", l.c_str()); ImGui::PopStyleColor(); } }
    if (!done) ImGui::SetScrollHereY(1.0f);
    ImGui::PopFont(); ImGui::EndChild(); ImGui::PopStyleColor();
    ImGui::SetCursorPos(ImVec2(m, ds.y - px(178)));
    if (!done) { if (big_button("Stop", px(300))) g_job->cancel(); focus_here(); ImGui::SameLine(); if (big_button("Back (keeps building)", px(420))) back(); }
    else {
        if (ok && !g_job_tid.empty()) {
            static Job *refreshed = nullptr; if (refreshed != g_job.get()) { LIB.refresh_builds(); refreshed = g_job.get(); }   // once per finished build
            Game g;
            if (find_game(g_job_tid, g) && g.state == GameState::Ready) { if (big_button("Play now", px(300), true)) { g_job.reset(); back(); start_play(g); return; } focus_here(); ImGui::SameLine(); }
        }
        if (big_button("Done", px(300), g_job_tid.empty() || !ok)) { if (!ok && !g_job->cancelled) {} g_job.reset(); back(); g_focus_first = true; return; }
        if (!ok || g_job_tid.empty()) focus_here();
        ImGui::SameLine(); if (big_button("Open the full log", px(380))) { g_log_path = g_job->log_path; g_log_text = read_file(g_log_path, 4 << 20); go(Screen::Log); g_focus_first = true; }
        if (!ok) { ImGui::PushFont(F_REG, px(21)); ImGui::PushStyleColor(ImGuiCol_Text, v4(C_DIM)); ImGui::Text("Log: %s", g_job->log_path.c_str()); ImGui::PopStyleColor(); ImGui::PopFont(); }
    }
    if (back_pressed() && !g_stack.empty()) { if (done) g_job.reset(); back(); }
    hints({{"A", "Select"}, {"B", done ? "Close" : "Back"}});
}

// ---------- Log viewer ----------
static void screen_log() {
    topbar(basename_of(g_log_path).c_str());
    ImVec2 ds = ImGui::GetIO().DisplaySize; float m = px(56);
    ImGui::SetCursorPos(ImVec2(m, px(130)));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, v4(C_PANEL));
    ImGui::BeginChild("logv", ImVec2(ds.x - 2 * m, ds.y - px(130) - px(110)), 0, 0);
    ImGui::PushFont(F_REG, px(18));
    ImGui::TextUnformatted(g_log_text.c_str(), g_log_text.c_str() + g_log_text.size());
    if (g_focus_first) { ImGui::SetScrollHereY(1.0f); g_focus_first = false; }
    float sy = ImGui::GetScrollY(), step = px(60);
    if (ImGui::IsKeyDown(ImGuiKey_GamepadDpadDown) || ImGui::IsKeyDown(ImGuiKey_DownArrow) || ImGui::IsKeyDown(ImGuiKey_GamepadLStickDown)) ImGui::SetScrollY(sy + step * 0.5f);
    if (ImGui::IsKeyDown(ImGuiKey_GamepadDpadUp) || ImGui::IsKeyDown(ImGuiKey_UpArrow) || ImGui::IsKeyDown(ImGuiKey_GamepadLStickUp)) ImGui::SetScrollY(sy - step * 0.5f);
    ImGui::PopFont(); ImGui::EndChild(); ImGui::PopStyleColor();
    if (back_pressed()) back();
    hints({{"B", "Back"}});
}

// ---------- Browser ----------
static void screen_browser() {
    topbar(B.title.c_str());
    ImVec2 ds = ImGui::GetIO().DisplaySize; float m = px(56), w = ds.x - 2 * m;
    ImGui::SetCursorPos(ImVec2(m, px(124)));
    ImGui::PushFont(F_SEMI, px(24)); ImGui::PushStyleColor(ImGuiCol_Text, v4(C_ACCENT_SOFT)); ImGui::TextUnformatted(B.cwd.c_str()); ImGui::PopStyleColor(); ImGui::PopFont();
    // places
    std::vector<std::pair<std::string, std::string>> places = {{"Home", home()}};
    const char *user = getenv("USER");
    for (std::string base : {std::string("/run/media/") + (user ? user : ""), std::string("/run/media"), std::string("/media/") + (user ? user : ""), std::string("/mnt")}) {
        DIR *d = opendir(base.c_str()); if (!d) continue;
        while (auto *e = readdir(d)) { if (e->d_name[0] == '.') continue; std::string p = base + "/" + e->d_name; if (is_dir(p) && base != "/run/media") places.push_back({e->d_name, p}); }
        closedir(d);
    }
    for (auto &n : {"Downloads", "Emulation", "Games", "ROMs"}) if (is_dir(home() + "/" + n)) places.push_back({n, home() + "/" + n});
    ImGui::SetCursorPosX(m);
    for (size_t i = 0; i < places.size() && i < 8; i++) { if (i) ImGui::SameLine(); ImGui::PushFont(F_REG, px(22)); if (ImGui::Button(places[i].first.c_str())) B.cwd = places[i].second; ImGui::PopFont(); }
    // entries
    std::vector<std::pair<std::string, bool>> ents;
    if (DIR *d = opendir(B.cwd.c_str())) {
        while (auto *e = readdir(d)) {
            std::string n = e->d_name; if (n == "." || n == ".." || n[0] == '.') continue;
            std::string p = B.cwd + "/" + n; bool dir = is_dir(p);
            if (!dir) { if (B.dirs) continue; bool okx = B.exts.empty(); for (auto &x : B.exts) if (x != "/" && lower(n).size() >= x.size() && lower(n).compare(n.size() - x.size(), x.size(), x) == 0) okx = true; if (!okx) continue; }
            ents.push_back({n, dir});
        }
        closedir(d);
    }
    std::sort(ents.begin(), ents.end(), [](auto &a, auto &b) { if (a.second != b.second) return a.second; return lower(a.first) < lower(b.first); });
    float listh = ds.y - ImGui::GetCursorPosY() - px(200);
    ImGui::SetCursorPosX(m);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, v4(C_PANEL));
    ImGui::BeginChild("ents", ImVec2(w, listh), 0, 0);
    ImGui::PushFont(F_REG, px(24));
    bool first = true; float rw = w - px(24);
    auto row = [&](const std::string &name, int kind) -> bool {   // kind: 0 up, 1 folder, 2 file
        ImGui::PushID(name.c_str()); ImVec2 p = ImGui::GetCursorScreenPos(); float h = px(58);
        bool pressed = ImGui::InvisibleButton("r", ImVec2(rw, h));
        bool hov = ImGui::IsItemHovered() || ImGui::IsItemFocused(); ImDrawList *dl = ImGui::GetWindowDrawList();
        if (hov) { dl->AddRectFilled(p, ImVec2(p.x + rw, p.y + h), C_CARD_HI, px(10)); dl->AddRect(p, ImVec2(p.x + rw, p.y + h), C_ACCENT, px(10), 0, px(2)); }
        float ix = p.x + px(22), iy = p.y + h / 2;
        if (kind == 1) { dl->AddRectFilled(ImVec2(ix, iy - px(10)), ImVec2(ix + px(30), iy + px(12)), C_ACCENT, px(3)); dl->AddRectFilled(ImVec2(ix, iy - px(14)), ImVec2(ix + px(13), iy - px(8)), C_ACCENT, px(2)); }
        else if (kind == 2) { dl->AddRectFilled(ImVec2(ix + px(4), iy - px(14)), ImVec2(ix + px(26), iy + px(14)), C_DIM, px(3)); dl->AddRectFilled(ImVec2(ix + px(8), iy - px(6)), ImVec2(ix + px(22), iy - px(4)), C_PANEL); dl->AddRectFilled(ImVec2(ix + px(8), iy + px(1)), ImVec2(ix + px(22), iy + px(3)), C_PANEL); }
        else { dl->AddTriangleFilled(ImVec2(ix + px(15), iy - px(12)), ImVec2(ix + px(3), iy + px(6)), ImVec2(ix + px(27), iy + px(6)), C_DIM); }
        ImVec2 ts = text_size(F_REG, 24, name.c_str());
        text_at(dl, kind == 2 ? F_SEMI : F_REG, 24, ImVec2(p.x + px(72), iy - ts.y / 2), kind == 2 ? C_ACCENT_SOFT : C_TEXT, name.c_str());
        ImGui::PopID(); return pressed;
    };
    if (B.cwd != "/") { if (row("Up one folder", 0)) { B.cwd = dirname_of(B.cwd); g_focus_first = true; } if (first) focus_here(); first = false; }
    for (auto &e : ents) {
        if (row(e.first, e.second ? 1 : 2)) {
            std::string p = B.cwd + "/" + e.first;
            if (e.second) { B.cwd = p; g_focus_first = true; }
            else { auto pick = B.pick; back(); g_focus_first = true; pick(p); ImGui::PopFont(); ImGui::EndChild(); ImGui::PopStyleColor(); return; }
        }
        if (first) focus_here();
        first = false;
    }
    if (ents.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, v4(C_FAINT)); ImGui::TextUnformatted(B.dirs ? "No folders here." : "No matching files here."); ImGui::PopStyleColor(); }
    ImGui::PopFont(); ImGui::EndChild(); ImGui::PopStyleColor();
    ImGui::SetCursorPos(ImVec2(m, ds.y - px(170)));
    bool want_dir = B.dirs || std::find(B.exts.begin(), B.exts.end(), "/") != B.exts.end();
    if (want_dir) { if (big_button("Use this folder", px(380), true)) { auto pick = B.pick; std::string p = B.cwd; back(); g_focus_first = true; pick(p); return; } ImGui::SameLine(); }
    if (big_button("Cancel", px(260))) { back(); g_focus_first = true; return; }
    if (back_pressed()) { if (B.cwd != "/" && !pad_pressed(ImGuiKey_Escape)) { B.cwd = dirname_of(B.cwd); g_focus_first = true; } else back(); }
    if (pad_pressed(ImGuiKey_GamepadFaceUp) && want_dir) { auto pick = B.pick; std::string p = B.cwd; back(); pick(p); return; }
    hints(want_dir ? std::initializer_list<std::pair<const char *, const char *>>{{"A", "Open"}, {"Y", "Use this folder"}, {"B", "Up"}}
                   : std::initializer_list<std::pair<const char *, const char *>>{{"A", "Open / choose"}, {"B", "Up"}});
}

// ---------- Settings ----------
static bool toggle_row(const char *label, const char *desc, bool v, float w) {
    ImGui::PushID(label);
    ImVec2 p = ImGui::GetCursorScreenPos(); float h = px(desc ? 96 : 70);
    bool pressed = ImGui::InvisibleButton("row", ImVec2(w, h));
    bool hov = ImGui::IsItemHovered() || ImGui::IsItemFocused(); ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), hov ? C_CARD_HI : C_CARD, px(14));
    if (hov) dl->AddRect(p, ImVec2(p.x + w, p.y + h), C_ACCENT, px(14), 0, px(3));
    text_at(dl, F_SEMI, 25, ImVec2(p.x + px(24), p.y + px(desc ? 14 : 20)), C_TEXT, label);
    if (desc) text_at(dl, F_REG, 20, ImVec2(p.x + px(24), p.y + px(54)), C_DIM, desc);
    float sw = px(72), sh = px(38), sx = p.x + w - sw - px(24), sy = p.y + (h - sh) / 2;
    dl->AddRectFilled(ImVec2(sx, sy), ImVec2(sx + sw, sy + sh), v ? C_ACCENT : IM_COL32(60, 66, 80, 255), sh / 2);
    dl->AddCircleFilled(ImVec2(v ? sx + sw - sh / 2 : sx + sh / 2, sy + sh / 2), sh / 2 - px(4), IM_COL32(245, 245, 245, 255), 24);
    ImGui::PopID();
    return pressed;
}
static bool choice_row(const char *label, const char *desc, const std::vector<std::pair<std::string, std::string>> &opts, std::string &value, float w) {
    ImGui::PushID(label);
    ImVec2 p = ImGui::GetCursorScreenPos(); float h = px(desc ? 96 : 70);
    bool pressed = ImGui::InvisibleButton("row", ImVec2(w, h));
    bool hov = ImGui::IsItemHovered() || ImGui::IsItemFocused(); ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), hov ? C_CARD_HI : C_CARD, px(14));
    if (hov) dl->AddRect(p, ImVec2(p.x + w, p.y + h), C_ACCENT, px(14), 0, px(3));
    text_at(dl, F_SEMI, 25, ImVec2(p.x + px(24), p.y + px(desc ? 14 : 20)), C_TEXT, label);
    if (desc) text_at(dl, F_REG, 20, ImVec2(p.x + px(24), p.y + px(54)), C_DIM, desc);
    size_t idx = 0; for (size_t i = 0; i < opts.size(); i++) if (opts[i].first == value) idx = i;
    std::string shown = "<  " + opts[idx].second + "  >";
    ImVec2 ts = text_size(F_SEMI, 24, shown.c_str()); text_at(dl, F_SEMI, 24, ImVec2(p.x + w - ts.x - px(28), p.y + (h - ts.y) / 2), C_ACCENT_SOFT, shown.c_str());
    bool changed = false;
    if (pressed || (ImGui::IsItemFocused() && (pad_pressed(ImGuiKey_GamepadDpadRight) || pad_pressed(ImGuiKey_RightArrow)))) { idx = (idx + 1) % opts.size(); changed = true; }
    if (ImGui::IsItemFocused() && (pad_pressed(ImGuiKey_GamepadDpadLeft) || pad_pressed(ImGuiKey_LeftArrow))) { idx = (idx + opts.size() - 1) % opts.size(); changed = true; }
    if (changed) value = opts[idx].first;
    ImGui::PopID();
    return changed;
}
static void link_row(const char *label, const std::string &value, float w, std::function<void()> on) {
    ImGui::PushID(label);
    ImVec2 p = ImGui::GetCursorScreenPos(); float h = px(96);
    bool pressed = ImGui::InvisibleButton("row", ImVec2(w, h));
    bool hov = ImGui::IsItemHovered() || ImGui::IsItemFocused(); ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), hov ? C_CARD_HI : C_CARD, px(14));
    if (hov) dl->AddRect(p, ImVec2(p.x + w, p.y + h), C_ACCENT, px(14), 0, px(3));
    text_at(dl, F_SEMI, 25, ImVec2(p.x + px(24), p.y + px(14)), C_TEXT, label);
    dl->PushClipRect(p, ImVec2(p.x + w - px(60), p.y + h), true);
    text_at(dl, F_REG, 20, ImVec2(p.x + px(24), p.y + px(54)), value.empty() ? C_FAINT : C_DIM, value.empty() ? "not set" : value.c_str());
    dl->PopClipRect();
    text_at(dl, F_SEMI, 30, ImVec2(p.x + w - px(48), p.y + px(28)), C_DIM, ">");
    ImGui::PopID();
    if (pressed) on();
}
static void section(const char *t) { ImGui::Dummy(ImVec2(0, px(10))); ImGui::PushFont(F_SEMI, px(22)); ImGui::PushStyleColor(ImGuiCol_Text, v4(C_ACCENT_SOFT)); ImGui::TextUnformatted(t); ImGui::PopStyleColor(); ImGui::PopFont(); }

static void screen_settings() {
    topbar("Settings");
    ImVec2 ds = ImGui::GetIO().DisplaySize; float w = std::min(ds.x - px(112), px(1240));
    ImGui::SetCursorPos(ImVec2((ds.x - w) / 2, px(124)));
    ImGui::BeginChild("set", ImVec2(w + px(20), ds.y - px(124) - px(100)), 0, ImGuiWindowFlags_NoBackground);
    w -= px(4);
    section("Your files");
    link_row("Keys", C.get("keys"), w, [] { browse("Choose your prod.keys", C.get("keys").empty() ? home() : C.get("keys"), false, {".keys"}, [](const std::string &p) { C.set("keys", p); LIB.rescan(); }); });
    focus_here();
    link_row("Games folder", C.get("games_dir"), w, [] { browse("Choose your games folder", C.get("games_dir"), true, {}, [](const std::string &p) { C.set("games_dir", p); LIB.rescan(); }); });
    link_row("Updates & DLC folder", C.get("updates_dir").empty() ? std::string("Optional: a folder of updates and DLC, matched to the games in your games folder") : C.get("updates_dir"), w, [] {
        browse("Choose your updates & DLC folder", C.get("updates_dir").empty() ? C.get("games_dir") : C.get("updates_dir"), true, {}, [](const std::string &p) { C.set("updates_dir", p); LIB.rescan(); }); });
    if (!C.get("updates_dir").empty()) link_row("Stop using the updates & DLC folder", C.get("updates_dir"), w, [] { C.set("updates_dir", ""); LIB.rescan(); });
    link_row("Firmware (fonts)", is_file(P.data + "/firmware/fonts/standard.ttf") ? C.get("firmware") : std::string(), w, [] {
        browse("Choose your firmware folder or .zip", C.get("firmware").empty() ? home() : C.get("firmware"), false, {".zip", "/"}, [](const std::string &p) { C.set("firmware", p); g_job = extract_fonts_job(p); g_job_tid.clear(); go(Screen::Build); g_focus_first = true; }); });
    link_row("Build folder", builds_root(), w, [] { browse("Choose where builds are kept", builds_root(), true, {}, [](const std::string &p) { C.set("builds_dir", p); LIB.refresh_builds(); }); });
    section("Playing (every game; each game's own settings can change them)");
    { std::string v = C.get("layout", "switch");
      if (choice_row("Button layout", "Switch: A is the right button, as on the Switch. Labels: A is the button labelled A.", {{"switch", "Switch"}, {"xbox", "Labels"}}, v, w)) C.set("layout", v); }
    { std::string v = C.get("quit_combo", "1");
      if (choice_row("Quit a game", "Hold these buttons for 1.5 seconds while playing to close the game", {{"1", "Select + Start"}, {"2", "L + R + Select + Start"}, {"3", "L3 + R3"}, {"0", "Off"}}, v, w)) C.set("quit_combo", v); }
    if (toggle_row("Max clocks while playing", clocks_helper_installed() ? "CPU and GPU held at their highest speed while a game runs" : "Needs a one-time setup with your password (below)", C.on("max_clocks", true), w)) C.set("max_clocks", C.on("max_clocks", true) ? "0" : "1");
    if (!clocks_helper_installed()) link_row("Set up max clocks", "Installs a small helper so no password is asked each time", w, [] {
        std::string inst = clocks_installer_path(); std::string out; const char *user = getenv("USER");
        int rc = run_capture({"pkexec", "env", std::string("PN_USER=") + (user ? user : ""), "sh", inst}, {}, out);
        if (rc == 0 && clocks_helper_installed()) message("Max clocks are set up", "Games now run with the CPU and GPU at full speed. You can turn it off here or per game.");
        else message("Set up max clocks in a terminal", "Open a terminal (Konsole) in Desktop Mode and run:\n\n    sudo sh " + inst + "\n\nThen come back here." + (out.empty() ? "" : "\n\n" + trim(out))); });
    if (toggle_row("Motion controls", "The controller's gyro, for games that use it", C.on("gyro", true), w)) C.set("gyro", C.on("gyro", true) ? "0" : "1");
    if (toggle_row("Rumble", nullptr, C.on("rumble", true), w)) C.set("rumble", C.on("rumble", true) ? "0" : "1");
    if (toggle_row("Background shader builds", "Smoother start; a few effects may pop in late the first time", C.on("pso_async", true), w)) C.set("pso_async", C.on("pso_async", true) ? "0" : "1");
    { std::string v = C.get("display", "0");
      if (choice_row("Screen", "Which display games open on (the Thor's top screen is the first)", {{"0", "First"}, {"1", "Second"}}, v, w)) C.set("display", v); }
    if (toggle_row("Play in a window", "For Desktop Mode; F11 switches while playing", C.on("windowed", false), w)) C.set("windowed", C.on("windowed", false) ? "0" : "1");
    section("Steam");
    link_row("Add all built games to Steam", steam_users().empty() ? "Steam is not set up on this handheld" : "Shortcuts with artwork, so they can be played from Game Mode", w, [] {
        std::vector<Game> ready; for (auto &g : LIB.snapshot()) if (g.state == GameState::Ready) ready.push_back(g);
        if (ready.empty()) message("No games to add", "Build a game first; built games can then be added to Steam."); else add_to_steam(ready); });
    section("Updates");
    if (toggle_row("Check for updates when it starts", "Asks GitHub for a newer Peanuts Native for Linux; it never updates without asking you", C.get("update_check", "1") == "1", w))
        C.set("update_check", C.get("update_check", "1") == "1" ? "0" : "1");
    { int st = g_upd_state; std::string v; { std::lock_guard<std::mutex> l(g_upd_mu); v = st == 2 ? "Version " + g_upd.version + " is available (you have " + P.version + ")" : st == 3 ? "You have the newest version (" + P.version + ")" : st == 1 ? "Checking…" : st == 4 ? "Could not check: " + g_upd_why : "You have " + P.version; }
      if (st == 2) link_row("Update now", v, w, [] { update_offer(); });
      else link_row("Check for updates", v, w, [] { update_check_async(); }); }
    section("About");
    { std::string why; bool t = tools_ok(&why);
      link_row("Build tools", t ? "Python 3.14, the importer, .NET 10 and spirv-val are bundled and ready" : why, w, [] {}); }
    link_row("Logs", P.logs, w, [] {});
    ImGui::PushFont(F_REG, px(20)); ImGui::PushStyleColor(ImGuiCol_Text, v4(C_DIM)); ImGui::PushTextWrapPos(w);
    ImGui::TextUnformatted("Peanuts Native for Linux, built on Peanuts Native by igawa6 (a fork of Banana Native by moceure, built on nxnative by seedds) and the Eden Duo companion runtime. "
                           "GPL-3.0-or-later. It contains no Nintendo code, keys or firmware, and plays only games you dumped from your own console.");
    ImGui::PopTextWrapPos(); ImGui::PopStyleColor(); ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, px(40)));
    ImGui::EndChild();
    if (back_pressed()) back();
    hints({{"A", "Change"}, {"B", "Back"}});
}

static void screen_game_settings() {
    Game g; if (!find_game(g_sel_tid, g)) { back(); return; }
    topbar(("Settings · " + g.name).c_str());
    ImVec2 ds = ImGui::GetIO().DisplaySize; float w = std::min(ds.x - px(112), px(1240));
    ImGui::SetCursorPos(ImVec2((ds.x - w) / 2, px(124)));
    ImGui::BeginChild("gset", ImVec2(w + px(20), ds.y - px(124) - px(100)), 0, ImGuiWindowFlags_NoBackground);
    if (const char *sy = getenv("PNL_DEMO_SCROLL")) ImGui::SetScrollY(px((float)atof(sy)));   // screenshots of the lower rows
    w -= px(4);
    auto key = [&](const char *k) { return g.tid + "." + k; };
    auto gv = [&](const char *k, const char *def) { return C.game(g.tid, k, def); };
    { std::string v = gv("layout", "switch"); if (choice_row("Button layout", nullptr, {{"switch", "Switch"}, {"xbox", "Labels"}}, v, w)) C.set(key("layout"), v); focus_here(); }
    { std::string v = gv("console_mode", "auto"); if (choice_row("Console mode", "Docked games render at a higher resolution; handheld runs lighter", {{"auto", "Game default"}, {"handheld", "Handheld"}, {"docked", "Docked"}}, v, w)) C.set(key("console_mode"), v); }
    if (toggle_row("Max clocks while playing", nullptr, gv("max_clocks", "1") == "1", w)) C.set(key("max_clocks"), gv("max_clocks", "1") == "1" ? "0" : "1");
    { std::string lab = companion_label(g.tid);
      std::string d = lab.empty() ? "No companion package for this game yet (import one below)" : lab + ", on the Thor's bottom screen";
      if (toggle_row("Bottom screen companion", d.c_str(), gv("bottom_screen", "1") == "1", w)) C.set(key("bottom_screen"), gv("bottom_screen", "1") == "1" ? "0" : "1");
      std::string tid = g.tid;
      link_row("Import a companion package", "An Eden Duo .dsmod.zip with a Linux ARM module (it replaces the bundled one for this game)", w, [tid] {
          browse("Choose a .dsmod.zip", home() + "/Downloads", false, {".zip"}, [tid](const std::string &p) {
              std::string why; std::string got = companion_import(p, why);
              if (got.empty()) message("Not imported", why);
              else if (got != tid) message("Imported for another game", "That package is for title " + got + ", not this game. It is kept and used when you play that game.");
              else message("Companion imported", companion_label(tid) + " is used on the bottom screen from the next time you play."); }); }); }
    if (toggle_row("Background shader builds", nullptr, gv("pso_async", "1") == "1", w)) C.set(key("pso_async"), gv("pso_async", "1") == "1" ? "0" : "1");
    if (toggle_row("RAM saver", "Keeps big texture pools in a file instead of memory (for the largest games)", gv("ram_saver", "0") == "1", w)) C.set(key("ram_saver"), gv("ram_saver", "0") == "1" ? "0" : "1");
    if (toggle_row("Motion controls", nullptr, gv("gyro", "1") == "1", w)) C.set(key("gyro"), gv("gyro", "1") == "1" ? "0" : "1");
    if (toggle_row("Rumble", nullptr, gv("rumble", "1") == "1", w)) C.set(key("rumble"), gv("rumble", "1") == "1" ? "0" : "1");
    { std::string v = gv("display", "0"); if (choice_row("Screen", nullptr, {{"0", "First"}, {"1", "Second"}}, v, w)) C.set(key("display"), v); }
    if (toggle_row("Play in a window", nullptr, gv("windowed", "0") == "1", w)) C.set(key("windowed"), gv("windowed", "0") == "1" ? "0" : "1");
    // the game's version: which update the next build uses
    { auto vs = versions_available(g);
      if (!g.is_nup && vs.size() > 1) {
          section("Game version");
          std::vector<std::pair<std::string, std::string>> opts = {{"", "Automatic"}};
          for (auto v : vs) { bool sup = false; if (g.cat) for (auto &b : g.cat->builds) if (b.second == v) sup = true;
                              opts.push_back({std::to_string(v), version_label(g, v) + (sup ? "" : " (untested)")}); }
          std::string cur = gv("version_pick", "");
          std::string d = "Next build: " + version_label(g, g.version) + (g.version_supported ? "" : ". Peanuts Native has no profile row for it, so it may not start");
          if (choice_row("Update to build with", d.c_str(), opts, cur, w)) { C.set(key("version_pick"), cur); LIB.rescan(); }
      } }
    // mods: romfs (files) at every start, code (exefs) at the next build
    { section("Mods");
      std::string tid = g.tid;
      for (auto &m : mods_of(g.tid)) {
          std::string d = std::string(m.romfs ? "game files" : "") + (m.code ? std::string(m.romfs ? " + " : "") + "code (applied when the game is built)" : "") +
                          (m.cheats && !m.romfs && !m.code ? "cheats only (not used yet)" : "");
          if (toggle_row(m.name.c_str(), d.c_str(), m.enabled, w)) { mod_enable(tid, m.name, !m.enabled); if (m.code) LIB.refresh_builds(); }
          std::string nm = m.name;
          link_row(("Remove " + m.name).c_str(), "Deletes this mod from Peanuts Native's mods folder", w, [tid, nm] {
              confirm("Remove " + nm + "?", "The mod's files are deleted from Peanuts Native's mods folder. Your original download is not touched.", "Remove", [tid, nm] {
                  mod_remove(tid, nm); LIB.refresh_builds(); }); });
      }
      link_row("Add a mod", "A mod folder or .zip (romfs / exefs / atmosphere/contents/" + g.tid + "/...)", w, [tid] {
          browse("Choose a mod folder or .zip", home() + "/Downloads", false, {".zip", "/"}, [tid](const std::string &p) {
              std::string why; std::string got = mod_import(tid, p, why);
              if (got.empty()) message("Not added", why);
              else { bool code = false; for (auto &m : mods_of(tid)) if (m.name == got) code = m.code;
                     message("Mod added", got + (code ? " changes the game's code, so the game needs to be built again (the game's page offers it)." : " is used from the next time you play."));
                     LIB.refresh_builds(); } }); }); }
    section("Advanced: extra environment (one KEY=value per line, the host's DM_* switches)");
    static char buf[4096]; static std::string loaded_for;
    if (loaded_for != g.tid) { snprintf(buf, sizeof buf, "%s", C.get(key("env")).c_str()); loaded_for = g.tid; }
    ImGui::PushFont(F_REG, px(22));
    if (ImGui::InputTextMultiline("##env", buf, sizeof buf, ImVec2(w, px(200)))) C.set(key("env"), buf);
    ImGui::PopFont();
    if (big_button("Reset this game's settings", px(520))) {
        for (auto it = C.kv.begin(); it != C.kv.end();) { if (it->first.rfind(g.tid + ".", 0) == 0) it = C.kv.erase(it); else ++it; }
        C.save(); loaded_for.clear();
    }
    ImGui::Dummy(ImVec2(0, px(40)));
    ImGui::EndChild();
    if (back_pressed() && !ImGui::GetIO().WantTextInput) back();
    hints({{"A", "Change"}, {"B", "Back"}});
}

// ---------- the message box ----------
static void draw_message() {
    if (!g_message_open) return;
    ImGui::OpenPopup("msg");
    ImVec2 ds = ImGui::GetIO().DisplaySize; float w = std::min(ds.x - px(160), px(1100));
    ImGui::SetNextWindowSize(ImVec2(w, 0)); ImGui::SetNextWindowPos(ImVec2(ds.x / 2, ds.y / 2), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("msg", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::PushFont(F_DISP, px(36)); ImGui::TextWrapped("%s", g_message_title.c_str()); ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, px(6)));
        ImGui::PushFont(F_REG, px(23)); ImGui::PushStyleColor(ImGuiCol_Text, v4(C_DIM));
        ImGui::BeginChild("msgtext", ImVec2(0, std::min(px(520), text_size(F_REG, 23, g_message.c_str(), w - px(60)).y + px(20))), 0, ImGuiWindowFlags_NoBackground);
        ImGui::TextWrapped("%s", g_message.c_str()); ImGui::EndChild();
        ImGui::PopStyleColor(); ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, px(10)));
        if (g_confirm) {
            if (big_button(g_confirm_label.c_str(), px(460), true)) { auto f = g_confirm; g_confirm = nullptr; g_message_open = false; ImGui::CloseCurrentPopup(); f(); }
            ImGui::SameLine();
            if (big_button("Cancel", px(220)) || back_pressed()) { g_confirm = nullptr; g_message_open = false; ImGui::CloseCurrentPopup(); }
        } else if (big_button("OK", px(220), true) || back_pressed()) { g_message_open = false; ImGui::CloseCurrentPopup(); }
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere(-1);
        ImGui::EndPopup();
    }
}

// ---------- --launch <title id>: straight into a built game, no launcher window (what Steam shortcuts run) ----------
static volatile pid_t g_direct_pid = -1;
static void direct_forward(int sig) { if (g_direct_pid > 0) kill(-g_direct_pid, sig == SIGINT ? SIGTERM : sig); }
static int launch_direct(std::string tid) {
    for (auto &c : tid) c = (char)toupper((unsigned char)c);
    auto box = [](const std::string &title, const std::string &text) { fprintf(stderr, "%s: %s\n", title.c_str(), text.c_str()); SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title.c_str(), text.c_str(), nullptr); };
    Game g; g.tid = tid; g.cat = CAT.find(tid); g.name = g.cat ? g.cat->name : tid;
    refresh_build(g);
    if (!engine_ok()) { box("Peanuts Native", "Open Peanuts Native once to finish setting it up, then start the game again."); return 1; }
    if (g.pack_dir.empty()) { box("Peanuts Native", "Game " + tid + " is not built. Open Peanuts Native and build it first."); return 1; }
    std::string log, err;
    pid_t pid = play_start(g, log, err);
    if (pid < 0) { box("The game could not start", err); return 1; }
    g_direct_pid = pid;
    struct sigaction sa = {}; sa.sa_handler = direct_forward; sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, nullptr); sigaction(SIGINT, &sa, nullptr); sigaction(SIGHUP, &sa, nullptr);
    int rc = 0; double t0 = (double)SDL_GetTicks() / 1000;
    while (!play_poll(pid, rc)) usleep(100000);
    if (rc != 0 && rc != 143 && rc != 130 && rc != 128 + SIGTERM) {
        std::string tail = read_file(log, 8 << 20); auto lines = split(tail, '\n'); std::string last;
        for (size_t i = lines.size() > 12 ? lines.size() - 12 : 0; i < lines.size(); i++) last += lines[i] + "\n";
        box((double)SDL_GetTicks() / 1000 - t0 < 15 ? "The game did not start" : "The game stopped", "Exit code " + std::to_string(rc) + ". The end of its log:\n\n" + last + "\nFull log: " + log);
    }
    return rc;
}

// ---------- main ----------
int main(int argc, char **argv) {
    P.init(); C.load(); CAT.load(P.share + "/catalog.tsv");
    for (int i = 1; i + 1 < argc; i++) if (!strcmp(argv[i], "--launch")) return launch_direct(argv[i + 1]);
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--check-update")) {   // from a terminal: what the newest release is
        UpdateInfo u; std::string why;
        if (!update_check(u, why)) { printf("could not check: %s\n", why.c_str()); return 1; }
        printf("newest: %s (you have %s)%s\n%s\nsha256: %s\nsize: %llu\n", u.version.c_str(), P.version.c_str(), version_newer(u.version, P.version) ? ": an update" : "", u.url.c_str(), u.sha_url.c_str(), (unsigned long long)u.size);
        return 0;
    }
    for (int i = 1; i + 1 < argc; i++) if (!strcmp(argv[i], "--add-to-steam")) {   // from a terminal: peanuts-native --add-to-steam <title id> (Steam closed)
        std::string tid = argv[i + 1]; for (auto &c : tid) c = (char)toupper((unsigned char)c);
        Game g; g.tid = tid; g.cat = CAT.find(tid); g.name = g.cat ? g.cat->name : tid; refresh_build(g);
        if (const char *n = getenv("PNL_STEAM_NAME")) g.name = n;
        std::string msg; bool ok = steam_add(g, msg); printf("%s\n", msg.c_str()); return ok ? 0 : 1;
    }
    bool windowed = C.on("launcher_windowed", false);
    for (int i = 1; i < argc; i++) { if (!strcmp(argv[i], "--windowed")) windowed = true; if (!strcmp(argv[i], "--fullscreen")) windowed = false; }
    SDL_SetHint(SDL_HINT_APP_NAME, "Peanuts Native");
    SDL_SetHint(SDL_HINT_APP_ID, "io.github.peanuts_native.linux");
    SDL_SetHint(SDL_HINT_VIDEO_WAYLAND_SCALE_TO_DISPLAY, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    int nd = 0; SDL_DisplayID *ds = SDL_GetDisplays(&nd); SDL_Rect db = {0, 0, 1920, 1080}; if (ds && nd) SDL_GetDisplayBounds(ds[0], &db); SDL_free(ds);
    W = SDL_CreateWindow("Peanuts Native", windowed ? 1600 : db.w, windowed ? 900 : db.h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | (windowed ? 0 : SDL_WINDOW_FULLSCREEN));
    if (!W) { fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
    R = SDL_CreateRenderer(W, nullptr);
    if (!R) { fprintf(stderr, "renderer: %s\n", SDL_GetError()); return 1; }
    SDL_SetRenderVSync(R, 1);
    if (std::string icon = P.share + "/peanuts-native.png"; is_file(icon)) { int w, h, n; unsigned char *p = stbi_load(icon.c_str(), &w, &h, &n, 4); if (p) { SDL_Surface *s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ABGR8888, p, w * 4); SDL_SetWindowIcon(W, s); SDL_DestroySurface(s); stbi_image_free(p); } }
    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO(); io.IniFilename = nullptr; io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigNavCursorVisibleAuto = true;
    std::string fd = P.share + "/fonts/";
    F_REG = io.Fonts->AddFontFromFileTTF((fd + "Inter-Regular.ttf").c_str(), 26.f);
    F_SEMI = io.Fonts->AddFontFromFileTTF((fd + "Inter-SemiBold.ttf").c_str(), 26.f);
    F_DISP = io.Fonts->AddFontFromFileTTF((fd + "InterDisplay-Bold.ttf").c_str(), 40.f);
    if (!F_REG) F_REG = io.Fonts->AddFontDefault(); if (!F_SEMI) F_SEMI = F_REG; if (!F_DISP) F_DISP = F_SEMI;
    ImGui_ImplSDL3_InitForSDLRenderer(W, R); ImGui_ImplSDLRenderer3_Init(R);
    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_AutoAll, nullptr, 0);
    int last_h = 0;
    if (!engine_ok()) go(Screen::Prepare);
    else if (!keys_ok() || !is_dir(C.get("games_dir"))) go(Screen::Setup);
    else { go(Screen::Library); LIB.rescan(); }
    if (C.get("update_check", "1") == "1" && !getenv("PNL_DEMO")) update_check_async();
    std::string why; if (!tools_ok(&why) && !getenv("PNL_DEMO")) message("Peanuts Native is incomplete", why + "\n\nDownload the AppImage again.");
    // ---- screenshot hooks for the build machine's checks: PNL_SHOT=<file.bmp> after PNL_SHOT_FRAMES frames; PNL_DEMO=1 fills the library with sample games ----
    const char *shot = getenv("PNL_SHOT"); int shot_frames = getenv("PNL_SHOT_FRAMES") ? atoi(getenv("PNL_SHOT_FRAMES")) : 30, frame = 0;
    if (getenv("PNL_DEMO")) {
        std::lock_guard<std::mutex> l(LIB.mu); LIB.games.clear(); int i = 0;
        for (auto &kv : CAT.by_tid) {
            Game g; g.tid = kv.first; g.name = kv.second.name; g.cat = &kv.second; g.base = "/games/" + g.name + ".nsp";
            g.version = kv.second.builds.empty() ? 0 : kv.second.builds.back().second; g.version_supported = true;
            g.state = i % 5 == 0 ? GameState::Ready : i % 7 == 3 ? GameState::NeedsRebuild : GameState::NeedsBuild;
            if (getenv("PNL_DEMO_ICONS")) g.icon_path = std::string(getenv("PNL_DEMO_ICONS")) + "/" + std::to_string(i % 6) + ".png";
            LIB.games.push_back(g); if (++i >= 14) break;
        }
        Game u; u.tid = "0100ABCD12340000"; u.name = "A Game Without a Profile"; u.base = "/games/x.nsp"; LIB.games.push_back(u);
        g_stack.clear(); go(Screen::Library);
        const char *sc = getenv("PNL_DEMO_SCREEN");
        if (sc && !strcmp(sc, "game")) { g_sel_tid = LIB.games[1].tid; go(Screen::Game); }
        if (sc && !strcmp(sc, "ready")) { g_sel_tid = LIB.games[0].tid; go(Screen::Game); }
        if (sc && !strcmp(sc, "settings")) go(Screen::Settings);
        if (sc && !strcmp(sc, "gsettings")) { g_sel_tid = LIB.games[0].tid; go(Screen::GameSettings); }
        if (sc && !strcmp(sc, "setup")) { g_stack.clear(); go(Screen::Setup); }
        if (sc && !strcmp(sc, "browser")) browse("Choose your games folder", "/usr", true, {}, [](const std::string &) {});
        if (sc && !strcmp(sc, "build")) {
            g_job = std::make_shared<Job>(); g_job->title = "Building " + LIB.games[2].name; g_job->running = true; g_job_tid = LIB.games[2].tid;
            g_job->started = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count() - 754; g_job->set_phase("Translating shaders (the longest step)"); g_job->progress = 0.62f;
            for (auto *l : {"+ nximport_cli --keys prod.keys nup ...", "import: 6.2 GiB in 41.3s", "pack_android.py: rewriting main (12.4 MiB)", "shaders: 41,228 of 66,410 translated", "[RED] example: one shader was skipped"}) g_job->add_line(l);
            go(Screen::Build);
        }
    }
    bool quit = false;
    while (!quit) {
        SDL_Event e;
        bool playing = g_play_pid > 0;
        if (playing) {   // the game has the screen: wait for it quietly
            int rc;
            if (play_poll(g_play_pid, rc)) {
                double secs = ImGui::GetTime() - g_play_start; g_play_pid = -1;
                SDL_ShowWindow(W); SDL_RaiseWindow(W); g_focus_first = true;
                if (rc != 0 && rc != 143 && !(rc == 130)) {
                    std::string tail = read_file(g_play_log, 8 << 20); auto lines = split(tail, '\n'); std::string last;
                    for (size_t i = lines.size() > 14 ? lines.size() - 14 : 0; i < lines.size(); i++) last += lines[i] + "\n";
                    message(secs < 15 ? "The game did not start" : "The game stopped", "Exit code " + std::to_string(rc) + ". The end of its log:\n\n" + last + "\nFull log: " + g_play_log);
                }
            }
            while (SDL_PollEvent(&e)) if (e.type == SDL_EVENT_QUIT) quit = true;
            if (g_play_pid > 0) { SDL_Delay(100); continue; }
        }
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT) quit = true;
            if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(W)) quit = true;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F11) SDL_SetWindowFullscreen(W, !(SDL_GetWindowFlags(W) & SDL_WINDOW_FULLSCREEN));
        }
        if (SDL_GetWindowFlags(W) & SDL_WINDOW_MINIMIZED) { SDL_Delay(50); continue; }
        int ww, wh; SDL_GetRenderOutputSize(R, &ww, &wh);
        if (wh != last_h && wh > 0) {
            last_h = wh; S = std::max(0.6f, wh / 1080.f);
            int lw, lh; SDL_GetWindowSize(W, &lw, &lh); float dens = lh > 0 ? (float)wh / (float)lh : 1.f; (void)dens;
            apply_style();
        }
        ImGui_ImplSDLRenderer3_NewFrame(); ImGui_ImplSDL3_NewFrame(); ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
        ImGui::PushFont(F_REG, px(26));
        static Screen last = Screen::Prepare; if (cur() != last) { last = cur(); g_focus_first = true; }
        switch (cur()) {
        case Screen::Prepare: screen_prepare(); break;
        case Screen::Setup: screen_setup(); break;
        case Screen::Library: screen_library(); break;
        case Screen::Game: screen_game(); break;
        case Screen::Build: screen_build(); break;
        case Screen::Settings: screen_settings(); break;
        case Screen::GameSettings: screen_game_settings(); break;
        case Screen::Browser: screen_browser(); break;
        case Screen::Log: screen_log(); break;
        }
        draw_message();
        ImGui::PopFont();
        ImGui::End();
        ImGui::Render();
        SDL_SetRenderScale(R, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(R, 13, 15, 20, 255); SDL_RenderClear(R);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), R);
        if (shot && ++frame == shot_frames) {
            SDL_Surface *sf = SDL_RenderReadPixels(R, nullptr);
            if (sf) { SDL_SaveBMP(sf, shot); SDL_DestroySurface(sf); }
            quit = true;
        }
        SDL_RenderPresent(R);
    }
    if (g_job && g_job->running) g_job->cancel();
    ImGui_ImplSDLRenderer3_Shutdown(); ImGui_ImplSDL3_Shutdown(); ImGui::DestroyContext();
    SDL_DestroyRenderer(R); SDL_DestroyWindow(W); SDL_Quit();
    return 0;
}
