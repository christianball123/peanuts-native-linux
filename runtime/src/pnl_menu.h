// The in-game menu (pnl_menu.c). SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>

enum { PNL_IN_UP, PNL_IN_DOWN, PNL_IN_LEFT, PNL_IN_RIGHT, PNL_IN_OK, PNL_IN_BACK };
enum { PNL_MENU_NONE, PNL_MENU_QUIT };

void pnl_menu_init(int w, int h, const char *title);   // loads the font; the menu stays off without one
void pnl_menu_resize(int w, int h);
int pnl_menu_is_open(void);
void pnl_menu_open(void);
void pnl_menu_close(void);
void pnl_menu_toggle(void);
void pnl_menu_redraw(void);
int pnl_menu_input(int what);       // a menu press; PNL_MENU_QUIT = leave the game
void pnl_menu_tick(uint64_t now_ms); // the frame rate counter
