/* NLargedXSLauncher (platform/pc/LAUNCHER.md): makes "Crash Bandicoot
 * XS.exe", the PC port, from the player's own ROM, and starts it.
 *
 * The launcher holds no ROM data. It carries a package (make_package.py,
 * launcher_package.S): the port's executable as a list of operations -
 * runs to copy from the ROM, the code as it is, and everything else XORed
 * with a key stream made from the ROM. "Import ROM" checks the ROM's
 * SHA-1, replays the operations, checks the result's SHA-1 against the
 * executable's and writes it next to the launcher. That executable is the
 * whole game: the launcher isn't needed by it, and can go.
 *
 * Its window is the PC settings menu's category page: the game's fonts
 * and pause screen art, read out of the built executable (the package
 * says where they are), so before the game is built - with no ROM data at
 * hand - it draws its own look-alike (the borders' riveted wall and
 * plates, a font of its own in the game's colours).
 *
 * The command line does the same without a window (see Usage). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_syswm.h>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "host_platform.h"

#define GAME_FILE "Crash Bandicoot XS.exe"
#define INI_FILE  "NLargedXSLauncher.ini"
#define W 240
#define H 160

extern const unsigned char gLauncherPackage[];
extern const unsigned char gLauncherPackageEnd[];

/* --- The package ----------------------------------------------------------- */

struct Package {
    const unsigned char *base;
    u8 romSha1[20];
    u8 outSha1[20];
    u32 outSize;
    char version[17];
    u32 artCount;
    u32 opCount;
    const unsigned char *art;     /* artCount * 36 */
    const unsigned char *ops;     /* opCount * 12 */
    const unsigned char *payload;
};

static struct Package sPkg;

static u32 Le32(const unsigned char *p)
{
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static int ReadPackage(void)
{
    const unsigned char *p = gLauncherPackage;

    if (gLauncherPackageEnd - p < 80 || memcmp(p, "NLXSPKG1", 8) != 0 || Le32(p + 8) != 1) {
        return 0;
    }
    sPkg.base = p;
    memcpy(sPkg.romSha1, p + 12, 20);
    memcpy(sPkg.outSha1, p + 32, 20);
    sPkg.outSize = Le32(p + 52);
    memcpy(sPkg.version, p + 56, 16);
    sPkg.version[16] = '\0';
    sPkg.artCount = Le32(p + 72);
    sPkg.opCount = Le32(p + 76);
    sPkg.art = p + 80;
    sPkg.ops = sPkg.art + 36 * sPkg.artCount;
    sPkg.payload = sPkg.ops + 12 * sPkg.opCount;
    return 1;
}

/* --- Files, by UTF-8 paths --------------------------------------------------- */

#ifdef _WIN32
static wchar_t *Wide(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = malloc((size_t)(n > 0 ? n : 1) * sizeof(wchar_t));

    if (w != NULL) {
        w[0] = 0;
        MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    }
    return w;
}

static void Utf8(const wchar_t *w, char *out, size_t size)
{
    out[0] = 0;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)size, NULL, NULL);
}
#endif

static FILE *OpenU8(const char *path, const char *mode)
{
#ifdef _WIN32
    wchar_t *w = Wide(path);
    wchar_t m[8];
    FILE *f;
    int i;

    for (i = 0; mode[i] && i < 7; i++) {
        m[i] = (wchar_t)mode[i];
    }
    m[i] = 0;
    f = w != NULL ? _wfopen(w, m) : NULL;
    free(w);
    return f;
#else
    return fopen(path, mode);
#endif
}

static unsigned char *ReadFile8(const char *path, size_t *size, size_t limit)
{
    FILE *f = OpenU8(path, "rb");
    unsigned char *buf;
    long n;

    *size = 0;
    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || (size_t)n > limit) {
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)n + 1);
    if (buf != NULL && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf != NULL) {
        *size = (size_t)n;
    }
    return buf;
}

static int ReplaceU8(const char *from, const char *to)
{
#ifdef _WIN32
    wchar_t *a = Wide(from);
    wchar_t *b = Wide(to);
    int ok = a != NULL && b != NULL && MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING);

    free(a);
    free(b);
    return ok;
#else
    return rename(from, to) == 0;
#endif
}

static void RemoveU8(const char *path)
{
#ifdef _WIN32
    wchar_t *w = Wide(path);

    if (w != NULL) {
        DeleteFileW(w);
    }
    free(w);
#else
    remove(path);
#endif
}

static int Exists(const char *path)
{
    FILE *f = OpenU8(path, "rb");

    if (f != NULL) {
        fclose(f);
        return 1;
    }
    return 0;
}

static char sDir[1024];       /* the launcher's folder, with its separator */
static char sGamePath[1100];  /* the game's executable */
static char sIniPath[1100];   /* the ROM last imported */

static void FindPaths(void)
{
    char *base = SDL_GetBasePath();

    snprintf(sDir, sizeof(sDir), "%s", base != NULL ? base : "./");
    SDL_free(base);
    snprintf(sGamePath, sizeof(sGamePath), "%s%s", sDir, GAME_FILE);
    snprintf(sIniPath, sizeof(sIniPath), "%s%s", sDir, INI_FILE);
}

static void RememberRom(const char *rom)
{
    FILE *f = OpenU8(sIniPath, "w");

    if (f != NULL) {
        fprintf(f, "rom=%s\n", rom);
        fclose(f);
    }
}

static int RememberedRom(char *out, size_t size)
{
    FILE *f = OpenU8(sIniPath, "r");
    char line[1200];
    int found = 0;

    if (f == NULL) {
        return 0;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        if (strncmp(line, "rom=", 4) == 0) {
            line[strcspn(line, "\r\n")] = '\0';
            snprintf(out, size, "%s", line + 4);
            found = out[0] != '\0' && Exists(out);
        }
    }
    fclose(f);
    return found;
}

/* --- Building the game -------------------------------------------------------- */

enum {
    BUILD_OK,
    BUILD_NO_ROM,    /* can't read it */
    BUILD_WRONG_ROM, /* not this ROM */
    BUILD_BAD,       /* the result isn't the game: a broken package */
    BUILD_NO_WRITE,  /* can't write the game */
};

static void Sha1(const void *data, size_t n, u8 out[20])
{
    struct HostSha1 s;

    HostSha1Init(&s);
    HostSha1Update(&s, data, (u32)n);
    HostSha1Final(&s, out);
}

/* Is `path` a ROM this package can use? 1 yes, 0 not this ROM, -1 unreadable. */
static int CheckRom(const char *path)
{
    size_t size;
    unsigned char *rom = ReadFile8(path, &size, 64u << 20);
    u8 sha[20];

    if (rom == NULL) {
        return -1;
    }
    Sha1(rom, size, sha);
    free(rom);
    return memcmp(sha, sPkg.romSha1, 20) == 0;
}

static int Build(const char *romPath, const char *outPath)
{
    size_t romSize;
    unsigned char *rom = ReadFile8(romPath, &romSize, 64u << 20);
    unsigned char *out;
    u8 sha[20];
    u8 key[20];
    u32 pos = 0;
    u32 i;
    char tmp[1200];
    FILE *f;
    int ok;

    if (rom == NULL) {
        return BUILD_NO_ROM;
    }
    Sha1(rom, romSize, sha);
    if (memcmp(sha, sPkg.romSha1, 20) != 0) {
        free(rom);
        return BUILD_WRONG_ROM;
    }
    memcpy(key, sha, 20);
    out = malloc(sPkg.outSize);
    if (out == NULL) {
        free(rom);
        return BUILD_BAD;
    }
    for (i = 0; i < sPkg.opCount; i++) {
        const unsigned char *op = sPkg.ops + 12 * i;
        u32 kind = Le32(op);
        u32 a = Le32(op + 4);
        u32 n = Le32(op + 8);

        if (n > sPkg.outSize - pos || (kind == 0 && (a > romSize || n > romSize - a))) {
            pos = 0xFFFFFFFF;
            break;
        }
        if (kind == 0) {
            memcpy(out + pos, rom + a, n);
        } else {
            memcpy(out + pos, sPkg.payload + a, n);
            if (kind == 2) {
                /* the key stream: SHA-1(ROM's SHA-1, block), 20 bytes a block */
                u32 k;
                u32 block = 0xFFFFFFFF;
                u8 bytes[20];

                for (k = 0; k < n; k++) {
                    u32 o = pos + k;

                    if (o / 20 != block) {
                        unsigned char seed[24];

                        block = o / 20;
                        memcpy(seed, key, 20);
                        seed[20] = (unsigned char)block;
                        seed[21] = (unsigned char)(block >> 8);
                        seed[22] = (unsigned char)(block >> 16);
                        seed[23] = (unsigned char)(block >> 24);
                        Sha1(seed, 24, bytes);
                    }
                    out[o] ^= bytes[o % 20];
                }
            }
        }
        pos += n;
    }
    free(rom);
    if (pos != sPkg.outSize) {
        free(out);
        return BUILD_BAD;
    }
    Sha1(out, sPkg.outSize, sha);
    if (memcmp(sha, sPkg.outSha1, 20) != 0) {
        free(out);
        return BUILD_BAD;
    }
    /* written whole beside it, then put in place */
    snprintf(tmp, sizeof(tmp), "%s.new", outPath);
    f = OpenU8(tmp, "wb");
    if (f == NULL) {
        free(out);
        return BUILD_NO_WRITE;
    }
    ok = fwrite(out, 1, sPkg.outSize, f) == sPkg.outSize;
    ok = fclose(f) == 0 && ok;
    free(out);
    if (!ok || !ReplaceU8(tmp, outPath)) {
        RemoveU8(tmp);
        return BUILD_NO_WRITE;
    }
#ifndef _WIN32
    chmod(outPath, 0755);
#endif
    return BUILD_OK;
}

/* The game next to the launcher: 0 none, 1 this package's, 2 another version. */
static int GameState(void)
{
    size_t size;
    unsigned char *exe = ReadFile8(sGamePath, &size, 256u << 20);
    u8 sha[20];

    if (exe == NULL) {
        return 0;
    }
    Sha1(exe, size, sha);
    free(exe);
    return memcmp(sha, sPkg.outSha1, 20) == 0 ? 1 : 2;
}

static int Launch(const char *exe, int argc, char **argv)
{
#ifdef _WIN32
    char cmd[8192];
    wchar_t *wcmd;
    wchar_t *wexe;
    wchar_t *wdir;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    int i;
    int ok;

    snprintf(cmd, sizeof(cmd), "\"%s\"", exe);
    for (i = 0; i < argc; i++) {
        size_t len = strlen(cmd);

        snprintf(cmd + len, sizeof(cmd) - len, " \"%s\"", argv[i]);
    }
    wcmd = Wide(cmd);
    wexe = Wide(exe);
    wdir = Wide(sDir);
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    ok = wcmd != NULL && wexe != NULL &&
         CreateProcessW(wexe, wcmd, NULL, NULL, FALSE, 0, NULL, wdir, &si, &pi);
    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    free(wcmd);
    free(wexe);
    free(wdir);
    return ok;
#else
    pid_t pid = fork();

    if (pid == 0) {
        char *args[64];
        int i;

        args[0] = (char *)exe;
        for (i = 0; i < argc && i < 62; i++) {
            args[i + 1] = argv[i];
        }
        args[i + 1] = NULL;
        if (chdir(sDir) != 0) {
            _exit(127);
        }
        execv(exe, args);
        _exit(127);
    }
    return pid > 0;
#endif
}

static void HexSha(const u8 sha[20], char out[41])
{
    int i;

    for (i = 0; i < 20; i++) {
        snprintf(out + 2 * i, 3, "%02x", sha[i]);
    }
}

/* --- Drawing: the game's art, once the game is here ------------------------- */

static u32 sCanvas[W * H];
static int sRealArt;

static unsigned char sSmallCells[79][256];
static signed char sSmallAdvance[79];
static signed char sSmallYOffset[79];
static unsigned char sSmallLookup[256];
static unsigned char sLargeCells[75][256];
static signed char sLargeAdvance[75];
static signed char sLargeYOffset[75];
static unsigned char sLargeLookup[256];
static unsigned short sFontPalette[16];
static unsigned char sPauseArt[H][W];
static unsigned short sPausePalette[256];

static u32 Color15(u32 c)
{
    u32 r = c & 0x1F;
    u32 g = (c >> 5) & 0x1F;
    u32 b = (c >> 10) & 0x1F;

    r = r << 3 | r >> 2;
    g = g << 3 | g >> 2;
    b = b << 3 | b >> 2;
    return 0xFF000000 | r << 16 | g << 8 | b;
}

/* Reads the settings menu's art out of the built game (this package's
 * build only: the places are its). */
static void LoadRealArt(void)
{
    static const struct {
        const char *name;
        void *dst;
        size_t size;
    } want[] = {
        { "gUiSmallFontCells", sSmallCells, sizeof(sSmallCells) },
        { "gUiSmallFontAdvance", sSmallAdvance, sizeof(sSmallAdvance) },
        { "gUiSmallFontYOffset", sSmallYOffset, sizeof(sSmallYOffset) },
        { "gUiSmallFontLookup", sSmallLookup, sizeof(sSmallLookup) },
        { "gUiLargeFontCells", sLargeCells, sizeof(sLargeCells) },
        { "gUiLargeFontAdvance", sLargeAdvance, sizeof(sLargeAdvance) },
        { "gUiLargeFontYOffset", sLargeYOffset, sizeof(sLargeYOffset) },
        { "gUiLargeFontLookup", sLargeLookup, sizeof(sLargeLookup) },
        { "gUiFontPalette", sFontPalette, sizeof(sFontPalette) },
        { "gUiPauseArt", sPauseArt, sizeof(sPauseArt) },
        { "gUiPausePalette", sPausePalette, sizeof(sPausePalette) },
    };
    size_t size;
    unsigned char *exe;
    u8 sha[20];
    size_t i;
    int got = 0;

    sRealArt = 0;
    exe = ReadFile8(sGamePath, &size, 256u << 20);
    if (exe == NULL) {
        return;
    }
    Sha1(exe, size, sha);
    if (memcmp(sha, sPkg.outSha1, 20) == 0) {
        for (i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
            u32 k;

            for (k = 0; k < sPkg.artCount; k++) {
                const unsigned char *e = sPkg.art + 36 * k;
                u32 off = Le32(e + 28);
                u32 n = Le32(e + 32);

                if (strncmp((const char *)e, want[i].name, 28) == 0 && n >= want[i].size && off + want[i].size <= size) {
                    memcpy(want[i].dst, exe + off, want[i].size);
                    got++;
                }
            }
        }
    }
    free(exe);
    sRealArt = got == (int)(sizeof(want) / sizeof(want[0]));
}

enum {
    PAL_NORMAL,
    PAL_WHITE,
    PAL_GREY,
};

/* The game's font (ui.c's HostUiText): palette indices, its palettes. */
static int RealTextWidth(int large, const char *s)
{
    int w = 0;

    for (; *s; s++) {
        u8 c = (u8)*s;
        u8 g = large ? sLargeLookup[c] : sSmallLookup[c];

        w += (c == ' ' || g == 255) ? (large ? 6 : 4) : (large ? sLargeAdvance[g] : sSmallAdvance[g]);
    }
    return w;
}

static void RealText(int large, int x, int y, const char *s, int pal)
{
    u32 colors[16];
    int i;

    for (i = 0; i < 16; i++) {
        u32 c = Color15(sFontPalette[i]);
        u32 l = (((c >> 16) & 0xFF) * 30 + ((c >> 8) & 0xFF) * 59 + (c & 0xFF) * 11) / 100;
        u32 grey = 40 + l * 45 / 100;

        colors[i] = pal == PAL_NORMAL || i == 1 || i == 6 ? c
                    : pal == PAL_WHITE                     ? 0xFFFFFFFF
                                                           : 0xFF000000 | grey << 16 | grey << 8 | grey;
    }
    for (; *s; s++) {
        u8 c = (u8)*s;
        u8 g = large ? sLargeLookup[c] : sSmallLookup[c];
        const unsigned char *cell;
        int top;
        int cx;
        int cy;

        if (c == ' ' || g == 255) {
            x += large ? 6 : 4;
            continue;
        }
        cell = large ? sLargeCells[g] : sSmallCells[g];
        top = y + (large ? sLargeYOffset[g] : sSmallYOffset[g]);
        for (cy = 0; cy < 16; cy++) {
            for (cx = 0; cx < 16; cx++) {
                int px = x + cx;
                int py = top + cy;
                u8 index = cell[cy * 16 + cx];

                if (index != 0 && px >= 0 && py >= 0 && px < W && py < H) {
                    sCanvas[py * W + px] = colors[index];
                }
            }
        }
        x += large ? sLargeAdvance[g] : sSmallAdvance[g];
    }
}

/* --- Drawing: its own look, before the game is here ---------------------------- */

/* A 5x7 font of its own, drawn in the game's font colours (yellow at the
 * top to red at the bottom, a dark outline and shadow). */
static const struct {
    char c;
    const char *rows; /* 7 rows of 5 */
} sOwnFont[] = {
    { 'a', ".###." "#...#" "#...#" "#####" "#...#" "#...#" "#...#" },
    { 'b', "####." "#...#" "#...#" "####." "#...#" "#...#" "####." },
    { 'c', ".####" "#...." "#...." "#...." "#...." "#...." ".####" },
    { 'd', "####." "#...#" "#...#" "#...#" "#...#" "#...#" "####." },
    { 'e', "#####" "#...." "#...." "####." "#...." "#...." "#####" },
    { 'f', "#####" "#...." "#...." "####." "#...." "#...." "#...." },
    { 'g', ".####" "#...." "#...." "#..##" "#...#" "#...#" ".####" },
    { 'h', "#...#" "#...#" "#...#" "#####" "#...#" "#...#" "#...#" },
    { 'i', "#####" "..#.." "..#.." "..#.." "..#.." "..#.." "#####" },
    { 'j', "....#" "....#" "....#" "....#" "#...#" "#...#" ".###." },
    { 'k', "#...#" "#..#." "#.#.." "##..." "#.#.." "#..#." "#...#" },
    { 'l', "#...." "#...." "#...." "#...." "#...." "#...." "#####" },
    { 'm', "#...#" "##.##" "#.#.#" "#.#.#" "#...#" "#...#" "#...#" },
    { 'n', "#...#" "##..#" "#.#.#" "#..##" "#...#" "#...#" "#...#" },
    { 'o', ".###." "#...#" "#...#" "#...#" "#...#" "#...#" ".###." },
    { 'p', "####." "#...#" "#...#" "####." "#...." "#...." "#...." },
    { 'q', ".###." "#...#" "#...#" "#...#" "#.#.#" "#..#." ".##.#" },
    { 'r', "####." "#...#" "#...#" "####." "#.#.." "#..#." "#...#" },
    { 's', ".####" "#...." "#...." ".###." "....#" "....#" "####." },
    { 't', "#####" "..#.." "..#.." "..#.." "..#.." "..#.." "..#.." },
    { 'u', "#...#" "#...#" "#...#" "#...#" "#...#" "#...#" ".###." },
    { 'v', "#...#" "#...#" "#...#" "#...#" "#...#" ".#.#." "..#.." },
    { 'w', "#...#" "#...#" "#...#" "#.#.#" "#.#.#" "##.##" "#...#" },
    { 'x', "#...#" "#...#" ".#.#." "..#.." ".#.#." "#...#" "#...#" },
    { 'y', "#...#" "#...#" ".#.#." "..#.." "..#.." "..#.." "..#.." },
    { 'z', "#####" "....#" "...#." "..#.." ".#..." "#...." "#####" },
    { '0', ".###." "#...#" "#..##" "#.#.#" "##..#" "#...#" ".###." },
    { '1', "..#.." ".##.." "..#.." "..#.." "..#.." "..#.." ".###." },
    { '2', ".###." "#...#" "....#" "...#." "..#.." ".#..." "#####" },
    { '3', "####." "....#" "....#" ".###." "....#" "....#" "####." },
    { '4', "#...#" "#...#" "#...#" "#####" "....#" "....#" "....#" },
    { '5', "#####" "#...." "#...." "####." "....#" "....#" "####." },
    { '6', ".###." "#...." "#...." "####." "#...#" "#...#" ".###." },
    { '7', "#####" "....#" "...#." "..#.." "..#.." "..#.." "..#.." },
    { '8', ".###." "#...#" "#...#" ".###." "#...#" "#...#" ".###." },
    { '9', ".###." "#...#" "#...#" ".####" "....#" "....#" ".###." },
    { '.', "....." "....." "....." "....." "....." "....." "..#.." },
    { ',', "....." "....." "....." "....." "....." "..#.." ".#..." },
    { '\'', "..#.." "..#.." "....." "....." "....." "....." "....." },
    { '-', "....." "....." "....." ".###." "....." "....." "....." },
    { ':', "....." "..#.." "....." "....." "....." "..#.." "....." },
    { '!', "..#.." "..#.." "..#.." "..#.." "..#.." "....." "..#.." },
    { '?', ".###." "#...#" "....#" "...#." "..#.." "....." "..#.." },
    { '%', "##..#" "##..#" "...#." "..#.." ".#..." "#..##" "#..##" },
    { '+', "....." "..#.." "..#.." "#####" "..#.." "..#.." "....." },
};

static const char *OwnGlyph(char c)
{
    size_t i;

    for (i = 0; i < sizeof(sOwnFont) / sizeof(sOwnFont[0]); i++) {
        if (sOwnFont[i].c == c) {
            return sOwnFont[i].rows;
        }
    }
    return NULL;
}

static int OwnGlyphWidth(const char *rows)
{
    int w = 0;
    int r;
    int c;

    for (r = 0; r < 7; r++) {
        for (c = 0; c < 5; c++) {
            if (rows[r * 5 + c] == '#' && c + 1 > w) {
                w = c + 1;
            }
        }
    }
    return w < 1 ? 1 : w;
}

static int OwnTextWidth(int large, const char *s)
{
    int w = 0;

    for (; *s; s++) {
        const char *g = OwnGlyph(*s);

        w += g == NULL ? 4 : OwnGlyphWidth(g) + 2;
    }
    return w * (large ? 2 : 1);
}

static void Put(int x, int y, u32 c)
{
    if (x >= 0 && y >= 0 && x < W && y < H) {
        sCanvas[y * W + x] = c;
    }
}

static void OwnText(int large, int x, int y, const char *s, int pal)
{
    static const u32 ramp[7] = { 0xFFFFF070, 0xFFFFE040, 0xFFFFC830, 0xFFF8A020, 0xFFF08018, 0xFFE85818, 0xFFD03818 };
    int scale = large ? 2 : 1;

    for (; *s; s++) {
        const char *g = OwnGlyph(*s);
        int pass;

        if (g == NULL) {
            x += 4 * scale;
            continue;
        }
        /* the outline and shadow first, then the letter */
        for (pass = 0; pass < 2; pass++) {
            int r;
            int c;

            for (r = 0; r < 7; r++) {
                for (c = 0; c < 5; c++) {
                    int sx;
                    int sy;

                    if (g[r * 5 + c] != '#') {
                        continue;
                    }
                    for (sy = 0; sy < scale; sy++) {
                        for (sx = 0; sx < scale; sx++) {
                            int px = x + c * scale + sx;
                            int py = y + r * scale + sy;

                            if (pass == 0) {
                                int dx;
                                int dy;

                                for (dy = -1; dy <= 2; dy++) {
                                    for (dx = -1; dx <= 2; dx++) {
                                        Put(px + dx, py + dy, 0xFF180C08);
                                    }
                                }
                            } else {
                                u32 col = pal == PAL_WHITE ? 0xFFFFFFFF
                                          : pal == PAL_GREY ? 0xFF8A8A8A
                                                            : ramp[r];
                                Put(px, py, col);
                            }
                        }
                    }
                }
            }
        }
        x += (OwnGlyphWidth(g) + 2) * scale;
    }
}

static u32 Dark(u32 c, int num, int den)
{
    u32 r = ((c >> 16) & 0xFF) * num / den;
    u32 g = ((c >> 8) & 0xFF) * num / den;
    u32 b = (c & 0xFF) * num / den;

    return 0xFF000000 | r << 16 | g << 8 | b;
}

/* The wall: dark steel bands and bolt strips, as the borders' themed wall. */
static void OwnWall(void)
{
    int x;
    int y;

    for (y = 0; y < H; y++) {
        int r = y % 32;

        for (x = 0; x < W; x++) {
            u32 c;

            if (r < 8) {
                /* a bolt strip: blue steel, a bolt every 24 */
                c = r == 0 || r == 7 ? 0xFF1C2448 : 0xFF2C3C78;
                if ((r == 3 || r == 4) && (x % 24 == 4 || x % 24 == 5)) {
                    c = 0xFFB8C8F0;
                }
            } else {
                /* a panel: dark grey with a seam every 48 */
                int s = (r - 8) % 12;

                c = s == 0 ? 0xFF101418 : s < 3 ? 0xFF2A2E34 : 0xFF1E2228;
                if (x % 48 == 0) {
                    c = 0xFF0C0E10;
                }
            }
            sCanvas[y * W + x] = c;
        }
    }
}

/* The borders' plate: dark, see-through, a blue rim. */
static void OwnPlate(int x0, int y0, int x1, int y1)
{
    int x;
    int y;

    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            if (x < 0 || y < 0 || x >= W || y >= H) {
                continue;
            }
            if (y == y0 || y == y1 - 1 || x == x0 || x == x1 - 1) {
                sCanvas[y * W + x] = 0xFF384890;
            } else {
                sCanvas[y * W + x] = Dark(sCanvas[y * W + x], 1, 4);
            }
        }
    }
}

/* --- The page ----------------------------------------------------------------- */

static int TextWidth(int large, const char *s)
{
    return sRealArt ? RealTextWidth(large, s) : OwnTextWidth(large, s);
}

static void Text(int large, int x, int y, const char *s, int pal)
{
    if (sRealArt) {
        RealText(large, x, y, s, pal);
    } else {
        OwnText(large, x, y, s, pal);
    }
}

static void TextCentered(int large, int cx, int y, const char *s, int pal)
{
    Text(large, cx - TextWidth(large, s) / 2, y, s, pal);
}

static int Wrap(const char *s, int width, char lines[][64], int maxLines)
{
    int n = 0;

    while (*s && n < maxLines) {
        char line[64] = "";

        while (*s == ' ') {
            s++;
        }
        while (*s) {
            char word[64];
            char test[130];
            int wl = 0;

            while (s[wl] && s[wl] != ' ' && wl < 63) {
                word[wl] = s[wl];
                wl++;
            }
            word[wl] = '\0';
            snprintf(test, sizeof(test), "%s%s%s", line, line[0] ? " " : "", word);
            if (line[0] && TextWidth(0, test) > width) {
                break;
            }
            snprintf(line, sizeof(line), "%s", test);
            s += wl;
            while (*s == ' ') {
                s++;
            }
        }
        snprintf(lines[n++], 64, "%s", line);
    }
    return n;
}

enum {
    ROW_LAUNCH,
    ROW_IMPORT,
    ROW_QUIT,
    ROW_COUNT
};

static int sCursor = ROW_IMPORT;
static int sGame;            /* GameState() */
static char sStatus[256];
static int sStatusGood;

static int RowEnabled(int row)
{
    return row != ROW_LAUNCH || sGame != 0;
}

static const char *RowName(int row)
{
    static const char *const names[ROW_COUNT] = { "launch game", "import rom", "quit" };
    char rom[1024];

    if (row == ROW_IMPORT && sGame == 2 && RememberedRom(rom, sizeof(rom))) {
        return "update game";
    }
    return names[row];
}

/* The settings menu's category page: the title, the version at the top
 * left, the rows in the left box (centred on x 0x32 from y 0x45, 10
 * apart) and the status in the monitor on the right. */
static void Draw(void)
{
    char lines[7][64];
    int n;
    int i;

    if (sRealArt) {
        int x;
        int y;

        for (y = 0; y < H; y++) {
            for (x = 0; x < W; x++) {
                sCanvas[y * W + x] = Color15(sPausePalette[sPauseArt[y][x]]);
            }
        }
    } else {
        OwnWall();
        OwnPlate(4, 4, W - 4, 30);
        OwnPlate(8, 0x3E, 0x5C, 0x3E + 10 * ROW_COUNT + 10);
        OwnPlate(150, 0x26, 236, 150);
    }
    TextCentered(1, W / 2, sRealArt ? 14 : 9, "n. larged xs", PAL_NORMAL);
    if (sRealArt) {
        Text(1, 0x20, 0x26, sPkg.version, PAL_NORMAL);
    } else {
        TextCentered(0, 0x32, 0x2C, sPkg.version, PAL_NORMAL);
    }
    TextCentered(0, 194, sRealArt ? 0x2D : 0x2C, "launcher", PAL_NORMAL);
    for (i = 0; i < ROW_COUNT; i++) {
        int pal = !RowEnabled(i) ? PAL_GREY : i == sCursor ? PAL_WHITE : PAL_NORMAL;

        TextCentered(0, 0x32, 0x45 + 10 * i - (sRealArt ? 0 : 1), RowName(i), pal);
    }
    /* the monitor's screen (the game's art), or the plate (its own) */
    n = Wrap(sStatus, sRealArt ? 66 : 78, lines, sRealArt ? 6 : 10);
    for (i = 0; i < n; i++) {
        int top = sRealArt ? 108 - n * 5 : 94 - n * 5;

        TextCentered(0, 194, top + 10 * i, lines[i], sStatusGood ? PAL_WHITE : PAL_NORMAL);
    }
}

static void Refresh(void)
{
    sGame = GameState();
    LoadRealArt();
    if (sGame == 1) {
        snprintf(sStatus, sizeof(sStatus), "%s", "the game is ready!");
    } else if (sGame == 2) {
        snprintf(sStatus, sizeof(sStatus), "%s",
                 "the game here is another version: import your rom to update it.");
    } else {
        snprintf(sStatus, sizeof(sStatus), "%s",
                 "import your crash bandicoot xs rom, europe, or drop it here.");
    }
    sStatusGood = 0;
    if (!RowEnabled(sCursor)) {
        sCursor = ROW_IMPORT;
    }
}

static const char *BuildMessage(int result)
{
    switch (result) {
    case BUILD_OK:
        return "done! the game is ready.";
    case BUILD_NO_ROM:
        return "couldn't read that file.";
    case BUILD_WRONG_ROM:
        return "that isn't the right rom: it needs crash bandicoot xs, europe.";
    case BUILD_NO_WRITE:
        return "couldn't write the game here: move the launcher to a folder you own.";
    default:
        return "the launcher seems damaged. download it again.";
    }
}

/* --- The window ---------------------------------------------------------------- */

static SDL_Window *sWindow;
static SDL_Renderer *sRenderer;
static SDL_Texture *sTex;

static void Present(void)
{
    int ww;
    int wh;
    SDL_Rect r;

    Draw();
    SDL_UpdateTexture(sTex, NULL, sCanvas, W * 4);
    SDL_GetRendererOutputSize(sRenderer, &ww, &wh);
    r.w = ww / W < wh / H ? ww / W * W : wh / H * W;
    r.h = r.w * H / W;
    if (r.w <= 0) {
        r.w = ww;
        r.h = wh;
    }
    r.x = (ww - r.w) / 2;
    r.y = (wh - r.h) / 2;
    SDL_SetRenderDrawColor(sRenderer, 0, 0, 0, 255);
    SDL_RenderClear(sRenderer);
    SDL_RenderCopy(sRenderer, sTex, NULL, &r);
    SDL_RenderPresent(sRenderer);
}

static void Import(const char *path)
{
    int result;

    snprintf(sStatus, sizeof(sStatus), "%s", "making the game...");
    sStatusGood = 0;
    Present();
    result = Build(path, sGamePath);
    if (result == BUILD_OK) {
        RememberRom(path);
        Refresh();
        sCursor = ROW_LAUNCH;
    }
    snprintf(sStatus, sizeof(sStatus), "%s", BuildMessage(result));
    sStatusGood = result == BUILD_OK;
}

static int PickRom(char *out, size_t size)
{
#ifdef _WIN32
    wchar_t file[MAX_PATH * 4] = L"";
    OPENFILENAMEW ofn;
    SDL_SysWMinfo wm;

    memset(&ofn, 0, sizeof(ofn));
    SDL_VERSION(&wm.version);
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = SDL_GetWindowWMInfo(sWindow, &wm) ? wm.info.win.window : NULL;
    ofn.lpstrFilter = L"Game Boy Advance ROM (*.gba)\0*.gba\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH * 4;
    ofn.lpstrTitle = L"Choose your Crash Bandicoot XS ROM";
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) {
        return 0;
    }
    Utf8(file, out, size);
    return 1;
#else
    (void)out;
    (void)size;
    snprintf(sStatus, sizeof(sStatus), "%s", "drop your rom onto this window, or use --import.");
    return 0;
#endif
}

static int Activate(int row)
{
    char rom[1024];

    if (!RowEnabled(row)) {
        return 0;
    }
    switch (row) {
    case ROW_LAUNCH:
        if (Launch(sGamePath, 0, NULL)) {
            return 1; /* the launcher's job is done */
        }
        snprintf(sStatus, sizeof(sStatus), "%s", "couldn't start the game.");
        break;
    case ROW_IMPORT:
        if (sGame == 2 && RememberedRom(rom, sizeof(rom))) {
            Import(rom);
        } else if (PickRom(rom, sizeof(rom))) {
            Import(rom);
        }
        break;
    case ROW_QUIT:
        return 1;
    default:
        break;
    }
    return 0;
}

static int RowAt(int mx, int my, int *row)
{
    int ww;
    int wh;
    int ow;
    int oh;
    int rw;
    int rh;
    int x;
    int y;

    SDL_GetWindowSize(sWindow, &ww, &wh);
    SDL_GetRendererOutputSize(sRenderer, &ow, &oh);
    if (ww > 0 && wh > 0) {
        mx = mx * ow / ww;
        my = my * oh / wh;
    }
    rw = ow / W < oh / H ? ow / W * W : oh / H * W;
    rh = rw * H / W;
    if (rw <= 0) {
        return 0;
    }
    x = (mx - (ow - rw) / 2) * W / rw;
    y = (my - (oh - rh) / 2) * H / rh;
    if (x < 0 || x >= 0x64 || y < 0x44) {
        return 0;
    }
    *row = (y - 0x44) / 10;
    return *row < ROW_COUNT;
}

static int RunWindow(void)
{
    int done = 0;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        return 1;
    }
    sWindow = SDL_CreateWindow("N. Larged XS Launcher", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W * 4, H * 4,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (sWindow == NULL) {
        return 1;
    }
    sRenderer = SDL_CreateRenderer(sWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (sRenderer == NULL) {
        sRenderer = SDL_CreateRenderer(sWindow, -1, 0);
    }
    sTex = SDL_CreateTexture(sRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
    SDL_SetTextureScaleMode(sTex, SDL_ScaleModeNearest);
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    Refresh();
    if (sGame == 1) {
        sCursor = ROW_LAUNCH;
    }
    while (!done) {
        SDL_Event e;

        while (!done && SDL_PollEvent(&e)) {
            int row;
            int dir = 0;
            int ok = 0;
            int back = 0;

            switch (e.type) {
            case SDL_QUIT:
                done = 1;
                break;
            case SDL_KEYDOWN:
                switch (e.key.keysym.scancode) {
                case SDL_SCANCODE_UP:
                    dir = -1;
                    break;
                case SDL_SCANCODE_DOWN:
                    dir = 1;
                    break;
                case SDL_SCANCODE_RETURN:
                case SDL_SCANCODE_KP_ENTER:
                case SDL_SCANCODE_SPACE:
                case SDL_SCANCODE_X:
                    ok = 1;
                    break;
                case SDL_SCANCODE_ESCAPE:
                    back = 1;
                    break;
                default:
                    break;
                }
                break;
            case SDL_CONTROLLERDEVICEADDED:
                SDL_GameControllerOpen(e.cdevice.which);
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP:
                    dir = -1;
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                    dir = 1;
                    break;
                case SDL_CONTROLLER_BUTTON_A:
                case SDL_CONTROLLER_BUTTON_START:
                    ok = 1;
                    break;
                case SDL_CONTROLLER_BUTTON_B:
                    back = 1;
                    break;
                default:
                    break;
                }
                break;
            case SDL_MOUSEMOTION:
                if (RowAt(e.motion.x, e.motion.y, &row) && RowEnabled(row)) {
                    sCursor = row;
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (e.button.button == SDL_BUTTON_LEFT && RowAt(e.button.x, e.button.y, &row) && RowEnabled(row)) {
                    sCursor = row;
                    ok = 1;
                }
                break;
            case SDL_DROPFILE:
                Import(e.drop.file);
                SDL_free(e.drop.file);
                break;
            default:
                break;
            }
            if (dir != 0) {
                int next = sCursor;

                do {
                    next = (next + dir + ROW_COUNT) % ROW_COUNT;
                } while (!RowEnabled(next));
                sCursor = next;
            }
            if (ok && Activate(sCursor)) {
                done = 1;
            }
            if (back) {
                done = 1;
            }
        }
        if (!done) {
            Present();
            SDL_Delay(16);
        }
    }
    SDL_Quit();
    return 0;
}

/* --- The command line ---------------------------------------------------------- */

static void Usage(void)
{
    printf("N. Larged XS launcher %s: makes \"%s\" from your own Crash Bandicoot XS ROM.\n"
           "\n"
           "Usage: NLargedXSLauncher [options] [-- game options]\n"
           "  (no options)         open the launcher's window\n"
           "  -i, --import ROM     make the game from ROM, without a window\n"
           "  -o, --output PATH    where to write the game (default: next to the launcher)\n"
           "  -l, --launch         start the game (after --import, with both) and exit\n"
           "  -c, --check ROM      only check that ROM is the right one\n"
           "  -s, --status         say whether the game is here and up to date\n"
           "  -f, --force          make the game again even if it is up to date\n"
           "  -v, --version        the launcher's version and the game it makes\n"
           "  -h, --help           this\n"
           "  -- ARGS              with --launch: hand ARGS to the game (e.g. -- --fullscreen)\n"
           "\n"
           "The ROM: Crash Bandicoot XS (Europe) (En,Fr,De,Es,It,Nl), SHA-1 ",
           sPkg.version, GAME_FILE);
    {
        char hex[41];

        HexSha(sPkg.romSha1, hex);
        printf("%s.\n", hex);
    }
    printf("Exit status: 0 done, 1 failed, 2 not the right ROM, 3 no game to launch.\n");
}

#ifdef _WIN32
/* A GUI program has no console: use the one it was started from, if any. */
static void UseParentConsole(void)
{
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        printf("\n");
    }
}
#endif

int main(int argc, char **argv)
{
    const char *importRom = NULL;
    const char *output = NULL;
    const char *checkRom = NULL;
    int launch = 0;
    int status = 0;
    int force = 0;
    int gameArgc = 0;
    char **gameArgv = NULL;
    int i;

    SDL_SetMainReady();
#ifdef _WIN32
    {
        /* the arguments as UTF-8 (main's are the ANSI code page's: a
         * path with letters outside it wouldn't open) */
        int wargc;
        wchar_t **wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);

        if (wargv != NULL) {
            char **u = calloc((size_t)wargc + 1, sizeof(char *));

            for (i = 0; u != NULL && i < wargc; i++) {
                int n = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, NULL, 0, NULL, NULL);

                u[i] = malloc((size_t)(n > 0 ? n : 1));
                if (u[i] != NULL) {
                    Utf8(wargv[i], u[i], (size_t)(n > 0 ? n : 1));
                }
            }
            if (u != NULL) {
                argc = wargc;
                argv = u;
            }
            LocalFree(wargv);
        }
    }
#endif
    FindPaths();
    if (!ReadPackage()) {
        fprintf(stderr, "the launcher's package is damaged: download it again.\n");
        return 1;
    }
    if (argc <= 1) {
        return RunWindow();
    }
#ifdef _WIN32
    UseParentConsole();
#endif
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        int more = i + 1 < argc;

        if (strcmp(a, "--") == 0) {
            gameArgc = argc - i - 1;
            gameArgv = argv + i + 1;
            break;
        } else if ((strcmp(a, "-i") == 0 || strcmp(a, "--import") == 0) && more) {
            importRom = argv[++i];
        } else if ((strcmp(a, "-o") == 0 || strcmp(a, "--output") == 0) && more) {
            output = argv[++i];
        } else if ((strcmp(a, "-c") == 0 || strcmp(a, "--check") == 0) && more) {
            checkRom = argv[++i];
        } else if (strcmp(a, "-l") == 0 || strcmp(a, "--launch") == 0) {
            launch = 1;
        } else if (strcmp(a, "-s") == 0 || strcmp(a, "--status") == 0) {
            status = 1;
        } else if (strcmp(a, "-f") == 0 || strcmp(a, "--force") == 0) {
            force = 1;
        } else if (strcmp(a, "-v") == 0 || strcmp(a, "--version") == 0) {
            char hex[41];

            HexSha(sPkg.outSha1, hex);
            printf("N. Larged XS launcher %s (makes %s, %u bytes, SHA-1 %s)\n", sPkg.version, GAME_FILE,
                   (unsigned)sPkg.outSize, hex);
            return 0;
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0 || strcmp(a, "/?") == 0) {
            Usage();
            return 0;
        } else {
            fprintf(stderr, "unknown option %s (--help lists them)\n", a);
            return 1;
        }
    }
    if (output != NULL) {
        snprintf(sGamePath, sizeof(sGamePath), "%s", output);
    }
    if (checkRom != NULL) {
        int r = CheckRom(checkRom);

        printf("%s\n", r == 1 ? "the right ROM." : r == 0 ? "not the right ROM." : "can't read it.");
        if (r != 1) {
            return r == 0 ? 2 : 1;
        }
    }
    if (status) {
        int g = GameState();

        printf("%s: %s\n", sGamePath,
               g == 1 ? "here, up to date" : g == 2 ? "here, another version (import again to update)" : "not made yet");
        if (g == 0 && !importRom && !launch) {
            return 3;
        }
    }
    if (importRom != NULL) {
        if (!force && GameState() == 1) {
            printf("the game is already up to date (--force makes it again).\n");
        } else {
            int r = Build(importRom, sGamePath);

            printf("%s\n", BuildMessage(r));
            if (r != BUILD_OK) {
                return r == BUILD_WRONG_ROM ? 2 : 1;
            }
            if (output == NULL) {
                RememberRom(importRom);
            }
        }
    }
    if (launch) {
        if (!Exists(sGamePath)) {
            fprintf(stderr, "no game at %s: --import your ROM first.\n", sGamePath);
            return 3;
        }
        if (!Launch(sGamePath, gameArgc, gameArgv)) {
            fprintf(stderr, "couldn't start the game.\n");
            return 1;
        }
    }
    return 0;
}
