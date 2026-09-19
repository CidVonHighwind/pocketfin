/* What portable code needs from the machine. Services only: nothing here has
 * a power state. */
#ifndef PORT_PLATFORM_H
#define PORT_PLATFORM_H

/* Wraps every ~71 minutes: compare differences, never absolutes. */
unsigned platform_clock_us(void);

void platform_sleep_us(unsigned us);

/* NULL when one could not be made; callers must treat that as fatal. */
typedef struct platform_lock platform_lock;

platform_lock *platform_lock_new(const char *name);
void           platform_lock_take(platform_lock *lock);
void           platform_lock_give(platform_lock *lock);

/* 0 when one is running. Not joinable: a worker stops by watching a flag. */
int platform_thread_start(const char *name, int (*fn)(void *), void *arg);

/* The calling thread cannot be late: sound, which the hardware drains 23 ms
 * after it was last fed. */
void platform_thread_urgent(void);

/* In MHz; the bus follows at half, which is why this is one call. Returns the
 * clock in force afterwards: this machine has two, and the PLL rounds anything
 * between. 0 on every desktop. */
int platform_set_cpu_mhz(int mhz);
int platform_cpu_mhz(void);

/* The EBOOT's folder, ms0:/PSP/GAME/pocketfin/ under PSPLINK. Every file the
 * application writes goes here. Not SAVEDATA, where an entry without a
 * PARAM.SFO shows in the save manager as "Corrupted Data". */
const char *platform_data_dir(void);

/* What the server's session list should call this machine: the console's
 * nickname, or "PSP"/"PC" when there is nothing better. Never empty. */
const char *platform_device_name(void);

/* Where the PC and the console see the same files. Empty with no cable, and
 * only io/link.h's thread opens anything there. */
const char *platform_hostfs_dir(void);

/* The machine's own screen. NOT printf, which PSPLINK captures down the
 * cable, leaving the panel blank on a boot with no other readout. */
void platform_console_print(const char *text);

/* Both become no-ops once the panel is taken: text drawn into a framebuffer
 * whose format changed underneath it is garbage on screen. */
void platform_console_silence(void);
void platform_console_at(int column, int row, const char *text);

/* Set by the machine's own exit path -- HOME, a window closing -- or by the
 * harness's quit word. */
int  platform_should_quit(void);
void platform_note_quit(void);

/* The firmware suspends on an idle timer that counts button presses, and a
 * film is two hours of not pressing anything. A no-op on a desktop. */
void platform_activity_tick(void);

/* The console's own network chooser: the profiles are the firmware's, so no
 * network credential passes through this application.
 *
 * ONE STEP A FRAME, from INSIDE an open frame after the page has drawn what
 * goes behind it. The dialog draws through the caller's frame: as a blocking
 * loop it rendered nowhere, and a dialog that did not finish left nothing to
 * stop the application with short of the power switch. */
typedef enum {
    PLATFORM_PICKER_RUNNING = 0, /* still up; call again next frame */
    PLATFORM_PICKER_JOINED,      /* it joined a network itself      */
    PLATFORM_PICKER_CANCELLED,   /* the viewer backed out           */
    PLATFORM_PICKER_UNAVAILABLE  /* nothing to open on this machine */
} platform_picker_result;

/* 0 when it is up and step() should be called, -1 when there is none. */
int platform_picker_begin(void);

platform_picker_result platform_picker_step(void);

/* 1 once the dialog is on the panel. The page behind is the ground it
 * composites onto, so anything of ours still there reads as part of its
 * list. */
int platform_picker_showing(void);

/* Bounded, whether or not a viewer was finished. The dialog's threads OUTLIVE
 * THIS MODULE: left up, the next load cannot start another, and only a reboot
 * of the console clears them. */
void platform_picker_end(void);

/* 0 once the dialog is unusable for the life of this module, and NOT because
 * it has been used: a viewer who cancels must be able to open it again.
 *
 * Also 0 while platform_picker_offer() says nobody is there to answer, as in
 * a check run. An application that never calls it keeps the dialog. */
int  platform_has_picker(void);
void platform_picker_offer(int somebody_is_there);

#endif
