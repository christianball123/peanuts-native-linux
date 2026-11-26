#!/usr/bin/env python3
"""Applies Peanuts Native for Linux's few edits to a Peanuts Native source tree (igawa6/peanuts-native), in place.

Every edit is marked PNL and guarded by PEANUTS_LINUX, so the Android build of the same tree is unchanged. Each one must match exactly once:
a changed upstream file stops the build here with the file and the text that moved, instead of building something half-patched.
  usage: patch_upstream.py <peanuts-native tree>
"""
import sys
from pathlib import Path

EDITS = [
    # Vulkan: the window system's instance extensions (SDL's) instead of VK_KHR_android_surface; the loader under its runtime name
    ("dm/gfx/vk_android/vka_dev.c",
     '    if (!lib) lib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);\n',
     '#ifdef PEANUTS_LINUX   // PNL: the runtime loader is libvulkan.so.1 (libvulkan.so is only in the -devel package)\n'
     '    if (!lib) lib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);\n'
     '#endif\n'
     '    if (!lib) lib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);\n'),
    ("dm/gfx/vk_android/vka_dev.c",
     '    const char *iext[4]; uint32_t niext = 0;\n'
     '    if (has_ext(ie, ne, VK_KHR_SURFACE_EXTENSION_NAME) && has_ext(ie, ne, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME)) { iext[niext++] = VK_KHR_SURFACE_EXTENSION_NAME; iext[niext++] = VK_KHR_ANDROID_SURFACE_EXTENSION_NAME; V.has_android_surface = 1; }\n',
     '    const char *iext[16]; uint32_t niext = 0;   // PNL: room for the window system\'s extensions\n'
     '#ifdef PEANUTS_LINUX   // PNL: the extensions SDL needs for the game window (VK_KHR_surface + Wayland or X11), all present or none\n'
     '    { uint32_t nw = 0; const char *const *we = pnl_window_instance_extensions(&nw); int all = nw > 0 && nw <= 12;\n'
     '      for (uint32_t i = 0; all && i < nw; i++) if (!has_ext(ie, ne, we[i])) { VRED("the window system needs instance extension %s, which this driver lacks", we[i]); all = 0; }\n'
     '      if (all) { for (uint32_t i = 0; i < nw; i++) iext[niext++] = we[i]; V.has_android_surface = 1; VLOG("window system: %u instance extensions (%s%s)", nw, we[0], nw > 1 ? ", ..." : ""); } }\n'
     '#else\n'
     '    if (has_ext(ie, ne, VK_KHR_SURFACE_EXTENSION_NAME) && has_ext(ie, ne, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME)) { iext[niext++] = VK_KHR_SURFACE_EXTENSION_NAME; iext[niext++] = VK_KHR_ANDROID_SURFACE_EXTENSION_NAME; V.has_android_surface = 1; }\n'
     '#endif\n'),
    ("dm/gfx/vk_android/vka_dev.c",
     '#include <dlfcn.h>\n',
     '#include <dlfcn.h>\n'
     '#ifdef PEANUTS_LINUX\n#include <android/native_window.h>   // PNL: pnl_window_instance_extensions\n#endif\n'),
    # The present: the surface made by SDL for the game window
    ("dm/gfx/vk_android/vka_present.c",
     '        VkAndroidSurfaceCreateInfoKHR si = {VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR, .window = w};\n'
     '        VkResult r = vk.vkCreateAndroidSurfaceKHR(V.inst, &si, NULL, &g_surf); VkBool32 ok = VK_FALSE;\n',
     '#ifdef PEANUTS_LINUX   // PNL: the game window\'s surface, made by SDL (Wayland or X11)\n'
     '        uint64_t sh = 0; VkResult r = pnl_window_create_surface(w, (void *)V.inst, &sh) ? VK_ERROR_INITIALIZATION_FAILED : VK_SUCCESS; g_surf = (VkSurfaceKHR)sh; VkBool32 ok = VK_FALSE;\n'
     '#else\n'
     '        VkAndroidSurfaceCreateInfoKHR si = {VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR, .window = w};\n'
     '        VkResult r = vk.vkCreateAndroidSurfaceKHR(V.inst, &si, NULL, &g_surf); VkBool32 ok = VK_FALSE;\n'
     '#endif\n'),
    # A command record the replay cannot read (a list's jump or handle into memory that is gone: Mystery Dungeon DX read 0x4160xxxxxx, outside the guest address space,
    # about 77 s in, twice, with the replay worker on and off): the rest of that list is dropped (one frame's draws) instead of the process ending. Only an address with
    # nothing mapped (mincore: ENOMEM) is taken; a mapped page another hook guards (pool swaps, dirty tracking) goes on to that hook
    ("dm/gfx/vk_android/vka_exec.c",
     'static int exec(const uint8_t *p, int depth) {   // 1: parked at a wait (g_pk holds where it goes on)\n'
     '    for (;;) {\n'
     '        op_hdr h; memcpy(&h, p, sizeof h);\n',
     '#ifdef PEANUTS_LINUX   // PNL: guarded record reads (scripts/patch_upstream.py)\n'
     '#include <setjmp.h>\n#include <signal.h>\n#include <errno.h>\n#include <sys/mman.h>\n#include <unistd.h>\n'
     'void dm_fault_hook_register(int (*fn)(void *addr, int is_write, void *uc));\n'
     'static __thread jmp_buf *g_hdr_guard;\n'
     'static _Atomic uint64_t g_hdr_faults;\n'
     'static int hdr_fault(void *addr, int is_write, void *uc) {\n'
     '    (void)uc; jmp_buf *j = g_hdr_guard; if (!j || is_write) return 0;\n'
     '    unsigned char v; long pg = sysconf(_SC_PAGESIZE); int e = errno;\n'
     '    int gone = mincore((void *)((uintptr_t)addr & ~(uintptr_t)(pg - 1)), (size_t)pg, &v) != 0 && errno == ENOMEM; errno = e;\n'
     '    if (!gone) return 0;\n'
     '    g_hdr_guard = NULL; _longjmp(*j, 1);\n'
     '}\n'
     'static int hdr_read(op_hdr *h, const uint8_t *p) {\n'
     '    static _Atomic int hooked; if (!atomic_load(&hooked)) { int z = 0; if (atomic_compare_exchange_strong(&hooked, &z, 1)) dm_fault_hook_register(hdr_fault); }\n'
     '    jmp_buf j;\n'
     '    if (_setjmp(j)) {\n'
     '        sigset_t m; sigemptyset(&m); sigaddset(&m, SIGSEGV); sigaddset(&m, SIGBUS); pthread_sigmask(SIG_UNBLOCK, &m, NULL);\n'
     '        uint64_t n = atomic_fetch_add(&g_hdr_faults, 1);\n'
     '        if (n < 8 || n % 100 == 0) VRED("command record at %p not readable (nothing mapped there): the rest of its list dropped (%llu so far)", (const void *)p, (unsigned long long)n + 1);\n'
     '        return 0;\n'
     '    }\n'
     '    g_hdr_guard = &j; atomic_signal_fence(memory_order_seq_cst);   // (the fences keep the compiler from dropping the guard store around the load)\n'
     '    memcpy(h, p, sizeof *h); atomic_signal_fence(memory_order_seq_cst); g_hdr_guard = NULL;\n'
     '    return 1;\n'
     '}\n'
     '#endif\n'
     'static int exec(const uint8_t *p, int depth) {   // 1: parked at a wait (g_pk holds where it goes on)\n'
     '    for (;;) {\n'
     '#ifdef PEANUTS_LINUX\n'
     '        op_hdr h; if (!hdr_read(&h, p)) return 0;   // PNL\n'
     '#else\n'
     '        op_hdr h; memcpy(&h, p, sizeof h);\n'
     '#endif\n'),
    # Guest memory without the CPU cache (DM_VK_GUEST_UNCACHED=1): the memory type the GPU reads the game's pools from, chosen without HOST_CACHED. A test for
    # Mystery Dungeon DX on the Thor (mainline msm + Turnip): CPU-written vertex data (posed models, text) torn on screen, worse the more pools are placed
    ("dm/gfx/vk_android/vka_dev.c",
     '    if (V.guest_type == UINT32_MAX) V.guest_type = vka_mem_type(~0u, HV, 0);\n',
     '    if (V.guest_type == UINT32_MAX) V.guest_type = vka_mem_type(~0u, HV, 0);\n'
     '#ifdef PEANUTS_LINUX   // PNL: DM_VK_GUEST_UNCACHED=1 (scripts/patch_upstream.py)\n'
     '    { char tl[256] = ""; int k = 0; for (uint32_t i = 0; i < V.memp.memoryTypeCount && k < 200; i++) k += snprintf(tl + k, sizeof tl - k, " %u:%#x", i, V.memp.memoryTypes[i].propertyFlags);\n'
     '      VLOG("memory types:%s", tl); }\n'
     '    { const char *e = getenv("DM_VK_GUEST_UNCACHED"); if (e && atoi(e)) {\n'
     '        uint32_t t = vka_mem_type(~0u, HV | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);\n'
     '        if (t == UINT32_MAX) t = vka_mem_type(~0u, HV, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);\n'
     '        if (t != UINT32_MAX) { V.guest_type = t; VLOG("DM_VK_GUEST_UNCACHED=1: guest memory type %u (%#x), not cached by the CPU", t, V.memp.memoryTypes[t].propertyFlags); }\n'
     '        else VRED("DM_VK_GUEST_UNCACHED=1: no host-visible coherent type without HOST_CACHED: guest memory type kept"); } }\n'
     '#endif\n'),
    # A program's shader key is made at its first draw, on the replay thread, from the guest program object's control and code pointers. Mystery Dungeon DX
    # freed and reused a program object before that draw was replayed (4.5 minutes in, its control pointer read as 0x54fc934961660000): a pointer outside the
    # guest address space now gives no key (the draw is skipped) instead of a crash
    ("dm/gfx/nvn/hle_nvn.c",
     '    if (!sd->control) return 0;\n'
     '    nvn_ctl_lens(sd->control, &nctl, &ncode);\n'
     '    int code = sd->data && ncode && ncode < (1u << 24);\n',
     '    if (!sd->control) return 0;\n'
     '#ifdef PEANUTS_LINUX   // PNL: pointers of a program object the game reused (scripts/patch_upstream.py)\n'
     '    { int dm_guest_as_contains(uintptr_t a); uintptr_t ca = (uintptr_t)sd->control;\n'
     '      if (!dm_guest_as_contains(ca) || !dm_guest_as_contains(ca + 0x880 - 1)) { static _Atomic int nr; if (atomic_fetch_add(&nr, 1) < 8) LOG("[RED] nvn: a program\'s control section at %#lx is outside guest memory (a program object reused before its draw was replayed?): no key, the draw is skipped", (unsigned long)ca); return 0; } }\n'
     '#endif\n'
     '    nvn_ctl_lens(sd->control, &nctl, &ncode);\n'
     '    int code = sd->data && ncode && ncode < (1u << 24);\n'
     '#ifdef PEANUTS_LINUX\n'
     '    { int dm_guest_as_contains(uintptr_t a); uintptr_t da = (uintptr_t)sd->data; if (code && !(dm_guest_as_contains(da) && dm_guest_as_contains(da + ncode - 1))) code = 0; }\n'
     '#endif\n'),
    # ANativeWindow_setFrameRate is not in libnativewindow on Linux: the dlopen of it simply fails (no edit). The frame rate of the panel is the compositor's.
]

# Mystery Dungeon DX (01003D200BAA2000) update 1.0.2, main 3ab632dee82d59448599b2291f30994a: the 131 ThreadedStreamBuffer load-acquire cuts of 1.0.0's main
# (patches.py), found again in 1.0.2's main by the same shape (stlr of one stream field, add + plain ldr of its partner field from the same base: reader-sleeping
# +0x118 / write position +0x80, writer-sleeping +0xd8 / read position +0x40). Exactly 131 sites, the same 91 writers / 38 readers / 2 going-to-sleep sides, each
# at its 1.0.0 offset + 0x40, each holding the same original word. Without them 1.0.2 starts hang as 1.0.0's did (the profile's README, "Found on this game")
RTDX_102 = '3ab632dee82d59448599b2291f30994a'
def rtdx_102(root):
    p = root / 'dm/games/01003D200BAA2000/patches.py'
    s = p.read_text(encoding='utf-8')
    if RTDX_102 in s: return
    import re
    rows = re.findall(r"\n    \((0x[0-9a-f]+), ('[0-9a-f]{8}'), ('[0-9a-f]{8}'), ('[^'\n]*')\),", s)
    if len(rows) != 131: sys.exit(f'[RED] {p}: {len(rows)} cuts of 1.0.0, not 131 (upstream changed): the 1.0.2 cuts are not added')
    block = f"  '{RTDX_102}': [   # PNL: 1.0.2's main, the same 131 sites at 1.0.0's offsets + 0x40 (scripts/patch_upstream.py rtdx_102)\n" + \
            ''.join(f'    ({int(o, 16) + 0x40:#x}, {a}, {b}, {d}),\n' for o, a, b, d in rows) + '  ],\n'
    end = s.index('\n}\n', s.index('CUTS = {'))
    p.write_text(s[:end + 1] + block + s[end + 1:], encoding='utf-8')
    print(f'patched {p.relative_to(root)} (1.0.2 cuts)')

def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    root = Path(sys.argv[1])
    for rel, old, new in EDITS:
        p = root / rel
        s = p.read_text(encoding='utf-8')
        if new in s:
            continue   # applied already
        n = s.count(old)
        if n != 1:
            sys.exit(f'[RED] {rel}: the text to patch is there {n} times, not once (upstream changed):\n{old}')
        p.write_text(s.replace(old, new), encoding='utf-8')
        print(f'patched {rel}')
    rtdx_102(root)

if __name__ == '__main__':
    main()
