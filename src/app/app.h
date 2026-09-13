/* Start-up order, one frame, shutdown. app_draw() and app_inject() are for
 * tools/drive.c, which must draw between taking a command and acking it.
 *
 * app_start() wants the port up and the log open: the log's primary sink is a
 * file on the other side of io/link, so that thread has to be running first. */
#ifndef APP_APP_H
#define APP_APP_H

void app_start(void);
void app_frame(void);

/* Waits for the workers to leave: unloading a module while a thread sits in
   a usbhostfs operation needs a power-cycle afterwards. */
void app_stop(void);

void     app_draw(void);
void     app_inject(unsigned pad_mask);
unsigned app_frames(void);
unsigned app_uptime_us(void);

#endif
