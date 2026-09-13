/* The desktop's window: where a finished frame goes, and where the keyboard
 * comes from. Only the windowed target compiles it. */
#ifndef PORT_DESKTOP_WINDOW_H
#define PORT_DESKTOP_WINDOW_H

int  window_open(void);
void window_pump(void);

/* The frame at full depth, which is what a film is composed at -- see
 * port/decode.h. 0xAABBGGRR, the console's packing. */
void window_present8888(const unsigned *fb);

int window_closed(void);

/* Every key the window receives, as a Windows virtual-key code. Implemented
 * by port/desktop/input.c, which decides what each key is on the pad. */
void input_key(unsigned vk, int down);

#endif
