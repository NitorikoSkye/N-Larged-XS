#!/usr/bin/env python3
"""What the app does with the player's ROM (ImportActivity.java, the same
steps), for testing on a computer: the core's game data file from its
package (make_core_package.py) and the ROM.

    make_data_file.py PACKAGE ROM OUT
"""
import hashlib
import struct
import sys


def key_block(rom_sha1, block):
    return hashlib.sha1(rom_sha1 + struct.pack("<I", block)).digest()


def main():
    pkg = open(sys.argv[1], "rb").read()
    rom = open(sys.argv[2], "rb").read()
    if pkg[:8] != b"NLXSAND1":
        raise SystemExit("not a core package")
    rom_sha1 = pkg[12:32]
    build_id = pkg[32:52]
    size, nregions, nops = struct.unpack_from("<III", pkg, 52)
    if hashlib.sha1(rom).digest() != rom_sha1:
        raise SystemExit("not the right ROM")
    p = 64
    regions = [struct.unpack_from("<QI", pkg, p + 12 * i) for i in range(nregions)]
    p += 12 * nregions
    ops = [struct.unpack_from("<III", pkg, p + 12 * i) for i in range(nops)]
    payload = pkg[p + 12 * nops:]
    data = bytearray()
    for kind, a, length in ops:
        if kind == 0:
            data += rom[a:a + length]
        else:
            o = len(data)
            for i in range(length):
                pos = o + i
                if pos % 20 == 0 or i == 0:
                    blk = key_block(rom_sha1, pos // 20)
                data.append(payload[a + i] ^ blk[pos % 20])
    if len(data) != size or hashlib.sha1(data).digest() != build_id:
        raise SystemExit("the data came out wrong")
    out = b"NLXSDAT1" + struct.pack("<I", nregions) + build_id
    for addr, length in regions:
        out += struct.pack("<QI", addr, length)
    open(sys.argv[3], "wb").write(out + data)


if __name__ == "__main__":
    main()
