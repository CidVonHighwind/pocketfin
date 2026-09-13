#!/usr/bin/env python3
"""Turns assets/font.bin into what the drawing code links against.

    python3 tools/font_bake.py assets/font.bin build/gen

Writes two generated, gitignored files:

    font_atlas.c     the packed glyph atlas
    font_data.c      the glyph records and the faces

Run at parse time by both builds. Rebaking the blob itself from .ttf files is
a different job and needs the font toolchain.

Only the FIRST family is emitted. Choosing between families is a settings
question and there are no settings yet; the blob keeps the rest.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from font_pack import unpack

QUOTE = chr(39)
TAGS = ["SMALL", "BODY", "TITLE"]

# Must match TEXT_ATLAS_W/H in text.h; the assertion at the end of font_data.c
# breaks the build if they ever drift.
ATLAS_W = 512
ATLAS_H = 128
ATLAS_PAD = 1


def pack(faces):
    """Every face into one atlas, and where each glyph landed.

    Tallest first, which is what makes it fit: in the order the blob lists
    them the same glyphs need 134 rows, and 95 this way.
    """
    atlas = bytearray(ATLAS_W * ATLAS_H)
    slots = [[(0, 0)] * len(f["glyphs"]) for f in faces]

    boxes = []
    for fi, f in enumerate(faces):
        for gi, (off, w, h, _bx, _by, _adv4) in enumerate(f["glyphs"]):
            if not w or not h:
                continue
            if off + w * h > len(f["coverage"]):
                sys.exit("%s: a glyph runs off the coverage blob" % f["name"])
            boxes.append((h, w, fi, gi, off))
    boxes.sort(key=lambda b: -b[0])

    pen_x, pen_y, row_h = ATLAS_PAD, ATLAS_PAD, 0
    for h, w, fi, gi, off in boxes:
        if pen_x + w + ATLAS_PAD > ATLAS_W:
            pen_x, pen_y, row_h = ATLAS_PAD, pen_y + row_h + ATLAS_PAD, 0
        if pen_y + h + ATLAS_PAD > ATLAS_H:
            sys.exit("the faces do not fit a %dx%d atlas" % (ATLAS_W, ATLAS_H))
        cov = faces[fi]["coverage"]
        for y in range(h):
            src = off + y * w
            dst = (pen_y + y) * ATLAS_W + pen_x
            atlas[dst:dst + w] = cov[src:src + w]
        slots[fi][gi] = (pen_x, pen_y)
        pen_x += w + ATLAS_PAD
        row_h = max(row_h, h)

    return bytes(atlas), slots, pen_y + row_h + ATLAS_PAD


HEAD = ("/* Generated from assets/font.bin by tools/font_bake.py during the\n"
        " * build -- do not edit, do not commit.\n"
        " *\n"
        " * Roboto (Apache 2.0), 8-bit coverage. The advance is in QUARTER\n"
        " * pixels; see text.h.\n"
        " */\n"
        '#include "port/text.h"\n'
        '#include "base/align.h"\n\n')


def emit_atlas(atlas, used, write):
    write(HEAD)
    write("/* %d of %d rows hold glyphs; the rest is what a power-of-two\n"
          " * texture costs. */\n" % (used, ATLAS_H))
    write("POCKETFIN_ALIGN16 const unsigned char\n"
          "text_atlas_start[TEXT_ATLAS_W * TEXT_ATLAS_H] = {")
    for i, b in enumerate(atlas):
        if i % 24 == 0:
            write("\n    ")
        write("%d," % b)
    write("\n};\n")


def emit_data(faces, slots, write):
    write(HEAD)

    for tag, f, here in zip(TAGS, faces, slots):
        write("/* %s %d px, ascent %d, line box %d */\n"
              % (f["name"], f["pixels"], f["ascent"], f["height"]))
        write("static const text_glyph rec_%s[%d] = {\n" % (tag, len(f["glyphs"])))
        for code, (off, w, h, bx, by, adv4) in enumerate(f["glyphs"], f["first"]):
            ax, ay = here[code - f["first"]]
            ch = chr(code)
            # Printable ASCII names itself; anything else gets its NUMBER.
            # Writing the character would put a non-ASCII byte into a
            # generated C file and leave both compilers guessing at the
            # source encoding.
            if 32 < code < 127 and ch not in (QUOTE, chr(92)):
                label = QUOTE + ch + QUOTE
            else:
                label = "U+%04X" % code
            write("    { %3d, %3d, %2d, %2d, %3d, %3d, %3d },  /* %s */\n"
                  % (ax, ay, w, h, bx, by, adv4, label))
        write("};\n\n")

    write("const text_face text_faces[TEXT_FACE_N] = {\n")
    for tag, f in zip(TAGS, faces):
        write("    { rec_%s, %d, %d, %d, %d },\n"
              % (tag, f["ascent"], f["height"], f["cap_top"], f["cap_h"]))
    write("};\n\n")

    # text.h names the range both renderers index by. A blob that covers a
    # different one is the fault that made accented titles into question
    # marks on one machine and not the other.
    first, last = faces[0]["first"], faces[0]["last"]
    for f in faces[1:]:
        if f["first"] != first or f["last"] != last:
            sys.exit("faces disagree on their character range")
    write("/* Breaks the build if text.h and this file disagree on the range of\n"
          " * characters, or on the atlas these records index into. */\n")
    write("typedef char text_range_check[\n")
    write("    (TEXT_FIRST == %d && TEXT_LAST == %d) ? 1 : -1];\n"
          % (first, last))
    write("typedef char text_atlas_check[\n")
    write("    (TEXT_ATLAS_W == %d && TEXT_ATLAS_H == %d) ? 1 : -1];\n"
          % (ATLAS_W, ATLAS_H))


def put(path, data):
    """Written only when it CHANGED, or every build rebuilds everything."""
    if os.path.exists(path):
        with open(path, "rb") as fh:
            if fh.read() == data:
                return
    with open(path, "wb") as fh:
        fh.write(data)


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: font_bake.py <font.bin> <out-dir>")
    blob_path, out_dir = sys.argv[1], sys.argv[2]

    with open(blob_path, "rb") as fh:
        families = unpack(fh.read())
    faces = families[0]["faces"]
    if len(faces) != len(TAGS):
        sys.exit("the blob has %d faces a family, this build wants %d"
                 % (len(faces), len(TAGS)))

    atlas, slots, used = pack(faces)

    if not os.path.isdir(out_dir):
        os.makedirs(out_dir)

    chunks = []
    emit_data(faces, slots, chunks.append)
    put(os.path.join(out_dir, "font_data.c"), "".join(chunks).encode("utf8"))

    chunks = []
    emit_atlas(atlas, used, chunks.append)
    put(os.path.join(out_dir, "font_atlas.c"), "".join(chunks).encode("utf8"))


main()
