#ifndef COLORSRECOMP_WINDOW_H
#define COLORSRECOMP_WINDOW_H

#include "core/types.h"

#include <stdbool.h>

// Host window for the software rasteriser's EFB. SDL2 when available; a
// headless build compiles to no-ops so the emulator runs unchanged.

// Create the window. Returns false when no display is available (the caller
// then stays headless). `scale` multiplies the 640x480 EFB.
bool window_init(u32 width, u32 height, const char* title, int scale);

// Upload one RGBA8 frame and present it. Safe to call when the window is not
// active (no-op).
void window_present(const u8* rgba, u32 width, u32 height);

// Pump the event queue. Returns false once the user asked to close (window
// closed, Esc, or Ctrl+Q).
bool window_poll(void);

// Keyboard state mapped to Wii Remote buttons (WPAD_BUTTON_* bits: D-pad in
// 0x000F, + = 0x0010, 2 = 0x0100, 1 = 0x0200, B = 0x0400, A = 0x0800,
// - = 0x1000, Home = 0x8000). Returns 0 when no window is open.
u32 window_buttons(void);

// True while a window is open (window mode implies GX decoding and the font
// shortcut so the game runs unattended).
bool window_active(void);

// True after window_poll returned false.
bool window_should_quit(void);

void window_shutdown(void);

#endif
