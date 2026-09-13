/* usbhostfs: the cable to the development machine. Harness, not application.
 *
 * host0: carries one request at a time, and an operation on a desynced host0:
 * does not return. So every file operation in platform_hostfs_dir(), reads
 * included, runs on this one thread, and callers only ever wait bounded.
 * Nothing else may open a file there. */
#ifndef IO_LINK_H
#define IO_LINK_H

#define HOSTFS_LINE_MAX 256
#define HOSTFS_PASS_US  5000u

/* -1 when platform_hostfs_dir() names nowhere. */
int hostfs_start(void);

/* 0 on a boot from the card, and every caller must cope with that. */
int hostfs_up(void);

/* -1 at the bound. Unloading while the thread sits inside a usbhostfs
 * operation needs a power-cycle. */
int hostfs_stop(unsigned bound_ms);

/* Copies the bytes and returns how many it took. Two files at a time: the log
 * and a screenshot. */
unsigned hostfs_append(const char *leaf, const void *bytes, unsigned n);

/* Replaces `leaf` with `text`; latest wins. */
int hostfs_put(const char *leaf, const char *text);

/* The whole file, up to n - 1 bytes and at most 64 kB, NUL-terminated. 0 when
 * it is absent, or the thread did not answer within a second. A cut file is
 * not reported, and a cut JPEG still decodes: check the length where it
 * matters. */
unsigned hostfs_slurp(const char *leaf, char *out, unsigned n);

/* One watched file. hostfs_line() is 1 when `out` holds a first line that
 * changed since the last call; what the file held before watching started is
 * not delivered. */
int hostfs_watch(const char *leaf);
int hostfs_line(char *out, unsigned n);

/* 1 when nothing handed over for `leaf` is still waiting to be written. */
int hostfs_idle(const char *leaf);

/* For standby_watch(). hostfs_verify() waits for a write to land: the firmware
 * saying RESUME_COMPLETE is not proof the connection is back. */
void hostfs_release(void);
int  hostfs_acquire(void);
int  hostfs_verify(void);

#endif
