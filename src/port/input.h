/* The pad. Bit values are the console's; port/psp/input.c checks that at
 * compile time. */
#ifndef PORT_INPUT_H
#define PORT_INPUT_H

#define PAD_SELECT   0x000001u
#define PAD_START    0x000008u
#define PAD_UP       0x000010u
#define PAD_RIGHT    0x000020u
#define PAD_DOWN     0x000040u
#define PAD_LEFT     0x000080u
#define PAD_L        0x000100u
#define PAD_R        0x000200u
#define PAD_TRIANGLE 0x001000u
#define PAD_CIRCLE   0x002000u
#define PAD_CROSS    0x004000u
#define PAD_SQUARE   0x008000u

int input_start(void);

/* Once a frame, from the one loop: pressed is the difference between two
 * samples, so a second caller eats presses the first never sees. */
void input_sample(void);

unsigned input_held(void);
unsigned input_pressed(void); /* went down at the last sample */

#endif
