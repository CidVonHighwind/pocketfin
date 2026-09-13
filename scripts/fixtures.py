#!/usr/bin/env python3
"""Real artwork from the server, for the checks to decode.

    python scripts/fixtures.py

Read-only: one auth POST, then GETs. Nothing is written to the library.
Credentials come from run/jellyfin.txt and are never printed; an empty
password is tried first, which is what this server wants.

A spread of sizes on purpose -- the decoder's rules are about size, and a
fixture set that is all 128x192 checks one of them.
"""
import json, re, sys, urllib.request, urllib.parse, pathlib

root = pathlib.Path(__file__).resolve().parent.parent

# The same lines jf_conn_parse() reads: `name value`, `name: value`, # comments.
cfg = {}
for line in (root / "run" / "jellyfin.txt").read_text().splitlines():
    line = line.split("#", 1)[0].strip()
    if line:
        m = re.match(r"([^:=\s]+)[:=\s]*(.*)", line)
        cfg[m.group(1).lower()] = m.group(2).strip()
base = "http://%s:%s" % (cfg["host"], cfg.get("port", "8096"))
AUTH = ('MediaBrowser Client="Pocketfin", Device="PC", '
        'DeviceId="pocketfin-fixture", Version="0.1"')

def call(path, data=None, token=None, raw=False):
    req = urllib.request.Request(base + path, data=data)
    req.add_header("X-Emby-Authorization",
                   AUTH + (', Token="%s"' % token if token else ""))
    if data is not None:
        req.add_header("Content-Type", "application/json")
    with urllib.request.urlopen(req, timeout=20) as r:
        return r.read() if raw else json.loads(r.read())

token = uid = None
for pw in ("", cfg.get("password", cfg.get("pass", ""))):
    try:
        a = call("/Users/AuthenticateByName",
                 json.dumps({"Username": cfg.get("user", cfg.get("username", "")), "Pw": pw}).encode())
        token, uid = a["AccessToken"], a["User"]["Id"]
        print("authenticated as %s" % a["User"]["Name"])
        break
    except Exception:
        continue
if not token:
    sys.exit("the server refused both passwords")

q = urllib.parse.urlencode({"IncludeItemTypes": "Movie,Series",
                            "Recursive": "true", "Limit": "60",
                            "SortBy": "SortName"})
items = [i for i in call("/Users/%s/Items?%s" % (uid, q), token=token)["Items"]
         if i.get("ImageTags", {}).get("Primary")]
if len(items) < 4:
    sys.exit("only %d items with artwork" % len(items))

# The sizes that matter: what the app asks for, either side of it, the panel
# itself, powers of two, and two that are neither.
WANT = [(128, 192), (64, 96), (100, 150), (256, 384), (91, 137),
        (200, 300), (480, 272), (128, 128), (32, 48), (300, 200)]

# TWO ROOTS, because the machines do not share one. usbhostfs is started in
# run/, so host0:/ IS run/ to the console; the desktop's hostfs seam is
# run/pocketfin-link/. hostfs_slurp() is the only way a check reads a file, so
# a fixture written to one root is staged for one machine -- which is how
# markers.jpg and probe/ came to pass on the PC and skip on the console.
ROOTS = [root / "run", root / "run" / "pocketfin-link"]


def stage(rel, data):
    """One fixture into every root, making its directory."""
    for where in ROOTS:
        f = where / rel
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_bytes(data)


def clear(rel_dir):
    for where in ROOTS:
        d = where / rel_dir
        if d.is_dir():
            for old_file in d.glob("*.jpg"):
                old_file.unlink()


clear("fixtures")

for n, (w, h) in enumerate(WANT):
    item = items[n % len(items)]
    img = call("/Items/%s/Images/Primary?%s" % (
        item["Id"], urllib.parse.urlencode(
            {"maxWidth": w, "maxHeight": h, "format": "jpg", "quality": 90})),
        token=token, raw=True)
    name = "%02d-%dx%d.jpg" % (n, w, h)
    stage("fixtures/" + name, img)
    ok = img[:2] == b"\xff\xd8" and img[-2:] == b"\xff\xd9"
    print("%-14s %6d bytes  %s  %s" %
          (name, len(img), "whole" if ok else "TRUNCATED", item["Name"][:34]))

# The card page draws ONE poster at five radii, so the radii are what differs
# and not the artwork. Asked for AT THE SIZE IT IS DRAWN, which is what the
# app does and what makes the decoder's resize the identity.
CARD = "Akira"
clear("cards")

pick = [i for i in items if i["Name"].lower().startswith(CARD.lower())]
if not pick:
    pick = items
img = call("/Items/%s/Images/Primary?%s" % (
    pick[0]["Id"], urllib.parse.urlencode(
        {"maxWidth": 72, "maxHeight": 108, "format": "jpg", "quality": 90})),
    token=token, raw=True)
stage("cards/card.jpg", img)
print("cards/card.jpg  %6d bytes  %s" % (len(img), pick[0]["Name"]))

# THE SIZES THE DECODER ARGUES WITH. None is a multiple of 16, so each is
# created at a padded size and written at that stride -- which is the rule
# "which sizes the engine takes" exists to map. Aligned artwork cannot tell.
PROBE = [(100, 150), (96, 144), (100, 150), (96, 144),
         (100, 150), (107, 160), (100, 150), (98, 147)]

clear("probe")

for n, (w, h) in enumerate(PROBE):
    item = items[n % len(items)]
    img = call("/Items/%s/Images/Primary?%s" % (
        item["Id"], urllib.parse.urlencode(
            {"maxWidth": w, "maxHeight": h, "format": "jpg", "quality": 90})),
        token=token, raw=True)
    stage("probe/p%d.jpg" % n, img)
print("probe/p0..p%d.jpg  %d sizes" % (len(PROBE) - 1, len(PROBE)))

first = sorted((ROOTS[0] / "fixtures").glob("*.jpg"))[0]
stage("poster.jpg", first.read_bytes())
print("poster.jpg <- %s" % first.name)

# DRAWN, not photographed: "a real poster decodes" reads two pixels back and
# no real poster has a known colour at a known place. 100x150 on purpose --
# neither is a multiple of 16, so the decoder is created at 112x160 and writes
# its rows at that stride, and an aligned fixture cannot catch a shrink that
# reads at the image's own width.
#
# 4:2:0, which is what the console's MJPEG unit takes: written 4:4:4 to keep
# the blocks crisp, sceJpegDecodeMJpeg refused it with 0x80650016 while every
# 4:2:0 image off the server at the same size decoded. The markers are 36 px
# square and read 16 px in, so half-resolution chroma costs them nothing.
try:
    from PIL import Image, ImageDraw
except ImportError:
    print("markers.jpg  SKIPPED -- no PIL; 'a real poster decodes' stays skipped")
else:
    im = Image.new("RGB", (100, 150), (90, 90, 90))
    d = ImageDraw.Draw(im)
    d.rectangle([4, 4, 40, 40], fill=(230, 20, 20))
    d.rectangle([60, 110, 96, 146], fill=(20, 220, 20))
    import io
    buf = io.BytesIO()
    im.save(buf, "JPEG", quality=95, subsampling=2)
    stage("markers.jpg", buf.getvalue())
    print("markers.jpg   %6d bytes  drawn, 100x150" % len(buf.getvalue()))

import subprocess

# THE SHAPE JELLYFIN SENDS, for tests/model/test_fmp4.c: ftyp + moov with empty
# sample tables, then moof + mdat fragments. default_base_moof is the flag that
# makes a trun's data offset relative to its own moof, which is what the parser
# assumes and what the server produces.
try:
    for where in ROOTS:
        subprocess.run(["ffmpeg", "-y", "-v", "error",
                        "-f", "lavfi", "-i", "color=c=green:s=320x240:d=2:r=25",
                        # AND SOUND, because the demuxer has to find the audio
                        # track beside the video one and a silent fixture
                        # proves half the parser. 44.1 kHz stereo AAC-LC, which
                        # is what the device profile asks the server for and
                        # the only rate the console's output channel takes.
                        "-f", "lavfi", "-i", "sine=frequency=440:duration=2",
                        # BASELINE, and the size of something a console would
                        # really be sent: the Media Engine refuses High profile
                        # outright, which is why the device profile asks the
                        # server for this -- a fixture the desktop decodes
                        # happily and the console will not is a fixture that
                        # tests one machine.
                        "-c:v", "libx264", "-profile:v", "baseline", "-level", "3.0",
                        "-pix_fmt", "yuv420p", "-g", "25",
                        "-c:a", "aac", "-profile:a", "aac_low", "-ar", "44100", "-ac", "2", "-b:a", "128k",
                        # THE RATE ON THE OUTPUT, not just the source: with an
                        # audio input beside it ffmpeg retimes the video and
                        # the track comes out at 15.8 fps, which makes a
                        # fixture whose frame rate is an accident.
                        "-r", "25", "-t", "2",
                        "-movflags", "+frag_keyframe+empty_moov+default_base_moof",
                        "-f", "mp4", str(where / "frag.mp4")], check=True)
    print("frag.mp4       a fragmented mp4, for the demuxer")
except (OSError, subprocess.CalledProcessError):
    print("frag.mp4     SKIPPED -- no ffmpeg on PATH; the fmp4 checks stay skipped")
