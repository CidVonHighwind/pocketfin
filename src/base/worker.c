/* See worker.h. */

#include "base/worker.h"

#include "base/log.h"
#include "port/platform.h"

#include <string.h>

static int run(void *arg) {
    worker *w = (worker *)arg;

    while (w->running) {
        int did = w->pass(w->user);

        w->passes++;
        if (!did) platform_sleep_us(w->idle_us);
    }

    w->left = 1;
    return 0;
}

int worker_start(worker *w, const char *name, int (*pass)(void *), void *user, unsigned idle_us) {
    if (!w || !pass) return -1;
    if (w->running) return 0;

    memset(w, 0, sizeof(*w));
    w->name    = name ? name : "worker";
    w->pass    = pass;
    w->user    = user;
    w->idle_us = idle_us ? idle_us : 20000u;
    w->running = 1;

    if (platform_thread_start(w->name, run, w) != 0) {
        w->running = 0;
        return -1;
    }
    return 0;
}

int worker_stop(worker *w, unsigned bound_us) {
    unsigned waited = 0;

    if (!w || !w->running) return 0;
    w->running = 0;

    /* A pass can be inside a request that takes the whole timeout, still
       writing into the caller's arrays. */
    while (!w->left && waited < bound_us) {
        platform_sleep_us(2000);
        waited += 2000;
    }
    if (!w->left) {
        log_printf("worker: %s DID NOT LEAVE after %u ms", w->name, bound_us / 1000u);
        return -1;
    }
    return 0;
}

int worker_quiet(worker *w, unsigned bound_us) {
    unsigned was, waited = 0;

    if (!w || !w->running) return 0;

    was = w->passes;
    while (w->passes == was && waited < bound_us) {
        platform_sleep_us(2000);
        waited += 2000;
    }
    if (w->passes == was) {
        log_printf("worker: %s is still in a pass after %u ms", w->name, bound_us / 1000u);
        return -1;
    }
    return 0;
}

int worker_running(const worker *w) { return w && w->running; }
