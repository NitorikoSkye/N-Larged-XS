/* "Add to Steam" (docs/platforms.md, "Steam"; Extras' Add to steam): puts
 * the port in the Steam library as a non-Steam game, with its art, from
 * the settings menu.
 *
 * Steam keeps a user's non-Steam games in
 * <Steam>/userdata/<account id>/config/shortcuts.vdf (binary VDF: a map is
 * 0x00 key, its entries, 0x08; a string 0x01 key value; a 32-bit number
 * 0x02 key and 4 bytes, little-endian) and their art in that config's
 * grid/ folder, named by the shortcut's app id: <id>p (the capsule), <id>
 * (the wide capsule), <id>_hero, <id>_logo; the icon is any file the
 * shortcut names. The shortcut's app id is CRC-32(exe + name) with the top
 * bit set, as Steam makes them.
 *
 * The shortcut named "Crash Bandicoot XS" is made, or if one is there,
 * pointed at this executable (a new version is a new file name), keeping
 * its app id: its play time, its collections and its art stay. Its art is
 * written again either way (steam-art/, steam_art.S). Steam writes
 * shortcuts.vdf from memory when it quits, so it must be closed first:
 * the action says so and changes nothing while it runs. The account is
 * the one loginusers.vdf marks MostRecent, or every account there is.
 * shortcuts.vdf is copied to shortcuts.vdf.bak before it is written.
 *
 * Windows reads Steam's place from the registry; Linux from ~/.steam,
 * ~/.local/share/Steam or Flatpak's. Under Wine or Proton the registry's
 * Steam isn't the real one, so it says to add it from Windows (or with
 * the Linux build) instead (HOST_STEAM_IGNORE_WINE=1 goes on: the tests'
 * fake Steam). */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "host_platform.h"
#include "host_ui.h"

#define STEAM_NAME "Crash Bandicoot XS"

extern const unsigned char gSteamArtPortrait[], gSteamArtPortraitEnd[];
extern const unsigned char gSteamArtWide[], gSteamArtWideEnd[];
extern const unsigned char gSteamArtHero[], gSteamArtHeroEnd[];
extern const unsigned char gSteamArtLogo[], gSteamArtLogoEnd[];
extern const unsigned char gSteamArtIcon[], gSteamArtIconEnd[];

/* --- Files, by UTF-8 paths ------------------------------------------------- */

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

static void MakeDir(const char *path)
{
#ifdef _WIN32
    wchar_t *w = Wide(path);

    if (w != NULL) {
        CreateDirectoryW(w, NULL);
    }
    free(w);
#else
    mkdir(path, 0755);
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

static int IsDir(const char *path)
{
#ifdef _WIN32
    wchar_t *w = Wide(path);
    DWORD a = w != NULL ? GetFileAttributesW(w) : INVALID_FILE_ATTRIBUTES;

    free(w);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;

    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static unsigned char *ReadAll(const char *path, size_t *size)
{
    FILE *f = OpenU8(path, "rb");
    unsigned char *buf = NULL;
    size_t cap = 0;
    size_t n = 0;

    *size = 0;
    if (f == NULL) {
        return NULL;
    }
    for (;;) {
        size_t got;

        if (n == cap) {
            unsigned char *b = realloc(buf, cap = cap ? cap * 2 : 65536);

            if (b == NULL) {
                free(buf);
                fclose(f);
                return NULL;
            }
            buf = b;
        }
        got = fread(buf + n, 1, cap - n, f);
        if (got == 0) {
            break;
        }
        n += got;
    }
    fclose(f);
    *size = n;
    return buf;
}

static int WriteAll(const char *path, const unsigned char *data, size_t size)
{
    FILE *f = OpenU8(path, "wb");
    int ok;

    if (f == NULL) {
        return 0;
    }
    ok = fwrite(data, 1, size, f) == size;
    return fclose(f) == 0 && ok;
}

/* --- Binary VDF ------------------------------------------------------------ */

enum {
    VDF_MAP = 0,
    VDF_STRING = 1,
    VDF_INT = 2,
    VDF_END = 8,
};

struct Vdf {
    int type;
    char *key;
    char *str;            /* VDF_STRING */
    unsigned char raw[8]; /* the numbers (types 2-7), as they were */
    int rawSize;
    struct Vdf *child; /* VDF_MAP */
    struct Vdf *next;
};

static void VdfFree(struct Vdf *n)
{
    while (n != NULL) {
        struct Vdf *next = n->next;

        VdfFree(n->child);
        free(n->key);
        free(n->str);
        free(n);
        n = next;
    }
}

static char *Dup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = malloc(n);

    if (d != NULL) {
        memcpy(d, s, n);
    }
    return d;
}

/* A NUL-terminated string at *pos, or NULL past the end. */
static char *TakeString(const unsigned char *buf, size_t size, size_t *pos)
{
    size_t start = *pos;

    while (*pos < size && buf[*pos] != 0) {
        (*pos)++;
    }
    if (*pos >= size) {
        return NULL;
    }
    (*pos)++;
    return Dup((const char *)buf + start);
}

/* A map's entries up to its 0x08; NULL with *ok 0 if the file is bad. */
static struct Vdf *VdfParse(const unsigned char *buf, size_t size, size_t *pos, int *ok)
{
    struct Vdf *head = NULL;
    struct Vdf **tail = &head;

    while (*pos < size) {
        int type = buf[(*pos)++];
        struct Vdf *n;
        static const int rawSizes[8] = { 0, 0, 4, 4, 4, 0, 4, 8 };

        if (type == VDF_END) {
            return head;
        }
        if (type > 7 || type == 5) {
            break;
        }
        n = calloc(1, sizeof(*n));
        if (n == NULL) {
            break;
        }
        *tail = n;
        tail = &n->next;
        n->type = type;
        n->key = TakeString(buf, size, pos);
        if (n->key == NULL) {
            break;
        }
        if (type == VDF_MAP) {
            n->child = VdfParse(buf, size, pos, ok);
            if (!*ok) {
                VdfFree(head);
                return NULL;
            }
        } else if (type == VDF_STRING) {
            n->str = TakeString(buf, size, pos);
            if (n->str == NULL) {
                break;
            }
        } else {
            n->rawSize = rawSizes[type];
            if (*pos + (size_t)n->rawSize > size) {
                break;
            }
            memcpy(n->raw, buf + *pos, (size_t)n->rawSize);
            *pos += (size_t)n->rawSize;
        }
    }
    *ok = 0;
    VdfFree(head);
    return NULL;
}

struct Out {
    unsigned char *buf;
    size_t size;
    size_t cap;
    int bad;
};

static void Put(struct Out *o, const void *data, size_t n)
{
    if (o->size + n > o->cap) {
        size_t cap = o->cap ? o->cap : 4096;
        unsigned char *b;

        while (cap < o->size + n) {
            cap *= 2;
        }
        b = realloc(o->buf, cap);
        if (b == NULL) {
            o->bad = 1;
            return;
        }
        o->buf = b;
        o->cap = cap;
    }
    memcpy(o->buf + o->size, data, n);
    o->size += n;
}

static void PutByte(struct Out *o, int b)
{
    unsigned char c = (unsigned char)b;

    Put(o, &c, 1);
}

static void VdfWrite(struct Out *o, const struct Vdf *n)
{
    for (; n != NULL; n = n->next) {
        PutByte(o, n->type);
        Put(o, n->key, strlen(n->key) + 1);
        if (n->type == VDF_MAP) {
            VdfWrite(o, n->child);
            PutByte(o, VDF_END);
        } else if (n->type == VDF_STRING) {
            Put(o, n->str, strlen(n->str) + 1);
        } else {
            Put(o, n->raw, (size_t)n->rawSize);
        }
    }
}

static int SameKey(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = (char)(*a >= 'A' && *a <= 'Z' ? *a + 32 : *a);
        char y = (char)(*b >= 'A' && *b <= 'Z' ? *b + 32 : *b);

        if (x != y) {
            return 0;
        }
    }
    return *a == *b;
}

static struct Vdf *VdfFind(struct Vdf *map, const char *key)
{
    struct Vdf *n;

    for (n = map != NULL ? map->child : NULL; n != NULL; n = n->next) {
        if (SameKey(n->key, key)) {
            return n;
        }
    }
    return NULL;
}

/* Sets map's `key` (adding it at the end) to a string or a number. */
static struct Vdf *VdfSlot(struct Vdf *map, const char *key, int type)
{
    struct Vdf *n = VdfFind(map, key);
    struct Vdf **tail;

    if (n != NULL && n->type == type) {
        return n;
    }
    if (n == NULL) {
        n = calloc(1, sizeof(*n));
        if (n == NULL) {
            return NULL;
        }
        n->key = Dup(key);
        for (tail = &map->child; *tail != NULL; tail = &(*tail)->next) {
        }
        *tail = n;
    }
    VdfFree(n->child);
    n->child = NULL;
    free(n->str);
    n->str = NULL;
    n->type = type;
    return n;
}

static void VdfSetString(struct Vdf *map, const char *key, const char *value)
{
    struct Vdf *n = VdfSlot(map, key, VDF_STRING);

    if (n != NULL) {
        free(n->str);
        n->str = Dup(value);
    }
}

static void VdfSetInt(struct Vdf *map, const char *key, unsigned int value)
{
    struct Vdf *n = VdfSlot(map, key, VDF_INT);

    if (n != NULL) {
        n->rawSize = 4;
        n->raw[0] = (unsigned char)value;
        n->raw[1] = (unsigned char)(value >> 8);
        n->raw[2] = (unsigned char)(value >> 16);
        n->raw[3] = (unsigned char)(value >> 24);
    }
}

static void VdfSetMap(struct Vdf *map, const char *key)
{
    if (VdfFind(map, key) == NULL) {
        VdfSlot(map, key, VDF_MAP);
    }
}

static unsigned int VdfInt(const struct Vdf *n)
{
    return n == NULL || n->type != VDF_INT
               ? 0
               : (unsigned int)n->raw[0] | (unsigned int)n->raw[1] << 8 |
                     (unsigned int)n->raw[2] << 16 | (unsigned int)n->raw[3] << 24;
}

/* --- Where Steam is ---------------------------------------------------------- */

static unsigned int Crc32(const char *s)
{
    unsigned int crc = 0xFFFFFFFFu;

    for (; *s; s++) {
        int k;

        crc ^= (unsigned char)*s;
        for (k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
        }
    }
    return ~crc;
}

static int UnderWine(void)
{
#ifdef _WIN32
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");

    /* the tests run the Windows code under Wine with a fake Steam */
    if (getenv("HOST_STEAM_IGNORE_WINE") != NULL) {
        return 0;
    }
    return ntdll != NULL && GetProcAddress(ntdll, "wine_get_version") != NULL;
#else
    return 0;
#endif
}

static int FindSteam(char *out, size_t size)
{
#ifdef _WIN32
    wchar_t w[MAX_PATH];
    DWORD bytes = sizeof(w);

    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ,
                     NULL, w, &bytes) != ERROR_SUCCESS) {
        return 0;
    }
    Utf8(w, out, size);
    return IsDir(out);
#else
    static const char *const places[] = {
        "/.steam/steam",
        "/.local/share/Steam",
        "/.var/app/com.valvesoftware.Steam/.local/share/Steam",
    };
    const char *home = getenv("HOME");
    size_t i;

    for (i = 0; home != NULL && i < sizeof(places) / sizeof(places[0]); i++) {
        char dir[1024];

        snprintf(out, size, "%s%s", home, places[i]);
        snprintf(dir, sizeof(dir), "%s/userdata", out);
        if (IsDir(dir)) {
            return 1;
        }
    }
    return 0;
#endif
}

static int SteamRunning(const char *steam)
{
#ifdef _WIN32
    DWORD pid = 0;
    DWORD bytes = sizeof(pid);
    HANDLE h;
    int running;

    (void)steam;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", L"pid",
                     RRF_RT_REG_DWORD, NULL, &pid, &bytes) != ERROR_SUCCESS ||
        pid == 0) {
        return 0;
    }
    h = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (h == NULL) {
        return 0;
    }
    running = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return running;
#else
    const char *home = getenv("HOME");
    char path[1024];
    FILE *f;
    int pid = 0;

    (void)steam;
    if (home == NULL) {
        return 0;
    }
    snprintf(path, sizeof(path), "%s/.steam/steam.pid", home);
    f = fopen(path, "r");
    if (f == NULL) {
        return 0;
    }
    if (fscanf(f, "%d", &pid) != 1) {
        pid = 0;
    }
    fclose(f);
    return pid > 0 && kill(pid, 0) == 0;
#endif
}

/* The account that logged in last (loginusers.vdf's MostRecent "1"), as
 * its userdata folder's number; 0 if none says so. */
static unsigned int RecentAccount(const char *steam)
{
    char path[1024];
    size_t size;
    unsigned char *text;
    unsigned long long id = 0;
    unsigned long long current = 0;
    int depth = 0;
    size_t i;
    char tokens[2][64];
    int ntok = 0;

    snprintf(path, sizeof(path), "%s/config/loginusers.vdf", steam);
    text = ReadAll(path, &size);
    if (text == NULL) {
        return 0;
    }
    for (i = 0; i < size; i++) {
        if (text[i] == '{') {
            depth++;
            ntok = 0;
        } else if (text[i] == '}') {
            depth--;
            ntok = 0;
        } else if (text[i] == '"') {
            size_t j = i + 1;
            size_t n = 0;

            while (j < size && text[j] != '"') {
                if (n < 63) {
                    tokens[ntok & 1][n++] = (char)text[j];
                }
                j++;
            }
            tokens[ntok & 1][n] = 0;
            i = j;
            ntok++;
            if (depth == 1 && ntok == 1) {
                current = strtoull(tokens[0], NULL, 10);
            }
            if (depth == 2 && (ntok & 1) == 0) {
                if (SameKey(tokens[0], "MostRecent") && strcmp(tokens[1], "1") == 0) {
                    id = current;
                }
                ntok = 0;
            }
        }
    }
    free(text);
    return id > 76561197960265728ull ? (unsigned int)(id - 76561197960265728ull) : 0;
}

/* Every account's userdata folder number. */
static int Accounts(const char *steam, unsigned int *ids, int max)
{
    char path[1024];
    int n = 0;
    unsigned int recent = RecentAccount(steam);

    snprintf(path, sizeof(path), "%s/userdata/%u", steam, recent);
    if (recent != 0 && IsDir(path)) {
        ids[0] = recent;
        return 1;
    }
#ifdef _WIN32
    {
        WIN32_FIND_DATAW fd;
        HANDLE h;
        wchar_t *w;

        snprintf(path, sizeof(path), "%s/userdata/*", steam);
        w = Wide(path);
        h = w != NULL ? FindFirstFileW(w, &fd) : INVALID_HANDLE_VALUE;
        free(w);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                unsigned int id = (unsigned int)wcstoul(fd.cFileName, NULL, 10);

                if (id != 0 && (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && n < max) {
                    ids[n++] = id;
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
#else
    {
        DIR *d;
        struct dirent *e;

        snprintf(path, sizeof(path), "%s/userdata", steam);
        d = opendir(path);
        while (d != NULL && (e = readdir(d)) != NULL) {
            unsigned int id = (unsigned int)strtoul(e->d_name, NULL, 10);

            if (id != 0 && n < max) {
                ids[n++] = id;
            }
        }
        if (d != NULL) {
            closedir(d);
        }
    }
#endif
    return n;
}

static int ExePath(char *out, size_t size)
{
#ifdef _WIN32
    wchar_t w[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(NULL, w, MAX_PATH * 2);

    if (n == 0 || n >= MAX_PATH * 2) {
        return 0;
    }
    Utf8(w, out, size);
    return 1;
#else
    ssize_t n = readlink("/proc/self/exe", out, size - 1);

    if (n <= 0) {
        return 0;
    }
    out[n] = 0;
    return 1;
#endif
}

/* --- Adding --------------------------------------------------------------- */

static void WriteArt(const char *grid, unsigned int appid, const char *name, const char *ext,
                     const unsigned char *start, const unsigned char *end, char *written,
                     size_t writtenSize)
{
    static const char *const exts[] = { ".png", ".jpg", ".jpeg", ".webp" };
    char path[1200];
    size_t i;

    /* the same picture under another extension would come first */
    for (i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
        if (strcmp(exts[i], ext) != 0) {
            snprintf(path, sizeof(path), "%s/%u%s%s", grid, appid, name, exts[i]);
            RemoveU8(path);
        }
    }
    snprintf(path, sizeof(path), "%s/%u%s%s", grid, appid, name, ext);
    WriteAll(path, start, (size_t)(end - start));
    if (written != NULL) {
        snprintf(written, writtenSize, "%s", path);
    }
}

/* One account: 1 added, 2 updated, 0 failed. */
static int AddForAccount(const char *steam, unsigned int account, const char *exe)
{
    char config[1100];
    char path[1200];
    char tmp[1210];
    char grid[1200];
    char quoted[1200];
    char dir[1100];
    char icon[1300];
    size_t size = 0;
    unsigned char *buf;
    struct Vdf *root = NULL;
    struct Vdf *list;
    struct Vdf *entry = NULL;
    struct Vdf *n;
    struct Out out = { 0 };
    unsigned int appid;
    int updated = 0;
    int index = 0;
    char *slash;
    int ok = 1;

    snprintf(config, sizeof(config), "%s/userdata/%u/config", steam, account);
    MakeDir(config);
    snprintf(path, sizeof(path), "%s/shortcuts.vdf", config);
    buf = ReadAll(path, &size);
    if (buf != NULL && size > 0) {
        size_t pos = 0;

        root = calloc(1, sizeof(*root));
        if (root == NULL) {
            free(buf);
            return 0;
        }
        root->type = VDF_MAP;
        root->key = Dup("");
        root->child = VdfParse(buf, size, &pos, &ok);
        if (!ok) {
            /* a file this can't read is left alone */
            VdfFree(root);
            free(buf);
            return 0;
        }
        /* the backup, before anything changes */
        snprintf(tmp, sizeof(tmp), "%s.bak", path);
        WriteAll(tmp, buf, size);
    } else {
        root = calloc(1, sizeof(*root));
        if (root == NULL) {
            return 0;
        }
        root->type = VDF_MAP;
        root->key = Dup("");
    }
    free(buf);
    VdfSetMap(root, "shortcuts");
    list = VdfFind(root, "shortcuts");

    for (n = list->child; n != NULL; n = n->next) {
        struct Vdf *name = VdfFind(n, "AppName");

        if (n->type == VDF_MAP && name != NULL && name->type == VDF_STRING &&
            strcmp(name->str, STEAM_NAME) == 0) {
            entry = n;
        }
        if (atoi(n->key) >= index) {
            index = atoi(n->key) + 1;
        }
    }
    snprintf(quoted, sizeof(quoted), "\"%s\"", exe);
    snprintf(dir, sizeof(dir), "%s", exe);
    slash = strrchr(dir, '/');
#ifdef _WIN32
    if (strrchr(dir, '\\') > slash) {
        slash = strrchr(dir, '\\');
    }
#endif
    if (slash != NULL) {
        *slash = 0;
    }
    if (entry != NULL) {
        updated = 1;
        appid = VdfInt(VdfFind(entry, "appid"));
    } else {
        char key[16];

        entry = calloc(1, sizeof(*entry));
        if (entry == NULL) {
            VdfFree(root);
            return 0;
        }
        snprintf(key, sizeof(key), "%d", index);
        entry->type = VDF_MAP;
        entry->key = Dup(key);
        for (n = list->child; n != NULL && n->next != NULL; n = n->next) {
        }
        if (n == NULL) {
            list->child = entry;
        } else {
            n->next = entry;
        }
        appid = 0;
    }
    if (appid == 0) {
        char seed[2500];

        snprintf(seed, sizeof(seed), "%s%s", quoted, STEAM_NAME);
        appid = Crc32(seed) | 0x80000000u;
    }

    snprintf(grid, sizeof(grid), "%s/grid", config);
    MakeDir(grid);
    WriteArt(grid, appid, "p", ".jpg", gSteamArtPortrait, gSteamArtPortraitEnd, NULL, 0);
    WriteArt(grid, appid, "", ".jpg", gSteamArtWide, gSteamArtWideEnd, NULL, 0);
    WriteArt(grid, appid, "_hero", ".jpg", gSteamArtHero, gSteamArtHeroEnd, NULL, 0);
    WriteArt(grid, appid, "_logo", ".png", gSteamArtLogo, gSteamArtLogoEnd, NULL, 0);
    WriteArt(grid, appid, "_icon", ".png", gSteamArtIcon, gSteamArtIconEnd, icon, sizeof(icon));
#ifdef _WIN32
    for (slash = icon; *slash; slash++) {
        if (*slash == '/') {
            *slash = '\\';
        }
    }
#endif

    VdfSetInt(entry, "appid", appid);
    VdfSetString(entry, "AppName", STEAM_NAME);
    VdfSetString(entry, "Exe", quoted);
    snprintf(tmp, sizeof(tmp), "\"%s\"", dir);
    VdfSetString(entry, "StartDir", tmp);
    VdfSetString(entry, "icon", icon);
    if (!updated) {
        VdfSetString(entry, "ShortcutPath", "");
        VdfSetString(entry, "LaunchOptions", "");
        VdfSetInt(entry, "IsHidden", 0);
        VdfSetInt(entry, "AllowDesktopConfig", 1);
        VdfSetInt(entry, "AllowOverlay", 1);
        VdfSetInt(entry, "OpenVR", 0);
        VdfSetInt(entry, "Devkit", 0);
        VdfSetString(entry, "DevkitGameID", "");
        VdfSetInt(entry, "DevkitOverrideAppID", 0);
        VdfSetInt(entry, "LastPlayTime", 0);
        VdfSetString(entry, "FlatpakAppID", "");
        VdfSetMap(entry, "tags");
    }

    VdfWrite(&out, root->child);
    PutByte(&out, VDF_END);
    VdfFree(root);
    if (out.bad) {
        free(out.buf);
        return 0;
    }
    snprintf(tmp, sizeof(tmp), "%s.new", path);
    ok = WriteAll(tmp, out.buf, out.size) && ReplaceU8(tmp, path);
    free(out.buf);
    return ok ? 1 + updated : 0;
}

/* Can it be done now (the menu greys the row otherwise): Steam there,
 * closed, and not under Wine or Proton. Looked at once a second, as the
 * menu asks every frame. */
int HostSteamCanAdd(void)
{
    static time_t sAt;
    static int sCan;
    time_t now = time(NULL);

    if (now != sAt) {
        char steam[1024];

        sAt = now;
        sCan = !UnderWine() && FindSteam(steam, sizeof(steam)) && !SteamRunning(steam);
    }
    return sCan;
}

/* The settings menu's action: says how it went in a toast. */
void HostSteamAdd(void)
{
    char steam[1024];
    char exe[1024];
    unsigned int accounts[16];
    int count;
    int i;
    int result = 0;

    if (UnderWine()) {
        HostToast("under proton: add it from windows or the linux build");
        return;
    }
    if (!FindSteam(steam, sizeof(steam))) {
        HostToast("steam wasn't found");
        return;
    }
    if (SteamRunning(steam)) {
        HostToast("close steam first, then try again");
        return;
    }
    if (!ExePath(exe, sizeof(exe))) {
        HostToast("couldn't find the game's file");
        return;
    }
    count = Accounts(steam, accounts, 16);
    if (count == 0) {
        HostToast("log in to steam once first");
        return;
    }
    for (i = 0; i < count; i++) {
        int r = AddForAccount(steam, accounts[i], exe);

        if (r > result) {
            result = r;
        }
    }
    HostToast(result == 2   ? "steam shortcut updated"
              : result == 1 ? "added to steam - open steam to see it"
                            : "couldn't write to steam");
}
