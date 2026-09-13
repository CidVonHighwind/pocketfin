/* The command channel, wired to the application. In tools/ because app/ may
 * not include from the harness. */
#ifndef TOOLS_DRIVE_H
#define TOOLS_DRIVE_H

void drive_start(void);
int  drive_serve(void); /* once a frame; non-zero when one was served */

#endif
