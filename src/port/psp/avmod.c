/* See avmod.h. */

#include "avmod.h"

#include <psputility.h>

#include "base/log.h"

#define AV_MAX 6

static int g_held[AV_MAX]; /* in the order they were taken */
static int g_n;

void av_module_take(int id) {
    int i, rc;

    for (i = 0; i < g_n; i++)
        if (g_held[i] == id) return;
    if (g_n >= AV_MAX) return;

    /* One a previous run left behind answers "already loaded": not ours. */
    rc = sceUtilityLoadAvModule(id);
    if (rc != 0) {
        log_printf("av: module %d was already loaded (0x%08X) -- not ours to free", id, rc);
        return;
    }
    g_held[g_n++] = id;
}

void av_modules_release(void) {
    while (g_n > 0) {
        int id = g_held[--g_n];

        log_printf("av: unload module %d -> 0x%08X", id, sceUtilityUnloadAvModule(id));
    }
}
