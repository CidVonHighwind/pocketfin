#include "model/fmp4.h"

#include <stdio.h>
#include <string.h>

#define BOX(a, b, c, d) (((unsigned)(a) << 24) | ((unsigned)(b) << 16) | ((unsigned)(c) << 8) | (unsigned)(d))

#define B_MOOV BOX('m', 'o', 'o', 'v')
#define B_TRAK BOX('t', 'r', 'a', 'k')
#define B_MDIA BOX('m', 'd', 'i', 'a')
#define B_MINF BOX('m', 'i', 'n', 'f')
#define B_STBL BOX('s', 't', 'b', 'l')
#define B_STSD BOX('s', 't', 's', 'd')
#define B_AVC1 BOX('a', 'v', 'c', '1')
#define B_AVCC BOX('a', 'v', 'c', 'C')
#define B_TKHD BOX('t', 'k', 'h', 'd')
#define B_MDHD BOX('m', 'd', 'h', 'd')
#define B_MOOF BOX('m', 'o', 'o', 'f')
#define B_TRAF BOX('t', 'r', 'a', 'f')
#define B_TFHD BOX('t', 'f', 'h', 'd')
#define B_TFDT BOX('t', 'f', 'd', 't')
#define B_TRUN BOX('t', 'r', 'u', 'n')
#define B_MDAT BOX('m', 'd', 'a', 't')
#define B_MP4A BOX('m', 'p', '4', 'a')
#define B_ESDS BOX('e', 's', 'd', 's')

static unsigned rd32(const unsigned char *p) {
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | (unsigned)p[3];
}

static unsigned rd16(const unsigned char *p) { return ((unsigned)p[0] << 8) | (unsigned)p[1]; }

static unsigned long long rd64(const unsigned char *p) { return ((unsigned long long)rd32(p) << 32) | rd32(p + 4); }

static int fail(fmp4 *m, const char *why) {
    snprintf(m->err, sizeof(m->err), "%s", why);
    return -1;
}

/* 1 with the box at *pos taken, 0 at the end, -1 for one that does not fit
   (64-bit sizes included). */
static int next_box(const unsigned char *p, unsigned len, unsigned *pos, unsigned *type, const unsigned char **body, unsigned *blen) {
    unsigned sz;

    if (*pos + 8 > len) return 0;
    sz = rd32(p + *pos);
    if (sz < 8 || sz > len - *pos) return -1;
    *type = rd32(p + *pos + 4);
    *body = p + *pos + 8;
    *blen = sz - 8;
    *pos += sz;
    return 1;
}

static int take_avcc(fmp4_trak *t, const unsigned char *p, unsigned len) {
    unsigned pos, n, i, l;

    if (len < 7) return -1;
    t->nal_len_size = (unsigned char)((p[4] & 0x03) + 1);

    pos = 5;
    n   = p[pos++] & 0x1Fu; /* SPS count */
    if (n < 1 || pos + 2 > len) return -1;
    l = rd16(p + pos);
    pos += 2;
    if (pos + l > len || l > FMP4_PARAM_MAX) return -1;
    memcpy(t->sps, p + pos, l);
    t->sps_len = l;
    pos += l;
    /* One of each is all a decoder needs. */
    for (i = 1; i < n && pos + 2 <= len; i++) pos += 2 + rd16(p + pos);

    if (pos >= len) return -1;
    n = p[pos++]; /* PPS count */
    if (n < 1 || pos + 2 > len) return -1;
    l = rd16(p + pos);
    pos += 2;
    if (pos + l > len || l > FMP4_PARAM_MAX) return -1;
    memcpy(t->pps, p + pos, l);
    t->pps_len = l;
    return 0;
}

/* esds descriptors: a tag, a 7-bit-group length, a body. Tag 5 is the ASC;
   tags 3 and 4 above it are descended into. */
static int take_asc(fmp4_trak *t, const unsigned char *p, unsigned len) {
    unsigned pos = 4; /* version and flags */

    while (pos < len) {
        unsigned tag = p[pos++], sz = 0;
        int      k;

        for (k = 0; k < 4 && pos < len; k++) {
            unsigned char b = p[pos++];

            sz = (sz << 7) | (b & 0x7Fu);
            if (!(b & 0x80u)) break;
        }
        if (pos + sz > len) return -1;

        if (tag == 0x03) { /* ES_Descriptor */
            unsigned char flags;

            if (pos + 3 > len) return -1;
            flags = p[pos + 2];
            pos += 3;
            if (flags & 0x80u) pos += 2;
            if ((flags & 0x40u) && pos < len) pos += 1u + p[pos];
            if (flags & 0x20u) pos += 2;
            continue;
        }
        if (tag == 0x04) { /* DecoderConfigDescriptor */
            pos += 13;
            continue;
        }
        if (tag == 0x05) { /* DecoderSpecificInfo */
            if (!sz || sz > FMP4_PARAM_MAX) return -1;
            memcpy(t->asc, p + pos, sz);
            t->asc_len = sz;
            return 0;
        }
        pos += sz;
    }
    return -1;
}

/* Recursive, so it holds nothing large: the stream worker has 32 kB of stack. */
static void walk_trak(fmp4_trak *t, const unsigned char *p, unsigned len) {
    unsigned             pos = 0, type, blen;
    const unsigned char *body;

    while (next_box(p, len, &pos, &type, &body, &blen) > 0) {
        switch (type) {
        case B_MDIA:
        case B_MINF:
        case B_STBL: walk_trak(t, body, blen); break;

        case B_STSD: /* version, flags and an entry count come first */
            if (blen > 8) walk_trak(t, body + 8, blen - 8);
            break;

        /* A VisualSampleEntry is 8 shared bytes and 70 more before its
           extension boxes begin. */
        case B_AVC1:
            if (blen > 78) walk_trak(t, body + 78, blen - 78);
            break;

        case B_AVCC:
            if (take_avcc(t, body, blen) != 0) t->sps_len = 0;
            break;

        /* An AudioSampleEntry is 8 shared bytes and 20 more; the rate is
           16.16 fixed point. */
        case B_MP4A:
            if (blen >= 28) {
                t->channels = (unsigned char)((body[16] << 8) | body[17]);
                t->rate     = rd32(body + 24) >> 16;
                walk_trak(t, body + 28, blen - 28);
            }
            break;

        case B_ESDS: (void)take_asc(t, body, blen); break;

        case B_TKHD:
            if (blen >= 84) {
                t->id     = (body[0] == 1) ? rd32(body + 20) : rd32(body + 12);
                t->width  = rd32(body + blen - 8) >> 16;
                t->height = rd32(body + blen - 4) >> 16;
            }
            break;

        case B_MDHD:
            if (blen >= 24) t->timescale = (body[0] == 1) ? rd32(body + 20) : rd32(body + 12);
            break;

        default: break;
        }
    }
}

int fmp4_init(fmp4 *m, const unsigned char *data, unsigned len) {
    unsigned             pos = 0, type, blen;
    const unsigned char *body;

    if (!m || !data) return -1;
    memset(m, 0, sizeof(*m));
    while (next_box(data, len, &pos, &type, &body, &blen) > 0) {
        unsigned             at = 0, inner, tlen;
        const unsigned char *trak;

        if (type != B_MOOV) continue;
        /* Each trak into its own: walking the whole moov into one lets the
           audio track overwrite the video one. */
        while (next_box(body, blen, &at, &inner, &trak, &tlen) > 0) {
            fmp4_trak t;

            if (inner != B_TRAK) continue;
            memset(&t, 0, sizeof(t));
            walk_trak(&t, trak, tlen);
            if (t.asc_len && !m->audio.asc_len) {
                if (!t.rate) t.rate = t.timescale;
                m->audio = t;
            } else if (t.sps_len && !m->video.sps_len) {
                m->video = t;
            }
        }
    }

    if (!m->video.sps_len) return fail(m, "no video track in the initialisation segment");
    if (!m->video.timescale) return fail(m, "the video track has no timescale");
    return 0;
}

typedef struct {
    fmp4                *m;
    const unsigned char *mdat;
    unsigned             mdat_len;
    unsigned long long   dts[2]; /* by fmp4_track, each in its own timescale */
    fmp4_sample_cb       cb;
    void                *user;
    int                  emitted;
    int                  stop;
} frag;

/* The run's offset is from the start of the moof (default-base-is-moof, which
   every fragment from this server uses). */
static int emit_run(frag *f, const unsigned char *body, unsigned blen, unsigned moof_size, unsigned deflt_size, unsigned deflt_dur,
                    int track) {
    unsigned flags, n, q = 8, i, at = 0;

    if (blen < 8) return -1;
    if (track < 0) return 0;
    flags = rd32(body) & 0xFFFFFFu;
    n     = rd32(body + 4);

    if (flags & 0x000001u) { /* data-offset */
        unsigned off;

        if (q + 4 > blen) return -1;
        off = rd32(body + q);
        q += 4;
        /* The mdat's 8-byte header sits between the moof and its payload. */
        if (off < moof_size + 8u) return -1;
        at = off - (moof_size + 8u);
    }
    if (flags & 0x000004u) q += 4; /* first-sample flags */

    for (i = 0; i < n && !f->stop; i++) {
        unsigned    dur = deflt_dur, size = deflt_size;
        fmp4_sample s;

        if (flags & 0x000100u) { /* per-sample duration */
            if (q + 4 > blen) return -1;
            dur = rd32(body + q);
            q += 4;
        }
        if (flags & 0x000200u) { /* per-sample size */
            if (q + 4 > blen) return -1;
            size = rd32(body + q);
            q += 4;
        }
        if (flags & 0x000400u) q += 4; /* sample flags */
        if (flags & 0x000800u) q += 4; /* composition offset */

        if (!size || at > f->mdat_len || size > f->mdat_len - at) return -1;
        if (track == FMP4_VIDEO) {
            /* An offset read against the wrong base still lands inside the
               mdat with a plausible size; the first NAL's length is the cheap
               check that table and bytes agree. */
            if (f->m->video.nal_len_size == 4 && size >= 4 && rd32(f->mdat + at) > size - 4) return -1;
            /* Not the first: the opening sample of a fragment is padded to
               meet the sound (810 vs the usual 512), so a rate taken from it
               reads 15.8 fps on a 25 fps stream. */
            if (dur) f->m->sample_duration = dur;
        }

        s.track    = (fmp4_track)track;
        s.data     = f->mdat + at;
        s.len      = size;
        s.dts      = f->dts[track];
        s.duration = dur;
        if (f->cb && f->cb(&s, f->user)) f->stop = 1;
        f->emitted++;
        f->dts[track] += dur;
        at += size;
    }
    return 0;
}

static int walk_traf(frag *f, const unsigned char *p, unsigned len, unsigned moof_size) {
    unsigned             pos = 0, type, blen, deflt_size = 0, deflt_dur = 0;
    const unsigned char *body;
    int                  track = -1, r;

    while ((r = next_box(p, len, &pos, &type, &body, &blen)) > 0) {
        if (type == B_TFHD && blen >= 8) {
            const fmp4 *m     = f->m;
            unsigned    flags = rd32(body) & 0xFFFFFFu, id = rd32(body + 4), q = 8;

            track = id == m->video.id ? FMP4_VIDEO : (m->audio.asc_len && id == m->audio.id) ? FMP4_AUDIO : -1;
            /* Only the first 8 bytes are guaranteed. */
            if (flags & 0x000001u) q += 8; /* base-data-offset, unused: see emit_run */
            if (flags & 0x000002u) q += 4; /* sample-description index */
            if (q > blen) return -1;
            if ((flags & 0x000008u) && q + 4 <= blen) {
                deflt_dur = rd32(body + q);
                q += 4;
            }
            if ((flags & 0x000010u) && q + 4 <= blen) deflt_size = rd32(body + q);
        } else if (type == B_TFDT && track >= 0 && blen >= 8 && blen >= (body[0] == 1 ? 12u : 8u)) {
            /* The only thing that survives a discontinuity; durations alone
               drift. */
            f->dts[track] = (body[0] == 1) ? rd64(body + 4) : rd32(body + 4);
        } else if (type == B_TRUN) {
            if (emit_run(f, body, blen, moof_size, deflt_size, deflt_dur, track) != 0) return -1;
        }
    }
    return r;
}

int fmp4_fragment(fmp4 *m, const unsigned char *data, unsigned len, fmp4_sample_cb cb, void *user) {
    frag                 f;
    unsigned             pos = 0, type, blen, moof_len = 0;
    const unsigned char *body, *moof = 0;
    int                  r;

    if (!m || !data) return -1;
    if (!m->video.sps_len) return fail(m, "no initialisation segment yet");

    memset(&f, 0, sizeof(f));
    f.m    = m;
    f.cb   = cb;
    f.user = user;

    /* The mdat first: a trun's offsets point into bytes after the moof. */
    while ((r = next_box(data, len, &pos, &type, &body, &blen)) > 0) {
        if (type == B_MOOF) {
            moof     = body;
            moof_len = blen;
        } else if (type == B_MDAT) {
            f.mdat     = body;
            f.mdat_len = blen;
            if (moof && body == moof + moof_len + 8) break;
        }
    }
    if (r < 0) return fail(m, "a box runs past the end of the segment, or is 64-bit");
    if (!moof) return fail(m, "the fragment has no moof");
    if (!f.mdat) return fail(m, "the fragment has no mdat");

    pos = 0;
    while ((r = next_box(moof, moof_len, &pos, &type, &body, &blen)) > 0)
        if (type == B_TRAF && walk_traf(&f, body, blen, moof_len + 8) != 0) return fail(m, "the sample table does not fit its mdat");
    if (r < 0) return fail(m, "a box runs past the end of the moof");

    if (!f.emitted) return fail(m, "the fragment carried no samples");
    return f.emitted;
}
