// Peanuts Native for Linux: the glue between peanuts-run (SDL3 front end) and the Peanuts host built for glibc.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <android/native_window.h>
typedef struct SDL_Window SDL_Window;

ANativeWindow *pnl_window_wrap(SDL_Window *win);
void pnl_window_set_size(ANativeWindow *w, int pw, int ph);
SDL_Window *pnl_window_sdl(ANativeWindow *w);

// The host's slot word (Bionic's TLS_SLOT_APP, tpidr_el0 + 0x10) is the main executable's first thread-local variable on glibc (pnl_tls.c).
// 0 when the layout holds on this thread
int pnl_tls_check(void);
