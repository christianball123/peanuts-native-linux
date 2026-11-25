/* Peanuts Native for Linux - the companion (Eden Duo dual-screen page) on the bottom screen.
 *
 * The Android app shows the companion page in a second activity on the other display and talks
 * to the host through dev.peanuts.Bridge.  This is the same loop for Linux: a thread here pulls
 * the page's frames from the host (nxhost_peanuts_frame) into a shared file, and peanuts-bottom,
 * a small separate program, shows them on the bottom screen and sends the fingers back.
 *
 *   PNL_COMPANION=off         no bottom screen for this launch
 *   PNL_BOTTOM_DIRECT=1       start peanuts-bottom directly, not through armada-run-bottom
 *   PNL_BOTTOM_DISPLAY=<n>    (desktop) the monitor to use; default: the smallest one that is
 *                             not the game's
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "pnl_bottom.h"

void nxhost_peanuts_frame_size(int *w, int *h);
int nxhost_peanuts_frame(void *out_rgba, int w, int h);
void nxhost_peanuts_frame_again(void);
void nxhost_peanuts_touch(int id, float x, float y, int down);
void nxhost_peanuts_panel(int w, int h);
void nxhost_peanuts_present(int shown);
int nxhost_peanuts_rt_state(void);
int nxhost_peanuts_haptic(float *amplitude, int *ms, int *respect_system, int *kind);

void pnl_pad_pulse(float amplitude, int ms);   /* pnl_run.c */

#define NOTE(...) do { fprintf(stderr, "[peanuts-run] companion: " __VA_ARGS__); fputc('\n', stderr); } while (0)

static const char *ARMADA_RUN_BOTTOM = "/usr/bin/armada-run-bottom";

static char g_map_path[512];
static void *g_map;
static pid_t g_helper = -1;
static atomic_int g_stop;
static pthread_t g_thread;
static int g_started;

static void sleep_ms(int ms) { struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000L }; nanosleep(&t, NULL); }
static long long now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000; }

static int env_on(const char *k, int def) {
    const char *v = getenv(k);
    if (!v || !*v) return def;
    return !(strcmp(v, "0") == 0 || strcmp(v, "off") == 0 || strcmp(v, "false") == 0 || strcmp(v, "no") == 0);
}

static int make_map(void) {
    const char *dir = "/dev/shm";
    if (access(dir, W_OK) != 0) { dir = getenv("XDG_RUNTIME_DIR"); if (!dir || access(dir, W_OK) != 0) dir = "/tmp"; }
    snprintf(g_map_path, sizeof g_map_path, "%s/peanuts-native-bottom-%d", dir, (int)getpid());
    int fd = open(g_map_path, O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) { NOTE("cannot create %s: %s", g_map_path, strerror(errno)); return 0; }
    if (ftruncate(fd, (off_t)PNB_MAP_BYTES) != 0) { NOTE("ftruncate: %s", strerror(errno)); close(fd); unlink(g_map_path); return 0; }
    g_map = mmap(NULL, PNB_MAP_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (g_map == MAP_FAILED) { g_map = NULL; NOTE("mmap: %s", strerror(errno)); unlink(g_map_path); return 0; }
    pnb_header *h = (pnb_header *)g_map;
    h->magic = PNB_MAGIC;
    h->version = PNB_VERSION;
    h->host_pid = (uint32_t)getpid();
    return 1;
}

static int self_dir(char *out, size_t n) {
    ssize_t l = readlink("/proc/self/exe", out, n - 1);
    if (l <= 0) return 0;
    out[l] = 0;
    char *s = strrchr(out, '/');
    if (!s) return 0;
    *s = 0;
    return 1;
}

static pid_t spawn_helper(int *via_armada) {
    char dir[1024], exe[1200], log[1200];
    if (!self_dir(dir, sizeof dir)) return -1;
    snprintf(exe, sizeof exe, "%s/peanuts-bottom", dir);
    if (access(exe, X_OK) != 0) { NOTE("%s is missing", exe); return -1; }
    const char *lg = getenv("PNL_BOTTOM_LOG");
    if (lg && *lg) snprintf(log, sizeof log, "%s", lg);
    else snprintf(log, sizeof log, "/tmp/peanuts-bottom-%u.log", (unsigned)getuid());
    int armada = !env_on("PNL_BOTTOM_DIRECT", 0) && access(ARMADA_RUN_BOTTOM, X_OK) == 0;
    *via_armada = armada;
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        setsid();
        int fd = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); if (fd > 2) close(fd); }
        unsetenv("LD_PRELOAD");
        if (armada) {
            setenv("PNL_BOTTOM_ARMADA", "1", 1);
            execl(ARMADA_RUN_BOTTOM, ARMADA_RUN_BOTTOM, "--", exe, g_map_path, (char *)NULL);
            fprintf(stderr, "armada-run-bottom failed (%s); trying directly\n", strerror(errno));
            unsetenv("PNL_BOTTOM_ARMADA");
        }
        execl(exe, exe, g_map_path, (char *)NULL);
        _exit(127);
    }
    NOTE("bottom screen window started (%s), log: %s", armada ? "Game Mode, through armada-run-bottom" : "direct", log);
    return pid;
}

static void *companion_thread(void *arg) {
    (void)arg;
    /* 1. Wait for the host to decide whether this title has a companion package. */
    int st = 0;
    long long t0 = now_ms();
    while (!atomic_load(&g_stop)) {
        st = nxhost_peanuts_rt_state();
        if (st != 0) break;
        if (now_ms() - t0 > 10 * 60 * 1000) { NOTE("no answer from the companion runtime"); return NULL; }
        sleep_ms(100);
    }
    if (st == 2) { NOTE("none installed for this game; the bottom screen stays as it is"); return NULL; }
    if (st == 3) { NOTE("the companion package was refused (see the 'peanuts' lines above for why)"); return NULL; }
    if (st != 1) return NULL;
    if (!make_map()) return NULL;
    pnb_header *h = (pnb_header *)g_map;

    /* 2. Bring up the bottom window, and keep pulling frames while the game runs. */
    int tries = 0, via_armada = 0;
    pid_t child = -1;
    long long adopt_until = 0;
    uint32_t ready_seen = 0, panel_w = 0, panel_h = 0, touch_tail = 0;
    int presented = 0;
    uint64_t seq = 0;
    long long next_spawn = 0, last_log = 0;
    while (!atomic_load(&g_stop)) {
        long long now = now_ms();
        if (g_helper == -1 && now >= next_spawn) {
            if (tries >= 4) { NOTE("the bottom screen window keeps closing; giving up for this session"); break; }
            tries++;
            PNB_STORE32(&h->helper_pid, 0);
            g_helper = spawn_helper(&via_armada);
            if (g_helper <= 0) break;
            child = g_helper;
        }
        if (child > 0) {   /* our own child: peanuts-bottom, or armada-run-bottom starting it */
            int ws = 0;
            if (waitpid(child, &ws, WNOHANG) == child) {
                int code = WIFEXITED(ws) ? WEXITSTATUS(ws) : 128 + (WIFSIGNALED(ws) ? WTERMSIG(ws) : 0);
                child = -1;
                if (code == PNB_EXIT_NO_SCREEN) { NOTE("no second screen here (see the peanuts-bottom log)"); break; }
                if (code == 0 && via_armada) { g_helper = -2; adopt_until = now + 8000; }   /* armada-run-bottom may hand the window off and return */
                else { g_helper = -3; NOTE("bottom screen window closed (%d)", code); }
            }
        }
        if (g_helper == -2) {   /* waiting for the handed-off window to say who it is */
            uint32_t hp = PNB_LOAD32(&h->helper_pid);
            if (hp && kill((pid_t)hp, 0) == 0) { g_helper = (pid_t)hp; NOTE("armada-run-bottom returned; the window runs on (pid %u)", hp); }
            else if (now > adopt_until) { g_helper = -3; NOTE("the bottom screen window did not start"); }
        } else if (g_helper > 0 && child < 0 && kill(g_helper, 0) != 0 && errno == ESRCH) { g_helper = -3; NOTE("bottom screen window closed"); }
        if (g_helper == -3) {   /* gone: hide the page, try again a little later */
            g_helper = -1;
            if (presented) { nxhost_peanuts_present(0); presented = 0; }
            ready_seen = PNB_LOAD32(&h->helper_ready);
            next_spawn = now + 1000LL * tries;
        }

        uint32_t ready = PNB_LOAD32(&h->helper_ready);
        if (ready != ready_seen && g_helper > 0) {
            ready_seen = ready;
            tries = 1;   /* it came up: later closes start counting again */
            if (!presented) { nxhost_peanuts_present(1); presented = 1; }
            nxhost_peanuts_frame_again();
            NOTE("showing on %s (%ux%u)", h->where, PNB_LOAD32(&h->panel_w), PNB_LOAD32(&h->panel_h));
        }
        uint32_t pw = PNB_LOAD32(&h->panel_w), ph = PNB_LOAD32(&h->panel_h);
        if (presented && pw && ph && (pw != panel_w || ph != panel_h)) {
            panel_w = pw; panel_h = ph;
            nxhost_peanuts_panel((int)pw, (int)ph);
        }

        /* Frames: the host counts these calls as the page being looked at. */
        if (presented) {
            int fw = 0, fh = 0;
            nxhost_peanuts_frame_size(&fw, &fh);
            if (fw > 0 && fh > 0 && fw <= PNB_MAX_W && fh <= PNB_MAX_H) {
                uint8_t *dst = pnb_pixels(g_map, seq + 1);
                if (nxhost_peanuts_frame(dst, fw, fh)) {
                    PNB_STORE32(&h->frame_w, (uint32_t)fw);
                    PNB_STORE32(&h->frame_h, (uint32_t)fh);
                    seq++;
                    PNB_STORE64(&h->frame_seq, seq);
                }
            } else if (fw > 0 && fh > 0 && now - last_log > 10000) {
                last_log = now;
                NOTE("page %dx%d is larger than %dx%d; not shown", fw, fh, PNB_MAX_W, PNB_MAX_H);
            }
        }

        /* Fingers from the bottom screen. */
        uint32_t head = PNB_LOAD32(&h->touch_head);
        if (head - touch_tail > PNB_TOUCH_RING) touch_tail = head - PNB_TOUCH_RING;
        while (touch_tail != head) {
            pnb_touch t = h->touch[touch_tail % PNB_TOUCH_RING];
            touch_tail++;
            if (presented) nxhost_peanuts_touch(t.id, t.x, t.y, t.down);
        }

        /* Haptic pulses from the page go to the controller. */
        float amp = 0; int ms = 0, respect = 0, kind = 0;
        while (nxhost_peanuts_haptic(&amp, &ms, &respect, &kind)) pnl_pad_pulse(amp, ms);

        sleep_ms(16);
    }
    if (presented) nxhost_peanuts_present(0);
    return NULL;
}

void pnl_companion_start(void) {
    if (!env_on("PNL_COMPANION", 1)) { NOTE("off for this game"); return; }
    if (g_started) return;
    if (pthread_create(&g_thread, NULL, companion_thread, NULL) == 0) { pthread_detach(g_thread); g_started = 1; }
}

/* Called from quit paths (may run right before _exit). */
void pnl_companion_stop(void) {
    atomic_store(&g_stop, 1);
    if (g_map) PNB_STORE32(&((pnb_header *)g_map)->quit, 1);
    pid_t hp = g_map ? (pid_t)PNB_LOAD32(&((pnb_header *)g_map)->helper_pid) : 0;
    if (hp > 0) kill(hp, SIGTERM);
    if (g_helper > 0 && g_helper != hp) kill(g_helper, SIGTERM);
    if (g_map_path[0]) unlink(g_map_path);
}
