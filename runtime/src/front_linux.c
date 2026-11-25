// Peanuts Native for Linux: the host's front end (front.h), in front_android.c's place. The guest runs on the thread that calls nxhost_main (peanuts-run
// starts it with an 8 MB stack); the window reaches the NVN → Vulkan replay through nxhost_set_window, as the Android launcher's Surface does.
#include "dm.h"
#include "front.h"
#include "android/input_android.h"
#include "../gfx/vk_android/vk_android.h"
#include <stdatomic.h>

void front_run(void (*guest)(void)) {
    vka_pso_prepare_launch();   // waits for the cold pipeline build when the profile ships a seed (as on Android)
    LOG("front end (Linux): the guest runs on this thread; presents go to peanuts-run's window");
    guest();
}
void *front_metal_layer(void) { return NULL; }
int front_screen_max_fps(void) { return 0; }
int front_screen_variable_refresh(void) { return 0; }
void front_screen_match(int fps) { (void)fps; }
void front_progress(const char *title, long done, long total) {
    if (!title) return;
    static _Atomic long last_pct = -1; long pct = total > 0 ? done * 100 / total : -1;
    if (atomic_exchange(&last_pct, pct) != pct && (pct % 10 == 0 || pct < 0)) LOG("[progress] %s %ld%%", title, pct);
}
extern int g_swkbd_min_chars;
int front_text_input(const char *title, const char *sub, const char *guide, const char *initial, int max_chars, char *out, int out_cap) {
    return android_text_input(title, sub, guide, initial, max_chars, g_swkbd_min_chars > 0 ? g_swkbd_min_chars : 0, out, out_cap);
}
int front_user_select(const char *const *names, const int *offered, int n, int def) { return android_user_select(names, offered, n, def); }
int front_controller_support(const void *arg, int *count) { return android_controller_support(arg, count); }

int dm_host_main(int argc, char **argv);   // dm/host/main.c's main, renamed for this executable (CMakeLists.txt: -Dmain=dm_host_main on that file)
// glibc's malloc hands out untagged pointers already (Bionic's heap tagging, which front_android.c turns off, does not exist here)
__attribute__((visibility("default"))) int nxhost_main(int argc, char **argv) { return dm_host_main(argc, argv); }
