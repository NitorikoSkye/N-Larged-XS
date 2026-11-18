#!/usr/bin/env python3
"""Plays the Android app's part against a core run with --stream
(platform/pc/src/stream.c), for testing the core without a phone.

    stream_test.py CORE SCRIPT OUTDIR [--save FILE] [--dir DIR]

SCRIPT lines (by game picture count, from 0):
    N keys BITS          the GBA's keys held (a number, or names: a b start ...)
    N tap X Y            a tap on the picture's pixels
    N back | menu        the phone's back button, the settings button
    N hz HZ              the screen's top refresh rate
    N style S            the controller's prompt style (HOST_PROMPTS_*)
    N pad 0|1            whether a controller is connected
    N bars L R H W TOP   the screen around the picture (the borders)
    N scroll STEPS
    N shot NAME          write the latest picture as OUTDIR/NAME.png
    N end
It prints each state, settings and toast message as it comes.
"""
import os
import struct
import subprocess
import sys
import threading
import time

from PIL import Image

KEYS = {"a": 1, "b": 2, "select": 4, "start": 8, "right": 16, "left": 32, "up": 64, "down": 128, "r": 256, "l": 512}
TOUCH = {0: "play", 1: "menu", 2: "settings", 3: "any"}


def keybits(words):
    bits = 0
    for w in words:
        bits |= int(w, 0) if w[0].isdigit() else KEYS[w]
    return bits


def msg(kind, *args):
    vals = list(args) + [0] * (3 - len(args))
    return struct.pack("<BBhhh", kind, 0, *vals)


def main():
    core, script, out = sys.argv[1:4]
    extra = sys.argv[4:]
    save = "/tmp/stream_test.sav"
    data = "/tmp/stream_test_dir"
    if "--save" in extra:
        save = extra[extra.index("--save") + 1]
    if "--dir" in extra:
        data = extra[extra.index("--dir") + 1]
    os.makedirs(out, exist_ok=True)
    os.makedirs(data, exist_ok=True)
    steps = {}
    for line in open(script):
        line = line.split("#")[0].split()
        if line:
            steps.setdefault(int(line[0]), []).append(line[1:])
    env = dict(os.environ, CRASHXS_DIR=data)
    p = subprocess.Popen([core, "--stream", "--save", save], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=open(os.path.join(out, "core.log"), "w"), env=env)
    pictures = 0
    last = None
    started = time.time()
    while True:
        for step in steps.pop(pictures, []):
            kind = step[0]
            if kind == "keys":
                p.stdin.write(msg(1, keybits(step[1:]) if len(step) > 1 else 0))
            elif kind == "tap":
                p.stdin.write(msg(2, int(step[1]), int(step[2])))
            elif kind == "back":
                p.stdin.write(msg(3))
            elif kind == "menu":
                p.stdin.write(msg(4))
            elif kind == "hz":
                p.stdin.write(msg(5, int(step[1])))
            elif kind == "style":
                p.stdin.write(msg(6, int(step[1])))
            elif kind == "scroll":
                p.stdin.write(msg(7, int(step[1])))
            elif kind == "pad":
                p.stdin.write(msg(8, int(step[1])))
            elif kind == "bars":
                p.stdin.write(msg(9, int(step[1]), int(step[2]), int(step[3])))
                p.stdin.write(msg(10, int(step[4]), int(step[5])))
            elif kind == "shot":
                w, h, data_ = last
                img = Image.frombytes("RGB", (w, h), rgb(data_, w, h))
                img.save(os.path.join(out, step[1] + ".png"))
                print(f"{pictures}: shot {step[1]} {w}x{h}")
            elif kind == "end":
                p.stdin.close()
                try:
                    p.stdout.close()
                    p.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    p.kill()
                import resource
                ru = resource.getrusage(resource.RUSAGE_CHILDREN)
                print(f"{pictures} pictures in {time.time() - started:.1f}s, core cpu {ru.ru_utime + ru.ru_stime:.1f}s")
                return
            p.stdin.flush()
        head = p.stdout.read(8)
        if len(head) < 8:
            print("core ended", p.wait())
            return
        kind, n = struct.unpack("<II", head)
        body = p.stdout.read(n)
        if kind == 1:
            w, h = (240, 160) if n == 240 * 160 * 2 else (480, 320)
            last = (w, h, body)
            pictures += 1
        elif kind == 3:
            print(f"{pictures}: touch {TOUCH.get(body[0], body[0])}")
        elif kind == 4:
            print(f"{pictures}: settings {struct.unpack('<8i', body)}")
        elif kind == 5:
            print(f"{pictures}: toast {body.decode()}")
        elif kind == 6:
            side, unit, w, h = struct.unpack_from("<4i", body)
            img = Image.frombytes("RGB", (w, h), rgb(body[16:], w, h))
            img.save(os.path.join(out, f"bar{side}_{pictures}.png"))
            print(f"{pictures}: bar {side} unit {unit} {w}x{h}")


def rgb(data, w, h):
    out = bytearray(w * h * 3)
    for i in range(w * h):
        v = data[2 * i] | data[2 * i + 1] << 8
        out[3 * i] = (v >> 11 & 31) * 255 // 31
        out[3 * i + 1] = (v >> 5 & 63) * 255 // 63
        out[3 * i + 2] = (v & 31) * 255 // 31
    return bytes(out)


if __name__ == "__main__":
    main()
