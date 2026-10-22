// Window size (Video > Window size, runtime/window_size.c): the sizes offered, the window and fullscreen mode they
// give, and the image size in fullscreen when the display has no mode of that size.
#pragma once
#include <stdbool.h>

typedef struct { int w, h; } WinSize;
enum { WINSIZE_MAX = 32 };

// The sizes offered, grouped by shape (16:9, 21:9, 16:10, 4:3 / 5:4, then any other the desktop has): those of the
// list that fit on the window's display, and the desktop's own size. Fullscreen also offers bigger sizes the display
// has a mode for (GPU downsampling such as NVIDIA DSR). Any thread.
int winsize_list(WinSize *out, bool fullscreen);
// The size the setting gives (g_settings.window_w / window_h; unset: 1280 x 720 in a window, the desktop in
// fullscreen), one of the sizes offered. Any thread.
WinSize winsize_effective(bool fullscreen);
WinSize winsize_default(bool fullscreen);  // (the setting unset)
WinSize winsize_desktop(void);
// The window's display's refresh rate, Hz (0: not known yet). Any thread.
float winsize_refresh_rate(void);
// "1920 x 1080  (16:9)"
void winsize_name(WinSize s, char *buf, int n);
// "16:9", "21:9", "16:10", "4:3", "5:4", or NULL for another shape
const char *winsize_aspect(int w, int h);
// Fullscreen on a size the display has no mode for: the game image is that size, scaled to fit the screen (true, and
// the size). Any thread.
bool winsize_virtual(int *w, int *h);

// Main thread (the window loop): read the window's display again when it changed; apply the setting to the window.
void winsize_refresh(void *sdl_window);
void winsize_apply(void *sdl_window);
