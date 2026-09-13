/* Not a WIN32-subsystem binary, so the start-up errors main.c prints are seen. */

#include "port/desktop/window.h"

#include "port/gfx.h"

#include <windows.h>

#define SCALE 2

static HWND       g_win;
static int        g_quit;
static BITMAPINFO g_bmi;
/* 8-8-8, because StretchDIBits will not take 5-6-5 with a bottom-up DIB
   without a colour-mask header; converting 130k pixels a frame costs less
   than the special case. */
static unsigned *g_rgb;

static LRESULT CALLBACK window_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE:
    case WM_DESTROY: g_quit = 1; return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            g_quit = 1;
            return 0;
        }
        input_key((unsigned)wp, 1);
        return 0;
    case WM_KEYUP: input_key((unsigned)wp, 0); return 0;
    default: break;
    }
    return DefWindowProc(w, msg, wp, lp);
}

int window_open(void) {
    WNDCLASSA cls;
    RECT      r;

    if (g_win) return 0;

    memset(&cls, 0, sizeof(cls));
    cls.lpfnWndProc   = window_proc;
    cls.hInstance     = GetModuleHandleA(NULL);
    cls.hCursor       = LoadCursorA(NULL, IDC_ARROW);
    cls.lpszClassName = "PocketfinWindow";
    if (!RegisterClassA(&cls)) return -1;

    r.left = r.top = 0;
    r.right        = GFX_W * SCALE;
    r.bottom       = GFX_H * SCALE;
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);

    g_win = CreateWindowA("PocketfinWindow", "Pocketfin", WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, CW_USEDEFAULT,
                          CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, cls.hInstance, NULL);
    if (!g_win) return -1;

    g_rgb = (unsigned *)calloc((size_t)GFX_W * GFX_H, sizeof(unsigned));
    if (!g_rgb) return -1;

    memset(&g_bmi, 0, sizeof(g_bmi));
    g_bmi.bmiHeader.biSize  = sizeof(g_bmi.bmiHeader);
    g_bmi.bmiHeader.biWidth = GFX_W;
    /* Negative: top-down, the order the framebuffer is in. */
    g_bmi.bmiHeader.biHeight      = -GFX_H;
    g_bmi.bmiHeader.biPlanes      = 1;
    g_bmi.bmiHeader.biBitCount    = 32;
    g_bmi.bmiHeader.biCompression = BI_RGB;

    ShowWindow(g_win, SW_SHOW);
    return 0;
}

/* Every frame: a window that does not pump is marked "not responding", and the
   keyboard never arrives. */
void window_pump(void) {
    MSG m;

    while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
}

int window_closed(void) { return g_quit; }

void window_present8888(const unsigned *fb) {
    HDC dc;
    int i, n = GFX_W * GFX_H;

    if (!g_win || !g_rgb || !fb) return;

    /* 0xAABBGGRR in, 0x00RRGGBB out: the console packs red in the low byte
       and GDI wants it in the high one. */
    for (i = 0; i < n; i++) {
        unsigned p = fb[i];

        g_rgb[i] = ((p & 0xFFu) << 16) | (p & 0xFF00u) | ((p >> 16) & 0xFFu);
    }

    dc = GetDC(g_win);
    if (!dc) return;
    StretchDIBits(dc, 0, 0, GFX_W * SCALE, GFX_H * SCALE, 0, 0, GFX_W, GFX_H, g_rgb, &g_bmi, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(g_win, dc);
}
