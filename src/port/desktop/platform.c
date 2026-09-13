/* platform.h, for a PC.
 *
 * The clock is truncated to 32 bits, as the console's is, so code that
 * compares anything but a difference fails here too. */

#include "port/platform.h"

#include <windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")

#include <stdio.h>
#include <stdlib.h>

unsigned platform_clock_us(void) {
    static LARGE_INTEGER freq;
    LARGE_INTEGER        now;

    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (unsigned)((now.QuadPart * 1000000LL) / freq.QuadPart);
}

void platform_sleep_us(unsigned us) {
    static int fine;

    /* Without this the scheduler tick is 15.6 ms, so every 5 ms poll pass in
     * the tree -- the log writer's and the link's -- costs 15.6. Measured: the
     * check suite went from 106 s to 78 s on this line alone. */
    if (!fine) {
        timeBeginPeriod(1);
        fine = 1;
    }

    /* Sleep(0) yields rather than waits, which would spin a poll loop. */
    Sleep(us < 1000u ? 1 : us / 1000u);
}

platform_lock *platform_lock_new(const char *name) {
    CRITICAL_SECTION *cs = (CRITICAL_SECTION *)malloc(sizeof(*cs));

    (void)name;
    if (!cs) return NULL;
    InitializeCriticalSection(cs);
    return (platform_lock *)cs;
}

void platform_lock_take(platform_lock *lock) {
    if (lock) EnterCriticalSection((CRITICAL_SECTION *)lock);
}

void platform_lock_give(platform_lock *lock) {
    if (lock) LeaveCriticalSection((CRITICAL_SECTION *)lock);
}

typedef struct {
    int (*fn)(void *);
    void *arg;
} thread_start;

static DWORD WINAPI thread_entry(LPVOID p) {
    thread_start *s = (thread_start *)p;
    int           rc;

    rc = s->fn(s->arg);
    free(s);
    return (DWORD)rc;
}

/* A shared-time scheduler, and four buffers of sound queued ahead. */
void platform_thread_urgent(void) {}

int platform_thread_start(const char *name, int (*fn)(void *), void *arg) {
    thread_start *s;
    HANDLE        h;

    (void)name;
    if (!fn) return -1;
    s = (thread_start *)malloc(sizeof(*s));
    if (!s) return -1;
    s->fn  = fn;
    s->arg = arg;

    h = CreateThread(NULL, 0, thread_entry, s, 0, NULL);
    if (!h) {
        free(s);
        return -1;
    }
    CloseHandle(h);
    return 0;
}

/* This machine's clock is not ours to set, and saying 0 is how a caller tells
   that from a refusal. */
int platform_set_cpu_mhz(int mhz) {
    (void)mhz;
    return 0;
}
int platform_cpu_mhz(void) { return 0; }

/* Under run/, the one directory either machine is allowed to write into. */
static const char *dir_named(const char *leaf, char *store, unsigned n) {
    if (store[0]) return store;
    CreateDirectoryA("run", NULL);
    snprintf(store, n, "run/%s", leaf);
    CreateDirectoryA(store, NULL);
    snprintf(store, n, "run/%s/", leaf);
    return store;
}

const char *platform_data_dir(void) {
    static char ready[64];
    return dir_named("pocketfin-data", ready, sizeof(ready));
}

const char *platform_hostfs_dir(void) {
    static char ready[64];
    return dir_named("pocketfin-link", ready, sizeof(ready));
}

void platform_console_print(const char *text) {
    if (text) fputs(text, stdout);
}

void platform_console_at(int column, int row, const char *text) {
    /* Appended: drawing in place would overwrite how the run got here. */
    (void)column;
    (void)row;
    if (text && text[0]) printf("%s\n", text);
}

/* The window closing is the shell's own signal; this is the harness's quit
   word, and the check runner never sets either. */
static int g_quit;

void platform_note_quit(void) { g_quit = 1; }
int  platform_should_quit(void) { return g_quit; }

/* No dialog to offer, which lets the screens tell "nothing to choose from"
   from "the viewer chose nothing". */
int  platform_has_picker(void) { return 0; }
void platform_picker_offer(int somebody_is_there) { (void)somebody_is_there; }
int  platform_picker_begin(void) { return -1; }

platform_picker_result platform_picker_step(void) { return PLATFORM_PICKER_UNAVAILABLE; }
int platform_picker_showing(void) { return 0; }
void platform_picker_end(void) {}

/* No idle timer to hold off: a desktop does not suspend under a window that
   is drawing. */
void platform_activity_tick(void) {}
