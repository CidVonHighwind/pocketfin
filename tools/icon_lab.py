#!/usr/bin/env python3
"""Live editor for tools/icon.py.

    python tools/icon_lab.py

Drag a slider and the icon redraws with icon.py's own render(). Save (Ctrl+S)
writes assets/ICON0.PNG and puts the settings back into icon.py's PARAMS
block, so running the script afterwards makes the same icon.
"""

import json
import os
import re
import sys
import time
import tkinter as tk
from tkinter import colorchooser, messagebox, ttk

from PIL import Image, ImageTk

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import icon  # noqa: E402

ZOOM, PAD = 4, 16

SLIDERS = [
    ("Fin", [
        ("fin_width", "Width", 20, 90, 0.5),
        ("fin_aspect", "Height / width", 0.4, 1.4, 0.01),
        ("fin_dx", "Nudge sideways", -30, 30, 0.5),
        ("tip_u", "Tip lean", 0.4, 1.3, 0.005),
        ("lead_u", "Leading curve x", 0.0, 1.0, 0.005),
        ("lead_v", "Leading curve y", 0.0, 1.2, 0.005),
        ("trail_u", "Trailing curve x", 0.3, 1.4, 0.005),
        ("trail_v", "Trailing curve y", 0.0, 1.0, 0.005),
        ("gap", "Gap above water", 0, 10, 0.25),
    ]),
    ("Water", [
        ("waves", "Lines", 1, 3, 1),
        ("wave_width", "Width", 30, 140, 0.5),
        ("amp", "Swing", 0, 8, 0.1),
        ("cycles", "Waves across", 0.5, 6, 0.01),
        ("phase", "Phase", 0, 360, 1),
        ("thick", "Thickness", 1, 8, 0.25),
        ("spacing", "Line spacing", 4, 20, 0.2),
    ]),
    ("Placement", [
        ("dy", "Nudge up / down", -20, 20, 0.5),
        ("radius", "Corner radius", 0, 30, 0.5),
    ]),
]

COLOURS = [
    ("bg", "Background"),
    ("fin_base", "Fin at its base"),
    ("fin_tip", "Fin at its tip"),
    ("wave_left", "Water, left"),
    ("wave_right", "Water, right"),
]

BLOCK = re.compile(r"(# --- params: tools/icon_lab.py rewrites this block ---\r?\n)PARAMS = \{.*?\r?\n\}\r?\n(# --- end params ---)", re.S)


def write_params(p, path=os.path.join(HERE, "icon.py")):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    body = "PARAMS = " + json.dumps(p, indent=4) + "\n"
    new, n = BLOCK.subn(lambda m: m.group(1) + body + m.group(2), text)
    if n != 1:
        raise RuntimeError("the PARAMS block in tools/icon.py is not where the lab expects it")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(new)


def checker(w, h, a=(58, 64, 76), b=(72, 79, 93), size=12):
    img = Image.new("RGB", (w, h), a)
    tile = Image.new("RGB", (size, size), b)
    for y in range(0, h, size):
        for x in range((y // size) % 2 * size, w, 2 * size):
            img.paste(tile, (x, y))
    return img


class Lab(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Pocketfin icon lab")
        self.p = dict(icon.PARAMS)
        self.saved = dict(icon.PARAMS)
        self.scales, self.readouts, self.swatches = {}, {}, {}
        self.pending = None
        self.hq = tk.BooleanVar(value=False)
        self.back_big = checker(icon.W * ZOOM + 2 * PAD, icon.H * ZOOM + 2 * PAD)
        self.back_small = Image.new("RGB", (icon.W + 2 * PAD, icon.H + 2 * PAD), (46, 95, 163))
        self.build()
        self.redraw()
        self.bind_all("<Control-s>", lambda e: self.save())
        self.protocol("WM_DELETE_WINDOW", self.close)

    def build(self):
        left = ttk.Frame(self, padding=12)
        left.grid(row=0, column=0, sticky="n")
        self.big = ttk.Label(left)
        self.big.grid(row=0, column=0, columnspan=3, sticky="w")

        row = ttk.Frame(left)
        row.grid(row=1, column=0, columnspan=3, sticky="w", pady=(10, 0))
        self.small = ttk.Label(row)
        self.small.grid(row=0, column=0, rowspan=3, sticky="w")
        ttk.Label(row, text="Actual size, on the XMB's blue").grid(row=0, column=1, sticky="w", padx=12)
        ttk.Checkbutton(row, text="Preview at full quality (8×)", variable=self.hq,
                        command=self.schedule).grid(row=1, column=1, sticky="w", padx=12)
        self.status = ttk.Label(row, text="")
        self.status.grid(row=2, column=1, sticky="w", padx=12)

        buttons = ttk.Frame(left)
        buttons.grid(row=2, column=0, columnspan=3, sticky="w", pady=(12, 0))
        ttk.Button(buttons, text="Save  (Ctrl+S)", command=self.save).grid(row=0, column=0)
        ttk.Button(buttons, text="Back to last save", command=self.revert).grid(row=0, column=1, padx=8)

        colours = ttk.LabelFrame(left, text="Colours", padding=8)
        colours.grid(row=3, column=0, columnspan=3, sticky="ew", pady=(14, 0))
        for i, (key, label) in enumerate(COLOURS):
            ttk.Label(colours, text=label, width=16).grid(row=i, column=0, sticky="w")
            b = tk.Button(colours, width=10, relief="flat", command=lambda k=key, t=label: self.pick(k, t))
            b.grid(row=i, column=1, sticky="w", pady=2)
            self.swatches[key] = b
        self.paint_swatches()

        right = ttk.Frame(self, padding=12)
        right.grid(row=0, column=1, sticky="n")
        for g, (title, items) in enumerate(SLIDERS):
            frame = ttk.LabelFrame(right, text=title, padding=8)
            frame.grid(row=g, column=0, sticky="ew", pady=(0, 10))
            for r, (key, label, lo, hi, step) in enumerate(items):
                ttk.Label(frame, text=label, width=16).grid(row=r, column=0, sticky="w")
                s = tk.Scale(frame, from_=lo, to=hi, resolution=step, orient="horizontal", length=260,
                             showvalue=False, sliderlength=18, highlightthickness=0,
                             command=lambda v, k=key: self.changed(k, v))
                s.set(self.p[key])
                s.grid(row=r, column=1, sticky="ew")
                out = ttk.Label(frame, width=7, anchor="e")
                out.grid(row=r, column=2, sticky="e")
                self.scales[key], self.readouts[key] = s, out
                self.show_value(key)

    def show_value(self, key):
        v = self.p[key]
        self.readouts[key].configure(text=str(v) if key == "waves" else ("%.3f" % v).rstrip("0").rstrip("."))

    def paint_swatches(self):
        for key, b in self.swatches.items():
            c = self.p[key]
            r, g, bl = icon.rgb(c)
            b.configure(text=c, bg=c, activebackground=c, fg="#000" if (r * 299 + g * 587 + bl * 114) > 140000 else "#fff")

    def changed(self, key, value):
        self.p[key] = int(round(float(value))) if key == "waves" else round(float(value), 4)
        self.show_value(key)
        self.schedule()

    def pick(self, key, title):
        chosen = colorchooser.askcolor(self.p[key], title=title, parent=self)[1]
        if chosen:
            self.p[key] = chosen.lower()
            self.paint_swatches()
            self.schedule()

    def schedule(self):
        if self.pending:
            self.after_cancel(self.pending)
        self.pending = self.after(12, self.redraw)

    def redraw(self):
        self.pending = None
        ss = 8 if self.hq.get() else 4
        t0 = time.perf_counter()
        img = icon.render(self.p, ss)
        ms = (time.perf_counter() - t0) * 1000

        big = self.back_big.copy()
        z = img.resize((icon.W * ZOOM, icon.H * ZOOM), Image.NEAREST)
        big.paste(z, (PAD, PAD), z)
        self.big_tk = ImageTk.PhotoImage(big)
        self.big.configure(image=self.big_tk)

        small = self.back_small.copy()
        small.paste(img, (PAD, PAD), img)
        self.small_tk = ImageTk.PhotoImage(small)
        self.small.configure(image=self.small_tk)

        self.status.configure(text="%d× in %d ms%s" % (ss, ms, "  ·  unsaved" if self.p != self.saved else "  ·  saved"))

    def save(self):
        icon.render(self.p, 8).save(icon.OUT, optimize=True)
        write_params(self.p)
        self.saved = dict(self.p)
        self.redraw()
        self.status.configure(text="Saved assets/ICON0.PNG and tools/icon.py")

    def revert(self):
        self.p = dict(self.saved)
        for key, s in self.scales.items():
            s.set(self.p[key])
            self.show_value(key)
        self.paint_swatches()
        self.schedule()

    def close(self):
        if self.p != self.saved and not messagebox.askyesno("Unsaved changes", "Close without saving?", parent=self):
            return
        self.destroy()


if __name__ == "__main__":
    Lab().mainloop()
