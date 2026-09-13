/* Hardware H.264 through sceMpegGetAvcNalAu: MP4 samples as they are, with
 * SPS/PPS from avcC. The access unit is bound with sceMpegInitAu to a buffer
 * inside the "ddrtop" working block.
 *
 * The block is port/mem.h's MEM_DECODER, handed to video_decoder_open(). No
 * memalign() fallback: that fails once browsing has carved up the heap.
 *
 * sceJpeg and sceMpeg are the same silicon: whatever is decoding posters is
 * closed before sceMpegCreate. Audio opens before video, and only then may
 * audio decode -- the other order gives video with no chroma. */
#ifndef PORT_PSP_VIDEO_DECODER_H
#define PORT_PSP_VIDEO_DECODER_H

#include <stdint.h>

#include <pspmpeg.h>

#define VIDEO_STRIDE 512

/* Rows a destination must hold: the decoder writes whole macroblocks, and
 * 272-row buffers failed every access unit with 0x80628001. Headroom, NOT a
 * decodable size: a 288-row coded picture failed even with 320-row buffers,
 * so the engine stops at 272 coded rows -- see the profile in jelly/api.c. */
#define VIDEO_HEIGHT 288

/* sceMpegAvcDecode's destination is one slot per picture an access unit
 * yields: a one-element array let the firmware write into the stack. */
#define VIDEO_MAX_IMAGES 4

/* The access-unit buffer is 1 MB in: at 64 KB in, a keyframe wrote past it
 * into memory the decoder was also using. Must fit MEM_DECODER;
 * video_decoder_open() checks. */
#define VIDEO_DDRTOP_SIZE  0x400000u
#define VIDEO_DDRTOP_ALIGN 0x400000u
#define VIDEO_AU_OFFSET    0x100000u

typedef struct {
    void   *sps_buffer;
    int32_t sps_size;
    void   *pps_buffer;
    int32_t pps_size;
    int32_t nal_prefix_size;
    void   *nal_buffer;
    int32_t nal_size;
    int32_t mode;
} video_decoder_nal;

/* Not in pspsdk; imported by sceMpegNal.S. */
SceInt32 sceMpegGetAvcNalAu(SceMpeg *Mpeg, video_decoder_nal *pNal, SceMpegAu *pAu);

typedef struct {
    int did_init;
    int did_create;

    SceMpeg           mpeg;
    SceMpegRingbuffer ring;

    int32_t sps_size;
    int32_t pps_size;
    int32_t nal_prefix_size;

    char err[96];
} video_decoder;

/* 1 has hours of playback behind it. */
#define VIDEO_MODE_SD 1

#define VIDEO_FRAME_FIRST 3
#define VIDEO_FRAME_NEXT  0

/* 0 on success; see v->err. The block stays the caller's. */
int video_decoder_open(video_decoder *v, const uint8_t *sps, uint16_t sps_len, const uint8_t *pps, uint16_t pps_len,
                       uint8_t nal_prefix_size, void *block, uint32_t block_len);

/* out_frames is VIDEO_MAX_IMAGES 64-byte aligned
 * VIDEO_STRIDE * VIDEO_HEIGHT * 4 buffers, written back from the cache before
 * the call. An IDR reliably yields three pictures; only distinct buffers keep
 * them all. Returns the pictures written (0 is legitimate), or -1. */
int video_decoder_decode(video_decoder *v, const void *sample, uint32_t size, void **out_frames, int frame_mode);

void video_decoder_close(video_decoder *v);

#endif
