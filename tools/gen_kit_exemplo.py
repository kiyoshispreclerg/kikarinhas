#!/usr/bin/env python3
"""Generates docs/avatares/kit-exemplo: a tiny but complete avatar kit.

Everything is drawn here with flat colours (no antialiasing, so palettes
work), and the pivots are computed from the same numbers that place the
body, which is the trick for keeping hats glued to the head.
Run from the repository root:  python3 tools/gen_kit_exemplo.py
"""
import json
import os
from PIL import Image

OUT = "docs/avatares/kit-exemplo"
CELL = 16
RED, DARK, WHITE = (224, 61, 35), (30, 30, 40), (255, 255, 255)

# name, row, frames: dx, lift, body height
ANIMS = {
    "idle":   (0, [(0, 0, 10), (0, 0, 9)], 3),
    "walk":   (1, [(0, 0, 10), (0, 1, 10), (0, 0, 10), (0, 1, 10)], 9),
    "sit":    (2, [(0, 0, 8), (0, 0, 7)], 4),
    "stand":  (3, [(0, 0, 9)], 9),
    "jump":   (4, [(0, 2, 10)], 9),
    "dance":  (5, [(-1, 0, 10), (0, 2, 10), (1, 0, 10), (0, 2, 10)], 8),
    "attack": (6, [(0, 0, 10), (2, 0, 10), (4, 0, 10)], 8),
    "hurt":   (7, [(-1, 0, 9), (-2, 0, 8)], 6),
}
CUSTOM = {"dance": "custom1", "attack": "custom2", "hurt": "custom3"}


def rgb(c):
    return {"r": round(c[0] / 255, 9), "g": round(c[1] / 255, 9),
            "b": round(c[2] / 255, 9), "a": 1.0}


def draw_body(img, col, row, dx, lift, h):
    """Rounded-ish block with eyes; feet on the last row of the cell."""
    x0, x1 = 4 + dx, 12 + dx
    y1 = CELL - 1 - lift
    y0 = y1 - h + 1
    px = img.load()
    ox, oy = col * CELL, row * CELL
    for y in range(y0, y1 + 1):
        for x in range(x0, x1):
            edge = x in (x0, x1 - 1) or y in (y0, y1)
            corner = x in (x0, x1 - 1) and y in (y0, y1)
            if corner:
                continue
            px[ox + x, oy + y] = DARK + (255,) if edge else RED + (255,)
    for ex in (x1 - 4, x1 - 2):  # looks to the right
        px[ox + ex, oy + y0 + 2] = WHITE + (255,)


def solid(w, h, color):
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px = img.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = color + (255,)
    return img


def main():
    for d in ("avatars", "gear/hats", "gear/chairs"):
        os.makedirs(os.path.join(OUT, d), exist_ok=True)

    sheet = Image.new("RGBA", (4 * CELL, len(ANIMS) * CELL), (0, 0, 0, 0))
    anim_json = {}
    for name, (row, frames, fps) in ANIMS.items():
        key = CUSTOM.get(name, name)
        frame_data = []
        for col, (dx, lift, h) in enumerate(frames):
            draw_body(sheet, col, row, dx, lift, h)
            sitting = name == "sit"
            frame_data.append({
                "gearPivot": {
                    # the hat sits on top of the body: pivot y is the height
                    # of the head in avatar pixels, x follows the body
                    "hats": {"x": float(dx), "y": float(lift + h)},
                    # chairs only show while sitting
                    "chairs": {"x": float(dx), "y": 0.0} if sitting
                              else {"y": -1000.0},
                },
                "uniquePivot": {},
            })
        entry = {"animationName": key, "framesPerSecond": float(fps),
                 "frameData": frame_data, "returnsToIdle": True,
                 "targetInterrupt": True, "loopCount": 1, "targetDistance": 16}
        if name in CUSTOM:
            entry.update(customName=name, isCustomAnimation=True)
        if name == "dance":
            entry.update(animationLoops=True, loopCount=3)
        if name == "attack":
            entry.update(targetsUser=True, targetDistance=24)
        anim_json[key] = entry
    sheet.save(os.path.join(OUT, "avatars/bloco.png"))

    # hat: 8x4, centred, resting on the bottom edge of its cell
    hat = Image.new("RGBA", (16, 8), (0, 0, 0, 0))
    hat.paste(solid(8, 4, (250, 200, 40)), (4, 4))
    hat.save(os.path.join(OUT, "gear/hats/chapeu.png"))
    # chair: 14x6 behind the avatar
    solid(14, 6, (120, 120, 130)).save(os.path.join(OUT, "gear/chairs/banco.png"))

    data = {
        "avatarData": {
            "bloco": {
                "name": "Bloco", "width": CELL, "height": CELL,
                "pixelsPerUnit": 1.0, "bilinearFilter": False,
                "moveSpeed": 1.0, "CanUseGear": ["hats", "chairs"],
                "mainPalette": {"colors": [rgb(RED), rgb(DARK), rgb(WHITE)]},
                "swappablePalettes": {
                    "azul":  {"colors": [rgb((40, 100, 220)), rgb(DARK), rgb(WHITE)]},
                    "verde": {"colors": [rgb((60, 170, 70)), rgb(DARK), rgb(WHITE)]},
                },
                "animationData": anim_json,
            }
        },
        "gear": {
            "hats": {"globalZIndex": 3, "gearPiece": {
                "chapeu": {"pieceName": "chapeu", "PPU": 1.0, "width": 16.0,
                           "height": 8.0, "flipsWithAvatarX": True}}},
            "chairs": {"globalZIndex": -1, "gearPiece": {
                "banco": {"pieceName": "banco", "PPU": 1.0, "width": 14.0,
                          "height": 6.0, "flipsWithAvatarX": False}}},
        },
    }
    with open(os.path.join(OUT, "streamavatars_json.txt"), "w", encoding="utf-8") as f:
        json.dump(data, f, indent=1, ensure_ascii=False)
        f.write("\n")


main()
