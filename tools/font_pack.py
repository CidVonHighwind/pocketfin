#!/usr/bin/env python3
"""The font blob format: assets/font.bin, read by tools/font_bake.py.

The C tables are generated from the blob into build/ at build time and never
committed. cap_top and cap_h are measured from each face's 'H' when the blob is
baked, so a face at a different size cannot centre its labels a pixel out.

Layout, little-endian throughout:

    magic       8   "JFFONT2" NUL
    families    2   number of families
    per_family  2   faces in each (3: SM, MD, LG)
    then per family:
        label      24   what the settings screen calls it, NUL padded
        then per face:
            name       32   the source .ttf, NUL padded, for the comment
            pixels      2   the size it was rasterised at
            ascent      2   cell top to baseline
            height      2   the full line box
            first       2   first character code
            last        2   last character code
            cap_top     1   line-box top down to the top of a capital
            cap_h       1   height of a capital
            cov_len     4   bytes of coverage that follow the records
            records     8 * (last - first + 1)
            coverage    cov_len

Each record is: off u16, w u8, h u8, bx i8, by i8, adv4 u8, and one pad byte
so the next record starts aligned. port/text.h says what they mean."""

import struct

MAGIC = b"JFFONT2\0"
REC = struct.Struct("<HBBbbBx")
FACE_HDR = struct.Struct("<32sHHHHHBBI")
FAM_HDR = struct.Struct("<24s")


def pack(families, per_family=3):
    """families: list of dicts with label and faces, where each face has
    name, pixels, ascent, height, first, last, cap_top, cap_h,
    glyphs (list of (off, w, h, bx, by, adv4)) and coverage (bytes)."""
    out = bytearray(MAGIC)
    out += struct.pack("<HH", len(families), per_family)
    for fam in families:
        if len(fam["faces"]) != per_family:
            raise ValueError("family %s has %d faces, wanted %d"
                             % (fam["label"], len(fam["faces"]), per_family))
        out += FAM_HDR.pack(fam["label"].encode("ascii")[:24])
        for f in fam["faces"]:
            out += FACE_HDR.pack(f["name"].encode("ascii")[:32], f["pixels"],
                                 f["ascent"], f["height"], f["first"],
                                 f["last"], f["cap_top"], f["cap_h"],
                                 len(f["coverage"]))
            for g in f["glyphs"]:
                out += REC.pack(*g)
            out += f["coverage"]
    return bytes(out)


def unpack(blob):
    if blob[:8] != MAGIC:
        raise ValueError("not a font blob: bad magic (wanted %r)" % MAGIC)
    n, per = struct.unpack_from("<HH", blob, 8)
    at = 12
    families = []
    for _ in range(n):
        label, = FAM_HDR.unpack_from(blob, at)
        at += FAM_HDR.size
        faces = []
        for _ in range(per):
            (name, pixels, ascent, height, first, last, cap_top, cap_h,
             cov_len) = FACE_HDR.unpack_from(blob, at)
            at += FACE_HDR.size
            glyphs = []
            for _ in range(last - first + 1):
                glyphs.append(REC.unpack_from(blob, at))
                at += REC.size
            coverage = blob[at:at + cov_len]
            at += cov_len
            faces.append({
                "name": name.rstrip(b"\0").decode("ascii"),
                "pixels": pixels, "ascent": ascent, "height": height,
                "first": first, "last": last,
                "cap_top": cap_top, "cap_h": cap_h,
                "glyphs": glyphs, "coverage": coverage,
            })
        families.append({"label": label.rstrip(b"\0").decode("ascii"),
                         "faces": faces})
    return families
