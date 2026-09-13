/* input.h, for the console. */

#include "port/input.h"

#include "base/log.h"

#include <pspctrl.h>

/* The names must be the console's bits, or every mask crosses the seam wrong. */
_Static_assert(PAD_SELECT == PSP_CTRL_SELECT, "PAD_SELECT");
_Static_assert(PAD_START == PSP_CTRL_START, "PAD_START");
_Static_assert(PAD_UP == PSP_CTRL_UP, "PAD_UP");
_Static_assert(PAD_RIGHT == PSP_CTRL_RIGHT, "PAD_RIGHT");
_Static_assert(PAD_DOWN == PSP_CTRL_DOWN, "PAD_DOWN");
_Static_assert(PAD_LEFT == PSP_CTRL_LEFT, "PAD_LEFT");
_Static_assert(PAD_L == PSP_CTRL_LTRIGGER, "PAD_L");
_Static_assert(PAD_R == PSP_CTRL_RTRIGGER, "PAD_R");
_Static_assert(PAD_TRIANGLE == PSP_CTRL_TRIANGLE, "PAD_TRIANGLE");
_Static_assert(PAD_CIRCLE == PSP_CTRL_CIRCLE, "PAD_CIRCLE");
_Static_assert(PAD_CROSS == PSP_CTRL_CROSS, "PAD_CROSS");
_Static_assert(PAD_SQUARE == PSP_CTRL_SQUARE, "PAD_SQUARE");

static int      g_up;
static unsigned g_held, g_pressed;

int input_start(void) {
    if (g_up) return 0;

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    g_up = 1;

    log_line("input: pad sampling");
    return 0;
}

void input_sample(void) {
    SceCtrlData pad;

    if (!g_up) return;
    if (sceCtrlReadBufferPositive(&pad, 1) <= 0) return;

    g_pressed = pad.Buttons & ~g_held;
    g_held    = pad.Buttons;
}

unsigned input_held(void) { return g_held; }
unsigned input_pressed(void) { return g_pressed; }
