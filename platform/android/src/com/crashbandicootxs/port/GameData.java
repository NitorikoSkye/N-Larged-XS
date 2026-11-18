package com.crashbandicootxs.port;

import android.content.Context;
import android.content.res.AssetManager;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.security.MessageDigest;
import java.util.Arrays;

/**
 * The player's ROM and the cores' game data (platform/android/README.md,
 * "The ROM").
 *
 * The APK holds no part of the game: each core in it has its data blanked
 * out, and a package next to it (assets/ABI/CORE.pkg, made by
 * tools/make_core_package.py) says how to make the data back from the ROM:
 * runs copied from the ROM, and the rest XORed with a key stream made from
 * the ROM. The ROM is checked by its SHA-1 and kept in the app's own files
 * (rom.gba), so that an update of the app makes its new data at once; the
 * data goes to CORE.dat, which the core reads as it starts (CRASHXS_DATA,
 * rom_data.c).
 */
final class GameData {
    /** Crash Bandicoot XS (Europe) (En,Fr,De,Es,It,Nl). */
    static final String ROM_SHA1 = "bdd061e1b5187c0528928ec0ddb6886b1de16970";
    static final int ROM_SIZE = 8 * 1024 * 1024;

    private GameData() {
    }

    static File romFile(Context c) {
        return new File(c.getFilesDir(), "rom.gba");
    }

    static boolean haveRom(Context c) {
        File f = romFile(c);
        return f.isFile() && f.length() > 0;
    }

    /** Reads a ROM the player picked; null and `why` set if it isn't the one. */
    static byte[] readRom(InputStream in, String[] why) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream(ROM_SIZE);
        byte[] b = new byte[1 << 16];
        int n;

        while ((n = in.read(b)) > 0) {
            out.write(b, 0, n);
            if (out.size() > 64 * 1024 * 1024) {
                why[0] = "That file is far too big to be a GBA ROM.";
                return null;
            }
        }
        byte[] rom = out.toByteArray();
        String sha = hex(sha1(rom));
        if (!sha.equals(ROM_SHA1)) {
            why[0] = "That isn't the ROM this needs. It needs Crash Bandicoot XS (Europe) (En,Fr,De,Es,It,Nl), "
                    + "SHA-1 " + ROM_SHA1 + ". This file's SHA-1 is " + sha + ".";
            return null;
        }
        return rom;
    }

    static void keepRom(Context c, byte[] rom) throws IOException {
        File tmp = new File(c.getFilesDir(), "rom.gba.tmp");
        OutputStream out = new FileOutputStream(tmp);
        try {
            out.write(rom);
        } finally {
            out.close();
        }
        if (!tmp.renameTo(romFile(c))) {
            throw new IOException("can't keep the ROM");
        }
    }

    /** The data file for `core` (a core's file name) of `abi`, made if it is
     * missing or for another build; null if the APK has no package for it. */
    static File dataFor(Context c, String abi, String core) throws IOException {
        String name = core.replace(".so", "");
        byte[] pkg;
        try {
            pkg = readAll(c.getAssets().open(abi + "/" + name + ".pkg", AssetManager.ACCESS_STREAMING));
        } catch (IOException e) {
            return null;
        }
        File dat = new File(c.getFilesDir(), abi + "-" + name + ".dat");
        byte[] buildId = Arrays.copyOfRange(pkg, 32, 52);
        if (dat.isFile() && sameBuild(dat, buildId)) {
            return dat;
        }
        byte[] rom = readAll(new FileInputStream(romFile(c)));
        File tmp = new File(dat.getPath() + ".tmp");
        OutputStream out = new FileOutputStream(tmp);
        try {
            make(pkg, rom, out);
        } finally {
            out.close();
        }
        if (!tmp.renameTo(dat)) {
            throw new IOException("can't write " + dat);
        }
        return dat;
    }

    private static boolean sameBuild(File dat, byte[] buildId) {
        try {
            InputStream in = new FileInputStream(dat);
            byte[] head = new byte[32];
            int got = 0;
            try {
                while (got < 32) {
                    int n = in.read(head, got, 32 - got);
                    if (n <= 0) {
                        return false;
                    }
                    got += n;
                }
            } finally {
                in.close();
            }
            return Arrays.equals(Arrays.copyOfRange(head, 12, 32), buildId);
        } catch (IOException e) {
            return false;
        }
    }

    /** The package's recipe with the ROM: the data file (rom_data.c's format). */
    private static void make(byte[] pkg, byte[] rom, OutputStream out) throws IOException {
        if (!new String(pkg, 0, 8, "ISO-8859-1").equals("NLXSAND1")) {
            throw new IOException("not a core package");
        }
        byte[] romSha1 = Arrays.copyOfRange(pkg, 12, 32);
        byte[] buildId = Arrays.copyOfRange(pkg, 32, 52);
        int size = u32(pkg, 52);
        int regions = u32(pkg, 56);
        int ops = u32(pkg, 60);
        if (!Arrays.equals(sha1(rom), romSha1)) {
            throw new IOException("the ROM kept isn't the one the package wants");
        }
        int p = 64;
        int regionTable = p;
        p += 12 * regions;
        int opTable = p;
        int payload = p + 12 * ops;
        byte[] data = new byte[size];
        int at = 0;
        MessageDigest md = digest();
        byte[] block = null;
        int blockNo = -1;
        for (int i = 0; i < ops; i++) {
            int kind = u32(pkg, opTable + 12 * i);
            int a = u32(pkg, opTable + 12 * i + 4);
            int length = u32(pkg, opTable + 12 * i + 8);
            if (kind == 0) {
                System.arraycopy(rom, a, data, at, length);
                at += length;
            } else {
                for (int k = 0; k < length; k++, at++) {
                    int b = at / 20;
                    if (b != blockNo) {
                        md.reset();
                        md.update(romSha1);
                        md.update(new byte[] { (byte) b, (byte) (b >> 8), (byte) (b >> 16), (byte) (b >> 24) });
                        block = md.digest();
                        blockNo = b;
                    }
                    data[at] = (byte) (pkg[payload + a + k] ^ block[at % 20]);
                }
            }
        }
        if (at != size || !Arrays.equals(sha1(data), buildId)) {
            throw new IOException("the game data came out wrong");
        }
        byte[] head = new byte[32];
        System.arraycopy("NLXSDAT1".getBytes("ISO-8859-1"), 0, head, 0, 8);
        put32(head, 8, regions);
        System.arraycopy(buildId, 0, head, 12, 20);
        out.write(head);
        out.write(pkg, regionTable, 12 * regions);
        out.write(data);
    }

    private static int u32(byte[] b, int o) {
        return (b[o] & 0xFF) | (b[o + 1] & 0xFF) << 8 | (b[o + 2] & 0xFF) << 16 | (b[o + 3] & 0xFF) << 24;
    }

    private static void put32(byte[] b, int o, int v) {
        b[o] = (byte) v;
        b[o + 1] = (byte) (v >> 8);
        b[o + 2] = (byte) (v >> 16);
        b[o + 3] = (byte) (v >> 24);
    }

    private static MessageDigest digest() {
        try {
            return MessageDigest.getInstance("SHA-1");
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }

    static byte[] sha1(byte[] b) {
        MessageDigest md = digest();
        return md.digest(b);
    }

    static String hex(byte[] b) {
        StringBuilder s = new StringBuilder();
        for (byte x : b) {
            s.append(String.format("%02x", x & 0xFF));
        }
        return s.toString();
    }

    static byte[] readAll(InputStream in) throws IOException {
        try {
            ByteArrayOutputStream out = new ByteArrayOutputStream(1 << 20);
            byte[] b = new byte[1 << 16];
            int n;
            while ((n = in.read(b)) > 0) {
                out.write(b, 0, n);
            }
            return out.toByteArray();
        } finally {
            in.close();
        }
    }
}
