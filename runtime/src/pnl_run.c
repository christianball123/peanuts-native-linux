// peanuts-run: plays one built game. The Peanuts host (the Android build's libnxhost, here linked into this program for glibc) runs the game on a thread
// of its own; this file is what the Android launcher's GameActivity does around it: the window, controllers, touch, rumble, the keyboard applet and quitting.
//
//   peanuts-run <pack folder>          (the launcher sets the host's DM_* environment first: DM_ROMFS, DM_SAVE, DM_TEMP, DM_PROFILE_PATH, ...)
//
// Options from the environment (the launcher's game settings):
//   PNL_DISPLAY=<n>        the display to play on (0 = the first; the AYN Thor's top screen)        PNL_WINDOWED=1   a window instead of full screen
//   PNL_LAYOUT=switch|xbox switch (default): face buttons by position, as the Switch's;  xbox: by the labels printed on them (A is A)
//   PNL_GYRO=0             no controller motion                                                       PNL_RUMBLE=0     no rumble
//   PNL_QUIT_COMBO=<n>     the buttons held 1.5 s to quit: 1 Back + Start (default), 2 L + R + Back + Start, 3 L3 + R3 (both sticks pressed), 0 none                                       PNL_MOUSE_TOUCH=1 the mouse's left button is a finger
//   PNL_COMPANION=off      no companion page on the bottom screen (pnl_companion.c has the bottom-screen settings)
#define _GNU_SOURCE
#include "pnl.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "input_map.h"   // Android's key, axis and touch numbers (dm/host/android)
#include "pnl_menu.h"

// ---- The host's launcher interface (dm/host/android/input_android.h, dm/gfx/vk_android/vk_android.h, dm/peanuts) ----
int nxhost_main(int argc, char **argv);
void nxhost_set_window(ANativeWindow *w);
int nxhost_input_device_added(int device, const char *name, int labelled);
void nxhost_input_device_removed(int device);
bool nxhost_input_key(int device, int keycode, bool down);
bool nxhost_input_axes(int device, const int *axes, const float *values, int n);
bool nxhost_input_motion(int device, const float acc_ms2[3], const float gyro_rads[3]);
void nxhost_input_touch(int action, int id, float x, float y);
void nxhost_touch_layout(float view_w, float view_h, float pic_x, float pic_y, float pic_w, float pic_h);
void nxhost_touch_controls_enable(bool on);
void nxhost_lifecycle(int event);
typedef void (*nxhost_rumble_fn)(void *ctx, int player, float intensity, float sharpness);
void nxhost_rumble_callback(nxhost_rumble_fn fn, void *ctx);
void nxhost_rumble_route(int player, bool available);
int nxhost_input_device_player(int device);
typedef int (*nxhost_text_input_fn)(void *ctx, const char *title, const char *sub, const char *guide, const char *initial, int max_chars, int min_chars, char *out, int out_cap);
void nxhost_text_input_callback(nxhost_text_input_fn fn, void *ctx);
int64_t nxhost_prewarm_progress(void);
void pn_host_battery_set(int percent, int charging);
void nxhost_audio_route_changed(void);

static int env_int(const char *k, int def) { const char *e = getenv(k); return e && *e ? atoi(e) : def; }
static void note(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void note(const char *fmt, ...) { va_list ap; va_start(ap, fmt); fprintf(stderr, "[peanuts-run] "); vfprintf(stderr, fmt, ap); fputc('\n', stderr); va_end(ap); }

// ---- The host thread ----
static int g_argc; static char **g_argv; static _Atomic int g_host_done = 0, g_host_rc = 0;
static void *host_thread(void *p) {
    (void)p;
    int rc = nxhost_main(g_argc, g_argv);
    atomic_store(&g_host_rc, rc); atomic_store(&g_host_done, 1);
    SDL_Event e = {.type = SDL_EVENT_QUIT}; SDL_PushEvent(&e);
    return NULL;
}

// ---- Controllers ----
#define MAXPAD 8
static struct { SDL_Gamepad *gp; SDL_JoystickID id; int has_accel, has_gyro, rumble; float acc[3]; int have_acc; Uint32 held; Uint64 combo_since; Uint32 pend; Uint64 pend_since; Uint64 menu_since; int stick_dir; } g_pads[MAXPAD];
static int g_labelled;   // 1 = PNL_LAYOUT=xbox
static int pad_slot(SDL_JoystickID id) { for (int i = 0; i < MAXPAD; i++) if (g_pads[i].gp && g_pads[i].id == id) return i; return -1; }
static int button_key(SDL_GamepadButton b) {
    switch (b) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return AK_BUTTON_A;   // the bottom button, Android's BUTTON_A (the host maps by position unless labelled)
    case SDL_GAMEPAD_BUTTON_EAST: return AK_BUTTON_B;
    case SDL_GAMEPAD_BUTTON_WEST: return AK_BUTTON_X;
    case SDL_GAMEPAD_BUTTON_NORTH: return AK_BUTTON_Y;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return AK_BUTTON_L1;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return AK_BUTTON_R1;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return AK_BUTTON_THUMBL;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return AK_BUTTON_THUMBR;
    case SDL_GAMEPAD_BUTTON_START: return AK_BUTTON_START;
    case SDL_GAMEPAD_BUTTON_BACK: return AK_BUTTON_SELECT;
    case SDL_GAMEPAD_BUTTON_GUIDE: return AK_BUTTON_MODE;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return AK_DPAD_UP;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return AK_DPAD_DOWN;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return AK_DPAD_LEFT;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return AK_DPAD_RIGHT;
    default: return -1;
    }
}
// An Xbox-style pad labels its bottom button A; with PNL_LAYOUT=xbox the game's A is that button. A Nintendo-style pad (Switch Pro, Joy-Con) already has A on
// the right, where SDL reports EAST: the label layout is then the position layout, so "labelled" is only passed for pads whose bottom button is labelled A
static int pad_labelled(SDL_Gamepad *gp) {
    if (!g_labelled) return 0;
    return SDL_GetGamepadButtonLabel(gp, SDL_GAMEPAD_BUTTON_SOUTH) == SDL_GAMEPAD_BUTTON_LABEL_A ? 1 : 0;
}
static void pad_add(SDL_JoystickID id) {
    if (pad_slot(id) >= 0) return;
    int s = -1; for (int i = 0; i < MAXPAD && s < 0; i++) if (!g_pads[i].gp) s = i;
    if (s < 0) return;
    SDL_Gamepad *gp = SDL_OpenGamepad(id); if (!gp) { note("controller %u does not open: %s", (unsigned)id, SDL_GetError()); return; }
    memset(&g_pads[s], 0, sizeof g_pads[s]); g_pads[s].gp = gp; g_pads[s].id = id;
    const char *name = SDL_GetGamepadName(gp);
    int player = nxhost_input_device_added((int)id, name ? name : "Controller", pad_labelled(gp));
    if (env_int("PNL_GYRO", 1)) {
        if (SDL_GamepadHasSensor(gp, SDL_SENSOR_ACCEL) && SDL_SetGamepadSensorEnabled(gp, SDL_SENSOR_ACCEL, true)) g_pads[s].has_accel = 1;
        if (SDL_GamepadHasSensor(gp, SDL_SENSOR_GYRO) && SDL_SetGamepadSensorEnabled(gp, SDL_SENSOR_GYRO, true)) g_pads[s].has_gyro = 1;
    }
    SDL_PropertiesID pr = SDL_GetGamepadProperties(gp);
    g_pads[s].rumble = env_int("PNL_RUMBLE", 1) && SDL_GetBooleanProperty(pr, SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false);
    if (player >= 0) nxhost_rumble_route(player, g_pads[s].rumble);
    note("controller: %s → player %d%s%s%s", name ? name : "?", player + 1, g_pads[s].has_gyro ? ", motion" : "", g_pads[s].rumble ? ", rumble" : "", pad_labelled(gp) ? ", by labels" : "");
}
static void pad_remove(SDL_JoystickID id) {
    int s = pad_slot(id); if (s < 0) return;
    nxhost_input_device_removed((int)id);
    SDL_CloseGamepad(g_pads[s].gp); memset(&g_pads[s], 0, sizeof g_pads[s]);
}
static float axis_value(SDL_GamepadAxis a, Sint16 v) {
    if (a == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || a == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) return v <= 0 ? 0.f : v / 32767.f;
    return v < 0 ? v / 32768.f : v / 32767.f;
}
static void pad_axis(SDL_JoystickID id, SDL_GamepadAxis a, Sint16 v) {
    if (pnl_menu_is_open()) {   // the left stick moves through the menu (an edge past half way), the game gets nothing
        int s = pad_slot(id); if (s < 0 || (a != SDL_GAMEPAD_AXIS_LEFTX && a != SDL_GAMEPAD_AXIS_LEFTY)) return;
        int d = v > 16000 ? 1 : v < -16000 ? -1 : 0, code = a == SDL_GAMEPAD_AXIS_LEFTY ? (d > 0 ? 2 : 1) : (d > 0 ? 4 : 3);
        int key = a == SDL_GAMEPAD_AXIS_LEFTY ? 0 : 8;   // stick_dir: bits 0-3 the vertical, 8-11 the horizontal direction held
        int was = g_pads[s].stick_dir >> key & 15;
        if (d && was != code) { g_pads[s].stick_dir = (g_pads[s].stick_dir & ~(15 << key)) | code << key;
            pnl_menu_input(code == 1 ? PNL_IN_UP : code == 2 ? PNL_IN_DOWN : code == 3 ? PNL_IN_LEFT : PNL_IN_RIGHT); }
        if (!d && (v > -8000 && v < 8000)) g_pads[s].stick_dir &= ~(15 << key);
        return;
    }
    int ax;
    switch (a) {
    case SDL_GAMEPAD_AXIS_LEFTX: ax = AA_X; break;
    case SDL_GAMEPAD_AXIS_LEFTY: ax = AA_Y; break;
    case SDL_GAMEPAD_AXIS_RIGHTX: ax = AA_Z; break;
    case SDL_GAMEPAD_AXIS_RIGHTY: ax = AA_RZ; break;
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: ax = AA_LTRIGGER; break;
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: ax = AA_RTRIGGER; break;
    default: return;
    }
    float f = axis_value(a, v);
    nxhost_input_axes((int)id, &ax, &f, 1);
    // the triggers are buttons too (ZL / ZR): an analog trigger past the middle presses them, as an Android pad's L2 / R2 keys do
    if (ax == AA_LTRIGGER || ax == AA_RTRIGGER) {
        static uint8_t held[MAXPAD][2]; int s = pad_slot(id); if (s < 0) return;
        int k = ax == AA_LTRIGGER ? 0 : 1, down = f > 0.45f ? 1 : f < 0.35f ? 0 : held[s][k];
        if (down != held[s][k]) { held[s][k] = (uint8_t)down; nxhost_input_key((int)id, k ? AK_BUTTON_R2 : AK_BUTTON_L2, down); }
    }
}
static void pad_sensor(SDL_JoystickID id, int sensor, const float d[3]) {
    int s = pad_slot(id); if (s < 0) return;
    if (sensor == SDL_SENSOR_ACCEL) { memcpy(g_pads[s].acc, d, sizeof g_pads[s].acc); g_pads[s].have_acc = 1; return; }
    if (sensor == SDL_SENSOR_GYRO && g_pads[s].have_acc) nxhost_input_motion((int)id, g_pads[s].acc, d);   // m/s² and rad/s, Android's frame and units
}
static void rumble(void *ctx, int player, float intensity, float sharpness) {
    (void)ctx;
    for (int i = 0; i < MAXPAD; i++) if (g_pads[i].gp && g_pads[i].rumble && nxhost_input_device_player((int)g_pads[i].id) == player) {
        float lo = intensity * (1.f - 0.6f * sharpness), hi = intensity * (0.4f + 0.6f * sharpness);
        SDL_RumbleGamepad(g_pads[i].gp, (Uint16)(lo * 65535.f), (Uint16)(hi * 65535.f), intensity > 0 ? 5000 : 0);   // renewed by the next change
    }
}

// ---- Keyboard as a controller (a desktop without a pad): arrows, Z X A S (B A Y X), Q W (L R), 1 2 (ZL ZR), Enter (+), Backspace (−), IJKL right stick ----
#define KBD_DEV (-100)
static void kbd_key(SDL_Keycode k, bool down) {
    static int added; if (!added) { nxhost_input_device_added(KBD_DEV, "Keyboard", 0); added = 1; }
    int code = -1;
    switch (k) {
    case SDLK_UP: code = AK_DPAD_UP; break; case SDLK_DOWN: code = AK_DPAD_DOWN; break; case SDLK_LEFT: code = AK_DPAD_LEFT; break; case SDLK_RIGHT: code = AK_DPAD_RIGHT; break;
    case SDLK_Z: code = AK_BUTTON_A; break; case SDLK_X: code = AK_BUTTON_B; break; case SDLK_A: code = AK_BUTTON_X; break; case SDLK_S: code = AK_BUTTON_Y; break;
    case SDLK_Q: code = AK_BUTTON_L1; break; case SDLK_W: code = AK_BUTTON_R1; break; case SDLK_1: code = AK_BUTTON_L2; break; case SDLK_2: code = AK_BUTTON_R2; break;
    case SDLK_RETURN: code = AK_BUTTON_START; break; case SDLK_BACKSPACE: code = AK_BUTTON_SELECT; break;
    case SDLK_I: case SDLK_K: case SDLK_J: case SDLK_L: {
        static float rx, ry; float v = down ? 1.f : 0.f;
        if (k == SDLK_I) ry = -v; else if (k == SDLK_K) ry = v; else if (k == SDLK_J) rx = -v; else rx = v;
        int ax[2] = {AA_Z, AA_RZ}; float val[2] = {rx, ry}; nxhost_input_axes(KBD_DEV, ax, val, 2); return; }
    default: return;
    }
    nxhost_input_key(KBD_DEV, code, down);
}

// ---- Touch: the window's fingers (the Thor's top touchscreen) are the Switch's touchscreen ----
static SDL_FingerID g_fingers[10]; static int g_nfingers;
static int finger_index(SDL_FingerID f, int add) {
    for (int i = 0; i < 10; i++) if (g_fingers[i] == f && f) return i;
    if (!add) return -1;
    for (int i = 0; i < 10; i++) if (!g_fingers[i]) { g_fingers[i] = f; return i; }
    return -1;
}
static void touch(int what, SDL_FingerID f, float x, float y) {   // what: 0 down, 1 move, 2 up
    int i = finger_index(f, what == 0); if (i < 0) return;
    int act;
    if (what == 0) { act = g_nfingers ? AM_POINTER_DOWN : AM_DOWN; g_nfingers++; }
    else if (what == 2) { g_nfingers = g_nfingers > 0 ? g_nfingers - 1 : 0; act = g_nfingers ? AM_POINTER_UP : AM_UP; g_fingers[i] = 0; }
    else act = AM_MOVE;
    nxhost_input_touch(act, i, x, y);
}
// Where the picture lies in the window: the largest 16:9 rectangle (DM_VK_PICTURE_ASPECT can change it), centred, as the replay's present places it
static void touch_layout(int w, int h) {
    float aw = 16, ah = 9; const char *e = getenv("DM_VK_PICTURE_ASPECT");
    if (e) { int a = 0, b = 0; if (sscanf(e, "%d:%d", &a, &b) == 2 && a > 0 && b > 0) { aw = (float)a; ah = (float)b; } }
    if (env_int("DM_VK_STRETCH", 0)) { nxhost_touch_layout((float)w, (float)h, 0, 0, 1, 1); return; }
    float pw = (float)w, ph = pw * ah / aw; if (ph > (float)h) { ph = (float)h; pw = ph * aw / ah; }
    nxhost_touch_layout((float)w, (float)h, ((float)w - pw) / 2 / (float)w, ((float)h - ph) / 2 / (float)h, pw / (float)w, ph / (float)h);
}

// ---- The keyboard applet: a text box from zenity or kdialog when there is one; otherwise the game's own suggestion (or "Player"), so it never waits for ever ----
static int run_capture(const char *const argv[], char *out, int cap) {
    int p[2]; if (pipe(p)) return -1;
    pid_t pid = fork();
    if (pid == 0) { dup2(p[1], 1); close(p[0]); close(p[1]); execvp(argv[0], (char *const *)argv); _exit(127); }
    close(p[1]); if (pid < 0) { close(p[0]); return -1; }
    int n = 0; ssize_t k; while (n < cap - 1 && (k = read(p[0], out + n, (size_t)(cap - 1 - n))) > 0) n += (int)k;
    close(p[0]); out[n] = 0; int st = 0; waitpid(pid, &st, 0);
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r')) out[--n] = 0;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}
static int text_input(void *ctx, const char *title, const char *sub, const char *guide, const char *initial, int max_chars, int min_chars, char *out, int out_cap) {
    (void)ctx; (void)max_chars;
    char prompt[512]; snprintf(prompt, sizeof prompt, "%s%s%s", sub && *sub ? sub : (guide && *guide ? guide : "Enter text"), "", "");
    const char *t = title && *title ? title : "Peanuts Native";
    const char *zen[] = {"zenity", "--entry", "--title", t, "--text", prompt, "--entry-text", initial ? initial : "", NULL};
    const char *kd[] = {"kdialog", "--title", t, "--inputbox", prompt, initial ? initial : "", NULL};
    char buf[1024]; int r = run_capture(zen, buf, sizeof buf);
    if (r == 127 || r < 0) r = run_capture(kd, buf, sizeof buf);
    if (r == 0 && (int)strlen(buf) >= min_chars) { snprintf(out, (size_t)out_cap, "%s", buf); return 1; }
    if (r == 1) return 0;   // the player cancelled
    const char *def = initial && *initial ? initial : "Player";   // no dialog tool on this system
    note("keyboard applet: no zenity or kdialog to ask with; answering \"%s\"", def);
    snprintf(out, (size_t)out_cap, "%s", def); return 1;
}

// The companion page's haptic pulses (pnl_companion.c): a short buzz on the controllers that rumble
void pnl_pad_pulse(float amplitude, int ms) {
    if (amplitude <= 0 || ms <= 0) return;
    if (amplitude > 1) amplitude = 1;
    if (ms > 1000) ms = 1000;
    for (int i = 0; i < MAXPAD; i++) if (g_pads[i].gp && g_pads[i].rumble) SDL_RumbleGamepad(g_pads[i].gp, (Uint16)(amplitude * 65535.f), (Uint16)(amplitude * 65535.f), (Uint32)ms);
}
void pnl_companion_start(void);
void pnl_companion_stop(void);

// ---- Quitting ----
static _Atomic int g_quit;
static void on_term(int sig) { (void)sig; atomic_store(&g_quit, 1); }
// The game sees every button and stick let go (the in-game menu opens: its presses are the menu's)
static void release_all(void) {
    static const int axes[] = {AA_X, AA_Y, AA_Z, AA_RZ, AA_LTRIGGER, AA_RTRIGGER};
    for (int i = 0; i < MAXPAD; i++) if (g_pads[i].gp) {
        for (int b = 0; b < 32; b++) if (g_pads[i].held >> b & 1) { int k = button_key((SDL_GamepadButton)b); if (k >= 0) nxhost_input_key((int)g_pads[i].id, k, false); }
        float z[6] = {0}; nxhost_input_axes((int)g_pads[i].id, (int *)axes, z, 6);
        nxhost_input_key((int)g_pads[i].id, AK_BUTTON_L2, false); nxhost_input_key((int)g_pads[i].id, AK_BUTTON_R2, false);
        g_pads[i].pend = 0; g_pads[i].stick_dir = 0;
    }
}
static int g_menu_confirm, g_menu_back;   // the menu's A and B: the Switch's by position (right / bottom), or by label with PNL_LAYOUT=xbox
static int menu_button(int b) {
    if (b == SDL_GAMEPAD_BUTTON_DPAD_UP) return PNL_IN_UP;
    if (b == SDL_GAMEPAD_BUTTON_DPAD_DOWN) return PNL_IN_DOWN;
    if (b == SDL_GAMEPAD_BUTTON_DPAD_LEFT) return PNL_IN_LEFT;
    if (b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) return PNL_IN_RIGHT;
    if (b == g_menu_confirm) return PNL_IN_OK;
    if (b == g_menu_back) return PNL_IN_BACK;
    return -1;
}

static void quit_now(int code) {
    note("leaving the game");
    pnl_companion_stop();
    nxhost_lifecycle(0); nxhost_lifecycle(1);   // onPause, onStop: audio and the guest suspend, the replay drains
    SDL_Delay(400);
    fflush(stderr); fflush(stdout);
    _exit(code);
}

int main(int argc, char **argv) {
    setvbuf(stderr, NULL, _IOLBF, 0);
    int tls = pnl_tls_check();
    if (tls) { note("[RED] this build's thread slot is at tpidr_el0 %+d, not +16: games would crash. Please report this with your system details.", tls); return 3; }
    if (argc < 2) { fprintf(stderr, "usage: peanuts-run <pack folder>   (normally started by Peanuts Native)\n"); return 2; }
    g_labelled = getenv("PNL_LAYOUT") && !strcmp(getenv("PNL_LAYOUT"), "xbox");

    SDL_SetHint(SDL_HINT_APP_NAME, "Peanuts Native");
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "256");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) { note("[RED] SDL does not start: %s", SDL_GetError()); return 4; }
    if (!SDL_Vulkan_LoadLibrary(NULL)) { note("[RED] no Vulkan driver: %s", SDL_GetError()); return 5; }

    int nd = 0; SDL_DisplayID *ds = SDL_GetDisplays(&nd); int di = env_int("PNL_DISPLAY", 0); if (di < 0 || di >= nd) di = 0;
    SDL_DisplayID disp = ds && nd ? ds[di] : 0; SDL_free(ds);
    SDL_Rect db = {0, 0, 1280, 720}; if (disp) SDL_GetDisplayBounds(disp, &db);
    const char *title = getenv("PNL_TITLE"); char wt[256]; snprintf(wt, sizeof wt, "%s", title && *title ? title : "Peanuts Native");
    SDL_PropertiesID wp = SDL_CreateProperties();
    SDL_SetStringProperty(wp, SDL_PROP_WINDOW_CREATE_TITLE_STRING, wt);
    SDL_SetNumberProperty(wp, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(disp));
    SDL_SetNumberProperty(wp, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(disp));
    int windowed = env_int("PNL_WINDOWED", 0);
    SDL_SetNumberProperty(wp, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, windowed ? 1280 : db.w);
    SDL_SetNumberProperty(wp, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, windowed ? 720 : db.h);
    SDL_SetBooleanProperty(wp, SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN, true);
    SDL_SetBooleanProperty(wp, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, true);
    SDL_SetBooleanProperty(wp, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
    SDL_SetBooleanProperty(wp, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, !windowed);
    SDL_Window *win = SDL_CreateWindowWithProperties(wp); SDL_DestroyProperties(wp);
    if (!win) { note("[RED] no game window: %s", SDL_GetError()); return 6; }
    SDL_HideCursor();
    ANativeWindow *nw = pnl_window_wrap(win);
    int pw = 0, ph = 0; SDL_GetWindowSizeInPixels(win, &pw, &ph);
    note("window %dx%d on display %d (%s)", pw, ph, di, SDL_GetCurrentVideoDriver());
    pnl_menu_init(pw, ph, wt);
    g_menu_confirm = g_labelled ? SDL_GAMEPAD_BUTTON_SOUTH : SDL_GAMEPAD_BUTTON_EAST;
    g_menu_back = g_labelled ? SDL_GAMEPAD_BUTTON_EAST : SDL_GAMEPAD_BUTTON_SOUTH;

    // the host's side, set before it starts
    nxhost_touch_controls_enable(false);   // no on-screen controls: every touch goes to the game
    touch_layout(pw, ph);
    nxhost_rumble_callback(rumble, NULL);
    nxhost_text_input_callback(text_input, NULL);
    nxhost_set_window(nw);
    { int pct = -1; bool chg = false; SDL_PowerState st = SDL_GetPowerInfo(NULL, &pct); if (st != SDL_POWERSTATE_UNKNOWN && st != SDL_POWERSTATE_NO_BATTERY) pn_host_battery_set(pct, st == SDL_POWERSTATE_CHARGING || st == SDL_POWERSTATE_CHARGED); (void)chg; }

    static char *hargv[3]; hargv[0] = "nxhost"; hargv[1] = argv[1]; hargv[2] = NULL; g_argc = 2; g_argv = hargv;
    pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setstacksize(&a, 8u << 20);
    pthread_t th; if (pthread_create(&th, &a, host_thread, NULL)) { note("[RED] cannot start the game thread"); return 7; }
    pthread_attr_destroy(&a);
    { char gd[16]; snprintf(gd, sizeof gd, "%d", di); setenv("PNL_GAME_DISPLAY", gd, 1); }
    pnl_companion_start();   // the bottom screen, once the game's companion package has loaded
    signal(SIGTERM, on_term); signal(SIGHUP, on_term);

    int quit_combo = env_int("PNL_QUIT_COMBO", 1), mouse_touch = env_int("PNL_MOUSE_TOUCH", 0), mouse_down = 0;
    Uint64 last_power = SDL_GetTicks(), last_title = 0; int64_t last_prewarm = -2;
    #define PB(b) (1u << SDL_GAMEPAD_BUTTON_##b)
    const Uint32 quit_mask = quit_combo == 1 ? PB(BACK) | PB(START) : quit_combo == 2 ? PB(BACK) | PB(START) | PB(LEFT_SHOULDER) | PB(RIGHT_SHOULDER)
                           : quit_combo == 3 ? PB(LEFT_STICK) | PB(RIGHT_STICK) : 0;
    static const char *const quit_names[] = {"", "Back + Start", "L + R + Back + Start", "L3 + R3"};
    if (quit_mask) note("to quit: hold %s for 1.5 seconds", quit_names[quit_combo]);
    note("in-game menu: press Start + Select together");
    const Uint32 menu_mask = PB(BACK) | PB(START), shoulders = PB(LEFT_SHOULDER) | PB(RIGHT_SHOULDER);
    SDL_WindowID wid = SDL_GetWindowID(win);
    while (!atomic_load(&g_quit)) {
        SDL_Event e;
        if (!SDL_WaitEventTimeout(&e, 25)) e.type = 0;
        do {
            switch (e.type) {
            case SDL_EVENT_QUIT: case SDL_EVENT_WINDOW_CLOSE_REQUESTED: atomic_store(&g_quit, 1); break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: if (e.window.windowID == wid) { pnl_window_set_size(nw, e.window.data1, e.window.data2); touch_layout(e.window.data1, e.window.data2); pnl_menu_resize(e.window.data1, e.window.data2); } break;
            case SDL_EVENT_GAMEPAD_ADDED: pad_add(e.gdevice.which); break;
            case SDL_EVENT_GAMEPAD_REMOVED: pad_remove(e.gdevice.which); break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN: case SDL_EVENT_GAMEPAD_BUTTON_UP: {
                bool down = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN; int k = button_key((SDL_GamepadButton)e.gbutton.button);
                int s = pad_slot(e.gbutton.which), b = e.gbutton.button;
                if (s < 0 || b >= 32) { if (k >= 0 && !pnl_menu_is_open()) nxhost_input_key((int)e.gbutton.which, k, down); break; }
                Uint32 bit = 1u << b, was_held = g_pads[s].held;
                if (down) g_pads[s].held |= bit; else g_pads[s].held &= ~bit;
                int swallow = g_pads[s].menu_since && (bit & menu_mask) && !down;   // the combo's buttons let go after it opened or closed the menu
                if (!(g_pads[s].held & menu_mask)) g_pads[s].menu_since = 0;
                if (quit_mask) {
                    Uint32 was = was_held & quit_mask;
                    if ((g_pads[s].held & quit_mask) == quit_mask && was != quit_mask) g_pads[s].combo_since = SDL_GetTicks();
                    if ((g_pads[s].held & quit_mask) != quit_mask) g_pads[s].combo_since = 0;
                }
                // Start + Select together (no shoulder held: that is the quit combo) opens or closes the menu
                if ((g_pads[s].held & menu_mask) == menu_mask && !(g_pads[s].held & shoulders)) {
                    if ((was_held & menu_mask) != menu_mask) {
                        g_pads[s].pend &= ~menu_mask;   // the first of the two, held back, never reaches the game
                        if (!pnl_menu_is_open()) { pnl_menu_open(); if (pnl_menu_is_open()) release_all(); } else pnl_menu_close();
                        g_pads[s].menu_since = SDL_GetTicks();
                    }
                    break;
                }
                if (pnl_menu_is_open()) {   // the menu takes the presses
                    if (bit & menu_mask) break;
                    if (down) { int m = menu_button(b); if (m >= 0 && pnl_menu_input(m) == PNL_MENU_QUIT) { note("quit from the in-game menu"); atomic_store(&g_quit, 1); } }
                    break;
                }
                if (swallow) break;
                // Start or Select alone reaches the game a moment late (120 ms), so the combo's first button does not
                if ((bit & menu_mask) && !(g_pads[s].held & shoulders)) {
                    if (down) { g_pads[s].pend |= bit; g_pads[s].pend_since = SDL_GetTicks(); break; }
                    if (g_pads[s].pend & bit) { g_pads[s].pend &= ~bit; if (k >= 0) { nxhost_input_key((int)e.gbutton.which, k, true); nxhost_input_key((int)e.gbutton.which, k, false); } break; }   // a quick tap
                }
                if (k >= 0) nxhost_input_key((int)e.gbutton.which, k, down);
                break; }
            case SDL_EVENT_GAMEPAD_AXIS_MOTION: pad_axis(e.gaxis.which, (SDL_GamepadAxis)e.gaxis.axis, e.gaxis.value); break;
            case SDL_EVENT_GAMEPAD_SENSOR_UPDATE: pad_sensor(e.gsensor.which, e.gsensor.sensor, e.gsensor.data); break;
            case SDL_EVENT_FINGER_DOWN: if (e.tfinger.windowID == wid) touch(0, e.tfinger.fingerID, e.tfinger.x, e.tfinger.y); break;
            case SDL_EVENT_FINGER_MOTION: if (e.tfinger.windowID == wid) touch(1, e.tfinger.fingerID, e.tfinger.x, e.tfinger.y); break;
            case SDL_EVENT_FINGER_UP: case SDL_EVENT_FINGER_CANCELED: if (e.tfinger.windowID == wid) touch(2, e.tfinger.fingerID, e.tfinger.x, e.tfinger.y); break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: case SDL_EVENT_MOUSE_MOTION:
                if (mouse_touch && e.button.which != SDL_TOUCH_MOUSEID) {
                    int w = 1, h = 1; SDL_GetWindowSize(win, &w, &h);
                    float x = (e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.x : e.button.x) / (float)w, y = (e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.y : e.button.y) / (float)h;
                    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) { mouse_down = 1; nxhost_input_touch(AM_DOWN, 9, x, y); }
                    else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT && mouse_down) { mouse_down = 0; nxhost_input_touch(AM_UP, 9, x, y); }
                    else if (e.type == SDL_EVENT_MOUSE_MOTION && mouse_down) nxhost_input_touch(AM_MOVE, 9, x, y);
                }
                break;
            case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP:
                if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F11) { SDL_SetWindowFullscreen(win, !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN)); break; }
                if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE && (e.key.mod & SDL_KMOD_SHIFT)) { atomic_store(&g_quit, 1); break; }
                if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F1) { pnl_menu_toggle(); break; }
                if (pnl_menu_is_open()) {
                    if (e.type != SDL_EVENT_KEY_DOWN) break;
                    int m = e.key.key == SDLK_UP ? PNL_IN_UP : e.key.key == SDLK_DOWN ? PNL_IN_DOWN : e.key.key == SDLK_LEFT ? PNL_IN_LEFT : e.key.key == SDLK_RIGHT ? PNL_IN_RIGHT
                          : e.key.key == SDLK_RETURN || e.key.key == SDLK_Z ? PNL_IN_OK : e.key.key == SDLK_ESCAPE || e.key.key == SDLK_X ? PNL_IN_BACK : -1;
                    if (m >= 0 && pnl_menu_input(m) == PNL_MENU_QUIT) atomic_store(&g_quit, 1);
                    break;
                }
                if (!e.key.repeat) kbd_key(e.key.key, e.type == SDL_EVENT_KEY_DOWN);
                break;
            case SDL_EVENT_AUDIO_DEVICE_ADDED: case SDL_EVENT_AUDIO_DEVICE_REMOVED: if (!e.adevice.recording) nxhost_audio_route_changed(); break;
            default: break;
            }
        } while (!atomic_load(&g_quit) && SDL_PollEvent(&e));
        Uint64 now = SDL_GetTicks();
        for (int i = 0; i < MAXPAD; i++) if (g_pads[i].gp && g_pads[i].pend && now - g_pads[i].pend_since >= 120) {   // Start / Select alone: to the game now
            for (int b = 0; b < 32; b++) if (g_pads[i].pend >> b & 1) { int k = button_key((SDL_GamepadButton)b); if (k >= 0) nxhost_input_key((int)g_pads[i].id, k, true); }
            g_pads[i].pend = 0;
        }
        pnl_menu_tick(now);
        if (quit_mask) for (int i = 0; i < MAXPAD; i++) if (g_pads[i].gp && g_pads[i].combo_since && now - g_pads[i].combo_since >= 1500) {
            note("%s held: quitting", quit_names[quit_combo]); atomic_store(&g_quit, 1);
        }
        if (now - last_power > 30000) {
            last_power = now; int pct = -1; SDL_PowerState st = SDL_GetPowerInfo(NULL, &pct);
            if (st != SDL_POWERSTATE_UNKNOWN && st != SDL_POWERSTATE_NO_BATTERY) pn_host_battery_set(pct, st == SDL_POWERSTATE_CHARGING || st == SDL_POWERSTATE_CHARGED);
        }
        if (now - last_title > 500) {   // the shader preparing step's progress in the window title
            last_title = now; int64_t p = nxhost_prewarm_progress();
            if (p != last_prewarm) {
                last_prewarm = p; char t[300];
                int64_t total = p > 0 ? p >> 32 : 0, done = p > 0 ? p & 0xffffffff : 0;   // vka_pso.c: total << 32 | processed
                if (total > 0 && done < total) snprintf(t, sizeof t, "%s — preparing shaders %d%%", wt, (int)(done * 100 / total));
                else snprintf(t, sizeof t, "%s", wt);
                SDL_SetWindowTitle(win, t);
            }
        }
    }
    if (atomic_load(&g_host_done)) { int rc = atomic_load(&g_host_rc); note("the game ended (%d)", rc); pnl_companion_stop(); fflush(stderr); _exit(rc); }
    quit_now(0);
    return 0;
}
