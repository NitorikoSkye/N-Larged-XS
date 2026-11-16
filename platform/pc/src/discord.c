/* Discord Rich Presence (docs/platforms.md, "Discord"): the level being
 * played and the player's progress on their Discord profile, while the
 * Discord app runs on the same computer.
 *
 * Discord's local IPC, no SDK: a named pipe (\\.\pipe\discord-ipc-N) on
 * Windows, a Unix socket ($XDG_RUNTIME_DIR/discord-ipc-N, and the Flatpak
 * and Snap ones) elsewhere. Each message is a 4-byte opcode and a 4-byte
 * length (little-endian), then JSON: a handshake (op 0, the application's
 * id), then SET_ACTIVITY commands (op 1). What Discord sends back is read
 * and dropped.
 *
 * The game thread (HostDiscordFrame, once a second from the front end's
 * VBlank) puts together what to show: the level (from the PlayRoom wrap,
 * level_pick.c; held for 3 seconds between rooms so that a room change
 * doesn't flicker), or the warp room (the LevelSelect::Run wrap), or the
 * menus; and the progress, counted by the game's own functions as the
 * pause menu counts it. A thread of its own connects (again every 10
 * seconds while Discord isn't there) and sends it when it changes, at
 * most every 5 seconds (Discord allows 5 updates in 20 seconds).
 *
 * The pictures are the application's art assets, uploaded to it under
 * these names (--discord-assets writes them, borders.c): the level
 * select's world pictures in their porthole, and Crash's portrait. */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "gba/gba.h"
#include "host_platform.h"
#include "host_ui.h"

/* HOST_DISCORD_*: what the settings menu shows. */
int gHostDiscordStatus;
s32 gHostInWarpRoom;

#ifdef HOST_SDL

#include <SDL.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

extern s32 gHostPlayingLevel;
extern const int gUiLevelCount;
extern const char *const gUiLevelNames[];
extern const signed char gUiLevelPicture[];
extern void *gLevelState;
s32 CountCrystals(const void *progress);
s32 CountGems(const void *progress);
s32 CountRelics(const void *progress);
s32 GetCompletionPercent(const void *progress);

/* The world pictures (gUiPictures' order): the asset's name and an emoji. */
static const struct {
    const char *asset;
    const char *emoji;
    int boss;
} sWorlds[] = {
    { "world_snow", "\xE2\x9D\x84\xEF\xB8\x8F", 0 },      /* snowflake */
    { "world_jungle", "\xF0\x9F\x8C\xB4", 0 },            /* palm tree */
    { "world_sewer", "\xF0\x9F\xA7\xAA", 0 },             /* test tube */
    { "world_underwater", "\xF0\x9F\x8C\x8A", 0 },        /* wave */
    { "world_sky", "\xE2\x9C\x88\xEF\xB8\x8F", 0 },       /* aeroplane */
    { "world_space", "\xF0\x9F\x9A\x80", 0 },             /* rocket */
    { "boss_tiny", "\xE2\x9A\x94\xEF\xB8\x8F", 1 },       /* crossed swords */
    { "boss_dingodile", "\xE2\x9A\x94\xEF\xB8\x8F", 1 },
    { "boss_ngin", "\xE2\x9A\x94\xEF\xB8\x8F", 1 },
    { "boss_cortex", "\xE2\x9A\x94\xEF\xB8\x8F", 1 },
};
#define EMOJI_MEGA_MIX "\xF0\x9F\x91\xBE"   /* alien monster */
#define EMOJI_WARP     "\xF0\x9F\x8C\x80"   /* cyclone */
#define EMOJI_MENU     "\xF0\x9F\x8E\xAE"   /* game controller */
#define EMOJI_PERCENT  "\xF0\x9F\x92\xAF"   /* hundred points */
#define EMOJI_CRYSTAL  "\xF0\x9F\x94\xAE"   /* crystal ball */
#define EMOJI_GEM      "\xF0\x9F\x92\x8E"   /* gem stone */
#define EMOJI_RELIC    "\xF0\x9F\x8F\x86"   /* trophy */
#define DOT            " \xC2\xB7 "         /* middle dot */

/* What the thread sends: the activity's JSON, and a serial for "new". */
static SDL_mutex *sLock;
static char sWanted[1024];
static int sWantedSerial;
static SDL_Thread *sThread;
static SDL_atomic_t sQuit;

/* The game thread's side. */
static int sShownLevel = -1;
static int sShownWarp = -1;
static u32 sLevelSeen;
static long long sSince;
static u32 sFrames;

/* "jungle jam" -> "Jungle Jam", "no-fly zone" -> "No-Fly Zone". */
static void TitleCase(char *out, size_t size, const char *s)
{
    size_t i = 0;
    int start = 1;

    for (; *s != '\0' && i + 1 < size; s++) {
        char c = *s;

        if (start && c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        start = c == ' ' || c == '-' || c == '.';
        out[i++] = c;
    }
    out[i] = '\0';
}

/* A JSON string's contents: quotes and backslashes escaped. */
static void JsonEscape(char *out, size_t size, const char *s)
{
    size_t i = 0;

    for (; *s != '\0' && i + 2 < size; s++) {
        if (*s == '"' || *s == '\\') {
            out[i++] = '\\';
        }
        out[i++] = *s;
    }
    out[i] = '\0';
}

static void Compose(char *json, size_t size)
{
    char details[160];
    char state[160];
    char largeText[96];
    char name[64];
    const char *large = "crash";
    const u8 *progress = gLevelState;
    int level = sShownLevel;

    state[0] = '\0';
    largeText[0] = '\0';
    if (level >= 0 && level < gUiLevelCount) {
        int pic = gUiLevelPicture[level];

        TitleCase(name, sizeof(name), gUiLevelNames[level]);
        if (pic >= 0 && pic < (int)(sizeof(sWorlds) / sizeof(sWorlds[0]))) {
            snprintf(details, sizeof(details), "%s %s%s", sWorlds[pic].emoji, sWorlds[pic].boss ? "Boss: " : "",
                     name);
            large = sWorlds[pic].asset;
        } else {
            snprintf(details, sizeof(details), "%s %s", EMOJI_MEGA_MIX, name);
        }
        snprintf(largeText, sizeof(largeText), "%s", name);
    } else if (sShownWarp) {
        snprintf(details, sizeof(details), "%s In the warp room", EMOJI_WARP);
        snprintf(largeText, sizeof(largeText), "Warp room");
    } else {
        snprintf(details, sizeof(details), "%s In the menus", EMOJI_MENU);
        snprintf(largeText, sizeof(largeText), "Crash Bandicoot XS");
    }
    if (gHostSettings.discordProgress && progress != NULL) {
        snprintf(state, sizeof(state), "%s %d%%" DOT "%s %d/20" DOT "%s %d/28" DOT "%s %d/20", EMOJI_PERCENT,
                 (int)GetCompletionPercent(progress), EMOJI_CRYSTAL, (int)CountCrystals(progress), EMOJI_GEM,
                 (int)CountGems(progress), EMOJI_RELIC, (int)CountRelics(progress));
    }
    {
        char d[200];
        char s[200];
        char lt[120];

        JsonEscape(d, sizeof(d), details);
        JsonEscape(s, sizeof(s), state);
        JsonEscape(lt, sizeof(lt), largeText);
        snprintf(json, size,
                 "{\"details\":\"%s\"%s%s%s,\"timestamps\":{\"start\":%lld},"
                 "\"assets\":{\"large_image\":\"%s\",\"large_text\":\"%s\","
                 "\"small_image\":\"crash\",\"small_text\":\"%s - %s\"}}",
                 d, s[0] ? ",\"state\":\"" : "", s, s[0] ? "\"" : "", sSince, large, lt, HOST_PORT_NAME,
                 HOST_PORT_VERSION);
    }
}

/* --- the connection ------------------------------------------------------ */

#ifdef _WIN32
typedef HANDLE Conn;
#define NO_CONN INVALID_HANDLE_VALUE

static Conn OpenConn(void)
{
    int i;

    for (i = 0; i < 10; i++) {
        char name[64];
        HANDLE h;

        snprintf(name, sizeof(name), "\\\\.\\pipe\\discord-ipc-%d", i);
        h = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            return h;
        }
    }
    return NO_CONN;
}

static void CloseConn(Conn c)
{
    CloseHandle(c);
}

static int WriteConn(Conn c, const void *p, u32 n)
{
    DWORD w;

    return WriteFile(c, p, n, &w, NULL) && w == n;
}

/* Bytes ready to read: -1 when the connection is gone. */
static int AvailConn(Conn c)
{
    DWORD avail = 0;

    if (!PeekNamedPipe(c, NULL, 0, NULL, &avail, NULL)) {
        return -1;
    }
    return (int)avail;
}

static int ReadConn(Conn c, void *p, u32 n)
{
    DWORD r;

    return ReadFile(c, p, n, &r, NULL) && r == n;
}

static long long Pid(void)
{
    return (long long)GetCurrentProcessId();
}
#else
typedef int Conn;
#define NO_CONN (-1)

static Conn OpenConn(void)
{
    static const char *const vars[] = { "XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP" };
    static const char *const subs[] = { "", "app/com.discordapp.Discord/", "snap.discord/", ".flatpak/dev.vencord.Vesktop/xdg-run/" };
    size_t v;
    size_t s;
    int i;

    for (v = 0; v <= sizeof(vars) / sizeof(vars[0]); v++) {
        const char *dir = v < sizeof(vars) / sizeof(vars[0]) ? getenv(vars[v]) : "/tmp";

        if (dir == NULL) {
            continue;
        }
        for (s = 0; s < sizeof(subs) / sizeof(subs[0]); s++) {
            for (i = 0; i < 10; i++) {
                struct sockaddr_un addr;
                int fd;

                memset(&addr, 0, sizeof(addr));
                addr.sun_family = AF_UNIX;
                snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/%sdiscord-ipc-%d", dir, subs[s], i);
                fd = socket(AF_UNIX, SOCK_STREAM, 0);
                if (fd < 0) {
                    return NO_CONN;
                }
                if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
                    return fd;
                }
                close(fd);
            }
        }
    }
    return NO_CONN;
}

static void CloseConn(Conn c)
{
    close(c);
}

static int WriteConn(Conn c, const void *p, u32 n)
{
    const u8 *b = p;

    while (n > 0) {
        ssize_t w = send(c, b, n, MSG_NOSIGNAL);

        if (w < 0 && errno == EINTR) {
            continue;
        }
        if (w <= 0) {
            return 0;
        }
        b += w;
        n -= (u32)w;
    }
    return 1;
}

static int AvailConn(Conn c)
{
    struct pollfd p;
    u8 b;

    p.fd = c;
    p.events = POLLIN;
    if (poll(&p, 1, 0) <= 0) {
        return 0;
    }
    if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        return -1;
    }
    return recv(c, &b, 1, MSG_PEEK | MSG_DONTWAIT) == 1 ? 1 : -1;
}

static int ReadConn(Conn c, void *p, u32 n)
{
    u8 *b = p;

    while (n > 0) {
        ssize_t r = recv(c, b, n, 0);

        if (r < 0 && errno == EINTR) {
            continue;
        }
        if (r <= 0) {
            return 0;
        }
        b += r;
        n -= (u32)r;
    }
    return 1;
}

static long long Pid(void)
{
    return (long long)getpid();
}
#endif

static int SendFrame(Conn c, u32 op, const char *json)
{
    u32 n = (u32)strlen(json);
    u8 head[8];

    head[0] = (u8)op;
    head[1] = (u8)(op >> 8);
    head[2] = (u8)(op >> 16);
    head[3] = (u8)(op >> 24);
    head[4] = (u8)n;
    head[5] = (u8)(n >> 8);
    head[6] = (u8)(n >> 16);
    head[7] = (u8)(n >> 24);
    return WriteConn(c, head, 8) && WriteConn(c, json, n);
}

/* One message from Discord, its payload dropped: its opcode, or -1 when
 * the connection is gone. */
static int ReadFrame(Conn c)
{
    u8 head[8];
    u8 drop[512];
    u32 n;

    if (!ReadConn(c, head, 8)) {
        return -1;
    }
    n = head[4] | head[5] << 8 | head[6] << 16 | (u32)head[7] << 24;
    while (n > 0) {
        u32 k = n < sizeof(drop) ? n : (u32)sizeof(drop);

        if (!ReadConn(c, drop, k)) {
            return -1;
        }
        n -= k;
    }
    return head[0] | head[1] << 8 | head[2] << 16 | (int)head[3] << 24;
}

/* Waits up to `ms` for a message: 1 there is one, 0 none, -1 gone. */
static int WaitFrame(Conn c, int ms)
{
    while (ms > 0 && !SDL_AtomicGet(&sQuit)) {
        int a = AvailConn(c);

        if (a != 0) {
            return a > 0 ? 1 : -1;
        }
        SDL_Delay(50);
        ms -= 50;
    }
    return 0;
}

static int Connect(Conn *out)
{
    char json[128];
    Conn c = OpenConn();

    if (c == NO_CONN) {
        return HOST_DISCORD_NOT_RUNNING;
    }
    snprintf(json, sizeof(json), "{\"v\":1,\"client_id\":\"%s\"}", gHostSettings.discordAppId);
    /* The answer: READY (op 1), or CLOSE (op 2) for an id Discord doesn't know. */
    if (!SendFrame(c, 0, json) || WaitFrame(c, 5000) <= 0) {
        CloseConn(c);
        return HOST_DISCORD_NOT_RUNNING;
    }
    if (ReadFrame(c) != 1) {
        CloseConn(c);
        return HOST_DISCORD_BAD_ID;
    }
    *out = c;
    return HOST_DISCORD_CONNECTED;
}

static int Thread(void *unused)
{
    Conn c = NO_CONN;
    u32 nextTry = 0;
    u32 lastSend = 0;
    int sent = -1;
    int nonce = 0;

    (void)unused;
    while (!SDL_AtomicGet(&sQuit)) {
        int on = gHostSettings.discord && gHostSettings.discordAppId[0] != '\0';
        u32 now = SDL_GetTicks();

        if (!on) {
            /* Closing the connection takes the status off the profile. */
            if (c != NO_CONN) {
                CloseConn(c);
                c = NO_CONN;
            }
            gHostDiscordStatus = !gHostSettings.discord ? HOST_DISCORD_OFF : HOST_DISCORD_NO_ID;
            nextTry = 0;
            SDL_Delay(250);
            continue;
        }
        if (c == NO_CONN) {
            if ((s32)(now - nextTry) >= 0) {
                int st = Connect(&c);

                gHostDiscordStatus = st;
                sent = -1;
                /* An id Discord refused is tried again only if it changes:
                 * the menu says so. */
                nextTry = now + (st == HOST_DISCORD_BAD_ID ? 60000 : 10000);
            }
            SDL_Delay(250);
            continue;
        }
        /* Discord's answers: read and dropped. */
        while (AvailConn(c) > 0) {
            if (ReadFrame(c) < 0) {
                break;
            }
        }
        if (AvailConn(c) < 0) {
            CloseConn(c);
            c = NO_CONN;
            gHostDiscordStatus = HOST_DISCORD_NOT_RUNNING;
            continue;
        }
        if (sent != sWantedSerial && (sent == -1 || now - lastSend >= 5000)) {
            char cmd[1400];
            int serial;

            SDL_LockMutex(sLock);
            serial = sWantedSerial;
            snprintf(cmd, sizeof(cmd),
                     "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%lld,\"activity\":%s},\"nonce\":\"%d\"}", Pid(),
                     sWanted, nonce + 1);
            SDL_UnlockMutex(sLock);
            if (serial == 0) {
                /* nothing composed yet */
            } else if (SendFrame(c, 1, cmd)) {
                nonce++;
                sent = serial;
                lastSend = now;
            } else {
                CloseConn(c);
                c = NO_CONN;
                gHostDiscordStatus = HOST_DISCORD_NOT_RUNNING;
                continue;
            }
        }
        SDL_Delay(250);
    }
    if (c != NO_CONN) {
        CloseConn(c);
    }
    return 0;
}

void HostDiscordInit(void)
{
    sLock = SDL_CreateMutex();
    sSince = (long long)time(NULL);
    sThread = SDL_CreateThread(Thread, "discord", NULL);
}

void HostDiscordQuit(void)
{
    if (sThread != NULL) {
        SDL_AtomicSet(&sQuit, 1);
        SDL_WaitThread(sThread, NULL);
        sThread = NULL;
    }
}

/* Each VBlank (sdl.c): once a second, what to show. */
void HostDiscordFrame(void)
{
    char json[sizeof(sWanted)];
    int level = sShownLevel;
    int warp = gHostInWarpRoom != 0;

    if (sLock == NULL || ++sFrames % 60 != 0) {
        return;
    }
    if (gHostPlayingLevel >= 0) {
        level = gHostPlayingLevel;
        sLevelSeen = sFrames;
    } else if (warp || sFrames - sLevelSeen > 180) {
        level = -1;
    }
    if (level != sShownLevel || (level < 0 && warp != sShownWarp)) {
        /* A new place: its time starts now. */
        sSince = (long long)time(NULL);
    }
    sShownLevel = level;
    sShownWarp = warp;
    Compose(json, sizeof(json));
    SDL_LockMutex(sLock);
    if (strcmp(json, sWanted) != 0) {
        snprintf(sWanted, sizeof(sWanted), "%s", json);
        sWantedSerial++;
    }
    SDL_UnlockMutex(sLock);
}

#else

void HostDiscordInit(void) {}
void HostDiscordQuit(void) {}
void HostDiscordFrame(void) {}

void HostBordersWriteDiscordAssets(const char *dir)
{
    (void)dir;
    fprintf(stderr, "--discord-assets: built without SDL\n");
}

#endif
