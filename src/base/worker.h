/* A thread with one stop protocol. `pass` returns 1 when it did something,
 * 0 when there was nothing to do, and the worker then sleeps `idle_us`.
 * worker_stop() waits for the thread to say it has left: unloading a module
 * while a thread sits inside a usbhostfs operation needs a power-cycle. */
#ifndef BASE_WORKER_H
#define BASE_WORKER_H

typedef struct {
    const char *name;
    int (*pass)(void *user);
    void    *user;
    unsigned idle_us;

    volatile int      running;
    volatile int      left;
    volatile unsigned passes; /* bumped as each pass returns */
} worker;

int worker_start(worker *w, const char *name, int (*pass)(void *), void *user, unsigned idle_us);

/* 0 when it said it had left, -1 at the bound, which is logged. */
int worker_stop(worker *w, unsigned bound_us);

/* Waits out the current pass without stopping the thread. A caller that has
   just taken away the work -- a film closing -- needs to know the worker is
   not inside the state it is about to tear down; one completed pass after
   the work is gone is the proof. 0 when it came back, -1 at the bound. */
int worker_quiet(worker *w, unsigned bound_us);

int worker_running(const worker *w);

#endif
