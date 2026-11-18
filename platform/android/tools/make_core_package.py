#!/usr/bin/env python3
"""The Android core's package (platform/android/README.md, "The ROM"): the
core in the APK with the game's data blanked out, and the recipe the app
uses to make that data back from the player's own ROM.

    make_core_package.py --rom ROM --core libcrashxs_core.so --map libcrashxs_core.so.map \\
                         --nm aarch64-linux-gnu-nm --obj-dir build/android-musl-arm64/crashbandicootxs \\
                         --blanked OUT.so --package OUT.pkg

The data: every input section that the map says is from one of the build's
own objects (under --obj-dir) in .rodata, .data and .data.rel.ro, but
merged strings and constants (.rodata.str*, .rodata.cst*: the code's
literals) and rom_data.o (the loader itself). The C library's data stays,
since it runs before the data is back (rom_data.c's constructor).

The regions' bytes, in order, are the data: as in the launcher's package
(platform/pc/tools/make_package.py), runs of 32 bytes or more that are in
the ROM are stored as their places in it (COPY), the rest XORed with a key
stream made from the ROM (MASKED: SHA-1 of the ROM's SHA-1 and a block
number, the block counted in the data). The blanked core has zeros there,
and the data's SHA-1 (its build id) in rom_data.c's gHostDataBuildId.

Package format (little-endian): "NLXSAND1", u32 format 1, u8[20] ROM SHA-1,
u8[20] data SHA-1 (the build id), u32 data size, u32 region count, u32
operation count, the regions (u64 address, u32 length), the operations (u32
kind 0 COPY / 2 MASKED, u32 ROM offset or payload offset, u32 length), then
the payload.

The app writes the data file rom_data.c reads: "NLXSDAT1", u32 region
count, u8[20] build id, the regions, the data.
"""

import argparse
import hashlib
import re
import struct
import subprocess
import sys

sys.path.insert(0, __file__.rsplit("/", 3)[0] + "/pc/tools")
from make_package import copy_runs, key_stream  # noqa: E402

OUTPUT_SECTIONS = (".rodata", ".data", ".data.rel.ro")


def load_segments(elf):
    if elf[:4] != b"\x7fELF" or elf[4] != 2:
        raise SystemExit("make_core_package.py: not a 64-bit ELF")
    phoff = struct.unpack_from("<Q", elf, 0x20)[0]
    phentsize, phnum = struct.unpack_from("<HH", elf, 0x36)
    segs = []
    for i in range(phnum):
        p_type, p_flags, p_offset, p_vaddr, _, p_filesz, p_memsz, _ = struct.unpack_from(
            "<IIQQQQQQ", elf, phoff + i * phentsize)
        if p_type == 1:
            segs.append((p_vaddr, p_offset, p_filesz, p_flags))
    return segs


def file_offset(segs, addr, length):
    for vaddr, off, size, flags in segs:
        if vaddr <= addr and addr + length <= vaddr + size:
            if flags & 1:
                raise SystemExit("make_core_package.py: data at %#x is in an executable segment" % addr)
            return off + addr - vaddr
    raise SystemExit("make_core_package.py: %#x is in no segment's file data" % addr)


def map_regions(path, obj_dir):
    """The build's own input sections in the data output sections: (address, size)."""
    regions = []
    out = None
    pending = None
    for line in open(path, errors="replace"):
        m = re.match(r"^(\.\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)", line)
        if m:
            out = m.group(1)
            continue
        if line.startswith(".") or (line and not line[0].isspace() and line.strip()):
            out = line.split()[0] if line.split() else None
            continue
        if out not in OUTPUT_SECTIONS:
            pending = None
            continue
        parts = line.split()
        if pending is not None and len(parts) >= 3 and parts[0].startswith("0x"):
            parts = [pending] + parts
            pending = None
        if len(parts) == 1 and parts[0].startswith("."):
            pending = parts[0]  # the name alone: the rest on the next line
            continue
        if len(parts) < 4 or not parts[0].startswith(".") or not parts[1].startswith("0x"):
            continue
        name, addr, size, obj = parts[0], int(parts[1], 16), int(parts[2], 16), parts[3]
        if size == 0 or obj_dir not in obj:
            continue
        if name.startswith(".rodata.str") or name.startswith(".rodata.cst") or "rom_data.o" in obj:
            continue
        regions.append((addr, size))
    regions.sort()
    merged = []
    for a, n in regions:
        if merged and a <= merged[-1][0] + merged[-1][1]:
            end = max(merged[-1][0] + merged[-1][1], a + n)
            merged[-1] = (merged[-1][0], end - merged[-1][0])
        else:
            merged.append((a, n))
    return merged


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", required=True)
    ap.add_argument("--core", required=True, help="the core as linked (with its symbols)")
    ap.add_argument("--map", required=True)
    ap.add_argument("--nm", required=True)
    ap.add_argument("--strip", required=True)
    ap.add_argument("--obj-dir", required=True)
    ap.add_argument("--blanked", required=True, help="the core to publish: stripped, its data blanked")
    ap.add_argument("--package", required=True)
    args = ap.parse_args()

    rom = open(args.rom, "rb").read()
    rom_sha1 = hashlib.sha1(rom).digest()
    subprocess.run([args.strip, "-o", args.blanked, args.core], check=True)
    elf = bytearray(open(args.blanked, "rb").read())
    segs = load_segments(elf)
    syms = {}
    for line in subprocess.run([args.nm, args.core], capture_output=True, text=True, check=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3:
            syms[parts[2]] = int(parts[0], 16)
    if "gHostDataBuildId" not in syms:
        raise SystemExit("make_core_package.py: no gHostDataBuildId (not a HOST_ANDROID core?)")

    regions = map_regions(args.map, args.obj_dir)
    data = bytearray()
    places = []
    for addr, size in regions:
        off = file_offset(segs, addr, size)
        places.append(off)
        data += elf[off:off + size]
    build_id = hashlib.sha1(data).digest()

    # the recipe for the data
    runs = copy_runs(rom, bytes(data))
    stream = key_stream(rom_sha1, len(data))
    ops = []
    payload = bytearray()
    pos = 0

    def masked(a, b):
        ops.append((2, len(payload), b - a))
        payload.extend(x ^ y for x, y in zip(data[a:b], stream[a:b]))

    for start, rom_off, length in runs:
        if start > pos:
            masked(pos, start)
        ops.append((0, rom_off, length))
        pos = start + length
    if pos < len(data):
        masked(pos, len(data))
    out = bytearray()
    for kind, a, length in ops:
        if kind == 0:
            out += rom[a:a + length]
        else:
            o = len(out)
            out += bytes(x ^ y for x, y in zip(payload[a:a + length], stream[o:o + length]))
    if bytes(out) != bytes(data):
        raise SystemExit("make_core_package.py: the recipe doesn't give the data back")

    # the blanked core
    for (addr, size), off in zip(regions, places):
        elf[off:off + size] = bytes(size)
    id_off = file_offset(segs, syms["gHostDataBuildId"], 20)
    elf[id_off:id_off + 20] = build_id
    open(args.blanked, "wb").write(elf)

    head = b"NLXSAND1" + struct.pack("<I", 1) + rom_sha1 + build_id
    head += struct.pack("<III", len(data), len(regions), len(ops))
    for addr, size in regions:
        head += struct.pack("<QI", addr, size)
    for kind, a, length in ops:
        head += struct.pack("<III", kind, a, length)
    open(args.package, "wb").write(head + payload)

    # what of the ROM is left in the blanked core: runs of 32 bytes or more
    left = [r for r in copy_runs(rom, bytes(elf)) if r[2] >= 32]
    copied = sum(length for kind, a, length in ops if kind == 0)
    print("make_core_package.py: %s: %d regions, %d bytes of data (%d from the ROM, %d masked); "
          "package %d bytes; ROM runs left in the core: %d (%d bytes)"
          % (args.blanked, len(regions), len(data), copied, len(data) - copied, len(head) + len(payload),
             len(left), sum(r[2] for r in left)), file=sys.stderr)
    for start, rom_off, length in left[:10]:
        print("  left: core %#x ROM %#x %d bytes: %s" % (start, rom_off, length, bytes(elf[start:start + 16]).hex()),
              file=sys.stderr)


if __name__ == "__main__":
    main()
