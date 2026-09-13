/* Pocketfin, a Jellyfin client for the PlayStation Portable.
 * Copyright (C) 2026 CidVonHighwind
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. It is distributed WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License in LICENSE for details.
 *
 * Boot, the one loop, and the power callbacks. The callbacks record an edge
 * and nothing else: the lifecycle advances only on this thread. */

#include "app/app.h"
#include "tools/drive.h"
#include "base/log.h"
#include "base/prefs.h"
#include "base/standby.h"
#include "base/version.h"
#include "view/theme.h"
#include "io/link.h"
#include "io/net.h"
#include "port/gfx.h"
#include "port/jpeg.h"
#include "port/psp/avmod.h"
#include "port/platform.h"
#include "tools/remote.h"
#include "tools/selftest.h"
#include "tools/speed.h"

#include <pspkernel.h>
#include <pspdebug.h>
#include <pspiofilemgr.h>
#include <psppower.h>

#include <stdio.h>
#include <string.h>

void tests_register_all(void);

PSP_MODULE_INFO("pocketfin", PSP_MODULE_USER, 0, 1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
/* Beside the 8.4 MB player budget this leaves 1 3/4 MB for the cable. At
 * 15,360 a module 5,824 bytes larger starved usbhostfs: the checks ran and
 * logged, and every cable write silently failed. */
PSP_HEAP_SIZE_KB(14336);

/* Console-only, in port/psp/platform.c. */
void        platform_note_module_path(const char *path);
const char *platform_module_dir(void);

#define FLAG_RING 16

static volatile unsigned g_flags[FLAG_RING];
static volatile unsigned g_flag_w;

/* Anything more runs in a callback context during a suspend, the one place a
   mistake cannot be logged. */
static int power_callback(int unknown, int pwrflags, void *common) {
    (void)unknown;
    (void)common;

    g_flags[g_flag_w % FLAG_RING] = (unsigned)pwrflags;
    g_flag_w++;

    if (pwrflags & (PSP_POWER_CB_SUSPENDING | PSP_POWER_CB_STANDBY)) standby_note_going();
    if (pwrflags & PSP_POWER_CB_RESUME_COMPLETE) standby_note_back();
    return 0;
}

static int take_flags(unsigned *out) {
    static unsigned r;
    unsigned        w = g_flag_w;

    if (r == w) return 0;
    if (w - r > FLAG_RING) r = w - FLAG_RING;
    *out = g_flags[r % FLAG_RING];
    r++;
    return 1;
}

static void say_flags(void) {
    unsigned f;

    while (take_flags(&f)) {
        char line[96];

        snprintf(line, sizeof(line), "power: callback %08x%s%s%s%s", f, (f & PSP_POWER_CB_SUSPENDING) ? " suspending" : "",
                 (f & PSP_POWER_CB_STANDBY) ? " standby" : "", (f & PSP_POWER_CB_RESUMING) ? " resuming" : "",
                 (f & PSP_POWER_CB_RESUME_COMPLETE) ? " resume-complete" : "");
        /* Only an edge is a breadcrumb. The rest are the battery moving a
           percent, every ~36 s on a charger, and forced onto the card they
           wrote to it all through a film with its log off. */
        if (f & (PSP_POWER_CB_SUSPENDING | PSP_POWER_CB_STANDBY | PSP_POWER_CB_RESUMING | PSP_POWER_CB_RESUME_COMPLETE |
                 PSP_POWER_CB_POWER_SWITCH))
            log_mark(line);
        else
            log_line(line);
    }
}

/* leave() terminates this thread, so anything here that touches the card can
   be killed mid-write, which needs a power cycle. The loop writes the mark. */
static int exit_callback(int arg1, int arg2, void *common) {
    (void)arg1;
    (void)arg2;
    (void)common;
    platform_note_quit();
    return 0;
}

static volatile int g_cb_exit = -1, g_cb_power = -1, g_cb_slot = -1;

static int callback_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;

    g_cb_exit = sceKernelCreateCallback("exit", exit_callback, NULL);
    if (g_cb_exit >= 0) sceKernelRegisterExitCallback(g_cb_exit);

    g_cb_power = sceKernelCreateCallback("power", power_callback, NULL);
    if (g_cb_power >= 0) g_cb_slot = scePowerRegisterCallback(-1, g_cb_power);

    sceKernelSleepThreadCB();
    return 0;
}

static int g_cb_thread = -1;

static void setup_callbacks(void) {
    g_cb_thread = sceKernelCreateThread("update", callback_thread, 0x11, 0xFA0, 0, 0);
    if (g_cb_thread >= 0) sceKernelStartThread(g_cb_thread, 0, 0);
}

static void say_callbacks(void) {
    log_printf("callbacks: thread %d, exit cb %d, power cb %d in slot %d", g_cb_thread, g_cb_exit, g_cb_power, g_cb_slot);
    if (g_cb_slot < 0) log_line("callbacks: THE POWER CALLBACK IS NOT REGISTERED -- no standby event can be seen");
}

/* A breadcrumb that survives a frozen console: the cable may be dead, and the
   card log sits behind the writer thread this path is about to stop. 19 ms a
   step, on a path that is ending anyway. The last line is the call that hung. */
static void mark(const char *step) {
    static int started;
    char       path[96];
    SceUID     fd;

    snprintf(path, sizeof(path), "%spocketfin-exit.txt", platform_data_dir());
    /* This run only: an appended history buries where this shutdown stopped
       under the runs that worked. */
    fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | (started ? PSP_O_APPEND : PSP_O_TRUNC), 0777);
    started = 1;
    if (fd < 0) return;
    sceIoWrite(fd, step, (SceSize)strlen(step));
    sceIoWrite(fd, "\n", 1);
    sceIoClose(fd);
}

/* Everything is waited for: a module that unloads while a thread sits inside
   a usbhostfs operation needs a power-cycle afterwards. */
static void leave(void) {
    mark("leave: begin");
    /* Its threads outlive this module, and the next load cannot open another. */
    platform_picker_end();
    app_stop();

    /* AVCODEC, AAC and MPEGBASE outlive this module: their 49,152 bytes left in
       the middle of the partition cost 2.4 MB off the largest free run.
       jpeg_finish() first: same silicon, and it holds AVCODEC too. */
    jpeg_finish();
    av_modules_release();
    mark("leave: app stopped");

    /* The stack lives in the kernel and outlives this module: left up, it
       costs the 512 kB pool on every load, and the next load's inet layer and
       apctl refuse to start. */
    net_stop();
    mark("leave: net down");

    log_line("POCKETFIN-DONE");
    log_flush();
    mark("leave: log flushed");
    log_close();
    mark("leave: log closed");

    if (hostfs_stop(2000) != 0) platform_console_print("\n  the link thread would not stop\n");
    mark("leave: hostfs stopped");

    /* scePower holds the slot, so unloading does not reclaim it. */
    if (g_cb_slot >= 0) {
        scePowerUnregisterCallback(g_cb_slot);
        g_cb_slot = -1;
    }
    if (g_cb_thread >= 0) {
        sceKernelTerminateDeleteThread(g_cb_thread);
        g_cb_thread = -1;
    }

    mark("leave: done");

    /* Returning from main() leaves the module resident holding its whole heap,
     * and the next load is refused. Installed on the card the EBOOT is the
     * game, so ending the game returns to the XMB; it does not return, so
     * everything above has already been waited for. Over the cable
     * sceKernelExitGame changes nothing. */
    if (!strncmp(platform_module_dir(), "ms0:", 4)) sceKernelExitGame();

    /* Over the cable we return, and PSPLINK unloads us. Unloading ourselves
       never gives the heap back, and the next load has too little left to
       bring its cable up. */
}

#define CABLE_LOG "pocketfin-run.log"

static unsigned cable(const void *bytes, unsigned n) { return hostfs_append(CABLE_LOG, bytes, n); }
static int      cable_idle(void) { return hostfs_idle(CABLE_LOG); }

static void open_logs(char *where, unsigned n) {
    char card[96];
    int  cabled = hostfs_up(), carded;

    if (cabled) log_cable(cable, cable_idle);
    snprintf(card, sizeof(card), "%spocketfin.log", platform_data_dir());
    carded = log_open(card) == 0;
    snprintf(where, n, "%s + %s", cabled ? CABLE_LOG : "(no cable)", carded ? card : "(no card)");
}

/* On the panel too: a frozen heartbeat is visible from across the room, which
   makes a cold boot with no cable worth looking at. */
static char g_beat[LOG_LINE_MAX];

static void heartbeat(unsigned up_us) {
    log_stats st;

    log_get_stats(&st);
    snprintf(g_beat, sizeof(g_beat), "up %u.%u s | %s e%u | frames %u | log %u lines %u cable drop | card %u lines %u drop",
             up_us / 1000000u, (up_us / 100000u) % 10u, standby_text(standby_state()), standby_epoch(), app_frames(), st.lines,
             st.cable_dropped, st.card_lines, st.card_dropped);
    log_line(g_beat);
    platform_console_at(0, 2, g_beat);
}

static int mode_is(int argc, char *argv[], const char *what) { return argc > 1 && argv[1] && !strcmp(argv[1], what); }

int main(int argc, char *argv[]) {
    char     where[176];
    unsigned started, next_beat = 0;

    pspDebugScreenInit();
    platform_note_module_path(argc > 0 ? argv[0] : NULL);
    setup_callbacks();

    hostfs_start();
    open_logs(where, sizeof(where));
    started = platform_clock_us();

    log_line("POCKETFIN-READY");
    log_printf("module %s", (argc > 0 && argv[0]) ? argv[0] : "(no argv)");
    log_printf("version %s", POCKETFIN_VERSION);
    log_printf("from   %s", platform_module_dir()[0] ? platform_module_dir() : "(unknown)");
    log_printf("data   %s", platform_data_dir());
    log_printf("link   %s", platform_hostfs_dir()[0] ? platform_hostfs_dir() : "(none)");
    log_printf("log    %s", where);
    say_callbacks();
    log_flush();

    /* The checks start the workers they need, so the application must not
       start beside them. Its own command channel too, or whichever polls
       first eats the other's command. */
    if (mode_is(argc, argv, "checks")) {
        const char *only = argc > 2 ? argv[2] : 0;
        int         failed;

        remote_use_channel("cmd-check.txt", "ack-check.txt", "state-check.txt");
        prefs_load();
        log_to_card(pref(PREF_LOG_TO_CARD));
        ui_theme_set(pref(PREF_THEME));
        remote_start();
        /* Nobody is there to answer the firmware's network dialog: the suite
           once stopped dead at "screen: -> network". */
        platform_picker_offer(0);

        log_printf("mode: checks%s%s", only ? ", only " : "", only ? only : "");
        /* Nothing watches this panel, and pacing each frame to the refresh was
           over half the suite. */
        gfx_pace(0);
        tests_register_all();
        failed = only ? selftest_run_group(only) : selftest_run_all();
        leave();
        return failed;
    }

    /* A mode, not a check: it would fail on the world's state, not the code.
       Before app_start: two threads inside sceUtilityLoadNetModule left
       sceNetInetInit failing with 0x8002013A on every run after, across
       reboots. */
    if (mode_is(argc, argv, "speed")) {
        int rc;

        log_line("mode: speed");
        rc = speed_run();
        leave();
        return rc;
    }

    app_start();
    drive_start();

    platform_console_print("\n  Pocketfin -- cross opens, circle goes back, START is settings\n");

    while (!platform_should_quit()) {
        unsigned up = platform_clock_us() - started;

        say_flags();
        standby_step();
        drive_serve();
        app_frame();

        /* The clock counts through standby, so this compares against the last
           beat, not a schedule. */
        if (up - next_beat >= 1000000u || next_beat == 0) {
            next_beat = up;
            heartbeat(up);
        }

        /* gfx_frame_end paces on vblank; with no panel nothing else does. */
        if (!gfx_up()) platform_sleep_us(10000);
    }

    say_flags();
    mark("exit: the loop saw the quit flag");
    platform_console_print("\n  stopping\n");
    leave();
    return 0;
}
