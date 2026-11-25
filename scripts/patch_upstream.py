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
