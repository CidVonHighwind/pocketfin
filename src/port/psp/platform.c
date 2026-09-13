/* platform.h, for the console. */

#include "port/platform.h"

#include "base/log.h"
#include "port/gfx.h"

#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <pspthreadman.h>
#include <pspdebug.h>
#include <psppower.h>
#include <psputility.h>
#include <pspdisplay.h>
#include <pspnet_apctl.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

unsigned platform_clock_us(void) { return sceKernelLibcClock(); }

void platform_sleep_us(unsigned us) { sceKernelDelayThread(us); }

/* The semaphore id plus one, so id 0 is not NULL. */
platform_lock *platform_lock_new(const char *name) {
    SceUID id = sceKernelCreateSema(name ? name : "lock", 0, 1, 1, NULL);

    if (id < 0) return NULL;
    return (platform_lock *)(uintptr_t)(id + 1);
}

void platform_lock_take(platform_lock *lock) {
    if (lock) sceKernelWaitSema((SceUID)((uintptr_t)lock - 1), 1, NULL);
}

void platform_lock_give(platform_lock *lock) {
    if (lock) sceKernelSignalSema((SceUID)((uintptr_t)lock - 1), 1);
}

/* sceKernelStartThread copies only the slot index onto the new stack.
 *
 * Eight: the browser holds five -- the log writer, the cable, the artwork
 * fetcher, the library and the reports -- and a film adds its fetch and its
 * sound. A sixth refused here failed playback with "the stream worker would
 * not start". */
#define PLAT_THREADS 8

static struct {
    int busy;
    int (*fn)(void *);
    void *arg;
} g_slot[PLAT_THREADS];

static int claim_slot(void) {
    int i;

    for (i = 0; i < PLAT_THREADS; i++)
        if (!g_slot[i].busy) {
            g_slot[i].busy = 1;
            return i;
        }
    return -1;
}

static int thread_entry(SceSize args, void *argp) {
    int slot, rc;

    if (args != sizeof(slot) || !argp) return -1;
    slot = *(int *)argp;
    if (slot < 0 || slot >= PLAT_THREADS || !g_slot[slot].fn) return -1;

    rc                = g_slot[slot].fn(g_slot[slot].arg);
    g_slot[slot].busy = 0;
    sceKernelExitDeleteThread(rc);
    return rc;
}

/* 0x21, between the workers and the drawing thread. At 0x22, level with the
   fetcher, a running demux ran to its end first. At 0x1F, on the same episode
   at 222 MHz, 73 pictures came late where none had, and a segment took 881 ms
   where it had taken 810. */
void platform_thread_urgent(void) { (void)sceKernelChangeThreadPriority(sceKernelGetThreadId(), 0x21); }

int platform_thread_start(const char *name, int (*fn)(void *), void *arg) {
    SceUID th;
    int    slot;

    if (!fn) return -1;
    slot = claim_slot();
    if (slot < 0) return -1;
    g_slot[slot].fn  = fn;
    g_slot[slot].arg = arg;

    /* 0x22, two below the drawing thread's 0x20; lower is higher priority,
     * and strict. At 0x24, level with the link and log writer, cable traffic
     * starved the film though every stage fit in isolation (fetch 442 ms,
     * demux 0, decode 113 ms of a 1,001 ms segment). At 0x1A the workers
     * preempted the drawing loop: a single gfx_fill took 57 ms.
     *
     * 32 kB of stack. At 16 the film's worker overflowed formatting the
     * 1.4 kB device profile, and it surfaced as sceNetInetSocket refusing
     * with 0x80410005. */
    th = sceKernelCreateThread(name ? name : "worker", thread_entry, 0x22, 32 * 1024, 0, NULL);
    if (th < 0) {
        g_slot[slot].busy = 0;
        return -1;
    }
    if (sceKernelStartThread(th, sizeof(slot), &slot) < 0) {
        sceKernelDeleteThread(th);
        g_slot[slot].busy = 0;
        return -1;
    }
    return 0;
}

/* Read back, not assumed: the PLL rounds, and there are only two speeds. */
int platform_set_cpu_mhz(int mhz) {
    if (mhz > 0) {
        int rc = scePowerSetClockFrequency(mhz, mhz, mhz / 2);

        /* scePowerSetClockFrequency(333,333,166) returns 0 here and leaves
           the CPU at 222, so the two-call form is tried after it, and every
           return code is logged. */
        if (scePowerGetCpuClockFrequency() != mhz) {
            int rc2 = scePowerSetCpuClockFrequency(mhz);
            int rc3 = scePowerSetBusClockFrequency(mhz / 2);

            log_printf("cpu: %d MHz -- set 0x%08X, cpu 0x%08X, bus 0x%08X, now %d", mhz, (unsigned)rc, (unsigned)rc2, (unsigned)rc3,
                       scePowerGetCpuClockFrequency());
        } else {
            log_printf("cpu: asked %d MHz, rc 0x%08X, now %d", mhz, (unsigned)rc, scePowerGetCpuClockFrequency());
        }
    }
    return platform_cpu_mhz();
}

int platform_cpu_mhz(void) { return scePowerGetCpuClockFrequency(); }

static char g_module_dir[64];

void platform_note_module_path(const char *path) {
    const char *slash;
    unsigned    n;

    if (!path) return;
    slash = strrchr(path, '/');
    if (!slash) return;
    n = (unsigned)(slash - path) + 1; /* the separator stays */
    if (n >= sizeof(g_module_dir)) return;
    memcpy(g_module_dir, path, n);
    g_module_dir[n] = 0;
}

const char *platform_module_dir(void) { return g_module_dir; }

const char *platform_data_dir(void) {
    static char ready[64];
    SceUID      d;

    if (ready[0]) return ready;

    /* Beside the module, so a renamed install keeps its files. */
    if (!strncmp(g_module_dir, "ms0:", 4)) {
        d = sceIoDopen(g_module_dir);
        if (d >= 0) {
            sceIoDclose(d);
            snprintf(ready, sizeof(ready), "%s", g_module_dir);
            return ready;
        }
    }

    /* Loaded over the cable: the installed game's folder, never the card's
       root. */
    sceIoMkdir("ms0:/PSP", 0777);
    sceIoMkdir("ms0:/PSP/GAME", 0777);
    sceIoMkdir("ms0:/PSP/GAME/pocketfin", 0777);
    snprintf(ready, sizeof(ready), "ms0:/PSP/GAME/pocketfin/");
    return ready;
}

/* Whether host0: opens is whether a cable is attached. A "no" is cached only
 * after half a second: the module loads right behind a fresh usbhostfs, and
 * a first ask before the mount answers made a run that logged only to the
 * card, answered no command, and looked hung. Only a run with no cable pays
 * the wait. */
const char *platform_hostfs_dir(void) {
    static char ready[16];
    static int  settled;
    SceUID      d;
    int         tries;

    if (settled) return ready;

    for (tries = 0; tries < 10; tries++) {
        d = sceIoDopen("host0:/");
        if (d >= 0) {
            sceIoDclose(d);
            snprintf(ready, sizeof(ready), "host0:/");
            break;
        }
        platform_sleep_us(50000);
    }
    settled = 1;
    return ready;
}

/* pspDebugScreen assumes 8888: a line printed on the 5-6-5 panel is garbage. */
static int g_console_off;

void platform_console_silence(void) { g_console_off = 1; }

void platform_console_print(const char *text) {
    if (g_console_off) return;
    if (text) pspDebugScreenPrintf("%s", text);
}

void platform_console_at(int column, int row, const char *text) {
    int was_x, was_y;

    if (g_console_off) return;
    was_x = pspDebugScreenGetX();
    was_y = pspDebugScreenGetY();

    pspDebugScreenSetXY(column, row);
    pspDebugScreenPrintf("%s", text ? text : "");
    pspDebugScreenSetXY(was_x, was_y);
}

/* Read by the loop: stopping inside the exit callback never flushes the card. */
static volatile int g_quit;
static int          g_picker_up; /* defined with the picker below */

void platform_note_quit(void) { g_quit = 1; }

/* Not while the firmware's dialog is open. A visible netconf dialog cannot be
 * closed by software -- measured against ShutdownStart once, ShutdownStart
 * every frame for 600 frames, an update alongside it, and unloading all five
 * modules it loads. Only Back closes it. A run that leaves one up orphans it,
 * and the kernel then refuses every later dialog and refuses to suspend until
 * the console restarts. So this keeps drawing and quits once it closes; a
 * stop meanwhile times out, which is recoverable. */
int platform_should_quit(void) {
    static int said;

    if (g_quit && g_picker_up) {
        if (!said) {
            log_line("quit: the connection dialog is open and only Back closes it -- press it, and this leaves");
            said = 1;
        }
        return 0;
    }
    return g_quit;
}

/* The firmware's network chooser: no credential passes through this
 * application. Reusable -- opened with the net modules unloaded, or inside
 * our display list, it draws nothing. Gone means the kernel refused to start
 * it, or it would not shut down and its four threads are held until the
 * module is reloaded; a viewer cancelling is neither. */
static int g_picker_gone;

static int g_nobody;

void platform_picker_offer(int somebody_is_there) { g_nobody = !somebody_is_there; }

/* The firmware reads these for as long as the dialog is up. */
static pspUtilityNetconfData         g_conf;
static struct pspUtilityNetconfAdhoc g_adhoc;
static int                           g_picker_shown;   /* it has been VISIBLE once */
static int                           g_picker_closing; /* shutdown asked for       */
static int                           g_picker_reclaim; /* finishing the last run's  */
static unsigned                      g_picker_frames;

/* It is on screen within a few frames when it works. */
#define PICKER_APPEAR_FRAMES 120
/* Long enough for somebody at the console to press Back. */
#define PICKER_CLOSE_FRAMES 600
/* A dialog this run can finish answers at once; one it cannot, never. */
#define PICKER_RECLAIM_FRAMES 30

static int picker_open(void) {
    memset(&g_conf, 0, sizeof(g_conf));
    memset(&g_adhoc, 0, sizeof(g_adhoc));
    g_conf.base.size       = sizeof(g_conf);
    g_conf.base.language   = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
    g_conf.base.buttonSwap = PSP_UTILITY_ACCEPT_CROSS;
    /* The SDK samples' priorities. */
    g_conf.base.graphicsThread = 17;
    g_conf.base.accessThread   = 19;
    g_conf.base.fontThread     = 18;
    g_conf.base.soundThread    = 16;
    g_conf.action              = PSP_NETCONF_ACTION_CONNECTAP;
    /* Zeroed but present: null here is a dialog that never appears. */
    g_conf.adhocparam = &g_adhoc;

    {
        int rc = sceUtilityNetconfInitStart(&g_conf);

        if (rc < 0) {
            /* 0x80020190 is the kernel out of memory: this heap too large for
               what the dialog still has to load. */
            log_printf("net: the console would not open its connection dialog (0x%08X)", (unsigned)rc);
            g_picker_gone = 1;
            return -1;
        }
    }
    g_picker_shown   = 0;
    g_picker_closing = 0;
    g_picker_frames  = 0;
    return 0;
}

int platform_picker_begin(void) {
    int st;

    if (g_picker_gone || g_picker_up) return -1;

    /* A dialog the last run left open outlives it, and the kernel refuses
       another while it is up. The status is the kernel's, so it is shut down
       here, pumped to NONE in step(), and ours opened behind it.
       Negative is an error, not a state: 0x80110005 on every clean start. */
    st = sceUtilityNetconfGetStatus();
    if (st < 0) st = PSP_UTILITY_DIALOG_NONE;
    if (st == PSP_UTILITY_DIALOG_FINISHED) {
        /* Shut down but never let go -- measured against ShutdownStart, an
           update in that state, and unloading all five modules it loads.
           Opening anyway answers 0x80110001; only a restart clears it. */
        log_line("net: a connection dialog is wedged in the firmware -- the console must be restarted before it offers another");
        g_picker_gone = 1;
        return -1;
    }
    if (st != PSP_UTILITY_DIALOG_NONE) {
        log_printf("net: a connection dialog was left open at status %d -- taking it back", st);
        sceUtilityNetconfShutdownStart();
        g_picker_up      = 1;
        g_picker_reclaim = 1;
        g_picker_shown   = 0;
        g_picker_closing = 1;
        g_picker_frames  = 0;
        return 0;
    }

    if (picker_open() != 0) return -1;
    g_picker_up      = 1;
    g_picker_reclaim = 0;
    return 0;
}

/* Not while one is up: the offline overlay's Retry, over a network page
   already offering it, once stacked seven deep. */
int platform_has_picker(void) { return !g_picker_gone && !g_nobody && !g_picker_up; }

int platform_picker_showing(void) { return g_picker_up && g_picker_shown; }

void platform_picker_end(void) {
    int spin, last_end = -1;

    if (!g_picker_up) return;

    log_line("net: closing the connection dialog on the way out");
    sceUtilityNetconfShutdownStart();
    for (spin = 0; spin < PICKER_CLOSE_FRAMES; spin++) {
        int st = sceUtilityNetconfGetStatus();

        if (st < 0) break;
        /* The next run pays for this path failing, and cannot say how. */
        if (st != last_end) {
            log_printf("net: closing, status %d after %d frames", st, spin);
            last_end = st;
        }
        if (st == PSP_UTILITY_DIALOG_NONE || st == PSP_UTILITY_DIALOG_FINISHED) break;
        gfx_frame_begin();
        if (st == PSP_UTILITY_DIALOG_VISIBLE) {
            /* Every frame: a shutdown asked of a visible dialog is dropped --
               180 frames of it measured, never leaving VISIBLE. Updated too,
               so it still answers Back. */
            sceUtilityNetconfShutdownStart();
            gfx_list_suspend();
            sceUtilityNetconfUpdate(1);
        } else if (st == PSP_UTILITY_DIALOG_QUIT) {
            sceUtilityNetconfShutdownStart();
        }
        gfx_frame_end();
    }
    if (spin >= PICKER_CLOSE_FRAMES) log_line("net: the connection dialog would not close -- the console needs a restart before it offers another");
    g_picker_up = 0;
}

static platform_picker_result picker_done(void) {
    int state = 0;

    g_picker_up = 0;
    /* Asked of the stack: closing the dialog says nothing about joining. */
    if (sceNetApctlGetState(&state) == 0 && state == 4) return PLATFORM_PICKER_JOINED;
    return PLATFORM_PICKER_CANCELLED;
}

platform_picker_result platform_picker_step(void) {
    static int last = -1;
    int        status;

    if (!g_picker_up) return PLATFORM_PICKER_UNAVAILABLE;
    g_picker_frames++;

    status = sceUtilityNetconfGetStatus();
    if (status < 0) status = PSP_UTILITY_DIALOG_NONE; /* an error, not a state */
    /* A stuck dialog takes the drawing thread, and under PSPLINK only a
       reboot comes back: what it last did has to be written already. */
    if (status != last) {
        log_printf("net: dialog status %d after %u frames", status, g_picker_frames);
        last = status;
    }

    if (g_picker_reclaim) {
        /* NONE only: FINISHED has shut down but not let go, a second dialog
           is refused against it, and read as our end it popped the page with
           the old dialog's answer. */
        if (status != PSP_UTILITY_DIALOG_NONE) {
            if (status == PSP_UTILITY_DIALOG_QUIT) {
                sceUtilityNetconfShutdownStart();
            } else {
                /* It only moves in a frame it is updated in, FINISHED too. */
                gfx_list_suspend();
                sceUtilityNetconfUpdate(1);
            }
            if (g_picker_frames > PICKER_RECLAIM_FRAMES) {
                /* The driver will not let this run finish it -- measured at
                   VISIBLE and at FINISHED alike. */
                log_line("net: the dialog left behind belongs to a run that is gone -- the console must be restarted");
                g_picker_gone = 1;
                g_picker_up   = 0;
                return PLATFORM_PICKER_UNAVAILABLE;
            }
            return PLATFORM_PICKER_RUNNING;
        }
        log_line("net: the dialog left behind is closed -- opening ours");
        g_picker_reclaim = 0;
        last             = -1;
        if (picker_open() != 0) {
            g_picker_up = 0;
            return PLATFORM_PICKER_UNAVAILABLE;
        }
        return PLATFORM_PICKER_RUNNING;
    }

    switch (status) {
    case PSP_UTILITY_DIALOG_VISIBLE:
        g_picker_shown = 1;
        /* Outside our list: it runs its own, and nested neither finishes. The
           caller's gfx_frame_end() swaps what it drew onto the panel. */
        gfx_list_suspend();
        sceUtilityNetconfUpdate(1);
        return PLATFORM_PICKER_RUNNING;

    case PSP_UTILITY_DIALOG_QUIT: sceUtilityNetconfShutdownStart(); return PLATFORM_PICKER_RUNNING;

    case PSP_UTILITY_DIALOG_FINISHED: return picker_done();

    case PSP_UTILITY_DIALOG_NONE:
        /* NONE is also the state before it first becomes visible. */
        if (g_picker_shown || g_picker_closing) {
            if (g_picker_closing && g_picker_frames > PICKER_CLOSE_FRAMES) {
                g_picker_gone = 1;
                log_line("net: the picker would not shut down -- its memory is gone until this module is reloaded");
            }
            return picker_done();
        }
        if (g_picker_frames < PICKER_APPEAR_FRAMES) return PLATFORM_PICKER_RUNNING;
        /* Left running, its four threads are gone for the life of the module
           and the next load is refused for want of kernel memory. */
        log_line("net: the picker never appeared -- shutting it down");
        sceUtilityNetconfShutdownStart();
        g_picker_closing = 1;
        return PLATFORM_PICKER_RUNNING;

    default: return PLATFORM_PICKER_RUNNING; /* INIT: keep pumping */
    }
}

void platform_activity_tick(void) { scePowerTick(0); }
