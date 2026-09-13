/* One film, one frame at a time: fetches segments, demuxes fragments, and
 * hands over pictures on their own schedule. Both machines run this file;
 * under it sits one per-machine part, port/decode.h, which callers also draw
 * the film through.
 *
 * Non-blocking: stream_open() returns at once and the fetch runs on a worker.
 * One film at a time, so there is no handle; the rings are reserved from
 * port/mem.h when a film opens.
 *
 * No seek: the server returns a transcode starting where it was asked, with
 * nothing before that point. A seek is another stream_open(). */
#ifndef MODEL_STREAM_H
#define MODEL_STREAM_H

/* `at` is the picture's place in the film, in the server's ticks of 100 ns. */
typedef struct {
    int                w, h;
    unsigned long long at;
} stream_picture;

typedef struct {
    unsigned pictures;
    unsigned late;    /* handed over after their time had passed */
    unsigned starved; /* a picture was due and nothing had arrived */
    int      buffering;
    unsigned stalls, stall_ms;
    unsigned fps_centi; /* what the stream turned out to be */
    unsigned kbit;

    /* Next picture's distance from due: negative is normal; a large positive
       is playback stalled waiting for the clock, which no counter shows. */
    int due_ms;

    unsigned ring_used, ring_cap;
    unsigned aring_used, aring_cap;
    unsigned audio_frames, audio_dry, audio_dropped;
    /* Also counted once at the start and after each stall or pause, where
       empty is right. */
    unsigned audio_underruns;

    /* Per segment, except decode_us, which is per picture. */
    unsigned fetch_ms, demux_ms, decode_us;
    unsigned ttfb_ms, body_ms;
    /* Of `segments`, how many found their socket already sent. A ttfb high
       beside a high count here is a server that will not start the next
       segment early; a low count is a presend thrown away. */
    unsigned pipelined;

    unsigned segments, segment_largest;
    unsigned not_ready; /* times the encoder was behind */
    int      w, h;
} stream_stats;

/* Started with the application, not per film: the browser holds most of the
 * console's thread slots. Sleeps between films. */
int  stream_start(void);
void stream_stop(void);

/* `run_ticks` is the item's length, 0 when unknown -- it is what tells "the
 * encoder is behind" from "the film ended". */
int stream_open(const char *item_id, unsigned long long from, unsigned long long run_ticks);

/* Empty until the worker has opened the run. */
const char *stream_session(void);

/*   1  `out` holds a picture to draw
 *   0  nothing new yet -- draw the last one again, or nothing
 *  -1  the film ended, or it failed and stream_error() says so
 *
 * 1 only when a picture's own time has come, so a caller drawing at 60 Hz
 * over a 24 fps film gets each picture once, and never while `paused`. */
int stream_frame(stream_picture *out, int paused);

/* In ticks: without it a stall and a healthy stream look identical on the
 * seek bar. */
unsigned long long stream_buffered(void);

void        stream_close(void);
const char *stream_error(void);
void        stream_get_stats(stream_stats *out);

#endif
