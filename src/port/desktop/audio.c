/* See port/audio.h -- this machine, through libavcodec and waveOut.
 *
 * waveOut because it is a queue of small buffers that blocks when full, as
 * the console's channel does, so the pacing is the same on both machines.
 * More than one buffer: the hardware is still reading the one just handed
 * over, so a single buffer is a click every frame. */

#include "port/audio.h"

#include "base/log.h"

#include <stdio.h>
#include <string.h>

#if !defined(POCKETFIN_FFMPEG) || !defined(_WIN32)

int audio_available(void) { return 0; }
int audio_open(const unsigned char *asc, unsigned asc_len, unsigned rate, unsigned channels) {
    (void)asc;
    (void)asc_len;
    (void)rate;
    (void)channels;
    return -1;
}
int audio_play(const unsigned char *frame, unsigned len) {
    (void)frame;
    (void)len;
    return -1;
}
void audio_stats(unsigned *frames, unsigned *underruns) {
    if (frames) *frames = 0;
    if (underruns) *underruns = 0;
}
void        audio_close(void) {}
const char *audio_error(void) { return "this build has no sound"; }

#else

#include <libavcodec/avcodec.h>

#include <windows.h>

#include <mmsystem.h>

/* An AAC frame is 1024 samples, or 2048 with SBR doubling it. Four buffers of
   that is under a tenth of a second of queue -- enough to ride a scheduling
   hiccup, short enough that a pause is not heard as a tail. */
#define FRAME_SAMPLES 2048
#define FRAME_BYTES   (FRAME_SAMPLES * AUDIO_CHANNELS * 2)
#define BUFFERS       4

static AVCodecContext *g_dec;
static AVPacket       *g_pkt;
static AVFrame        *g_frame;

static HWAVEOUT g_out;
static WAVEHDR  g_hdr[BUFFERS];
static short    g_pcm[BUFFERS][FRAME_SAMPLES * AUDIO_CHANNELS];
static int      g_next;

static unsigned g_frames, g_underruns;
static int      g_open;
static char     g_err[160];

int         audio_available(void) { return 1; }
const char *audio_error(void) { return g_err; }

void audio_stats(unsigned *frames, unsigned *underruns) {
    if (frames) *frames = g_frames;
    if (underruns) *underruns = g_underruns;
}

void audio_close(void) {
    int i;

    if (g_out) {
        waveOutReset(g_out);
        for (i = 0; i < BUFFERS; i++)
            if (g_hdr[i].dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(g_out, &g_hdr[i], sizeof(g_hdr[i]));
        waveOutClose(g_out);
        g_out = 0;
    }
    if (g_dec) avcodec_free_context(&g_dec);
    if (g_pkt) av_packet_free(&g_pkt);
    if (g_frame) av_frame_free(&g_frame);
    memset(g_hdr, 0, sizeof(g_hdr));
    g_next = 0;
    g_open = 0;
}

int audio_open(const unsigned char *asc, unsigned asc_len, unsigned rate, unsigned channels) {
    const AVCodec *codec;
    WAVEFORMATEX   fmt;
    int            rc, i;

    audio_close();
    g_err[0]    = 0;
    g_frames    = 0;
    g_underruns = 0;

    if (!asc || !asc_len) {
        snprintf(g_err, sizeof(g_err), "no decoder configuration");
        return -1;
    }
    /* The one rate, refused here rather than resampled: the console cannot,
       and a desktop that quietly could would hide a stream the console has
       no way to play. */
    if (rate != AUDIO_RATE) {
        snprintf(g_err, sizeof(g_err), "%u Hz, and the hardware takes %u", rate, AUDIO_RATE);
        return -1;
    }

    codec = avcodec_find_decoder(AV_CODEC_ID_AAC);
    g_dec = codec ? avcodec_alloc_context3(codec) : 0;
    if (!g_dec) {
        snprintf(g_err, sizeof(g_err), "no aac decoder in this build");
        return -1;
    }
    g_dec->extradata = (unsigned char *)av_mallocz(asc_len + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!g_dec->extradata) {
        snprintf(g_err, sizeof(g_err), "no room for the decoder configuration");
        audio_close();
        return -1;
    }
    memcpy(g_dec->extradata, asc, asc_len);
    g_dec->extradata_size = (int)asc_len;

    if ((rc = avcodec_open2(g_dec, codec, 0)) < 0) {
        char buf[64];

        av_strerror(rc, buf, sizeof(buf));
        snprintf(g_err, sizeof(g_err), "the decoder would not open: %s", buf);
        audio_close();
        return -1;
    }

    g_pkt   = av_packet_alloc();
    g_frame = av_frame_alloc();
    if (!g_pkt || !g_frame) {
        snprintf(g_err, sizeof(g_err), "no frame or packet");
        audio_close();
        return -1;
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.wFormatTag      = WAVE_FORMAT_PCM;
    fmt.nChannels       = AUDIO_CHANNELS;
    fmt.nSamplesPerSec  = AUDIO_RATE;
    fmt.wBitsPerSample  = 16;
    fmt.nBlockAlign     = (WORD)(fmt.nChannels * fmt.wBitsPerSample / 8);
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

    if (waveOutOpen(&g_out, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        snprintf(g_err, sizeof(g_err), "no sound device");
        audio_close();
        return -1;
    }
    for (i = 0; i < BUFFERS; i++) {
        g_hdr[i].lpData         = (LPSTR)g_pcm[i];
        g_hdr[i].dwBufferLength = 0;
        g_hdr[i].dwFlags        = WHDR_DONE; /* free */
    }
    (void)channels;
    g_open = 1;
    log_printf("audio: aac %u Hz stereo, %d buffers", rate, BUFFERS);
    return 0;
}

/* The queue is the pacing: waiting for the next buffer to come free is
   exactly as long as the sound already handed over lasts. */
static short *free_buffer(void) {
    int spins = 0;

    for (;;) {
        WAVEHDR *h = &g_hdr[g_next];

        if (!(h->dwFlags & WHDR_PREPARED) || (h->dwFlags & WHDR_DONE)) {
            if (h->dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(g_out, h, sizeof(*h));
            return g_pcm[g_next];
        }
        if (++spins > 2000) return 0; /* a second: the device has stopped */
        Sleep(1);
    }
}

int audio_play(const unsigned char *frame, unsigned len) {
    short   *dst;
    WAVEHDR *h;
    int      rc, got, n;

    if (!g_open || !frame || !len) return -1;

    g_pkt->data = (unsigned char *)frame;
    g_pkt->size = (int)len;
    rc          = avcodec_send_packet(g_dec, g_pkt);
    if (rc < 0 && rc != AVERROR(EAGAIN)) return -1;

    rc = avcodec_receive_frame(g_dec, g_frame);
    if (rc < 0) return 0; /* it wants more before it says anything */
    got = g_frame->nb_samples;
    if (got <= 0) return 0;
    if (got > FRAME_SAMPLES) got = FRAME_SAMPLES;

    dst = free_buffer();
    if (!dst) {
        g_underruns++;
        snprintf(g_err, sizeof(g_err), "the sound device stopped taking buffers");
        return -1;
    }

    for (n = 0; n < got; n++) {
        int ch;

        for (ch = 0; ch < AUDIO_CHANNELS; ch++) {
            int    plane = (g_frame->ch_layout.nb_channels > ch) ? ch : 0;
            float  v     = 0.0f;
            int    s;

            if (g_frame->format == AV_SAMPLE_FMT_FLTP)
                v = ((const float *)g_frame->extended_data[plane])[n];
            else if (g_frame->format == AV_SAMPLE_FMT_S16P)
                v = ((const short *)g_frame->extended_data[plane])[n] / 32768.0f;
            else if (g_frame->format == AV_SAMPLE_FMT_S16)
                v = ((const short *)g_frame->extended_data[0])[n * g_frame->ch_layout.nb_channels + plane] / 32768.0f;

            s = (int)(v * 32767.0f);
            if (s > 32767) s = 32767;
            if (s < -32768) s = -32768;
            dst[n * AUDIO_CHANNELS + ch] = (short)s;
        }
    }

    h                 = &g_hdr[g_next];
    h->lpData         = (LPSTR)dst;
    h->dwBufferLength = (DWORD)(got * AUDIO_CHANNELS * 2);
    h->dwFlags        = 0;
    if (waveOutPrepareHeader(g_out, h, sizeof(*h)) != MMSYSERR_NOERROR) return -1;
    if (waveOutWrite(g_out, h, sizeof(*h)) != MMSYSERR_NOERROR) return -1;

    g_next = (g_next + 1) % BUFFERS;
    g_frames++;
    return 0;
}

#endif
