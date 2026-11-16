/* --stream (docs/platforms.md, "Android"): the game as a process that
 * another program shows and plays. The Android app (platform/android/)
 * starts the game this way: it draws the pictures, plays the sound and
 * sends the buttons; the game runs at the GBA's pace as it does in the
 * window.
 *
 * Out, on stdout, a message per frame and per sound block: a 4-byte
 * type, a 4-byte length (little-endian), then
 * - type 1, a picture: 240x160 RGB565 pixels (the GBA's colours are 5
 *   bits a channel, so nothing is lost but green's extra bit);
 * - type 2, sound: 16-bit mono samples at 48000 Hz (the block resampled
 *   from GAX2's rate as sdl.c does it).
 * In, on stdin: 2-byte messages, the GBA's keys held (KEYINPUT's bits,
 * set for pressed). Only the last one read counts.
 *
 * The pace is the GBA's, 280896 cycles of 16.78 MHz a frame, kept
 * against a running deadline. While the app stops reading (it is in the
 * background), the game stops at its next message, the pipe full, and
 * starts again from then when the app reads again. A pipe closed by the
 * app ends the run (the save is already written: eeprom.c writes it at
 * the VBlank after each change). */

#include "gba/gba.h"
#include "host_platform.h"

int gHostStream;

#ifdef _WIN32
/* Windows has no --stream (the Android app's): the window only. */
void HostStreamInit(void) {}
void HostStreamVBlank(void) {}
void HostStreamAudio(const s8 *buf, u32 n, u32 rate) { (void)buf; (void)n; (void)rate; }
u16 HostStreamKeys(void) { return 0; }
#else

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define GBA_CLOCK      16777216
#define CYCLES_A_FRAME 280896
#define OUT_RATE       48000

static int sOut = 1;
static u16 sKeys;
static struct timespec sDeadline;
static double sPhase;
static s32 sLast;

static void WriteAll(const void *data, size_t n)
{
    const u8 *p = data;

    while (n > 0) {
        ssize_t w = write(sOut, p, n);

        if (w < 0 && errno == EINTR) {
            continue;
        }
        if (w <= 0) {
            HostExit(0, "stream closed");
        }
        p += w;
        n -= (size_t)w;
    }
}

static void Message(u32 type, const void *data, u32 n)
{
    u8 head[8];

    head[0] = type;
    head[1] = type >> 8;
    head[2] = type >> 16;
    head[3] = type >> 24;
    head[4] = n;
    head[5] = n >> 8;
    head[6] = n >> 16;
    head[7] = n >> 24;
    WriteAll(head, 8);
    WriteAll(data, n);
}

/* A fatal signal, said on stderr (the app shows the log's end): the
 * signal, the address and, for a system call the phone refused, its
 * number. Only write(): nothing else is safe here. */
static void PutNum(char **p, unsigned long v, int base)
{
    char t[24];
    int n = 0;

    do {
        t[n++] = "0123456789abcdef"[v % base];
        v /= base;
    } while (v != 0);
    while (n > 0) {
        *(*p)++ = t[--n];
    }
}

static void Fatal(int sig, siginfo_t *info, void *ctx)
{
    char msg[160];
    char *p = msg;
    const char *s = "core: fatal signal ";

    (void)ctx;
    while (*s) *p++ = *s++;
    PutNum(&p, (unsigned long)sig, 10);
    s = " code ";
    while (*s) *p++ = *s++;
    PutNum(&p, (unsigned long)(unsigned)info->si_code, 10);
    s = " addr 0x";
    while (*s) *p++ = *s++;
    PutNum(&p, (unsigned long)info->si_addr, 16);
#ifdef SIGSYS
    if (sig == SIGSYS) {
        s = " syscall ";
        while (*s) *p++ = *s++;
        PutNum(&p, (unsigned long)(unsigned)info->si_syscall, 10);
    }
#endif
    *p++ = '\n';
    (void)!write(2, msg, (size_t)(p - msg));
    signal(sig, SIG_DFL);
    raise(sig);
}

void HostStreamInit(void)
{
    static const int sigs[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGSYS };
    struct sigaction sa;
    size_t i;

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = Fatal;
    sa.sa_flags = SA_SIGINFO;
    for (i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) {
        sigaction(sigs[i], &sa, NULL);
    }
    fprintf(stderr, "core: started\n");
    fflush(stderr);
    signal(SIGPIPE, SIG_IGN);
    /* The stream keeps stdout; what the game prints goes to stderr. */
    sOut = dup(1);
    dup2(2, 1);
    fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
    clock_gettime(CLOCK_MONOTONIC, &sDeadline);
}

static double Seconds(const struct timespec *t)
{
    return t->tv_sec + t->tv_nsec / 1e9;
}

static void Pace(void)
{
    struct timespec now;
    long ns = (long)((double)CYCLES_A_FRAME / GBA_CLOCK * 1e9);

    sDeadline.tv_nsec += ns;
    while (sDeadline.tv_nsec >= 1000000000L) {
        sDeadline.tv_nsec -= 1000000000L;
        sDeadline.tv_sec++;
    }
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (Seconds(&now) > Seconds(&sDeadline) + 4.0 * ns / 1e9) {
        /* Far behind (the app was in the background): from now on. */
        sDeadline = now;
        return;
    }
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &sDeadline, NULL) == EINTR) {
    }
}

void HostStreamVBlank(void)
{
    static u16 picture[HOST_SCREEN_WIDTH * HOST_SCREEN_HEIGHT];
    u8 in[64];
    ssize_t n;
    int i;

    for (i = 0; i < HOST_SCREEN_WIDTH * HOST_SCREEN_HEIGHT; i++) {
        u32 c = gHostScreen[i];

        picture[i] = (u16)((c >> 8 & 0xF800) | (c >> 5 & 0x07E0) | (c >> 3 & 0x001F));
    }
    Message(1, picture, sizeof(picture));
    /* The keys: the last whole message read (a message split across
     * reads keeps its first byte). */
    while ((n = read(0, in, sizeof(in))) > 0) {
        static int half = -1;
        ssize_t j;

        for (j = 0; j < n; j++) {
            if (half < 0) {
                half = in[j];
            } else {
                sKeys = (u16)(half | in[j] << 8);
                half = -1;
            }
        }
    }
    if (n == 0) {
        HostExit(0, "stream closed");
    }
    Pace();
}

void HostStreamAudio(const s8 *buf, u32 n, u32 rate)
{
    static s16 out[4096];
    double step = (double)rate / OUT_RATE;
    u32 m = 0;

    if (n == 0) {
        return;
    }
    while (sPhase < n && m < sizeof(out) / sizeof(out[0])) {
        u32 i = (u32)sPhase;
        double f = sPhase - i;
        s32 a = i == 0 ? sLast : buf[i - 1];

        out[m++] = (s16)((a + (buf[i] - a) * f) * 256);
        sPhase += step;
    }
    sPhase -= n;
    if (sPhase < 0) {
        sPhase = 0;
    }
    sLast = buf[n - 1];
    Message(2, out, m * sizeof(out[0]));
}

u16 HostStreamKeys(void)
{
    return sKeys;
}

#endif /* _WIN32 */
