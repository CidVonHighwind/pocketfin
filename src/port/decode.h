/* The only part of playback the two machines do differently: the desktop
 * decodes with libavcodec, the console with the Media Engine.
 *
 * The film stays 8888 from decoder to panel: 5-6-5 bands a decoded gradient,
 * and the Media Engine writes 8888 anyway. So while a film runs the decoder
 * owns the panel, and the browser's 5-6-5 path comes back the moment it
 * stops.
 *
 * One stream at a time, so there is no handle. */
#ifndef PORT_DECODE_H
#define PORT_DECODE_H

/* The pixels stay in the port and reach the panel through decode_blit(). */
typedef struct {
    int w, h;
} decode_picture;

int decode_available(void);

/* `nal_len_size` is the length prefix on each NAL: an fMP4 sample is
 * length-prefixed, not Annex-B, and the wrong size reads the first length as
 * picture data.
 *
 * `src_w`/`src_h` come from the initialisation segment. The panel is always
 * the target: the console cannot scale and is handed a transcode that
 * already fits. 0 on success; decode_error() says why not. */
int decode_open(const unsigned char *sps, unsigned sps_len, const unsigned char *pps, unsigned pps_len, unsigned nal_len_size, int src_w,
                int src_h);

/*   1  a picture came out, and `out` says how big it is
 *   0  the decoder took it and wants more -- normal, not an error
 *  -1  it failed, and decode_error() says so
 *
 * No pacing: whether a picture is due is the stream's question. */
int decode_sample(const unsigned char *data, unsigned len, decode_picture *out);

/* The pixel is 0xAABBGGRR, red in the low byte -- the console's GU_PSM_8888
 * and its display format, which the desktop matches. */
#define DECODE_PACK8888(r, g, b) (0xFF000000u | ((unsigned)(b) << 16) | ((unsigned)(g) << 8) | (unsigned)(r))

/* Before there is a codec, which cannot open until the init segment arrives:
 * the surfaces sit where the browser's buffers do, and taking the panel later
 * changes the surface halfway through opening, visibly. */
int decode_take_panel(void);

/* NULL when no film is open, and then the browser's own frame is used. */
void *decode_surface(int *stride);

/* 8888 to 8888: a film is never quantised on its way to the screen. */
void decode_blit(int x, int y);

void decode_show(void);

void        decode_close(void);
const char *decode_error(void);

#endif
