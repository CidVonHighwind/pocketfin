/* input.h, for the PC. The window hands over each key; the check runner has
 * no window, so its pad stays empty. */

#include "port/input.h"

#include "port/desktop/window.h"

#include <windows.h>

static unsigned g_down, g_held, g_pressed;

/* A key pressed and released between two samples is held nowhere, so the
   frame never saw it: a tap of the shoulders needed several goes. The latch
   carries such a key into exactly one sample. */
static unsigned g_latch;

/* WASD are the face buttons where they sit on the console, Q and E the
   shoulders beside them, the arrows the d-pad, Enter START, shift SELECT. */
static unsigned key_to_pad(unsigned vk) {
    switch (vk) {
    case VK_UP: return PAD_UP;
    case VK_DOWN: return PAD_DOWN;
    case VK_LEFT: return PAD_LEFT;
    case VK_RIGHT: return PAD_RIGHT;
    case 'S': return PAD_CROSS;
    case 'D': return PAD_CIRCLE;
    case 'W': return PAD_TRIANGLE;
    case 'A': return PAD_SQUARE;
    case 'Q': return PAD_L;
    case 'E': return PAD_R;
    case VK_RETURN: return PAD_START;
    case VK_SHIFT: return PAD_SELECT;
    default: return 0;
    }
}

void input_key(unsigned vk, int down) {
    unsigned bit = key_to_pad(vk);

    if (down) {
        g_down |= bit;
        g_latch |= bit;
    } else {
        g_down &= ~bit;
    }
}

int input_start(void) { return 0; }

void input_sample(void) {
    unsigned buttons = g_down | g_latch;

    g_latch   = 0;
    g_pressed = buttons & ~g_held;
    g_held    = buttons;
}

unsigned input_held(void) { return g_held; }
unsigned input_pressed(void) { return g_pressed; }
