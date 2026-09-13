/* What both renderers will and will not draw, decided in one place so neither
 * can accept what the other refuses. */

#include "port/gfx.h"

#include <stddef.h>

int gfx_blit_can(const void *src, int w, int h) {
    if (!src || w <= 0 || h <= 0) return 0;
    if (w > GFX_BLIT_MAX || h > GFX_BLIT_MAX) return 0;
    /* The engine encodes texture size as a log2. */
    if (w & (w - 1)) return 0;
    if (h & (h - 1)) return 0;
    /* Its texture base wants a 16-byte boundary. */
    if ((unsigned long)(size_t)src & 15u) return 0;
    return 1;
}
