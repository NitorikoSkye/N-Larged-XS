/* --stream (docs/platforms.md, "Android"): the game as a process that
 * another program shows and plays. The Android app (platform/android/)
 * starts the game this way: it draws the pictures, plays the sound and
 * sends the buttons; the game runs at the GBA's pace as it does in the
 * window.
 *
 * Out, on stdout, messages: a 4-byte type, a 4-byte length
 * (little-endian), then
 * - type 1, a picture: 240x160 RGB565 pixels (the GBA's colours are 5
 *   bits a channel, so nothing is lost but green's extra bit), or 480x320
 *   at 120 frames a second (the pictures between the game's frames,
 *   interp.c, drawn at twice the size so that what moves half a pixel
 *   shows it), or 480x320 or 720x480 through Display's scalers (Scale2x,
 *   Scale3x, xBRZ: SendGame), with LCD ghosting; the length says which
 *   (always 3:2);
 * - type 2, sound: 16-bit mono samples at 48000 Hz (the block resampled
 *   from GAX2's rate as sdl.c does it);
 * - type 3, what a touch does now (HOST_TOUCH_*, one byte), when it
 *   changes: the app shows the on-screen buttons only for a level;
 * - type 4, the settings the app uses (HOST_ANDROID builds: phone.c), as
 *   little-endian s32s: the touch controls' size, opacity and whether
 *   they show in menus, the haptics, the scaling, the filter, the frames
 *   a second, the borders, the d-pad's style and whether it floats, the
 *   scanlines and the LCD grid (the app's masks over the picture); at the
 *   start and when they change;
 * - type 5, a toast's text (auto save's "saved");
 * - type 6, a themed border's bar (borders.c; SendBar below), when it
 *   changes;
 * - type 7, the touch buttons' icons: a byte each for A, B, L and R
 *   (HOST_ICON_*, HOST_ICON_DIM; touch_context.cpp), when they change in
 *   a level;
 * - type 8, open the touch buttons' layout editor (Touch's layout; no
 *   bytes): the app draws it over the settings menu, which stays open;
 * - type 9, a buzz (rumble.c): its strength (a byte, 1-255), a byte
 *   unused, its length in ms (u16).
 * In, on stdin, 8-byte messages: a type byte, a byte unused, then up to
 * three little-endian u16s:
 * - 1, the GBA's keys held (KEYINPUT's bits, set for pressed);
 * - 2, a tap at (x, y) on the picture, in its 240x160 pixels: a mouse
 *   click there (mouse_hooks.cpp; the settings menu's own when open);
 * - 3, back (the phone's back button): the settings menu's B, a menu's
 *   right click, a level's START (its pause menu);
 * - 4, open the settings menu;
 * - 5, the screen's highest refresh rate, in Hz (120 is offered from
 *   119);
 * - 6, the button prompts the controller in use calls for (HOST_PROMPTS_*;
 *   GBA for the touch screen);
 * - 7, a scroll by (s16) steps, up positive (a swipe over a menu);
 * - 8, whether a controller is connected (none: the warp room's hint
 *   says to tap it, prompts.c);
 * - 9, the screen beside the picture: the left and right bars' widths
 *   and the screen's height, in its pixels (the borders);
 * - 10, the picture's width and the room above it.
 *
 * The settings menu (ui.c) runs here as in the window: while it is open
 * the game waits, and its canvas is the picture.
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
void HostStreamInit(void)
{
}
void HostStreamVBlank(void)
{
}
void HostStreamAudio(const s8 *buf, u32 n, u32 rate)
{
    (void)buf;
    (void)n;
    (void)rate;
}
u16 HostStreamKeys(void)
{
    return 0;
}
#else

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "host_interp.h"
#include "host_ui.h"

void HostUpdateAppAnswer(int major, int minor, int patch); /* update_check.c */

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
    while (*s)
        *p++ = *s++;
    PutNum(&p, (unsigned long)sig, 10);
    s = " code ";
    while (*s)
        *p++ = *s++;
    PutNum(&p, (unsigned long)(unsigned)info->si_code, 10);
    s = " addr 0x";
    while (*s)
        *p++ = *s++;
    PutNum(&p, (unsigned long)info->si_addr, 16);
#ifdef SIGSYS
    if (sig == SIGSYS) {
        s = " syscall ";
        while (*s)
            *p++ = *s++;
        PutNum(&p, (unsigned long)(unsigned)info->si_syscall, 10);
    }
#endif
    *p++ = '\n';
    (void)!write(2, msg, (size_t)(p - msg));
    signal(sig, SIG_DFL);
    raise(sig);
}

static double Seconds(const struct timespec *t)
{
    return t->tv_sec + t->tv_nsec / 1e9;
}

/* Waits for the deadline `part` of a GBA frame on. */
static void Pace(double part)
{
    struct timespec now;
    long ns = (long)((double)CYCLES_A_FRAME / GBA_CLOCK * 1e9 * part);

    sDeadline.tv_nsec += ns;
    while (sDeadline.tv_nsec >= 1000000000L) {
        sDeadline.tv_nsec -= 1000000000L;
        sDeadline.tv_sec++;
    }
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (Seconds(&now) > Seconds(&sDeadline) + 4.0 * ns / 1e9 / part) {
        /* Far behind (the app was in the background): from now on. */
        sDeadline = now;
        return;
    }
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &sDeadline, NULL) == EINTR) {
    }
}

/* --- What the app sends --------------------------------------------------- */

static u16 sMouseKeys; /* the taps' buttons this frame (mouse_hooks.cpp) */
static u16 sPulse;     /* buttons pressed for one frame (back in a level) */
static u16 sMask;      /* held when the settings menu closed: not the game's until let go */
static int sMenuRequest;
static int sDisplayHz = 60;
static int sPadStyle = HOST_PROMPTS_GBA;
static int sTouchMode = -1; /* as last sent */
static int sSettingsDirty = 1;
extern int gHostTapHint; /* prompts.c */
/* The app's screen around the picture, in its pixels (the borders): the
 * bars' widths left and right, the screen's height, the picture's width
 * and the room above it. */
static int sBarLeft, sBarRight, sBarScreenH, sBarPictureW, sBarTop;

#ifdef HOST_ANDROID
void HostBordersPhone(int left, int right, int screenH, int picW, int top,
                      void (*send)(int side, int unit, int w, int h,
                                   const u32 *px)); /* borders.c */
void HostBordersPhoneReset(void);
#define BordersReset() HostBordersPhoneReset()
#else
#define BordersReset() ((void)0)
#endif

int HostStreamDisplayHz(void)
{
    return sDisplayHz;
}

void HostStreamSettingsChanged(void)
{
    sSettingsDirty = 1;
}

static u16 Arg(const u8 *m, int i)
{
    return (u16)(m[2 + 2 * i] | m[3 + 2 * i] << 8);
}

/* One message from the app. `menu`: the settings menu is open. */
static void Handle(const u8 *m, int menu)
{
    switch (m[0]) {
    case 1:
        if (menu) {
            /* the D-pad and A and B work the menu (on presses) */
            u16 now = Arg(m, 0);
            u16 pressed = now & ~sKeys;

            if (pressed & DPAD_UP)
                HostUiInput(HOST_UI_UP);
            if (pressed & DPAD_DOWN)
                HostUiInput(HOST_UI_DOWN);
            if (pressed & DPAD_LEFT)
                HostUiInput(HOST_UI_LEFT);
            if (pressed & DPAD_RIGHT)
                HostUiInput(HOST_UI_RIGHT);
            if (pressed & A_BUTTON)
                HostUiInput(HOST_UI_OK);
            if (pressed & (B_BUTTON | START_BUTTON))
                HostUiInput(HOST_UI_BACK);
        }
        sKeys = Arg(m, 0);
        break;
    case 2:
        {
            int x = (s16)Arg(m, 0);
            int y = (s16)Arg(m, 1);

            if (menu) {
                HostUiMouse(HOST_UI_MOUSE_MOVE, x, y);
                HostUiMouse(HOST_UI_MOUSE_LEFT, x, y);
                break;
            }
            gHostMouse.inside = x >= 0 && x < HOST_SCREEN_WIDTH && y >= 0 && y < HOST_SCREEN_HEIGHT;
            gHostMouse.x = x;
            gHostMouse.y = y;
            gHostMouse.moved = 1;
            gHostMouse.left = 1;
            break;
        }
    case 3:
        if (menu) {
            HostUiInput(HOST_UI_BACK);
        } else if (sTouchMode == HOST_TOUCH_PLAY) {
            sPulse |= START_BUTTON;
        } else if (sTouchMode == HOST_TOUCH_MENU) {
            gHostMouse.inside = 1;
            gHostMouse.right = 1;
        }
        break;
    case 4:
        sMenuRequest = 1;
        break;
    case 5:
        sDisplayHz = Arg(m, 0);
        sSettingsDirty = 1;
        break;
    case 6:
        sPadStyle = Arg(m, 0);
        break;
    case 8:
        gHostTapHint = Arg(m, 0) == 0;
        break;
    case 9:
        sBarLeft = Arg(m, 0);
        sBarRight = Arg(m, 1);
        sBarScreenH = Arg(m, 2);
        BordersReset();
        break;
    case 10:
        sBarPictureW = Arg(m, 0);
        sBarTop = Arg(m, 1);
        BordersReset();
        break;
#ifdef HOST_ANDROID
    case 11:
        /* the update check's answer (update_check.c): the latest
         * release's numbers, or 0xFFFF for none */
        HostUpdateAppAnswer(Arg(m, 0), Arg(m, 1), Arg(m, 2));
        break;
#endif
    case 7:
        {
            int steps = (s16)Arg(m, 0);

            if (menu) {
                for (; steps > 0; steps--)
                    HostUiMouse(HOST_UI_MOUSE_WHEEL_UP, 0, 0);
                for (; steps < 0; steps++)
                    HostUiMouse(HOST_UI_MOUSE_WHEEL_DOWN, 0, 0);
            } else {
                gHostMouse.wheel += steps;
            }
            break;
        }
    default:
        break;
    }
}

/* Everything the app sent since the last frame (messages split across
 * reads are kept). */
static void ReadInput(int menu)
{
    static u8 msg[8];
    static int have;
    u8 in[256];
    ssize_t n;

    while ((n = read(0, in, sizeof(in))) > 0) {
        ssize_t j;

        for (j = 0; j < n; j++) {
            msg[have++] = in[j];
            if (have == 8) {
                Handle(msg, menu);
                have = 0;
            }
        }
    }
    if (n == 0) {
        HostExit(0, "stream closed");
    }
}

/* --- What goes to the app ---------------------------------------------------- */

#ifdef HOST_ANDROID
/* A buzz for the phone or the controller in use (message 9; rumble.c). */
void HostStreamRumble(u32 strength, u32 ms)
{
    u8 m[4];

    if (gHostStream) {
        m[0] = (u8)(strength > 255 ? 255 : strength);
        m[1] = 0;
        m[2] = (u8)ms;
        m[3] = (u8)(ms >> 8);
        Message(9, m, sizeof(m));
    }
}

/* Touch's layout: the app's editor (message 8). */
void HostStreamEditLayout(void)
{
    if (gHostStream) {
        Message(8, "", 0);
    }
}

/* The update check (update_check.c, message 10): 0 asks the app for the
 * latest release (it answers with message 11), 1 opens the releases
 * page. */
void HostStreamUpdate(int op)
{
    u8 b = (u8)op;

    if (gHostStream) {
        Message(10, &b, 1);
    }
}

void HostStreamToast(const char *text)
{
    if (gHostStream) {
        Message(5, text, (u32)strlen(text));
    }
}
#endif

static void SendState(int mode)
{
    u8 b = (u8)mode;

    if (mode != sTouchMode) {
        sTouchMode = mode;
        Message(3, &b, 1);
    }
#ifdef HOST_ANDROID
    {
        u8 icons[4];

        if (HostTouchIcons(icons, mode == HOST_TOUCH_PLAY)) {
            Message(7, icons, sizeof(icons));
        }
    }
#endif
    if (sSettingsDirty) {
        s32 v[12];

        sSettingsDirty = 0;
        v[0] = gHostSettings.touchSize;
        v[1] = gHostSettings.touchOpacity;
        v[2] = gHostSettings.touchInMenus;
        v[3] = gHostSettings.haptics;
        v[4] = gHostSettings.scaling;
        v[5] = gHostSettings.filter;
        v[6] = gHostFpsValues[gHostSettings.frameRate];
        v[7] = gHostSettings.borders;
        v[8] = gHostSettings.dpadStyle;
        v[9] = gHostSettings.dpadFloating;
        v[10] = gHostSettings.scanlines;
        v[11] = gHostSettings.lcdGrid;
        Message(4, v, sizeof(v));
    }
}

/* The GBA screen's colours (Graphics' colours, as sdl.c's), for 15-bit
 * colours to RGB565. */
static u16 *sLut;

static void BuildLut(void)
{
    static const double m[3][3] = {
        { 0.86, 0.14, 0.00 },
        { 0.06, 0.82, 0.12 },
        { 0.08, 0.12, 0.80 },
    };
    int i;

    if (sLut != NULL || (sLut = malloc(32768 * sizeof(u16))) == NULL) {
        return;
    }
    for (i = 0; i < 32768; i++) {
        double in[3];
        int q[3];
        int c;

        for (c = 0; c < 3; c++) {
            int v5 = i >> (10 - 5 * c) & 31;

            in[c] = pow((v5 * 8 + v5 / 4) / 255.0, 2.2);
        }
        for (c = 0; c < 3; c++) {
            double v = m[c][0] * in[0] + m[c][1] * in[1] + m[c][2] * in[2];

            v = v < 0 ? 0 : v > 1 ? 1 : v;
            v = pow(v * 0.95, 1.0 / 2.2 * 1.05);
            q[c] = (int)(v * 255.0 + 0.5);
        }
        sLut[i] = (u16)((q[0] >> 3) << 11 | (q[1] >> 2) << 5 | (q[2] >> 3));
    }
}

/* ARGB8888 pixels as an RGB565 picture message. */
static void SendPicture(const u32 *src, int w, int h)
{
    static u16 *picture;
    static int size;
    int colours = gHostSettings.colors;
    int i;

    if (size < w * h) {
        free(picture);
        picture = malloc((size_t)w * h * sizeof(u16));
        size = picture != NULL ? w * h : 0;
        if (picture == NULL) {
            return;
        }
    }
    if (colours) {
        BuildLut();
        colours = sLut != NULL;
    }
    for (i = 0; i < w * h; i++) {
        u32 c = src[i];

        if (colours) {
            picture[i] = sLut[(c >> 9 & 0x7C00) | (c >> 6 & 0x03E0) | (c >> 3 & 0x001F)];
        } else {
            picture[i] = (u16)((c >> 8 & 0xF800) | (c >> 5 & 0x07E0) | (c >> 3 & 0x001F));
        }
    }
    Message(1, picture, (u32)(w * h * sizeof(u16)));
}

#ifdef HOST_ANDROID
/* A themed border's bar (borders.c), type 6: s32 side (0 left, 1 right, 2
 * above and, flipped, below), its pixel size on the screen, its width and
 * height, then its RGB565 pixels. */
static void SendBar(int side, int unit, int w, int h, const u32 *px)
{
    static u8 *buf;
    static size_t size;
    size_t need = 16 + (size_t)w * h * 2;
    s32 head[4];
    int i;

    if (need > size) {
        free(buf);
        buf = malloc(need);
        size = buf != NULL ? need : 0;
        if (buf == NULL) {
            return;
        }
    }
    head[0] = side;
    head[1] = unit;
    head[2] = w;
    head[3] = h;
    memcpy(buf, head, 16);
    for (i = 0; i < w * h; i++) {
        u32 c = px[i];
        u16 v = (u16)((c >> 8 & 0xF800) | (c >> 5 & 0x07E0) | (c >> 3 & 0x001F));

        buf[16 + 2 * i] = (u8)v;
        buf[17 + 2 * i] = (u8)(v >> 8);
    }
    Message(6, buf, (u32)need);
}
#endif

/* --- The pictures between frames (120 a second) --------------------------- */

struct Part {
    HostLineRun run;
    void *ctx;
    int first;
    int end;
};

static void *RunPartThread(void *arg)
{
    struct Part *p = arg;

    p->run(p->ctx, p->first, p->end);
    return NULL;
}

/* The lines shared out to the phone's cores (at most 4). */
static void Parallel(HostLineRun run, void *ctx, int lines)
{
    static int cores;
    struct Part parts[4];
    pthread_t threads[4];
    int started[4] = { 0, 0, 0, 0 };
    int n;
    int i;

    if (cores == 0) {
        long c = sysconf(_SC_NPROCESSORS_ONLN);

        cores = c < 1 ? 1 : c > 4 ? 4 : (int)c;
    }
    n = cores;
    for (i = 0; i < n; i++) {
        parts[i].run = run;
        parts[i].ctx = ctx;
        parts[i].first = lines * i / n;
        parts[i].end = lines * (i + 1) / n;
    }
    for (i = 1; i < n; i++) {
        started[i] = pthread_create(&threads[i], NULL, RunPartThread, &parts[i]) == 0;
        if (!started[i]) {
            RunPartThread(&parts[i]);
        }
    }
    RunPartThread(&parts[0]);
    for (i = 1; i < n; i++) {
        if (started[i]) {
            pthread_join(threads[i], NULL);
        }
    }
}

#define HD_SCALE 2

/* --- The game's pictures through the filters (Display's, as on PC) -------- */

/* The pixel-art scalers take the GBA's own 240x160 (the pictures between
 * frames are drawn at 1x for them), here at a size the pipe carries well:
 * Scale2x 2x, Scale3x 3x, the cartoon filter xBRZ 3x (the app scales the
 * rest, smoothly), cartoon soft xBRZ 2x; at 120 a second all at most 2x
 * (twice the pictures). 1: none. */
static int UpscaleFactor(void)
{
    int f = gHostSettings.filter;
    int fast = gHostSettings.frameRate == HOST_FPS_120 && sDisplayHz >= 119;

    switch (f) {
    case HOST_FILTER_SCALE2X:
    case HOST_FILTER_CARTOON_SOFT:
        return 2;
    case HOST_FILTER_SCALE3X:
    case HOST_FILTER_CARTOON:
        return fast ? 2 : 3;
    default:
        return 1;
    }
}

struct XbrzJob {
    int factor;
    const u32 *src;
    u32 *dst;
};

static void XbrzLines(void *ctx, int first, int end)
{
    struct XbrzJob *j = ctx;

    HostXbrzScale(j->factor, j->src, j->dst, HOST_SCREEN_WIDTH, HOST_SCREEN_HEIGHT, first, end);
}

/* A game picture (w x h; 240x160 for the scalers) through the scaler and
 * LCD ghosting, then out. `dt`: the 60ths of a second since the last one
 * (ghosting keeps half a picture each 60th, as on PC). */
static void SendGame(const u32 *src, int w, int h, double dt)
{
    static u32 scaled[HOST_SCREEN_WIDTH_MAX * 3 * HOST_SCREEN_HEIGHT * 3];
    static u32 *ghost;
    static int ghostW;
    static int ghostH;
    int k = w == HOST_SCREEN_WIDTH ? UpscaleFactor() : 1;
    const u32 *p = src;
    size_t n;

    if (k > 1) {
        int f = gHostSettings.filter;

        if (f == HOST_FILTER_CARTOON || f == HOST_FILTER_CARTOON_SOFT) {
            struct XbrzJob job;

            HostXbrzWarm();
            job.factor = k;
            job.src = src;
            job.dst = scaled;
            Parallel(XbrzLines, &job, h);
        } else if (k == 2) {
            HostScale2x(src, scaled, w, h);
        } else {
            HostScale3x(src, scaled, w, h);
        }
        p = scaled;
        w *= k;
        h *= k;
    }
    n = (size_t)w * h;
    if (gHostSettings.ghosting) {
        u32 keep = (u32)(256.0 * pow(0.5, dt < 0.05 ? 0.05 : dt > 4 ? 4 : dt) + 0.5);
        size_t i;

        if (ghost == NULL || ghostW != w || ghostH != h) {
            free(ghost);
            ghost = malloc(n * 4);
            if (ghost == NULL) {
                SendPicture(p, w, h);
                return;
            }
            memcpy(ghost, p, n * 4);
            ghostW = w;
            ghostH = h;
        }
        for (i = 0; i < n; i++) {
            u32 a = p[i];
            u32 b = ghost[i];
            u32 rb = ((a & 0xFF00FF) * (256 - keep) + (b & 0xFF00FF) * keep) >> 8 & 0xFF00FF;
            u32 g = ((a & 0x00FF00) * (256 - keep) + (b & 0x00FF00) * keep) >> 8 & 0x00FF00;

            ghost[i] = rb | g;
        }
        p = ghost;
    }
    SendPicture(p, w, h);
}

static void SendBetween(double alpha)
{
    static u32 hd[HOST_SCREEN_WIDTH_MAX * HD_SCALE * HOST_SCREEN_HEIGHT * HD_SCALE];
    /* with a scaler: the in-between picture at 1x, for it to scale */
    int scale = UpscaleFactor() > 1 ? 1 : HD_SCALE;

    HostInterpRender(alpha, scale, hd, Parallel);
    SendGame(hd, HOST_SCREEN_WIDTH * scale, HOST_SCREEN_HEIGHT * scale, 0.5);
}

/* --- Each frame ------------------------------------------------------------ */

#ifdef HOST_ANDROID
void HostPromptsPhone(int style); /* prompts.c */
#endif

/* The settings menu: the game waits while it is open. */
static void Menu(void)
{
    static u32 canvas[HOST_UI_WIDTH_MAX * HOST_UI_HEIGHT];

    while (HostUiIsOpen()) {
        HostUiDraw(canvas);
        SendPicture(canvas, HOST_UI_WIDTH, HOST_UI_HEIGHT);
        SendState(HOST_TOUCH_SETTINGS);
        Pace(1.0);
        ReadInput(1);
    }
    sMask = sKeys;
    HostInterpReset();
    clock_gettime(CLOCK_MONOTONIC, &sDeadline);
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
#ifdef HOST_ANDROID
    HostConfigInit();
#endif
    clock_gettime(CLOCK_MONOTONIC, &sDeadline);
}

void HostStreamVBlank(void)
{
    int mode;

    ReadInput(0);
#ifdef HOST_ANDROID
    if (sMenuRequest) {
        sMenuRequest = 0;
        HostUiOpen();
    }
    if (HostUiIsOpen()) {
        Menu();
    }
    HostPromptsPhone(gHostSettings.prompts == HOST_PROMPTS_AUTO ? sPadStyle
                                                                : gHostSettings.prompts);
#else
    sMenuRequest = 0;
#endif
    mode = HostTouchMode();
    SendState(mode);
#ifdef HOST_ANDROID
    HostBordersPhone(sBarLeft, sBarRight, sBarScreenH, sBarPictureW, sBarTop, SendBar);
#endif
    /* the keys the game reads this frame: the app's, the taps', a pulse */
    sMask &= sKeys;
    sMouseKeys = HostMouseGameKeys() | sPulse;
    sPulse = 0;
    if (gHostSettings.frameRate == HOST_FPS_120 && sDisplayHz >= 119) {
        HostInterpCapture();
        if (HostInterpReady()) {
            SendBetween(0.5);
            Pace(0.5);
            SendBetween(1.0);
            Pace(0.5);
            return;
        }
    } else {
        HostInterpReset();
    }
    SendGame(gHostScreen, HOST_SCREEN_WIDTH, HOST_SCREEN_HEIGHT, 1.0);
    Pace(1.0);
}

static void Resampled(const s32 *buf, u32 n, u32 rate, int shift)
{
    static s16 out[4096];
    double step = (double)rate / OUT_RATE;
    s32 gain = gHostSettings.master * 256 / 100;
    u32 m = 0;

    if (n == 0) {
        return;
    }
#ifndef HOST_ANDROID
    gain = 256;
#endif
    while (sPhase < n && m < sizeof(out) / sizeof(out[0])) {
        u32 i = (u32)sPhase;
        double f = sPhase - i;
        s32 a = i == 0 ? sLast : buf[i - 1];
        s32 v = (s32)((a + (buf[i] - a) * f) * (1 << shift)) * gain >> 8;

        out[m++] = (s16)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        sPhase += step;
    }
    sPhase -= n;
    if (sPhase < 0) {
        sPhase = 0;
    }
    sLast = buf[n - 1];
    Message(2, out, m * sizeof(out[0]));
}

void HostStreamAudio(const s8 *buf, u32 n, u32 rate)
{
    static s32 wide[4096];
    u32 i;

    if (n > 4096) {
        n = 4096;
    }
    for (i = 0; i < n; i++) {
        wide[i] = buf[i];
    }
    Resampled(wide, n, rate, 8);
}

/* High-quality sound (gax_dsp.c): 16-bit blocks. */
void HostStreamAudio16(const s16 *buf, u32 n, u32 rate)
{
    static s32 wide[4096];
    u32 i;

    if (n > 4096) {
        n = 4096;
    }
    for (i = 0; i < n; i++) {
        wide[i] = buf[i];
    }
    Resampled(wide, n, rate, 0);
}

u16 HostStreamKeys(void)
{
    return (u16)((sKeys & ~sMask) | sMouseKeys);
}

#endif /* _WIN32 */
