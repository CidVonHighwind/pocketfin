"""A binary PPM to a PNG, with no image library: zlib and a CRC are enough."""
import binascii
import struct
import sys
import zlib


def read_ppm(path):
    with open(path, "rb") as f:
        blob = f.read()
    fields, at = [], 0
    while len(fields) < 4:
        while at < len(blob) and blob[at : at + 1].isspace():
            at += 1
        start = at
        while at < len(blob) and not blob[at : at + 1].isspace():
            at += 1
        fields.append(blob[start:at])
    at += 1
    if fields[0] != b"P6":
        raise SystemExit("%s is not a binary PPM" % path)
    w, h = int(fields[1]), int(fields[2])
    if len(blob) - at != w * h * 3:
        raise SystemExit(
            "%s holds %d pixel bytes, %dx%d needs %d"
            % (path, len(blob) - at, w, h, w * h * 3)
        )
    return w, h, blob[at:]


def chunk(tag, body):
    return (
        struct.pack(">I", len(body))
        + tag
        + body
        + struct.pack(">I", binascii.crc32(tag + body) & 0xFFFFFFFF)
    )


def main():
    src, dst = sys.argv[1], sys.argv[2]
    w, h, rgb = read_ppm(src)
    raw = b"".join(b"\x00" + rgb[y * w * 3 : (y + 1) * w * 3] for y in range(h))
    with open(dst, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


main()
