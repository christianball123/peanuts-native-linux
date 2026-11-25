/* Peanuts Native for Linux - the bottom-screen link.
 *
 * peanuts-run (the game) and peanuts-bottom (the window on the Thor's bottom screen) share one
 * file in /dev/shm (or $XDG_RUNTIME_DIR).  The game writes the companion page's pixels into it,
 * the bottom window shows them and writes the fingers back.  Two processes are needed because in
 * ArmadaOS Game Mode the bottom screen is its own Xwayland display, reached only through
 * armada-run-bottom.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <stdint.h>

#define PNB_MAGIC   0x31424E50u  /* "PNB1" */
#define PNB_VERSION 1u
#define PNB_MAX_W   2048
#define PNB_MAX_H   2048
#define PNB_BUF_BYTES ((uint64_t)PNB_MAX_W * PNB_MAX_H * 4u)
#define PNB_TOUCH_RING 128
#define PNB_PIXELS_OFF 8192u
#define PNB_MAP_BYTES (PNB_PIXELS_OFF + 2u * PNB_BUF_BYTES)

/* Helper exit codes the game understands. */
#define PNB_EXIT_NO_SCREEN 3   /* no second screen here: do not try again */

typedef struct {
    int32_t id;
    float x, y;      /* panel pixels */
    int32_t down;    /* 1 down/move, 0 up */
} pnb_touch;

typedef struct {
    uint32_t magic, version;
    uint32_t host_pid;          /* game */
    uint32_t helper_pid;        /* bottom window (written by the helper) */
    uint32_t panel_w, panel_h;  /* bottom window size in pixels (helper) */
    uint32_t helper_ready;      /* helper: window is up; bumped on every (re)creation */
    uint32_t quit;              /* game: the helper should close */
    uint64_t frame_seq;         /* game: pixels of frame N are in buffer N & 1 */
    uint32_t frame_w, frame_h;  /* game: size of the newest frame */
    uint32_t touch_head;        /* helper: number of touches ever written */
    uint32_t shown_frames;      /* helper: frames shown (for the log) */
    char     where[64];         /* helper: "x11 :1 (Game Mode)" etc., for the log */
    pnb_touch touch[PNB_TOUCH_RING];
} pnb_header;

_Static_assert(sizeof(pnb_header) <= PNB_PIXELS_OFF, "header too big");

#define PNB_LOAD32(p)     __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define PNB_STORE32(p, v) __atomic_store_n((p), (v), __ATOMIC_RELEASE)
#define PNB_LOAD64(p)     __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define PNB_STORE64(p, v) __atomic_store_n((p), (v), __ATOMIC_RELEASE)

static inline uint8_t *pnb_pixels(void *map, uint64_t seq) {
    return (uint8_t *)map + PNB_PIXELS_OFF + (seq & 1u) * PNB_BUF_BYTES;
}
