// Peanuts Native for Linux: the host's per-thread slot word.
//
// The Android host keeps each thread's guest slot block in Bionic's TLS_SLOT_APP, the word at tpidr_el0 + 0x10, and the game code its packer
// rewrote reads it from there ([tpidr_el0 + 0x10] + entry, dm/tools/patch_nso.py --target android). On glibc arm64 (TLS variant I) tpidr_el0
// points at a 16-byte TCB (dtv, private) and the main executable's static TLS block starts right after it, at tpidr_el0 + 0x10. So when this
// variable is the first one of the executable's TLS segment, that word is ours on every thread, exactly as Bionic leaves it to apps, and the
// Android packs run unchanged. This file must be the first object linked into peanuts-run (CMakeLists.txt), the segment's alignment must stay
// at most 16 (checked at build time with readelf), and pnl_tls_check() proves the layout on the running system before any guest code runs.
#include "pnl.h"
__attribute__((used, aligned(16), section(".tdata.0pnl_slot"))) __thread void *pnl_tls_app_slot = 0;
__attribute__((used)) __thread void *pnl_tls_app_slot_pad = 0;   // keeps the word away from a neighbour's writes

int pnl_tls_check(void) {
    char *tp; __asm__ volatile("mrs %0, tpidr_el0" : "=r"(tp));
    return (char *)&pnl_tls_app_slot == tp + 0x10 ? 0 : (int)((char *)&pnl_tls_app_slot - tp);
}
