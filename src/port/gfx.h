/* The panel. One file owns a frame from list-open to flip. */
#ifndef PORT_GFX_H
#define PORT_GFX_H

#define GFX_W      480
#define GFX_H      272
#define GFX_STRIDE 512 /* the console wants a power of two */

/* 5-6-5, red in the low bits. The one place this is written: open-coded
 * copies got the channels backwards and the picture came back blue. */
#define GFX_PACK565(r, g, b) ((unsigned short)((((unsigned)(b) & 0xF8u) << 8) | (((unsigned)(g) & 0xFCu) << 3) | ((unsigned)(r) >> 3)))

int  gfx_start(void);
void gfx_stop(void);
int  gfx_up(void);

/* Every fill must be inside a frame: one issued outside goes nowhere and says
 * nothing. */
void gfx_frame_begin(void);
/* Between these every primitive lands in `px`, 8888 at `stride`, not the
 * panel: while a film runs the decoder owns the panel, so this is how the
 * band over it reaches the screen. No clear and no swap. */
void gfx_frame_begin_on(void *px, int stride);
void gfx_frame_end_on(void);

void gfx_fill(int x, int y, int w, int h, unsigned rgb);

/* `r` is clamped to CORNER_MAX_R and to half the smaller side; r < 1 is a
 * plain fill. All four corners come off one baked mask, so both machines cut
 * the same curve. */
void gfx_round_fill(int x, int y, int w, int h, int r, unsigned rgb);

void gfx_round_frame(int x, int y, int w, int h, int r, unsigned rgb);

/* A rectangular gradient inset over a rounded fill looks like exactly that,
 * which is why this is one call. */
void gfx_round_hgrad(int x, int y, int w, int h, int r, unsigned left, unsigned right);

/* A bar filling part of a track is capped at the end it has reached and
 * square where it carries on, or it looks like a short pill in a groove. */
#define GFX_CAP_LEFT  1
#define GFX_CAP_RIGHT 2
#define GFX_CAP_BOTH  (GFX_CAP_LEFT | GFX_CAP_RIGHT)
void gfx_round_hgrad_caps(int x, int y, int w, int h, int r, unsigned left, unsigned right, int caps);

void gfx_round_hgrad_frame(int x, int y, int w, int h, int r, unsigned left, unsigned right);

void gfx_hgrad(int x, int y, int w, int h, unsigned left, unsigned right);
void gfx_vgrad(int x, int y, int w, int h, unsigned top, unsigned bottom);

/* `alpha` 0-255. The UI antialiases its arcs and arrows a pixel at a time
 * through this, so it does no work per call beyond the blend. */
void gfx_blend_rect(int x, int y, int w, int h, unsigned rgb, int alpha);

void gfx_scissor(int x, int y, int w, int h);
void gfx_scissor_none(void);

/* 5-6-5 straight to the panel; 1 when it drew. `src` is read as a texture
 * when the list runs: powers of two, 16-byte aligned, outliving the frame.
 * gfx_blit_can is the whole rule. */
#define GFX_BLIT_MAX 512
int gfx_blit(int x, int y, const unsigned short *src, int w, int h);

/* Only the texture must satisfy gfx_blit_can: a poster fits to 85x128,
 * neither a power of two, so it is staged into one that is. */
int gfx_blit_part(int x, int y, const unsigned short *src, int tex_w, int tex_h, int w, int h, int r, unsigned bg);

/* A picture and its frame composited once: alpha from the rounded shape,
 * colour mixed from the picture and the edge ramp by border coverage. A blit
 * then gfx_round_frame blends twice and the picture fringes through the ring,
 * worst on a dark frame over bright artwork.
 *
 * The PC ignores `bg` and composites over what is there; the console cannot
 * read the frame buffer mid-draw and paints the corner back in it. */
int gfx_blit_card(int x, int y, const unsigned short *src, int tex_w, int tex_h, int w, int h, int r, unsigned edge_from, unsigned edge_to,
                  unsigned bg);

/* Portable, so neither machine can accept what the other refuses. */
int gfx_blit_can(const void *src, int w, int h);

/* After the CPU writes pixels the drawing engine will read. */
void gfx_wrote_pixels(void);

/* Through the engine, not the CPU: half a megabyte a frame, and on the
 * console the pointers are uncached aliases the engine cannot use as given.
 * Starts its own display list when no frame is open. */
void gfx_copy8888(const void *src, int src_stride, int sx, int sy, void *dst, int dst_stride, int dx, int dy, int w, int h);

/* After a film took the panel at 8888 with its own buffer, or every page
 * drawn goes to the wrong depth in a buffer nothing is showing. */
void gfx_panel_restore(void);

/* Texture supplies alpha, vertex supplies colour. Through here because the
 * console caches what the engine holds; the PC does nothing. */
void gfx_use_mask_texture(void);

int gfx_in_frame(void);

/* Finish the open list and leave NOTHING open, without swapping, for a caller
 * with a display list of its own: a firmware dialog started inside ours leaves
 * each thread waiting on the other with the panel never drawn. What it draws
 * lands in the back buffer the following gfx_frame_end shows. */
void gfx_list_suspend(void);

/* Both waits are unbounded, the one place that is right: io/http.h's bound is
 * for a peer that may never answer, and the panel always does. Paces the loop
 * whether or not a frame was opened. */
void gfx_frame_end(void);

/* On by default: gfx_frame_end() is the loop's only pacing. The checks turn
 * it off -- waiting 16.7 ms a frame on a panel nobody watches was over half
 * of a 34 s suite. Off, elapsed real time across frames does not hold. */
void gfx_pace(int on);

/* The panel's own counter, the only thing that can say the picture is really
 * coming back. */
unsigned gfx_vcount(void);

int gfx_wait_frame(unsigned bound_us);

/* GFX_W pixels; 0 when it was read. Draw the frame twice first: the swap
 * latches at the next vertical blank. */
int gfx_capture_row(int y, unsigned short *dst);

/* A film's frame is composed at 8888, and reading it through the 5-6-5 seam
 * would quantise it. A browser frame comes back widened. */
int gfx_capture_row8888(int y, unsigned *dst);

#endif
