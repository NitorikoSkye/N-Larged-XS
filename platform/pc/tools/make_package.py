#!/usr/bin/env python3
"""The launcher's package (platform/pc/LAUNCHER.md): the Windows game
executable as a recipe for rebuilding it from the player's own ROM, so the
launcher can be published with no ROM data in it.

    make_package.py --rom crashbandicootxs.gba --exe CrashBandicootXS.exe \\
                    --symbols CrashBandicootXS-unstripped.exe --nm x86_64-w64-mingw32-nm \\
                    --version v0.37 -o package.bin

The executable is read as a list of operations that write it out again in
order:
- COPY: a run of at least MIN_RUN bytes that is in the ROM as it is (the
  graphics, the music, the levels: 95% of the ROM is in the executable
  byte for byte) is stored as its place in the ROM, not its bytes;
- MASKED: any other byte outside the code section (the ROM's data in
  another form - the settings menu's art, the music's structures, the
  icon - and the rest of the data) is stored XORed with a key stream made
  from the ROM (SHA-1 of the ROM's SHA-1 and a block number), so it means
  nothing without the ROM;
- PLAIN: the code section's bytes (the decompilation's and the port's
  compiled code) are stored as they are.
The launcher checks the ROM's SHA-1, replays the operations and checks the
result's SHA-1 against the executable's.

It also lists where the settings menu's art (the fonts and the pause
screen) is in the executable, so the launcher can draw itself with it once
the game has been built.

Format (little-endian): "NLXSPKG1", u32 format 1, u8[20] ROM SHA-1, u8[20]
output SHA-1, u32 output size, char[16] game version, u32 art count, u32
operation count, then the art entries (char[28] name, u32 offset, u32
size), the operations (u32 kind 0 COPY / 1 PLAIN / 2 MASKED, u32 ROM
offset or payload offset, u32 length), then the payload.
"""

import argparse
import collections
import hashlib
import struct
import subprocess
import sys

MIN_RUN = 32
KEY = 16

ART = [
    "gUiSmallFontCells",
    "gUiSmallFontAdvance",
    "gUiSmallFontYOffset",
    "gUiSmallFontLookup",
    "gUiLargeFontCells",
    "gUiLargeFontAdvance",
    "gUiLargeFontYOffset",
    "gUiLargeFontLookup",
    "gUiFontPalette",
    "gUiPauseArt",
    "gUiPausePalette",
]


def sections(exe):
    """The PE's sections: (name, virtual address, virtual size, file offset, file size), and the image base."""
    pe = struct.unpack_from("<I", exe, 0x3C)[0]
    if exe[pe:pe + 4] != b"PE\0\0":
        raise SystemExit("make_package.py: not a PE executable")
    count = struct.unpack_from("<H", exe, pe + 6)[0]
    opt_size = struct.unpack_from("<H", exe, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from("<H", exe, opt)[0]
    base = struct.unpack_from("<Q", exe, opt + 24)[0] if magic == 0x20B else struct.unpack_from("<I", exe, opt + 28)[0]
    out = []
    for i in range(count):
        o = opt + opt_size + 40 * i
        name = exe[o:o + 8].rstrip(b"\0").decode("latin-1")
        vsize, va, rsize, rptr = struct.unpack_from("<IIII", exe, o + 8)
        out.append((name, va, vsize, rptr, rsize))
    return out, base


def file_offset(secs, base, addr):
    rva = addr - base
    for name, va, vsize, rptr, rsize in secs:
        if va <= rva < va + max(vsize, rsize) and rva - va < rsize:
            return rptr + (rva - va)
    raise SystemExit("make_package.py: address %#x is in no section's file data" % addr)


def copy_runs(rom, exe):
    """The executable's runs that are in the ROM as they are: (exe offset, ROM offset, length)."""
    index = collections.defaultdict(list)
    for i in range(0, len(rom) - KEY + 1):
        k = rom[i:i + KEY]
        if k.count(k[0]) == KEY:
            continue  # fill: it would match everywhere
        index[k].append(i)
    runs = []
    i = 0
    n = len(exe)
    while i <= n - KEY:
        k = exe[i:i + KEY]
        best = (0, 0)
        if k in index:
            for r in index[k][:64]:
                length = KEY
                while i + length < n and r + length < len(rom) and exe[i + length] == rom[r + length]:
                    length += 1
                if length > best[0]:
                    best = (length, r)
        if best[0] >= MIN_RUN:
            runs.append((i, best[1], best[0]))
            i += best[0]
        else:
            i += 1
    return runs


def key_stream(rom_sha1, size):
    out = bytearray()
    block = 0
    while len(out) < size:
        out += hashlib.sha1(rom_sha1 + struct.pack("<I", block)).digest()
        block += 1
    return bytes(out[:size])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", required=True)
    ap.add_argument("--exe", required=True, help="the executable to publish the recipe of (stripped)")
    ap.add_argument("--symbols", required=True, help="the same executable before strip (for the art's places)")
    ap.add_argument("--nm", default="x86_64-w64-mingw32-nm")
    ap.add_argument("--version", required=True)
    ap.add_argument("-o", "--output", required=True)
    args = ap.parse_args()

    rom = open(args.rom, "rb").read()
    exe = open(args.exe, "rb").read()
    rom_sha1 = hashlib.sha1(rom).digest()
    secs, base = sections(exe)
    code = [(rptr, rptr + rsize) for name, va, vsize, rptr, rsize in secs if name == ".text"]

    # The art's places, from the unstripped executable's symbols (the same
    # addresses: strip only drops the symbol table).
    syms = {}
    for line in subprocess.run([args.nm, args.symbols], capture_output=True, text=True, check=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3:
            syms[parts[2]] = int(parts[0], 16)
    ordered = sorted(syms.values())
    art = []
    for name in ART:
        if name not in syms:
            raise SystemExit("make_package.py: no symbol " + name)
        addr = syms[name]
        nxt = min(a for a in ordered if a > addr)
        art.append((name, file_offset(secs, base, addr), nxt - addr))

    runs = copy_runs(rom, exe)
    ops = []
    payload = bytearray()
    stream = key_stream(rom_sha1, len(exe))

    def literal(start, end):
        pos = start
        while pos < end:
            in_code = any(a <= pos < b for a, b in code)
            # up to the next code boundary
            stop = end
            for a, b in code:
                if pos < a < stop:
                    stop = a
                if a <= pos < b and b < stop:
                    stop = b
            chunk = exe[pos:stop]
            if in_code:
                ops.append((1, len(payload), len(chunk)))
                payload.extend(chunk)
            else:
                ops.append((2, len(payload), len(chunk)))
                payload.extend(bytes(x ^ y for x, y in zip(chunk, stream[pos:stop])))
            pos = stop

    pos = 0
    for start, rom_off, length in runs:
        if start > pos:
            literal(pos, start)
        ops.append((0, rom_off, length))
        pos = start + length
    if pos < len(exe):
        literal(pos, len(exe))

    # check: replaying gives the executable back
    out = bytearray()
    for kind, a, length in ops:
        if kind == 0:
            out += rom[a:a + length]
        elif kind == 1:
            out += payload[a:a + length]
        else:
            o = len(out)
            out += bytes(x ^ y for x, y in zip(payload[a:a + length], stream[o:o + length]))
    if bytes(out) != exe:
        raise SystemExit("make_package.py: the recipe doesn't give the executable back")

    head = b"NLXSPKG1" + struct.pack("<I", 1) + rom_sha1 + hashlib.sha1(exe).digest()
    head += struct.pack("<I", len(exe)) + args.version.encode()[:15].ljust(16, b"\0")
    head += struct.pack("<II", len(art), len(ops))
    for name, off, size in art:
        head += name.encode().ljust(28, b"\0") + struct.pack("<II", off, size)
    for kind, a, length in ops:
        head += struct.pack("<III", kind, a, length)
    with open(args.output, "wb") as f:
        f.write(head + payload)

    copied = sum(length for kind, a, length in ops if kind == 0)
    plain = sum(length for kind, a, length in ops if kind == 1)
    masked = sum(length for kind, a, length in ops if kind == 2)
    print("make_package.py: %d bytes: %d from the ROM, %d code, %d masked; package %d bytes"
          % (len(exe), copied, plain, masked, len(head) + len(payload)), file=sys.stderr)


if __name__ == "__main__":
    main()
