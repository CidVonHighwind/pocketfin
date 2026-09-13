#!/usr/bin/env python3
"""Generates assets/ICON0.PNG, the 144x80 icon the XMB shows for the EBOOT.

    python tools/icon.py          write the icon from PARAMS below
    python tools/icon_lab.py      the same drawing, live, with sliders

A fin standing clear of the water. Drawn at eight times size and downsampled:
every edge is a curve, and at 144x80 unantialiased curves read as a mistake.
"""

import math
import os
from types import SimpleNamespace

from PIL import Image, ImageChops, ImageDraw

W, H = 144, 80
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "ICON0.PNG")

# The fin's outline lives in its own box: u runs from the leading base (0) to
# the trailing base (1), v from the base (0) to the tip (1). The group -- fin,
# gap and water -- is centred in the card, so size never moves it off-centre.
#
# --- params: tools/icon_lab.py rewrites this block ---
PARAMS = {
    "bg": "#0b0f1c",
    "radius": 8.0,
    "fin_base": "#29cfff",
    "fin_tip": "#8a55f5",
    "wave_left": "#2ccbff",
    "wave_right": "#6c6cff",
    "fin_width": 46.0,
    "fin_aspect": 0.75,
    "fin_dx": 0.0,
    "tip_u": 0.885,
    "lead_u": 0.28,
    "lead_v": 0.86,
    "trail_u": 0.885,
    "trail_v": 0.51,
    "gap": 3.0,
    "waves": 1,
    "wave_width": 94.0,
    "amp": 4.1,
    "cycles": 2.65,
    "phase": 156.0,
    "thick": 4.8,
    "spacing": 18.0,
    "dy": 1.5
}
# --- end params ---


def rgb(hex_colour):
    h = hex_colour.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def layout(p):
    n = int(p["waves"])
    fin_h = p["fin_width"] * p["fin_aspect"]
    group = fin_h + p["gap"] + p["thick"] + p["amp"] + (n - 1) * p["spacing"]
    top = H / 2.0 + p["dy"] - group / 2.0
    wave_c = top + fin_h + p["gap"] + p["thick"] / 2.0
    base_l = W / 2.0 + p["fin_dx"] - p["fin_width"] / 2.0
    return SimpleNamespace(
        n=n, fin_h=fin_h, wave_c=wave_c,
        y_ref=wave_c - p["thick"] / 2.0 - p["gap"],
        base_l=base_l, base_r=base_l + p["fin_width"],
        x0=W / 2.0 - p["wave_width"] / 2.0, x1=W / 2.0 + p["wave_width"] / 2.0)


def wave_y(p, L, x, row=0):
    t = (x - L.x0) / ((L.x1 - L.x0) or 1.0)
    return L.wave_c + row * p["spacing"] + p["amp"] * math.sin(2.0 * math.pi * p["cycles"] * t + math.radians(p["phase"]))


def bezier(p0, c, p1, steps=64):
    out = []
    for i in range(steps + 1):
        t = i / float(steps)
        u = 1.0 - t
        out.append((u * u * p0[0] + 2 * u * t * c[0] + t * t * p1[0],
                    u * u * p0[1] + 2 * u * t * c[1] + t * t * p1[1]))
    return out


# Gradients as images Pillow combines, not per-pixel Python: a sum of a
# horizontal and a vertical ramp is a diagonal, and a lookup table colours it.
def ramp_x(cw, ch, ss, x0, x1):
    span = (x1 - x0) or 1.0
    row = bytes(int(round(255 * min(1.0, max(0.0, ((x + 0.5) / ss - x0) / span)))) for x in range(cw))
    return Image.frombytes("L", (cw, 1), row).resize((cw, ch), Image.NEAREST)


def ramp_y(cw, ch, ss, y_bottom, y_top):
    span = (y_bottom - y_top) or 1.0
    col = bytes(int(round(255 * min(1.0, max(0.0, (y_bottom - (y + 0.5) / ss) / span)))) for y in range(ch))
    return Image.frombytes("L", (1, ch), col).resize((cw, ch), Image.NEAREST)


def colourise(t, c0, c1):
    return Image.merge("RGB", [t.point([int(round(c0[i] + (c1[i] - c0[i]) * v / 255.0)) for v in range(256)])
                               for i in range(3)])


def render(p, ss=8):
    L = layout(p)
    cw, ch = W * ss, H * ss
    out = Image.new("RGB", (cw, ch), rgb(p["bg"]))

    def box(u, v):
        return (L.base_l + u * p["fin_width"], L.y_ref - v * L.fin_h)

    def fin_bottom(x):
        return wave_y(p, L, x) - p["thick"] / 2.0 - p["gap"]

    base_l, base_r = (L.base_l, fin_bottom(L.base_l)), (L.base_r, fin_bottom(L.base_r))
    tip = box(p["tip_u"], 1.0)
    outline = (bezier(base_l, box(p["lead_u"], p["lead_v"]), tip)
               + bezier(tip, box(p["trail_u"], p["trail_v"]), base_r))
    for k in range(1, 64):
        x = L.base_r + (L.base_l - L.base_r) * k / 64.0
        outline.append((x, fin_bottom(x)))
    fin = Image.new("L", (cw, ch), 0)
    ImageDraw.Draw(fin).polygon([(x * ss, y * ss) for x, y in outline], fill=255)
    t = ImageChops.add(ramp_x(cw, ch, ss, min(L.base_l, tip[0]), max(L.base_r, tip[0])),
                       ramp_y(cw, ch, ss, L.y_ref, tip[1]), scale=2.0)
    out = Image.composite(colourise(t, rgb(p["fin_base"]), rgb(p["fin_tip"])), out, fin)

    # Filled as one outline -- the centre line pushed half the thickness out
    # along its normal on each side -- not stroked: a wide ImageDraw.line is
    # short rectangles stitched at round joints, and the seams came out as
    # steps with the stroke thinning and thickening along its length.
    water = Image.new("L", (cw, ch), 0)
    wd = ImageDraw.Draw(water)
    r = p["thick"] / 2.0
    span = (L.x1 - L.x0) or 1.0
    k_wave = 2.0 * math.pi * p["cycles"] / span
    for row in range(L.n):
        above, below = [], []
        for k in range(321):
            x = L.x0 + span * k / 320.0
            y = wave_y(p, L, x, row)
            slope = p["amp"] * k_wave * math.cos(k_wave * (x - L.x0) + math.radians(p["phase"]))
            n = math.hypot(slope, 1.0)
            nx, ny = -slope / n * r, 1.0 / n * r
            above.append(((x - nx) * ss, (y - ny) * ss))
            below.append(((x + nx) * ss, (y + ny) * ss))
        wd.polygon(above + below[::-1], fill=255)
        for x in (L.x0, L.x1):
            cx, cy = x * ss, wave_y(p, L, x, row) * ss
            wd.ellipse([cx - r * ss, cy - r * ss, cx + r * ss, cy + r * ss], fill=255)
    t = ramp_x(cw, ch, ss, L.x0, L.x1)
    out = Image.composite(colourise(t, rgb(p["wave_left"]), rgb(p["wave_right"])), out, water)

    card = Image.new("L", (cw, ch), 0)
    ImageDraw.Draw(card).rounded_rectangle([0, 0, cw - 1, ch - 1], radius=p["radius"] * ss, fill=255)
    out.putalpha(card)

    # An area average, not Lanczos: Lanczos rings at a hard edge, and beside a
    # bright stroke it undershot to a dark halo round the water and under the
    # fin. Downsampled WITH its alpha: flattened to RGB, the rounded corners
    # come out filled with the background, four dark notches on the XMB.
    return out.resize((W, H), Image.BOX)


def main():
    render(PARAMS).save(OUT, optimize=True)
    print("wrote %s %dx%d" % (os.path.relpath(OUT, ROOT), W, H))


if __name__ == "__main__":
    main()
