#!/usr/bin/env python3
"""Generates data/avatars/default: the pack kikarinhas falls back on when no
other avatar is found (a new install), made of drawings of our own.

Five characters (cat, slime, robot, ghost, mushroom) in 32x32 cells, each
with idle, walk, sit, stand, jump and the custom dance, attack and hurt
animations and three colour palettes; hats, glasses and chairs to wear.

Everything is drawn here with flat colours (no antialiasing, so the
palettes can swap them exactly). Each body part is a shape that gets its
own outline, drawn back to front; the places for hats, glasses and chairs
come out of the same numbers that place the body, which is the trick that
keeps them glued to the head. Run from the repository root:

    python3 tools/gen_default_avatars.py [--preview out.png]
"""
import argparse
import json
import math
import os
from dataclasses import dataclass, replace

from PIL import Image

OUT = "data/avatars/default"
CELL = 32
COLS = 6
OUTLINE = (38, 28, 46)


# ---- tiny pixel toolkit -----------------------------------------------------

def E(cx, cy, rx, ry):
    """Filled ellipse, centre in pixel units (pixel centres at +0.5)."""
    out = set()
    for y in range(int(cy - ry) - 1, int(cy + ry) + 2):
        for x in range(int(cx - rx) - 1, int(cx + rx) + 2):
            if ((x + .5 - cx) / rx) ** 2 + ((y + .5 - cy) / ry) ** 2 <= 1:
                out.add((x, y))
    return out


def R(x0, y0, x1, y1):
    """Rectangle, both corners included."""
    return {(x, y) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)}


def POLY(pts):
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    out = set()
    for y in range(int(min(ys)), int(max(ys)) + 1):
        for x in range(int(min(xs)), int(max(xs)) + 1):
            px, py, inside, j = x + .5, y + .5, False, len(pts) - 1
            for i in range(len(pts)):
                xi, yi, xj, yj = pts[i][0], pts[i][1], pts[j][0], pts[j][1]
                if (yi > py) != (yj > py) and px < (xj - xi) * (py - yi) / (yj - yi) + xi:
                    inside = not inside
                j = i
            if inside:
                out.add((x, y))
    return out


def LINE(x0, y0, x1, y1, w=2):
    """Thick line: a w x w brush walked from one end to the other."""
    out = set()
    n = max(abs(x1 - x0), abs(y1 - y0), 1)
    for i in range(n + 1):
        x = round(x0 + (x1 - x0) * i / n)
        y = round(y0 + (y1 - y0) * i / n)
        out |= R(x, y, x + w - 1, y + w - 1)
    return out


class Cell:
    def __init__(self, w=CELL, h=CELL):
        self.p = {}
        self.w, self.h = w, h

    def put(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.p[(x, y)] = c

    def px(self, x, y, c):
        self.put(x, y, c)

    def shape(self, pixels, c, edge=True):
        """Fills pixels with colour index c and rings them with the outline
        (over parts already drawn too, so layers look separated)."""
        if edge:
            for (x, y) in pixels:
                for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                    if (nx, ny) not in pixels:
                        self.put(nx, ny, 0)
        for (x, y) in pixels:
            self.put(x, y, c)

    def paint(self, pixels, c):
        """Recolours pixels that are already drawn (shading, spots)."""
        for xy in pixels:
            if xy in self.p and self.p[xy] != 0:
                self.p[xy] = c


@dataclass
class Pose:
    dx: int = 0       # sideways, px
    lift: int = 0     # up, px (the sprite itself, on top of the engine's jump)
    sink: int = 0     # body lowered / squashed, px
    step: int = 0     # 0, 1 or 2: walk cycle phase (0 = standing)
    arm: int = 0      # 0 down, 1 up, 2 forward
    eyes: str = "o"   # o open, - closed, ^ happy, x hurt
    mouth: str = "s"  # s smile, o open, n none
    tuck: bool = False  # legs pulled up (jumping)
    wiggle: int = 0   # 0/1: tail, antenna... sway


def eyes_pixels(c, kind, x, y, col, shine=None, w=2, h=3):
    """One eye whose top-left corner is (x, y)."""
    if kind == "o":
        for j in range(h):
            for i in range(w):
                c.px(x + i, y + j, col)
        if shine is not None:
            c.px(x, y, shine)
    elif kind == "-":
        for i in range(w + 1):
            c.px(x + i - 1, y + h - 1, col)
    elif kind == "^":
        c.px(x - 1, y + h - 1, col)
        c.px(x, y + h - 2, col)
        c.px(x + 1, y + h - 2, col)
        c.px(x + 2, y + h - 1, col)
    elif kind == "x":
        c.px(x - 1, y, col)
        c.px(x + 1, y, col)
        c.px(x, y + 1, col)
        c.px(x - 1, y + 2, col)
        c.px(x + 1, y + 2, col)


# ---- characters --------------------------------------------------------------
# Each draw(c, pose) paints one cell and returns the anchors in pixel
# coordinates: "hat" (middle of the top of the head), "eyes" (middle between
# them). Colour indexes are those of the character's palette.

def cat(c, p):
    OUT, FUR, STRIPE, BELLY, PINK, WHITE = range(6)
    dx, s = p.dx, p.sink
    y = -p.lift + s  # everything above the feet moves with this
    # tail, behind everything
    sway = [0, 1][p.wiggle]
    tail = [(8, 26), (7, 25), (6, 24), (5, 22), (5, 20), (5 + sway, 18), (6 + sway, 16)]
    c.shape(set().union(*[R(x + dx, ty + y + s // 2, x + dx + 1, ty + y + s // 2 + 1)
                          for x, ty in tail]), FUR)
    c.paint(R(5 + sway + dx, 16 + y + s // 2, 8 + sway + dx, 17 + y + s // 2), STRIPE)
    # legs
    if p.tuck:
        legs = [R(11 + dx, 27 + y, 14 + dx, 29 + y), R(18 + dx, 27 + y, 21 + dx, 29 + y)]
    else:
        up = [(0, 0), (0, 2), (2, 0)][p.step]
        legs = [R(10 + dx, 28 + y - up[0] + s, 13 + dx, 31), R(18 + dx, 28 + y - up[1] + s, 21 + dx, 31)]
    for leg in legs:
        c.shape(leg, FUR)
    # body, belly
    body = E(15 + dx, 24.5 + y, 7.5, 5.5 - s * 0.35)
    c.shape(body, FUR)
    c.paint(E(17.5 + dx, 26 + y, 4.5, 3.2) & body, BELLY)
    # arm (a front paw lifted or pushed forward)
    if p.arm == 1:
        c.shape(LINE(21 + dx, 22 + y, 25 + dx, 17 + y, 3), FUR)
    elif p.arm == 2:
        c.shape(LINE(21 + dx, 23 + y, 28 + dx, 22 + y, 3), FUR)
    # head with ears
    hy = 13.5 + y
    c.shape(POLY([(9.5 + dx, hy - 4), (10 + dx, hy - 11), (15.5 + dx, hy - 6)]), FUR)
    c.shape(POLY([(19 + dx, hy - 6), (25 + dx, hy - 11.5), (25.5 + dx, hy - 4)]), FUR)
    head = E(17.5 + dx, hy, 9.5, 7.5)
    c.shape(head, FUR)
    c.paint(POLY([(11 + dx, hy - 6), (11.5 + dx, hy - 9), (14 + dx, hy - 6.5)]), PINK)
    c.paint(POLY([(21 + dx, hy - 6.5), (24 + dx, hy - 9.5), (24.5 + dx, hy - 6)]), PINK)
    for i, sx in enumerate((15, 18, 21)):
        c.paint(R(sx + dx, int(hy) - 7, sx + dx, int(hy) - 5 - (i == 1)), STRIPE)
    # face, looking right
    ey = int(hy) - 1
    eyes_pixels(c, p.eyes, 19 + dx, ey, OUT, WHITE)
    eyes_pixels(c, p.eyes, 24 + dx, ey, OUT, WHITE)
    c.px(25 + dx, ey + 4, PINK)
    c.px(26 + dx, ey + 4, PINK)
    if p.mouth == "s":
        c.px(24 + dx, ey + 6, OUT)
        c.px(25 + dx, ey + 7, OUT)
        c.px(26 + dx, ey + 6, OUT)
    elif p.mouth == "o":
        c.px(25 + dx, ey + 6, OUT)
        c.px(26 + dx, ey + 6, OUT)
        c.px(25 + dx, ey + 7, OUT)
        c.px(26 + dx, ey + 7, OUT)
    return {"hat": (17.5 + dx, hy - 7.5), "eyes": (22.5 + dx, ey + 1.5)}


def slime(c, p):
    OUT, BODY, SHADE, SHINE, WHITE, BLUSH = range(6)
    dx, s = p.dx, p.sink
    # No legs: it squashes and stretches. sink > 0 flattens, step hops.
    stretch = {0: 0, 1: -2, 2: 2}[p.step]
    ry = 10 - s * 0.55 + stretch * 0.5 + (1.5 if p.tuck else 0)
    rx = 10.5 + s * 0.6 - stretch * 0.4
    cy = 32 - ry - p.lift
    cx = 16 + dx
    # a little drop on top
    c.shape(E(cx - 3, cy - ry + 0.5, 3, 2.5), BODY)
    body = E(cx, cy, rx, ry)
    c.shape(body, BODY)
    c.paint(E(cx, cy + ry * 0.55, rx * 0.95, ry * 0.55) & body, SHADE)
    c.paint(E(cx - rx * 0.45, cy - ry * 0.45, 2.6, 1.6), SHINE)
    # arms: two little blobs
    if p.arm == 1:
        c.shape(E(cx + rx - 1, cy - ry * 0.7, 2.2, 3), BODY)
        c.shape(E(cx - rx + 1, cy - ry * 0.7, 2.2, 3), BODY)
    elif p.arm == 2:
        c.shape(E(cx + rx + 1, cy, 3.5, 2.2), BODY)
    ey = int(cy - ry * 0.25)
    eyes_pixels(c, p.eyes, int(cx) + 1, ey, OUT, WHITE, 2, 4)
    eyes_pixels(c, p.eyes, int(cx) + 7, ey, OUT, WHITE, 2, 4)
    c.px(int(cx) - 1, ey + 5, BLUSH)
    c.px(int(cx), ey + 5, BLUSH)
    c.px(int(cx) + 10, ey + 5, BLUSH)
    c.px(int(cx) + 11, ey + 5, BLUSH)
    if p.mouth == "s":
        for i, yy in enumerate((0, 1, 1, 0)):
            c.px(int(cx) + 3 + i, ey + 6 + yy, OUT)
    elif p.mouth == "o":
        c.shape(R(int(cx) + 4, ey + 6, int(cx) + 6, ey + 7), OUT, edge=False)
    return {"hat": (cx + 2, cy - ry + 0.5), "eyes": (cx + 5.5, ey + 2)}


def robot(c, p):
    OUT, METAL, DARK, SCREEN, LIGHT, ACCENT = range(6)
    dx, s = p.dx, p.sink
    y = -p.lift + s
    wig = p.wiggle
    # antenna
    c.shape(R(15 + dx + wig, 3 + y, 16 + dx + wig, 6 + y), DARK)
    c.shape(E(16 + dx + wig, 3 + y, 2.2, 2.2), ACCENT)
    # legs: a dark stem and a metal shoe, kept at least two rows tall
    for lx, up in ((11, [0, 0, 2][p.step]), (18, [0, 2, 0][p.step])):
        top = min(27 + y - up, 29)
        bottom = 29 + y if p.tuck else 31
        bottom = max(bottom, top + 1)
        c.shape(R(lx + dx, top, lx + 3 + dx, bottom), DARK)
        c.shape(R(lx - 1 + dx, max(top, bottom - 1), lx + 4 + dx, bottom), METAL)
    # arms
    if p.arm == 1:
        c.shape(LINE(22 + dx, 19 + y, 26 + dx, 12 + y, 3), METAL)
        c.shape(LINE(8 + dx, 19 + y, 5 + dx, 12 + y, 3), METAL)
    elif p.arm == 2:
        c.shape(LINE(22 + dx, 20 + y, 29 + dx, 20 + y, 3), METAL)
        c.shape(R(7 + dx, 18 + y, 9 + dx, 25 + y), METAL)
    else:
        c.shape(R(22 + dx, 18 + y, 24 + dx, 25 + y), METAL)
        c.shape(R(8 + dx, 18 + y, 10 + dx, 25 + y), METAL)
    # body with a panel
    body = R(10 + dx, 16 + y, 22 + dx, 27 + y)
    c.shape(body, METAL)
    c.paint(R(10 + dx, 24 + y, 22 + dx, 27 + y), DARK)
    c.shape(R(13 + dx, 18 + y, 19 + dx, 22 + y), DARK, edge=False)
    c.px(14 + dx, 19 + y, ACCENT)
    c.px(16 + dx, 19 + y, SCREEN)
    c.px(18 + dx, 19 + y, LIGHT)
    c.px(14 + dx, 21 + y, LIGHT)
    c.px(17 + dx, 21 + y, SCREEN)
    # head with a screen face
    head = R(8 + dx, 6 + y + s // 2, 24 + dx, 15 + y)
    c.shape(head, METAL)
    c.paint(R(8 + dx, 14 + y, 24 + dx, 15 + y), DARK)
    c.shape(R(10 + dx, 8 + y + s // 2, 23 + dx, 12 + y), SCREEN, edge=True)
    ey = 9 + y + s // 2
    eyes_pixels(c, p.eyes, 15 + dx, ey, LIGHT, None, 2, 3)
    eyes_pixels(c, p.eyes, 20 + dx, ey, LIGHT, None, 2, 3)
    if p.mouth == "o":
        c.px(17 + dx, ey + 3, LIGHT)
        c.px(18 + dx, ey + 3, LIGHT)
    c.px(9 + dx, 7 + y + s // 2, LIGHT)
    return {"hat": (16 + dx, 6 + y + s // 2), "eyes": (18 + dx, ey + 1)}


def ghost(c, p):
    OUT, BODY, SHADE, BLUSH, WHITE = range(5)
    dx, s = p.dx, p.sink
    bob = [0, 1, 0, 0][p.step] if p.step else 0
    y = -p.lift - 2 + s + bob
    cx = 16 + dx
    # ragged bottom: a wave, a bit different at each frame of the cycle
    shape = E(cx, 14.5 + y, 10.5, 10.5) | R(int(cx) - 10, 14 + y, int(cx) + 10, 26 + y)
    for x in range(int(cx) - 11, int(cx) + 11):
        phase = (x - int(cx) + 11 + p.wiggle * 3) // 3
        shape |= R(x, 27 + y, x, 28 + y + (2 if phase % 2 == 0 else 0))
    # arms
    if p.arm == 1:
        c.shape(LINE(int(cx) + 8, 16 + y, int(cx) + 12, 8 + y, 3), BODY)
        c.shape(LINE(int(cx) - 10, 16 + y, int(cx) - 13, 9 + y, 3), BODY)
    elif p.arm == 2:
        c.shape(LINE(int(cx) + 8, 17 + y, int(cx) + 15, 16 + y, 3), BODY)
    c.shape(shape, BODY)
    c.paint(shape & (E(cx + 6, 22 + y, 8, 12) | R(int(cx) - 11, 26 + y, int(cx) + 11, 31 + y)), SHADE)
    c.paint(E(cx - 4, 7 + y, 2.5, 1.5), WHITE)
    ey = 11 + y
    eyes_pixels(c, p.eyes, int(cx) + 1, ey, OUT, None, 2, 4)
    eyes_pixels(c, p.eyes, int(cx) + 7, ey, OUT, None, 2, 4)
    c.px(int(cx) - 1, ey + 5, BLUSH)
    c.px(int(cx), ey + 5, BLUSH)
    c.px(int(cx) + 10, ey + 5, BLUSH)
    c.px(int(cx) + 11, ey + 5, BLUSH)
    if p.mouth == "s":
        c.px(int(cx) + 4, ey + 6, OUT)
        c.px(int(cx) + 5, ey + 7, OUT)
        c.px(int(cx) + 6, ey + 7, OUT)
        c.px(int(cx) + 7, ey + 6, OUT)
    elif p.mouth == "o":
        c.shape(R(int(cx) + 4, ey + 6, int(cx) + 6, ey + 8), OUT, edge=False)
    return {"hat": (cx + 1, 4 + y), "eyes": (cx + 5.5, ey + 2)}


def mushroom(c, p):
    OUT, CAP, CAP_DARK, SPOT, STEM, STEM_DARK = range(6)
    dx, s = p.dx, p.sink
    y = -p.lift + s
    cx = 16 + dx
    # feet
    if p.tuck:
        feet = [E(cx - 4, 29.5 + y, 3.2, 1.8), E(cx + 5, 29.5 + y, 3.2, 1.8)]
    else:
        up = [(0, 0), (0, 2), (2, 0)][p.step]
        feet = [E(cx - 4, 30 - up[0], 3.5, 2), E(cx + 5, 30 - up[1], 3.5, 2)]
    for f in feet:
        c.shape(f, STEM_DARK)
    # arms
    if p.arm == 1:
        c.shape(LINE(int(cx) + 6, 21 + y, int(cx) + 10, 14 + y, 3), STEM)
        c.shape(LINE(int(cx) - 8, 21 + y, int(cx) - 10, 14 + y, 3), STEM)
    elif p.arm == 2:
        c.shape(LINE(int(cx) + 6, 23 + y, int(cx) + 13, 22 + y, 3), STEM)
    # stem/body
    body = E(cx, 24 + y, 7.5, 6.5 - s * 0.2) | R(int(cx) - 6, 18 + y, int(cx) + 6, 26 + y)
    c.shape(body, STEM)
    c.paint(body & E(cx + 4, 28 + y, 8, 4), STEM_DARK)
    # cap
    cap = E(cx, 15 + y + s // 2, 13.5, 10) & R(0, 0, 31, 17 + y + s // 2)
    c.shape(cap, CAP)
    c.paint(cap & R(0, 15 + y + s // 2, 31, 17 + y + s // 2), CAP_DARK)
    c.paint(cap & E(cx + 7, 18 + y, 9, 6), CAP_DARK)
    for sx, sy, sr in ((-7, 10, 2.6), (2, 7, 3), (9, 11, 2.2), (-1, 13, 1.6)):
        c.paint(E(cx + sx, sy + y + s // 2, sr, sr) & cap, SPOT)
    ey = 20 + y
    eyes_pixels(c, p.eyes, int(cx) + 1, ey, OUT, SPOT, 2, 3)
    eyes_pixels(c, p.eyes, int(cx) + 6, ey, OUT, SPOT, 2, 3)
    if p.mouth == "s":
        c.px(int(cx) + 3, ey + 4, OUT)
        c.px(int(cx) + 4, ey + 5, OUT)
        c.px(int(cx) + 5, ey + 4, OUT)
    elif p.mouth == "o":
        c.shape(R(int(cx) + 3, ey + 4, int(cx) + 4, ey + 5), OUT, edge=False)
    return {"hat": (cx + 1, 5.5 + y + s // 2), "eyes": (cx + 5, ey + 1.5)}


CHARACTERS = {
    "gato": dict(name="Gato", draw=cat, palettes=[
        ("laranja", [OUTLINE, (240, 147, 43), (200, 100, 26), (255, 232, 190), (255, 143, 163), (255, 255, 255)]),
        ("cinza", [OUTLINE, (154, 165, 177), (107, 118, 132), (232, 237, 242), (255, 143, 163), (255, 255, 255)]),
        ("preto", [OUTLINE, (84, 84, 104), (58, 58, 76), (150, 150, 170), (255, 143, 163), (255, 255, 255)]),
    ]),
    "slime": dict(name="Slime", draw=slime, palettes=[
        ("verde", [OUTLINE, (96, 208, 96), (52, 160, 80), (190, 245, 170), (255, 255, 255), (255, 150, 170)]),
        ("azul", [OUTLINE, (84, 160, 240), (48, 100, 200), (180, 220, 255), (255, 255, 255), (255, 150, 170)]),
        ("rosa", [OUTLINE, (240, 120, 180), (200, 70, 140), (255, 190, 225), (255, 255, 255), (255, 120, 140)]),
    ]),
    "robo": dict(name="Robô", draw=robot, palettes=[
        ("cinza", [OUTLINE, (176, 188, 200), (110, 124, 140), (60, 190, 200), (235, 250, 255), (240, 80, 80)]),
        ("vermelho", [OUTLINE, (224, 100, 90), (150, 56, 60), (255, 210, 90), (255, 245, 220), (90, 200, 255)]),
        ("verde", [OUTLINE, (120, 200, 130), (66, 130, 90), (40, 60, 70), (140, 255, 170), (255, 200, 60)]),
    ]),
    "fantasma": dict(name="Fantasma", draw=ghost, palettes=[
        ("branco", [OUTLINE, (246, 246, 255), (196, 200, 232), (255, 160, 180), (255, 255, 255)]),
        ("menta", [OUTLINE, (200, 250, 225), (140, 205, 180), (255, 170, 170), (255, 255, 255)]),
        ("lilas", [OUTLINE, (222, 200, 250), (170, 148, 215), (255, 150, 200), (255, 255, 255)]),
    ]),
    "cogumelo": dict(name="Cogumelo", draw=mushroom, palettes=[
        ("vermelho", [OUTLINE, (224, 60, 60), (160, 36, 56), (255, 255, 255), (255, 236, 205), (232, 196, 160)]),
        ("roxo", [OUTLINE, (150, 90, 210), (100, 56, 160), (255, 245, 200), (255, 236, 215), (225, 196, 178)]),
        ("azul", [OUTLINE, (70, 150, 230), (40, 96, 180), (255, 255, 255), (236, 244, 235), (190, 214, 204)]),
    ]),
}


# ---- animations ---------------------------------------------------------------

P = Pose
ANIMS = {
    # name: (row, fps, [poses])
    "idle": (0, 3, [P(), P(sink=1), P(), P(eyes="-")]),
    "walk": (1, 8, [P(step=1), P(), P(step=2), P()]),
    "sit": (2, 4, [P(sink=3), P(sink=5, eyes="-")]),
    "stand": (3, 6, [P(sink=2)]),
    "jump": (4, 8, [P(lift=2, tuck=True, arm=1, mouth="o")]),
    "dance": (5, 8, [P(dx=-1, arm=1, eyes="^", wiggle=0), P(lift=2, arm=1, eyes="^", wiggle=1),
                     P(dx=1, arm=1, eyes="^", wiggle=0), P(lift=2, arm=1, eyes="^", wiggle=1),
                     P(dx=-1, arm=1, eyes="^", wiggle=0), P(lift=2, arm=1, eyes="^", wiggle=1)]),
    "attack": (6, 9, [P(dx=0, arm=2, mouth="o"), P(dx=2, arm=2, mouth="o"), P(dx=4, arm=2, mouth="o")]),
    "hurt": (7, 6, [P(dx=-1, eyes="x", mouth="o"), P(dx=-2, sink=1, eyes="x", mouth="o")]),
}
CUSTOM = {"dance": "custom1", "attack": "custom2", "hurt": "custom3"}


def render(char):
    """The sheet of a character, its pivots per frame, and a preview-ready
    palette-0 image."""
    sheet = Image.new("RGBA", (COLS * CELL, len(ANIMS) * CELL), (0, 0, 0, 0))
    anchors = {}
    for name, (row, fps, poses) in ANIMS.items():
        for col, pose in enumerate(poses):
            c = Cell()
            anchors[(name, col)] = char["draw"](c, pose)
            pal = [tuple(v) for v in char["palettes"][0][1]]
            for (x, y), idx in c.p.items():
                sheet.putpixel((col * CELL + x, row * CELL + y), pal[idx] + (255,))
    return sheet, anchors


# ---- gear ---------------------------------------------------------------------
# Pieces are drawn with fixed colours (the SA ignores palettes on gear). The
# bottom middle of each image is what gets pinned to the pivot.

GOLD, GOLD_D, RED, BLUE, WHITE, PINK, PURPLE, PURPLE_D, YELLOW, GREY, GREY_D = (
    (250, 204, 60), (200, 140, 30), (230, 60, 70), (70, 140, 240), (255, 255, 255),
    (255, 140, 190), (120, 70, 190), (80, 44, 140), (255, 236, 120), (70, 76, 92), (40, 44, 56))


def piece(w, h, palette, fn):
    c = Cell(w, h)
    fn(c)
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    for (x, y), idx in c.p.items():
        img.putpixel((x, y), palette[idx] + (255,))
    return img


def crown(c):
    c.shape(R(1, 5, 14, 9) | POLY([(1, 5), (1, 1), (4.5, 4), (8, 0), (11.5, 4), (15, 1), (15, 5)]), 1)
    c.paint(R(1, 8, 14, 9), 2)
    c.px(4, 6, 3)
    c.px(8, 6, 4)
    c.px(11, 6, 3)
    c.px(8, 2, 3)


def party_hat(c):
    cone = POLY([(6, 1), (1, 15.5), (11, 15.5)])
    c.shape(cone, 1)
    for y in range(3, 15, 4):
        c.paint({(x, yy) for (x, yy) in cone if y <= yy < y + 2}, 2)
    c.shape(E(6, 1.5, 2.2, 2.2), 3)


def wizard_hat(c):
    c.shape(POLY([(11, 0), (6, 17), (16, 17)]) | POLY([(11, 0), (16, 3), (17, 8)]), 1)
    c.shape(E(11, 18, 10.5, 2.2), 1)
    c.paint(R(6, 14, 16, 15), 2)
    c.px(10, 9, 3)
    c.px(11, 8, 3)
    c.px(11, 10, 3)
    c.px(12, 9, 3)
    c.px(11, 9, 3)


def cap_hat(c):
    c.shape(E(8, 9, 8, 7) & R(0, 0, 19, 9), 1)
    c.shape(R(11, 8, 19, 9), 2)
    c.paint(R(5, 3, 7, 4), 3)
    c.px(8, 1, 2)


def glasses(c):
    c.shape(R(0, 0, 5, 3), 1)
    c.shape(R(7, 0, 12, 3), 1)
    c.px(6, 1, 1)
    c.px(1, 1, 2)
    c.px(2, 0, 2)
    c.px(8, 1, 2)
    c.px(9, 0, 2)


def armchair(c):
    c.shape(R(3, 0, 24, 22), 1)
    c.paint(R(5, 2, 22, 12), 2)
    c.shape(R(0, 12, 5, 22), 1)
    c.shape(R(22, 12, 27, 22), 1)
    c.paint(R(1, 13, 4, 14), 2)
    c.paint(R(23, 13, 26, 14), 2)
    c.paint(R(3, 20, 24, 22), 3)


def cushion(c):
    c.shape(E(13, 4, 13, 4) & R(0, 0, 25, 7), 1)
    c.px(13, 2, 2)


GEAR = {
    "hats": dict(z=3, pieces={
        "coroa": (16, 10, [(38, 28, 46), GOLD, GOLD_D, RED, BLUE], crown),
        "festa": (12, 16, [(38, 28, 46), PINK, YELLOW, BLUE], party_hat),
        "bruxo": (22, 20, [(38, 28, 46), PURPLE, PURPLE_D, YELLOW], wizard_hat),
        "bone": (20, 10, [(38, 28, 46), BLUE, (40, 96, 180), WHITE], cap_hat),
    }),
    "glasses": dict(z=4, pieces={
        "oculos": (13, 4, [(38, 28, 46), GREY_D, (120, 130, 160)], glasses),
    }),
    "chairs": dict(z=-1, pieces={
        "poltrona": (28, 23, [(38, 28, 46), PURPLE, PURPLE_D, (176, 120, 230)], armchair),
        "almofada": (26, 8, [(38, 28, 46), RED, (255, 170, 170)], cushion),
    }),
}


def rgbf(c):
    return {"r": round(c[0] / 255, 9), "g": round(c[1] / 255, 9),
            "b": round(c[2] / 255, 9), "a": 1.0}


def pivots(anchor, name):
    """Where each gear set goes on a frame (see the header of the file)."""
    hx, hy = anchor["hat"]
    ex, ey = anchor["eyes"]
    sitting = name == "sit"
    return {
        # the hat overlaps the top of the head by a row
        "hats": {"x": round(hx - CELL / 2, 1), "y": round(CELL - hy + 1, 1)},
        # the bottom of the glasses goes just under the eyes
        "glasses": {"x": round(ex - CELL / 2, 1), "y": round(CELL - ey - 2.5, 1)},
        # chairs only show while sitting
        "chairs": {"x": round(hx - 17.5 + 1, 1), "y": 0.0} if sitting else {"y": -1000.0},
    }


def build():
    for d in ("avatars",) + tuple("gear/" + g for g in GEAR):
        os.makedirs(os.path.join(OUT, d), exist_ok=True)
    avatars = {}
    for key, char in CHARACTERS.items():
        sheet, anchors = render(char)
        sheet.save(os.path.join(OUT, "avatars", key + ".png"))
        anim_json = {}
        for name, (row, fps, poses) in ANIMS.items():
            jkey = CUSTOM.get(name, name)
            frames = [{"gearPivot": pivots(anchors[(name, col)], name), "uniquePivot": {}}
                      for col in range(len(poses))]
            entry = {"animationName": jkey, "framesPerSecond": float(fps),
                     "frameData": frames, "returnsToIdle": True,
                     "targetInterrupt": True, "loopCount": 1, "targetDistance": 16}
            if name in CUSTOM:
                entry.update(customName=name, isCustomAnimation=True)
            if name == "dance":
                entry.update(animationLoops=True, loopCount=3)
            if name == "attack":
                entry.update(targetsUser=True, targetDistance=24)
            anim_json[jkey] = entry
        pals = char["palettes"]
        avatars[key] = {
            "name": char["name"], "width": CELL, "height": CELL,
            "pixelsPerUnit": 1.0, "bilinearFilter": False, "moveSpeed": 1.0,
            "CanUseGear": list(GEAR),
            "mainPalette": {"colors": [rgbf(c) for c in pals[0][1]]},
            "swappablePalettes": {n: {"colors": [rgbf(c) for c in cols]} for n, cols in pals[1:]},
            "animationData": anim_json,
        }
    gear = {}
    for gset, spec in GEAR.items():
        pieces = {}
        for pname, (w, h, pal, fn) in spec["pieces"].items():
            piece(w, h, pal, fn).save(os.path.join(OUT, "gear", gset, pname + ".png"))
            pieces[pname] = {"pieceName": pname, "PPU": 1.0, "width": float(w),
                             "height": float(h), "flipsWithAvatarX": gset != "chairs"}
        gear[gset] = {"globalZIndex": spec["z"], "gearPiece": pieces}
    with open(os.path.join(OUT, "streamavatars_json.txt"), "w", encoding="utf-8") as f:
        json.dump({"avatarData": avatars, "gear": gear}, f, indent=1, ensure_ascii=False)
        f.write("\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", help="write a contact sheet here and stop")
    args = ap.parse_args()
    if args.preview:
        sheets = [render(ch)[0] for ch in CHARACTERS.values()]
        w, h = sheets[0].size
        out = Image.new("RGBA", (w * len(sheets), h), (90, 110, 130, 255))
        for i, s in enumerate(sheets):
            out.alpha_composite(s, (i * w, 0))
        out.resize((out.width * 3, out.height * 3), Image.NEAREST).save(args.preview)
        return
    build()


main()
