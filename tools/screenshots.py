#!/usr/bin/env python3
"""Takes the README screenshots into docs/screenshots.

Runs build/kikarinhas on a Xephyr screen with an empty HOME and no Stream
Avatars, so the only avatars on it are the default pack that comes with the
program, over a pixel-art backdrop drawn below. The cast is set up with
chat commands through the control socket (!avatar, !color, !gear, !move),
then it says a few lines and captures:

    chat.png      avatars with speech bubbles and emotes flying (emote wall)
    danmaku.png   the same chat as danmaku

Usage (from the repository root, after `make`):
    python3 tools/screenshots.py [--display :78] [--keep]

Needs Xephyr and python3-pil. The people and lines are made up. With --keep
the program is left running on the last scene, for a look.
"""
import argparse
import math
import os
import random
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time

from PIL import Image, ImageDraw, ImageGrab

W, H = 1280, 720
OUT = "docs/screenshots"


# ---- the backdrop: a sunset over hills, drawn at 1/4 size and enlarged -------

def backdrop(path):
    w, h = W // 4, H // 4
    img = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(img)
    sky = [(58, 60, 130), (72, 70, 146), (92, 80, 160), (118, 90, 170), (148, 100, 175),
           (180, 110, 170), (208, 124, 160), (232, 142, 150), (248, 166, 148),
           (255, 190, 150), (255, 210, 160), (255, 226, 180)]
    band = 126 / len(sky)
    for i, c in enumerate(sky):
        d.rectangle([0, int(i * band), w, h if i == len(sky) - 1 else int((i + 1) * band)], fill=c)
    rng = random.Random(4)
    for _ in range(40):
        d.point((rng.randrange(w), rng.randrange(0, 34)), fill=(255, 240, 220))
    # sun and its halo
    d.ellipse([232 - 19, 112 - 19, 232 + 19, 112 + 19], fill=(255, 205, 150))
    d.ellipse([232 - 13, 112 - 13, 232 + 13, 112 + 13], fill=(255, 238, 176))
    # clouds: a light body and a shaded belly
    for cx, cy, s in ((58, 36, 1.0), (150, 22, 0.8), (268, 50, 1.1), (110, 66, 0.7)):
        for dx, dy, rx, ry in ((0, 0, 16, 5), (-11, 2, 10, 4), (12, 2, 11, 4), (2, -3, 9, 4)):
            d.ellipse([cx + (dx - rx) * s, cy + (dy - ry) * s, cx + (dx + rx) * s, cy + (dy + ry) * s],
                      fill=(255, 205, 195))
        d.ellipse([cx - 14 * s, cy + 2 * s, cx + 14 * s, cy + 6 * s], fill=(236, 162, 172))
        d.ellipse([cx - 16 * s, cy - 6 * s, cx + 16 * s, cy + 3 * s], fill=(255, 212, 200))

    def hills(base, amp, period, phase, colour):
        pts = [(0, h)]
        for x in range(0, w + 1):
            pts.append((x, base + amp * math.sin(x / period + phase) + 0.4 * amp * math.sin(x / (period / 2.3) + 2 * phase)))
        pts.append((w, h))
        d.polygon(pts, fill=colour)

    hills(124, 9, 26, 0.5, (156, 118, 176))
    hills(134, 8, 19, 2.0, (108, 100, 160))
    hills(143, 6, 15, 4.0, (62, 112, 120))
    # pines on the nearest hills
    for x in (14, 40, 47, 118, 126, 205, 212, 289, 298):
        y = 143 + 6 * math.sin(x / 15 + 4.0) + 0.4 * 6 * math.sin(x / (15 / 2.3) + 8.0)
        for k in range(4):
            d.polygon([(x - 2 - k, y - 5 + 3 * k), (x + 2 + k, y - 5 + 3 * k), (x, y - 9 + 3 * k)], fill=(34, 80, 92))
        d.rectangle([x, y + 3, x, y + 5], fill=(60, 44, 56))
    # grass, with a lighter rim, tufts and flowers
    d.rectangle([0, 152, w, h], fill=(88, 178, 96))
    d.rectangle([0, 152, w, 153], fill=(138, 216, 112))
    d.rectangle([0, 154, w, 155], fill=(112, 198, 104))
    for _ in range(190):
        x, y = rng.randrange(w), rng.randrange(156, h)
        d.point((x, y), fill=rng.choice([(66, 150, 84), (66, 150, 84), (128, 208, 108)]))
        if rng.random() < 0.5:
            d.point((x + 1, y - 1), fill=(66, 150, 84))
    for _ in range(26):
        x, y = rng.randrange(w), rng.randrange(158, h - 2)
        d.point((x, y), fill=rng.choice([(255, 255, 255), (255, 130, 170), (255, 222, 96)]))
    img.resize((W, H), Image.NEAREST).save(path)


# ---- the scene ----------------------------------------------------------------

# id, name, badges, avatar, palette, gear, place (% of the width)
CAST = [
    ("p1", "Ana_Clara", ["member"], "gato", "cinza", "coroa", 7),
    ("p2", "Tio_Patinhas", [], "robo", "vermelho", "oculos", 18),
    ("p3", "Lucas", ["member"], "slime", "azul", "festa", 29),
    ("p4", "Moderadora_Bia", ["mod"], "fantasma", "lilas", "bruxo", 40),
    ("p5", "capivara_feliz", [], "cogumelo", "roxo", "bone", 51),
    ("p6", "joaozinho_gamer", [], "gato", None, None, 62),
    ("p7", "pixelzinha", [], "slime", "rosa", "coroa", 73),
    ("p8", "Maré", [], "fantasma", "menta", "oculos", 84),
    ("p9", "Zé", [], "cogumelo", None, "festa", 94),
]

BUBBLES = {
    "p1": "oi chat 👋", "p2": "GG 🎉🎉🎉", "p3": "que live boa 💜",
    "p4": "😂😂😂😂", "p5": "primeira vez aqui, curti demais o canal",
    "p6": "amei ❤️❤️❤️", "p7": "salve salve", "p8": "que cenário lindo", "p9": "vai dar bom",
}

DANMAKU = [
    ("p1", "oi chat 👋"), ("p3", "que live boa"), ("p5", "primeira vez aqui, curti demais o canal 💜"),
    ("p2", "GG 🎉🎉🎉"), ("p7", "salve salve"), ("p4", "kkkkkkkkkkkk"), ("p6", "amei ❤️❤️❤️"),
    ("p8", "esse gato de coroa é minha cara"), ("p9", "vai dar bom"), ("p3", "😂😂😂"),
    ("p2", "alguém sabe que música é essa?"), ("p5", "o fantasma de chapéu de bruxo 🧙"),
    ("p1", "isso foi muito rápido, nem vi o que aconteceu"), ("p7", "🔥🔥🔥"), ("p6", "boa noite chat"),
    ("p4", "tô só olhando"), ("p8", "que cenário lindo"), ("p9", "volta aí pra gente ver de novo"),
]


class Control:
    def __init__(self, path):
        self.path = path

    def send(self, obj):
        line = obj if isinstance(obj, str) else __import__("json").dumps(obj, ensure_ascii=False)
        s = socket.socket(socket.AF_UNIX)
        s.settimeout(3)
        s.connect(self.path)
        s.sendall((line + "\n").encode())
        reply = s.recv(4096)
        s.close()
        return reply

    def say(self, who, text, **extra):
        uid, name, badges = who[0], who[1], who[2]
        msg = {"type": "message", "platform": "youtube", "user_id": uid, "name": name,
               "text": text, "badges": badges}
        msg.update(extra)
        return self.send(msg)


def wait_for(path, secs=10):
    end = time.time() + secs
    while time.time() < end:
        if os.path.exists(path):
            return True
        time.sleep(0.1)
    return False


def grab(display, name):
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name)
    ImageGrab.grab(xdisplay=display).convert("RGB").save(path, optimize=True)
    print("wrote", path, os.path.getsize(path) // 1024, "KiB")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--display", default=":78")
    ap.add_argument("--keep", action="store_true", help="leave kikarinhas running on the last scene")
    ap.add_argument("--seed", default="3")
    args = ap.parse_args()
    if not os.path.exists("build/kikarinhas"):
        sys.exit("build/kikarinhas not found: run make first")

    tmp = tempfile.mkdtemp(prefix="kk-shots.")
    os.makedirs(os.path.join(tmp, "home"))
    backdrop(os.path.join(tmp, "backdrop.png"))
    sock_path = "/tmp/kk-shots-%d.sock" % os.getpid()
    cfg = os.path.join(tmp, "kikarinhas.ini")

    def write_cfg(danmaku):
        with open(cfg, "w") as f:
            f.write(f"""[window]
size = {W}x{H}
[avatars]
scale = 3
count = 0
sa_dir = off
name_size = 12
bubble_size = 13
[chat]
max = 16
users = {tmp}/users.tsv
[control]
socket = {sock_path}
[emote_wall]
enabled = {"no" if danmaku else "yes"}
size = 60
duration = 6
[danmaku]
enabled = {"yes" if danmaku else "no"}
size = 26
seconds = 9
[scenery.fundo]
file = {tmp}/backdrop.png
""")

    write_cfg(False)
    env = {"PATH": os.environ["PATH"], "HOME": os.path.join(tmp, "home"),
           "DISPLAY": args.display, "LANG": os.environ.get("LANG", "C.UTF-8")}
    xephyr = None
    if not os.path.exists("/tmp/.X11-unix/X" + args.display.lstrip(":")):
        xephyr = subprocess.Popen(["Xephyr", args.display, "-screen", f"{W}x{H}", "-title", "kikarinhas screenshots"],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(1.5)
    prog = subprocess.Popen(["build/kikarinhas", "-c", cfg, "--seed", args.seed],
                            env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        if not wait_for(sock_path):
            sys.exit("kikarinhas did not come up")
        ctl = Control(sock_path)

        # The cast: look first, then where to stand.
        for who in CAST:
            _, _, _, av, pal, gear, _ = who
            ctl.say(who, "!avatar " + av)
            if pal:
                ctl.say(who, "!color " + pal)
            if gear:
                ctl.say(who, "!gear " + gear)
            time.sleep(0.05)
        time.sleep(2.5)  # (changing look makes them hop: no walking meanwhile)
        for who in CAST:
            ctl.say(who, "!move %d" % who[6])
            time.sleep(0.05)
        time.sleep(15)  # they walk (slowly) to their places
        # ...and stay there: the moderator freezes everyone (herself included).
        by_id = {w[0]: w for w in CAST}
        for who in CAST:
            ctl.say(by_id["p4"], "!freeze @" + who[1] if who[0] != "p4" else "!freeze")
            time.sleep(0.05)
        time.sleep(3.6)  # the "congelado" notes go away

        # Scene 1: bubbles and the emote wall.
        for uid, text in BUBBLES.items():
            ctl.say(by_id[uid], text)
            time.sleep(0.12)
        for emoji, n in (("❤", 3),):
            ctl.send({"type": "reaction", "platform": "youtube", "emoji": emoji, "count": n})
        time.sleep(float(os.environ.get("SHOT_DELAY", "2.4")))
        grab(args.display, "chat.png")

        # Scene 2: the same chat as danmaku.
        time.sleep(7)  # bubbles, emotes and jumps settle
        write_cfg(True)
        ctl.send("reload")
        time.sleep(1.0)
        for uid, text in DANMAKU:
            ctl.say(by_id[uid], text)
            time.sleep(0.32)
        ctl.say(by_id["p2"], "toma um cafezinho ☕", kind="paid", amount="R$ 10,00")
        time.sleep(0.4)
        ctl.say(by_id["p7"], "", kind="member")
        time.sleep(float(os.environ.get("SHOT_DELAY2", "1.2")))
        grab(args.display, "danmaku.png")
        if args.keep:
            print("left running; Ctrl-C to stop")
            prog.wait()
    finally:
        if prog.poll() is None:
            prog.send_signal(signal.SIGTERM)
            try:
                prog.wait(5)
            except subprocess.TimeoutExpired:
                prog.kill()
        if xephyr and not args.keep:
            xephyr.terminate()
        if not args.keep:
            shutil.rmtree(tmp, ignore_errors=True)
            if os.path.exists(sock_path):
                os.unlink(sock_path)


main()
