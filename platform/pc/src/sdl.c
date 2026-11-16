/* The pc platform's front end: an SDL2 window showing the PPU's picture,
 * the keyboard and gamepads as the GBA's keys, the sound, and the PC
 * port's own settings (host_ui.h; docs/platforms.md, "The settings menu").
 *
 * Built with SDL when platform.mk finds it (HOST_SDL=1); without it the
 * executable runs headless only, and this file is the few functions that
 * say so.
 *
 * Each VBlank (irq.c, line 160) HostFrontendVBlank:
 * - reads the events: the settings menu (~, or a controller's Guide
 *   button) and the window losing the focus stop the game there until
 *   they are done; the hotkeys (fullscreen, F12's screenshot);
 * - at 60 FPS shows the picture the PPU just finished and waits until
 *   the frame's time is up: the GBA shows 16777216 / 280896 = 59.7275
 *   frames a second, kept against a running deadline so that short waits
 *   even out (more than four frames behind, it starts again from now);
 * - above 60 FPS (frame interpolation, host_interp.h) shows pictures
 *   between the last two frames until the frame's time is up, each as far
 *   between them as its time is through the frame, so the game keeps the
 *   GBA's pace and the picture moves at the display's;
 * - the keys held (the bindings, gHostSettings) set the keys the game
 *   reads at its next frame (HostFrontendKeys, input.c). The fast-forward
 *   key held runs without waiting.
 *
 * A picture goes to the window through the graphics settings: the GBA
 * screen's colours (a table), the pixel-art scalers (Scale2x, Scale3x),
 * LCD ghosting (the last picture shown, fading), then the GPU: nearest,
 * linear, or "sharp" (nearest to the next integer size, then linear),
 * and the scanlines' and LCD grid's masks multiplied over it. The FPS
 * counter, the inputs and the menu are drawn on top, in the game's fonts.
 *
 * The sound (audio.c's blocks, a frame's samples at the song's mix rate)
 * goes to SDL's default audio device, at the rate it likes (48 kHz asked
 * for), queued (SDL_QueueAudio) about three frames ahead, at the master
 * volume (the music and sound effects volumes are applied in GAX2's
 * mixer, gHostGaxVolume). The blocks come at the frame rate of the pacing
 * above, the device takes them at its own clock, and the two drift apart;
 * so the conversion to the device's rate (linear interpolation) runs up to
 * 0.5% faster or slower to keep the queue at its length, as emulators do.
 * When the queue runs dry anyway (a slow host), a frame of silence starts
 * it again; while it is far too long (fast forward), blocks are dropped. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gba/gba.h"
#include "host_interp.h"
#include "host_platform.h"
#include "host_ui.h"

#ifndef HOST_SDL

/* Without SDL the one front end is --stream's (stream.c, the Android
 * app's): with it off, the run is headless. */
extern int gHostStream;
void HostStreamInit(void);
void HostStreamVBlank(void);
u16 HostStreamKeys(void);
void HostStreamAudio(const s8 *buf, u32 n, u32 rate);

int HostFrontendAvailable(void)
{
    return gHostStream;
}

void HostFrontendInit(void)
{
    if (gHostStream) {
        HostStreamInit();
    }
}

void HostFrontendVBlank(void)
{
    if (gHostStream) {
        HostStreamVBlank();
    }
}

u16 HostFrontendKeys(void)
{
    return gHostStream ? HostStreamKeys() : 0;
}

void HostFrontendAudio(const s8 *buf, u32 n, u32 rate)
{
    if (gHostStream) {
        HostStreamAudio(buf, n, rate);
    }
}

void HostToast(const char *text)
{
    (void)text;
}

/* The menu's needs, for --ui-preview without SDL. */
struct HostSettings gHostSettings;

void HostConfigDefaults(struct HostSettings *s)
{
    memset(s, 0, sizeof(*s));
    s->windowScale = 3;
    s->borders = HOST_BORDERS_AMBIENT;
    s->master = s->music = s->sfx = 100;
    s->deadzone = 35;
}

void HostConfigDefaultKeys(struct HostSettings *s)
{
    (void)s;
}

void HostConfigDefaultPad(struct HostSettings *s)
{
    (void)s;
}

void HostConfigSave(void)
{
}

int HostConfigSetPortable(int on, const char **why)
{
    (void)on;
    *why = "no window here.";
    return 0;
}

void HostFrontendApply(int what)
{
    (void)what;
}

int HostFrontendRefreshRate(void)
{
    return 0;
}

int HostFrontendFpsAllowed(int fps)
{
    (void)fps;
    return 1;
}

void HostFrontendQuit(void)
{
}

#else

#include <SDL.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

/* The GBA's frame: 280896 cycles of 16.78 MHz. */
#define GBA_CLOCK      16777216
#define CYCLES_A_FRAME 280896

#define SW HOST_SCREEN_WIDTH
#define SH HOST_SCREEN_HEIGHT

static SDL_Window *sWindow;
static SDL_Renderer *sRenderer;
static int sVsyncOn = -1;
static Uint64 sDeadline;
static Uint64 sFrameTicks; /* a frame in performance-counter ticks, << 8 */
static Uint64 sFraction;
static u16 sKeys;
static int sFocused = 1;
static int sMenuRequest;
static int sQuit;

/* The picture's way to the window. */
static SDL_Texture *sGameTex;
static int sGameTexW;
static int sGameTexH;
static SDL_Texture *sSharpTex;
static int sSharpW;
static int sSharpH;
static SDL_Texture *sMaskTex;
static int sMaskKind;
static int sMaskW;
static int sMaskH;
static SDL_Texture *sMenuTex;
static SDL_Texture *sFpsTex;
static SDL_Texture *sPadTex;
static u32 *sWork;
static u32 *sWork2;
static size_t sWorkSize;
static u32 *sGhost;
static int sGhostW;
static int sGhostH;
static u32 *sLut; /* the GBA screen's colours, 64 levels a channel */
static u32 *sHd;  /* the interpolated picture */
static size_t sHdSize;
static u32 sMenuCanvas[HOST_UI_WIDTH * HOST_UI_HEIGHT];

/* The FPS counter: pictures shown in the last half second. */
static Uint64 sFpsStart;
static u32 sFpsCount;
static u32 sFpsShown;

/* The audio device, and the conversion to its rate: the position of the
 * next output sample in the current block (input samples), and the last
 * input sample of the block before (position -1). */
static SDL_AudioDeviceID sAudio;
static SDL_AudioSpec sAudioSpec;
static u32 sAudioFrameSamples; /* a frame's time at the device's rate */
static double sAudioPhase;
static s32 sAudioLast;

/* GAX2's mixer volumes (gax_note_trigger.c): music, sound effects. */
extern u32 gHostGaxVolume[2];

int HostFrontendAvailable(void)
{
    return 1;
}

static void Fail(const char *what)
{
    char text[512];

    snprintf(text, sizeof(text), "%s: %s", what, SDL_GetError());
    fprintf(stderr, "%s\n", text);
    /* A GUI executable (Windows) has no console to show stderr on. */
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, HOST_PORT_NAME, text, NULL);
    exit(2);
}

void HostFrontendQuit(void)
{
    sQuit = 1;
}

/* --- Names for the bindings menu (the game's fonts have lowercase
 * letters, digits and a little punctuation) ----------------------------- */

static void Simplify(const char *in, char *out, size_t size)
{
    static const struct {
        char c;
        const char *name;
    } names[] = {
        { '`', "tilde" }, { '~', "tilde" }, { '/', "slash" },     { '\\', "backslash" }, { ';', "semicolon" },
        { '=', "equals" }, { '[', "lbracket" }, { ']', "rbracket" }, { '*', "times" },   { '&', "and" },
        { '"', "quote" },  { '#', "hash" },
    };
    size_t n = 0;

    if (in[0] != '\0' && in[1] == '\0') {
        size_t i;

        for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
            if (in[0] == names[i].c) {
                snprintf(out, size, "%s", names[i].name);
                return;
            }
        }
    }
    for (; *in != '\0' && n + 1 < size; in++) {
        char c = *in;

        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || strchr(" -+!:,?.'%", c) != NULL) {
            out[n++] = c;
        }
    }
    out[n] = '\0';
    if (n == 0) {
        snprintf(out, size, "%s", "?");
    }
}

static const char *KeyName(int scancode)
{
    static char text[48];
    const char *name = SDL_GetScancodeName((SDL_Scancode)scancode);

    if (scancode == SDL_SCANCODE_RETURN) {
        return "enter";
    }
    if (scancode == SDL_SCANCODE_GRAVE) {
        return "tilde";
    }
    Simplify(name != NULL ? name : "", text, sizeof(text));
    return text;
}

static const char *PadName(int v)
{
    static char text[48];

    if (HOST_PAD_IS_AXIS(v)) {
        static const char *const axes[SDL_CONTROLLER_AXIS_MAX][2] = {
            { "l stick left", "l stick right" }, { "l stick up", "l stick down" },
            { "r stick left", "r stick right" }, { "r stick up", "r stick down" },
            { "lt -", "lt" },                    { "rt -", "rt" },
        };
        int axis = (v & 0xFF) >> 1;

        if (axis < SDL_CONTROLLER_AXIS_MAX) {
            return axes[axis][v & 1];
        }
        return "axis";
    }
    switch (v & 0xFF) {
    case SDL_CONTROLLER_BUTTON_LEFTSTICK:
        return "l stick";
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
        return "r stick";
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
        return "lb";
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
        return "rb";
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
        return "dpad up";
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        return "dpad down";
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        return "dpad left";
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
        return "dpad right";
    default:
        break;
    }
    {
        const char *s = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)(v & 0xFF));

        Simplify(s != NULL ? s : "", text, sizeof(text));
    }
    return text;
}

/* --- The display --------------------------------------------------------- */

int HostFrontendRefreshRate(void)
{
    SDL_DisplayMode mode;
    int index = sWindow != NULL ? SDL_GetWindowDisplayIndex(sWindow) : 0;

    if (index < 0 || SDL_GetCurrentDisplayMode(index, &mode) != 0) {
        return 0;
    }
    return mode.refresh_rate;
}

int HostFrontendFpsAllowed(int fps)
{
    int refresh = HostFrontendRefreshRate();

    /* 59 and 60 Hz displays (and 143, 144) are the same here. */
    return fps <= 60 || refresh <= 0 || fps <= refresh + 1;
}

/* The frame rate in use: the setting's, or the fastest the display can
 * show below it (a window moved to a slower display). */
static int ActiveFps(void)
{
    int i;

    for (i = gHostSettings.frameRate; i > 0; i--) {
        if (HostFrontendFpsAllowed(gHostFpsValues[i])) {
            return gHostFpsValues[i];
        }
    }
    return 60;
}

static void ApplyWindow(void)
{
    if (gHostSettings.fullscreen) {
        SDL_SetWindowFullscreen(sWindow, SDL_WINDOW_FULLSCREEN_DESKTOP);
    } else {
        SDL_SetWindowFullscreen(sWindow, 0);
        SDL_SetWindowSize(sWindow, SW * gHostSettings.windowScale, SH * gHostSettings.windowScale);
        SDL_SetWindowPosition(sWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
}

static void ApplyVsync(void)
{
    if (sVsyncOn != gHostSettings.vsync) {
        SDL_RenderSetVSync(sRenderer, gHostSettings.vsync);
        sVsyncOn = gHostSettings.vsync;
    }
}

static void ApplyAudio(void)
{
    gHostGaxVolume[0] = (u32)gHostSettings.music * 256 / 100;
    gHostGaxVolume[1] = (u32)gHostSettings.sfx * 256 / 100;
}

/* The GBA's screen: no backlight, its colours darker and run together.
 * Each channel to light (gamma 2.2), mixed by a matrix, darkened, and back. */
static void BuildLut(void)
{
    static const double m[3][3] = {
        { 0.86, 0.14, 0.00 },
        { 0.06, 0.82, 0.12 },
        { 0.08, 0.12, 0.80 },
    };
    int r;
    int g;
    int b;

    if (sLut != NULL) {
        return;
    }
    sLut = malloc(64 * 64 * 64 * sizeof(u32));
    if (sLut == NULL) {
        return;
    }
    for (r = 0; r < 64; r++) {
        for (g = 0; g < 64; g++) {
            for (b = 0; b < 64; b++) {
                double in[3] = { pow((r * 4 + r / 16) / 255.0, 2.2), pow((g * 4 + g / 16) / 255.0, 2.2),
                                 pow((b * 4 + b / 16) / 255.0, 2.2) };
                u32 out = 0;
                int c;

                for (c = 0; c < 3; c++) {
                    double v = m[c][0] * in[0] + m[c][1] * in[1] + m[c][2] * in[2];
                    int q;

                    v = v < 0 ? 0 : v > 1 ? 1 : v;
                    v = pow(v * 0.95, 1.0 / 2.2 * 1.05);
                    q = (int)(v * 255.0 + 0.5);
                    out = out << 8 | (u32)(q > 255 ? 255 : q);
                }
                sLut[r << 12 | g << 6 | b] = out;
            }
        }
    }
}

void HostFrontendApply(int what)
{
    if (what & HOST_APPLY_WINDOW) {
        ApplyWindow();
    }
    if (what & HOST_APPLY_VSYNC) {
        ApplyVsync();
    }
    if (what & HOST_APPLY_AUDIO) {
        ApplyAudio();
    }
    if (what & HOST_APPLY_VIDEO) {
        if (gHostSettings.colors) {
            BuildLut();
        }
        /* The scalers need the game's own frames. */
        if (gHostSettings.frameRate != HOST_FPS_60 &&
            (gHostSettings.filter == HOST_FILTER_SCALE2X || gHostSettings.filter == HOST_FILTER_SCALE3X)) {
            gHostSettings.filter = HOST_FILTER_SHARP;
        }
        HostInterpReset();
    }
}

/* --- Threads for the HD picture ------------------------------------------ */

#define MAX_WORKERS 15

static struct {
    HostLineRun run;
    void *ctx;
    int lines;
    int parts;
} sJob;
static SDL_Thread *sWorkers[MAX_WORKERS];
static SDL_sem *sJobStart[MAX_WORKERS];
static SDL_sem *sJobDone;
static int sWorkerCount;

static void RunPart(int part)
{
    int first = sJob.lines * part / sJob.parts;
    int end = sJob.lines * (part + 1) / sJob.parts;

    if (end > first) {
        sJob.run(sJob.ctx, first, end);
    }
}

static int Worker(void *arg)
{
    int index = (int)(intptr_t)arg;

    for (;;) {
        SDL_SemWait(sJobStart[index]);
        RunPart(index + 1);
        SDL_SemPost(sJobDone);
    }
    return 0;
}

static void StartWorkers(void)
{
    int n = SDL_GetCPUCount() - 1;
    int i;

    if (n > MAX_WORKERS) {
        n = MAX_WORKERS;
    }
    sJobDone = SDL_CreateSemaphore(0);
    for (i = 0; i < n; i++) {
        sJobStart[i] = SDL_CreateSemaphore(0);
        sWorkers[i] = SDL_CreateThread(Worker, "hd", (void *)(intptr_t)i);
        if (sWorkers[i] == NULL) {
            break;
        }
        SDL_DetachThread(sWorkers[i]);
    }
    sWorkerCount = i;
}

static void Parallel(HostLineRun run, void *ctx, int lines)
{
    int i;

    sJob.run = run;
    sJob.ctx = ctx;
    sJob.lines = lines;
    sJob.parts = sWorkerCount + 1;
    for (i = 0; i < sWorkerCount; i++) {
        SDL_SemPost(sJobStart[i]);
    }
    RunPart(0);
    for (i = 0; i < sWorkerCount; i++) {
        SDL_SemWait(sJobDone);
    }
}

/* --- The audio ------------------------------------------------------------ */

/* The default audio device: 16-bit samples (SDL converts them if the
 * device wants others), at the rate and in the channels it likes. No
 * device (or no audio driver) is not an error: the game runs silent. */
static void OpenAudio(void)
{
    SDL_AudioSpec want;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "no audio: %s\n", SDL_GetError());
        return;
    }
    memset(&want, 0, sizeof(want));
    want.freq = 48000;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 512;
    sAudio =
        SDL_OpenAudioDevice(NULL, 0, &want, &sAudioSpec,
                            SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
    if (sAudio == 0) {
        fprintf(stderr, "no audio: %s\n", SDL_GetError());
        return;
    }
    sAudioFrameSamples = (u32)((u64)sAudioSpec.freq * CYCLES_A_FRAME / GBA_CLOCK);
    SDL_PauseAudioDevice(sAudio, 0);
}

void HostFrontendAudio(const s8 *buf, u32 n, u32 rate)
{
    static s16 out[16384];
    u32 frame = sAudioSpec.channels * sizeof(s16);
    u32 queued;
    u32 target;
    double step;
    double adjust;
    s32 gain = gHostSettings.master * 256 / 100;
    u32 m = 0;

    if (sAudio == 0 || n == 0) {
        return;
    }
    queued = SDL_GetQueuedAudioSize(sAudio) / frame;
    target = 3 * sAudioFrameSamples;
    if (queued > 4 * target) {
        /* far ahead (fast forward): this block is dropped */
        sAudioPhase = 0;
        sAudioLast = buf[n - 1];
        return;
    }
    if (queued == 0) {
        /* ran dry: a frame of silence first, so the next block has time */
        memset(out, 0, sAudioFrameSamples * frame);
        SDL_QueueAudio(sAudio, out, sAudioFrameSamples * frame);
        queued = sAudioFrameSamples;
    }
    adjust = ((double)queued - target) / target * 0.005;
    if (adjust > 0.005) {
        adjust = 0.005;
    } else if (adjust < -0.005) {
        adjust = -0.005;
    }
    step = (double)rate / sAudioSpec.freq * (1.0 + adjust);
    while (sAudioPhase < n && (m + 1) * sAudioSpec.channels <= sizeof(out) / sizeof(out[0])) {
        u32 i = (u32)sAudioPhase;
        double f = sAudioPhase - i;
        s32 a = i == 0 ? sAudioLast : buf[i - 1];
        s32 v = (s32)((a + (buf[i] - a) * f) * 256) * gain / 256;
        u32 c;

        for (c = 0; c < sAudioSpec.channels; c++) {
            out[m * sAudioSpec.channels + c] = (s16)v;
        }
        m++;
        sAudioPhase += step;
    }
    sAudioPhase -= n;
    if (sAudioPhase < 0) {
        sAudioPhase = 0;
    }
    sAudioLast = buf[n - 1];
    SDL_QueueAudio(sAudio, out, m * frame);
}

/* --- Startup ------------------------------------------------------------- */

/* The scanlines' and the LCD grid's masks, made at the picture's size on
 * the screen (w x h), so that each of the GBA's rows (and columns) gets
 * one line however it is scaled: a pixel's place in its GBA pixel decides
 * how dark it is. Multiplied over the picture (SDL_BLENDMODE_MOD). */
static SDL_Texture *MaskTexture(int w, int h, int kind)
{
    SDL_Texture *t;
    u32 *px;
    int x;
    int y;

    if (w < 1 || h < 1) {
        return NULL;
    }
    t = SDL_CreateTexture(sRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    px = malloc((size_t)w * h * 4);
    if (t == NULL || px == NULL) {
        free(px);
        return t;
    }
    for (y = 0; y < h; y++) {
        double fy = (y + 0.5) * SH / h;
        double ry = fy - (int)fy; /* where in its GBA row */

        for (x = 0; x < w; x++) {
            double fx = (x + 0.5) * SW / w;
            double rx = fx - (int)fx;
            double v = 1.0;

            if (kind % 3 != 0) {
                /* scanlines: the bottom third of each row, light or dark */
                double depth = kind % 3 == 1 ? 0.30 : 0.55;

                if (ry > 0.67) {
                    v = 1.0 - depth;
                } else if (ry > 0.55) {
                    v = 1.0 - depth / 2;
                }
            }
            if (kind >= 3) {
                /* the LCD grid: the gaps at each pixel's right and bottom */
                if (rx > 0.78) {
                    v *= 0.80;
                }
                if (ry > 0.78) {
                    v *= 0.80;
                }
            }
            {
                u32 c = (u32)(v * 255.0 + 0.5);

                px[(size_t)y * w + x] = 0xFF000000 | c << 16 | c << 8 | c;
            }
        }
    }
    SDL_UpdateTexture(t, NULL, px, w * 4);
    free(px);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_MOD);
    SDL_SetTextureScaleMode(t, SDL_ScaleModeNearest);
    return t;
}

void HostFrontendInit(void)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        Fail("SDL_Init");
    }
    HostConfigInit();
    /* Discord's status: its own thread; the process's end closes its
     * connection, which takes the status off the profile. */
    HostDiscordInit();
    gHostUiKeyName = KeyName;
    gHostUiPadName = PadName;
    sWindow = SDL_CreateWindow(HOST_PORT_NAME " - " HOST_PORT_VERSION, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               SW * gHostSettings.windowScale, SH * gHostSettings.windowScale,
                               SDL_WINDOW_RESIZABLE |
                                   (gHostSettings.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    if (sWindow == NULL) {
        Fail("SDL_CreateWindow");
    }
    sRenderer = SDL_CreateRenderer(sWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
    if (sRenderer == NULL) {
        sRenderer = SDL_CreateRenderer(sWindow, -1, 0);
    }
    if (sRenderer == NULL) {
        Fail("SDL_CreateRenderer");
    }
    ApplyVsync();
    ApplyAudio();
    if (gHostSettings.colors) {
        BuildLut();
    }
    sMenuTex = SDL_CreateTexture(sRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, HOST_UI_WIDTH,
                                 HOST_UI_HEIGHT);
    SDL_SetTextureBlendMode(sMenuTex, SDL_BLENDMODE_BLEND);
    StartWorkers();
    OpenAudio();
    /* Pads plugged in now are opened by their SDL_CONTROLLERDEVICEADDED. */
    sFrameTicks = (SDL_GetPerformanceFrequency() << 8) * CYCLES_A_FRAME / GBA_CLOCK;
    sDeadline = SDL_GetPerformanceCounter();
    sFpsStart = sDeadline;
}

/* --- Drawing ------------------------------------------------------------- */

/* Where the game's picture goes in the window. */
static SDL_Rect GameRect(void)
{
    SDL_Rect r;
    int ww;
    int wh;

    SDL_GetRendererOutputSize(sRenderer, &ww, &wh);
    if (gHostSettings.scaling == HOST_SCALING_STRETCH) {
        r.x = 0;
        r.y = 0;
        r.w = ww;
        r.h = wh;
        return r;
    }
    if (gHostSettings.scaling == HOST_SCALING_INTEGER && ww >= SW && wh >= SH) {
        int k = ww / SW < wh / SH ? ww / SW : wh / SH;

        r.w = SW * k;
        r.h = SH * k;
    } else if ((long)ww * SH > (long)wh * SW) {
        r.h = wh;
        r.w = wh * SW / SH;
    } else {
        r.w = ww;
        r.h = ww * SH / SW;
    }
    r.x = (ww - r.w) / 2;
    r.y = (wh - r.h) / 2;
    return r;
}

static u32 *Work(u32 **buf, size_t pixels)
{
    if (pixels > sWorkSize) {
        free(sWork);
        free(sWork2);
        sWork = malloc(pixels * 4);
        sWork2 = malloc(pixels * 4);
        sWorkSize = pixels;
        if (sWork == NULL || sWork2 == NULL) {
            Fail("out of memory");
        }
    }
    return *buf;
}

/* Scale2x and Scale3x (EPX): each pixel becomes 2x2 (3x3) taking its
 * neighbours' colour along an edge where two of them agree. */
static void Scale2x(const u32 *in, u32 *out, int w, int h)
{
    int x;
    int y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            u32 p = in[y * w + x];
            u32 a = in[(y > 0 ? y - 1 : y) * w + x];
            u32 b = in[y * w + (x < w - 1 ? x + 1 : x)];
            u32 c = in[y * w + (x > 0 ? x - 1 : x)];
            u32 d = in[(y < h - 1 ? y + 1 : y) * w + x];
            u32 *o = &out[(2 * y) * 2 * w + 2 * x];

            if (c == a && c != d && a != b) {
                o[0] = a;
            } else {
                o[0] = p;
            }
            o[1] = (a == b && a != c && b != d) ? b : p;
            o[2 * w] = (d == c && d != b && c != a) ? c : p;
            o[2 * w + 1] = (b == d && b != a && d != c) ? d : p;
        }
    }
}

static void Scale3x(const u32 *in, u32 *out, int w, int h)
{
    int x;
    int y;

    for (y = 0; y < h; y++) {
        int yu = y > 0 ? y - 1 : y;
        int yd = y < h - 1 ? y + 1 : y;

        for (x = 0; x < w; x++) {
            int xl = x > 0 ? x - 1 : x;
            int xr = x < w - 1 ? x + 1 : x;
            u32 A = in[yu * w + xl], B = in[yu * w + x], C = in[yu * w + xr];
            u32 D = in[y * w + xl], E = in[y * w + x], F = in[y * w + xr];
            u32 G = in[yd * w + xl], Hh = in[yd * w + x], I = in[yd * w + xr];
            u32 *o = &out[(3 * y) * 3 * w + 3 * x];
            int ow = 3 * w;

            if (B != Hh && D != F) {
                o[0] = D == B ? D : E;
                o[1] = (D == B && E != C) || (B == F && E != A) ? B : E;
                o[2] = B == F ? F : E;
                o[ow] = (D == B && E != G) || (D == Hh && E != A) ? D : E;
                o[ow + 1] = E;
                o[ow + 2] = (B == F && E != I) || (Hh == F && E != C) ? F : E;
                o[2 * ow] = D == Hh ? D : E;
                o[2 * ow + 1] = (D == Hh && E != I) || (Hh == F && E != G) ? Hh : E;
                o[2 * ow + 2] = Hh == F ? F : E;
            } else {
                o[0] = o[1] = o[2] = o[ow] = o[ow + 1] = o[ow + 2] = o[2 * ow] = o[2 * ow + 1] = o[2 * ow + 2] = E;
            }
        }
    }
}

/* The picture through the settings, into the game texture. `dt` is the
 * time since the last picture shown, in 60ths of a second (ghosting). */
static void UploadPicture(const u32 *src, int w, int h, int ownFrame, double dt)
{
    size_t n = (size_t)w * h;
    const u32 *p = src;
    int k = 1;

    if (gHostSettings.colors && sLut != NULL) {
        u32 *out = Work(&sWork, n * 9);
        size_t i;

        for (i = 0; i < n; i++) {
            u32 c = p[i];

            out[i] = sLut[(c >> 18 & 0x3F) << 12 | (c >> 10 & 0x3F) << 6 | (c >> 2 & 0x3F)];
        }
        p = out;
    }
    if (ownFrame && (gHostSettings.filter == HOST_FILTER_SCALE2X || gHostSettings.filter == HOST_FILTER_SCALE3X)) {
        u32 *out;

        Work(&sWork, n * 9);
        out = p == sWork ? sWork2 : sWork;
        k = gHostSettings.filter == HOST_FILTER_SCALE2X ? 2 : 3;
        if (k == 2) {
            Scale2x(p, out, w, h);
        } else {
            Scale3x(p, out, w, h);
        }
        p = out;
        w *= k;
        h *= k;
        n = (size_t)w * h;
    }
    if (gHostSettings.ghosting) {
        /* The GBA's screen keeps about half of a picture through the next
         * one: half each 60th of a second. */
        u32 keep = (u32)(256.0 * pow(0.5, dt < 0.05 ? 0.05 : dt > 4 ? 4 : dt) + 0.5);
        size_t i;

        if (sGhost == NULL || sGhostW != w || sGhostH != h) {
            free(sGhost);
            sGhost = malloc(n * 4);
            if (sGhost == NULL) {
                Fail("out of memory");
            }
            memcpy(sGhost, p, n * 4);
            sGhostW = w;
            sGhostH = h;
        }
        for (i = 0; i < n; i++) {
            u32 a = p[i];
            u32 b = sGhost[i];
            u32 rb = ((a & 0xFF00FF) * (256 - keep) + (b & 0xFF00FF) * keep) >> 8 & 0xFF00FF;
            u32 g = ((a & 0x00FF00) * (256 - keep) + (b & 0x00FF00) * keep) >> 8 & 0x00FF00;

            sGhost[i] = rb | g;
        }
        p = sGhost;
    }
    if (sGameTex == NULL || sGameTexW != w || sGameTexH != h) {
        if (sGameTex != NULL) {
            SDL_DestroyTexture(sGameTex);
        }
        sGameTex = SDL_CreateTexture(sRenderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, w, h);
        if (sGameTex == NULL) {
            Fail("SDL_CreateTexture");
        }
        sGameTexW = w;
        sGameTexH = h;
    }
    SDL_UpdateTexture(sGameTex, NULL, p, w * 4);
}

static void DrawGame(SDL_Rect dst)
{
    int filter = gHostSettings.filter;

    if (filter == HOST_FILTER_SHARP && (dst.w > sGameTexW || dst.h > sGameTexH)) {
        /* Nearest to the integer size at or above the window's, then
         * linear down to it: square pixels with soft edges. */
        int k = (dst.h + sGameTexH - 1) / sGameTexH;
        int tw = sGameTexW * k;
        int th = sGameTexH * k;

        if (tw > 4096 || th > 4096) {
            k = 4096 / sGameTexW;
            tw = sGameTexW * k;
            th = sGameTexH * k;
        }
        if (sSharpTex == NULL || sSharpW != tw || sSharpH != th) {
            if (sSharpTex != NULL) {
                SDL_DestroyTexture(sSharpTex);
            }
            sSharpTex = SDL_CreateTexture(sRenderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_TARGET, tw, th);
            sSharpW = tw;
            sSharpH = th;
        }
        if (sSharpTex != NULL && SDL_SetRenderTarget(sRenderer, sSharpTex) == 0) {
            SDL_SetTextureScaleMode(sGameTex, SDL_ScaleModeNearest);
            SDL_RenderCopy(sRenderer, sGameTex, NULL, NULL);
            SDL_SetRenderTarget(sRenderer, NULL);
            SDL_SetTextureScaleMode(sSharpTex, SDL_ScaleModeLinear);
            SDL_RenderCopy(sRenderer, sSharpTex, NULL, &dst);
            return;
        }
    }
    SDL_SetTextureScaleMode(sGameTex, (filter == HOST_FILTER_SMOOTH || filter == HOST_FILTER_SHARP)
                                          ? SDL_ScaleModeLinear
                                          : SDL_ScaleModeNearest);
    SDL_RenderCopy(sRenderer, sGameTex, NULL, &dst);
}

static void DrawMask(SDL_Rect dst, int kind)
{
    if (sMaskTex == NULL || sMaskKind != kind || sMaskW != dst.w || sMaskH != dst.h) {
        if (sMaskTex != NULL) {
            SDL_DestroyTexture(sMaskTex);
        }
        sMaskTex = MaskTexture(dst.w, dst.h, kind);
        sMaskKind = kind;
        sMaskW = dst.w;
        sMaskH = dst.h;
    }
    if (sMaskTex != NULL) {
        SDL_RenderCopy(sRenderer, sMaskTex, NULL, &dst);
    }
}

/* One mask for both: the scanlines' level (0-2) and the LCD grid (+3). */
static void DrawMasks(SDL_Rect dst)
{
    int kind = gHostSettings.scanlines + 3 * (gHostSettings.lcdGrid != 0);

    if (kind != 0) {
        DrawMask(dst, kind);
    }
}

/* An overlay canvas: ARGB, transparent where nothing is drawn. */
static void Fill(u32 *c, int w, int x0, int y0, int x1, int y1, u32 color)
{
    int x;
    int y;

    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            c[y * w + x] = color;
        }
    }
}

static void PutOverlay(SDL_Texture **tex, const u32 *canvas, int w, int h, int x, int y, int scale)
{
    SDL_Rect r = { x, y, w * scale, h * scale };

    if (*tex == NULL) {
        *tex = SDL_CreateTexture(sRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
        if (*tex == NULL) {
            return;
        }
        SDL_SetTextureBlendMode(*tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(*tex, SDL_ScaleModeNearest);
    }
    SDL_UpdateTexture(*tex, NULL, canvas, w * 4);
    SDL_RenderCopy(sRenderer, *tex, NULL, &r);
}

static void PlaceOverlay(SDL_Rect dst, int w, int h, int side, int bottom, int *scale, int *x, int *y);

#define FPS_W 64
#define FPS_H 12
#define PAD_W 72
#define PAD_H 30

static void DrawFps(SDL_Rect dst, int scale)
{
    static u32 canvas[FPS_W * FPS_H];
    char text[16];
    int w;

    snprintf(text, sizeof(text), "%u fps", sFpsShown);
    w = HostUiTextWidth(0, text) + 6;
    memset(canvas, 0, sizeof(canvas));
    Fill(canvas, FPS_W, 0, 0, w, FPS_H, 0xB0000000);
    HostUiText(canvas, FPS_W, FPS_H, 0, 3, 2, text, HOST_UI_PAL_NORMAL);
    {
        int x;
        int y;

        PlaceOverlay(dst, w, FPS_H, 0, 0, &scale, &x, &y);
        PutOverlay(&sFpsTex, canvas, FPS_W, FPS_H, x, y, scale);
    }
}

/* A message in the top right corner for a few seconds (auto save's,
 * autosave.c), fading out over its last half second. */
#define TOAST_W 200
#define TOAST_H 12
#define TOAST_MS 3000

static char sToast[48];
static Uint32 sToastAt;
static SDL_Texture *sToastTex;

void HostToast(const char *text)
{
    snprintf(sToast, sizeof(sToast), "%s", text);
    sToastAt = SDL_GetTicks();
}

static void DrawToast(SDL_Rect dst, int scale)
{
    static u32 canvas[TOAST_W * TOAST_H];
    Uint32 age = SDL_GetTicks() - sToastAt;
    int w;
    int x;
    int y;

    if (sToast[0] == '\0' || age >= TOAST_MS) {
        return;
    }
    w = HostUiTextWidth(0, sToast) + 6;
    if (w > TOAST_W) {
        w = TOAST_W;
    }
    memset(canvas, 0, sizeof(canvas));
    Fill(canvas, TOAST_W, 0, 0, w, TOAST_H, 0xB0000000);
    HostUiText(canvas, TOAST_W, TOAST_H, 0, 3, 2, sToast, HOST_UI_PAL_NORMAL);
    PlaceOverlay(dst, w, TOAST_H, 1, 0, &scale, &x, &y);
    if (sToastTex != NULL) {
        Uint32 left = TOAST_MS - age;

        SDL_SetTextureAlphaMod(sToastTex, left < 500 ? (Uint8)(left * 255 / 500) : 255);
    }
    PutOverlay(&sToastTex, canvas, TOAST_W, TOAST_H, x, y, scale);
}

/* The GBA's buttons, lit while held: the D-pad, A and B (the font's
 * button icons), L and R, Select and Start. */
static void DrawPad(SDL_Rect dst, int scale, int right)
{
    static u32 canvas[PAD_W * PAD_H];
    u16 k = sKeys;
    const u32 lit = 0xFFFFE718;
    const u32 dark = 0xFF3A3A48;
    const u32 edge = 0xFF100808;
    int ww;
    int wh;
    int i;
    static const struct {
        u16 bit;
        int x;
        int y;
    } arms[5] = { { DPAD_UP, 9, 9 }, { DPAD_DOWN, 9, 21 }, { DPAD_LEFT, 3, 15 }, { DPAD_RIGHT, 15, 15 }, { 0, 9, 15 } };

    memset(canvas, 0, sizeof(canvas));
    Fill(canvas, PAD_W, 0, 0, PAD_W, PAD_H, 0xA0000000);
    for (i = 0; i < 5; i++) {
        Fill(canvas, PAD_W, arms[i].x - 1, arms[i].y - 1, arms[i].x + 7, arms[i].y + 7, edge);
    }
    for (i = 0; i < 5; i++) {
        Fill(canvas, PAD_W, arms[i].x, arms[i].y, arms[i].x + 6, arms[i].y + 6,
             (arms[i].bit && (k & arms[i].bit)) ? lit : dark);
    }
    HostUiText(canvas, PAD_W, PAD_H, 0, 2, 9, "L", (k & L_BUTTON) ? HOST_UI_PAL_NORMAL : HOST_UI_PAL_GREY);
    HostUiText(canvas, PAD_W, PAD_H, 0, PAD_W - 18, 9, "R", (k & R_BUTTON) ? HOST_UI_PAL_NORMAL : HOST_UI_PAL_GREY);
    HostUiText(canvas, PAD_W, PAD_H, 0, 52, 20, "A", (k & A_BUTTON) ? HOST_UI_PAL_NORMAL : HOST_UI_PAL_GREY);
    HostUiText(canvas, PAD_W, PAD_H, 0, 42, 23, "B", (k & B_BUTTON) ? HOST_UI_PAL_NORMAL : HOST_UI_PAL_GREY);
    /* Select and Start: two small bars, as on the GBA. */
    Fill(canvas, PAD_W, 25, 24, 33, 28, edge);
    Fill(canvas, PAD_W, 26, 25, 32, 27, (k & SELECT_BUTTON) ? lit : dark);
    Fill(canvas, PAD_W, 25, 16, 33, 20, edge);
    Fill(canvas, PAD_W, 26, 17, 32, 19, (k & START_BUTTON) ? lit : dark);
    {
        int x;
        int y;

        (void)ww;
        (void)wh;
        PlaceOverlay(dst, PAD_W, PAD_H, right, 1, &scale, &x, &y);
        PutOverlay(&sPadTex, canvas, PAD_W, PAD_H, x, y, scale);
    }
}

/* The menu's canvas over the game's place, or `banner` (a word on a
 * transparent canvas: "paused"). */
static void DrawMenu(SDL_Rect dst, const char *banner)
{
    SDL_Rect r = dst;

    if (banner != NULL) {
        int w = HostUiTextWidth(1, banner);

        memset(sMenuCanvas, 0, sizeof(sMenuCanvas));
        Fill(sMenuCanvas, HOST_UI_WIDTH, 0, 64, HOST_UI_WIDTH, 92, 0xB0000000);
        HostUiText(sMenuCanvas, HOST_UI_WIDTH, HOST_UI_HEIGHT, 1, (HOST_UI_WIDTH - w) / 2, 70, banner,
                   HOST_UI_PAL_NORMAL);
    } else {
        HostUiDraw(sMenuCanvas);
    }
    SDL_UpdateTexture(sMenuTex, NULL, sMenuCanvas, HOST_UI_WIDTH * 4);
    SDL_SetTextureScaleMode(sMenuTex, SDL_ScaleModeNearest);
    /* The menu keeps the GBA's shape whatever the scaling. */
    if (gHostSettings.scaling == HOST_SCALING_STRETCH) {
        int ww = dst.w;
        int wh = dst.h;

        if ((long)ww * SH > (long)wh * SW) {
            r.w = wh * SW / SH;
        } else {
            r.h = ww * SH / SW;
        }
        r.x = dst.x + (dst.w - r.w) / 2;
        r.y = dst.y + (dst.h - r.h) / 2;
    }
    SDL_RenderCopy(sRenderer, sMenuTex, NULL, &r);
}

/* --- Borders --------------------------------------------------------------- */

void HostBordersDrawThemed(SDL_Renderer *r, SDL_Rect dst, int ww, int wh);

/* Where an overlay `w` x `h` goes: in the border beside the picture
 * (side 0 left, 1 right) when one is wide enough for it at `*scale` or
 * smaller, centred across it at its top or bottom; otherwise in the
 * window's corner, over the picture. */
static void PlaceOverlay(SDL_Rect dst, int w, int h, int side, int bottom, int *scale, int *x, int *y)
{
    int ww;
    int wh;
    int bar;
    int s;

    SDL_GetRendererOutputSize(sRenderer, &ww, &wh);
    bar = side == 0 ? dst.x : ww - (dst.x + dst.w);
    for (s = *scale; s >= 1; s--) {
        if (bar >= w * s + 2 * s) {
            *scale = s;
            *x = (side == 0 ? 0 : dst.x + dst.w) + (bar - w * s) / 2;
            *y = bottom ? wh - (h + 4) * s : 4 * s;
            return;
        }
    }
    *x = side == 0 ? 4 * *scale : ww - (w + 4) * *scale;
    *y = bottom ? wh - (h + 4) * *scale : 4 * *scale;
}

/* Ambient: the picture's own colours around it. The latest game frame,
 * averaged down to 30x20 (8x8 pixel blocks), blurred, and eased towards
 * over a quarter of a second so that a flash doesn't flicker the edges,
 * is stretched to fill the window behind the picture (linear, so it is
 * soft) and dimmed, so that the picture stays what the eye goes to. */
#define AMB_W 30
#define AMB_H 20

static float sAmb[AMB_H][AMB_W][3];
static int sAmbReady;
static SDL_Texture *sAmbTex;

static void BlurAmbient(float (*a)[AMB_W][3])
{
    static float t[AMB_H][AMB_W][3];
    int pass;
    int x;
    int y;
    int c;

    for (pass = 0; pass < 2; pass++) {
        for (y = 0; y < AMB_H; y++) {
            for (x = 0; x < AMB_W; x++) {
                int l = x > 0 ? x - 1 : x;
                int r = x < AMB_W - 1 ? x + 1 : x;

                for (c = 0; c < 3; c++) {
                    t[y][x][c] = (a[y][l][c] + 2 * a[y][x][c] + a[y][r][c]) / 4;
                }
            }
        }
        for (y = 0; y < AMB_H; y++) {
            int u = y > 0 ? y - 1 : y;
            int d = y < AMB_H - 1 ? y + 1 : y;

            for (x = 0; x < AMB_W; x++) {
                for (c = 0; c < 3; c++) {
                    a[y][x][c] = (t[u][x][c] + 2 * t[y][x][c] + t[d][x][c]) / 4;
                }
            }
        }
    }
}

static void UpdateAmbient(double dt)
{
    static float now[AMB_H][AMB_W][3];
    static u32 px[AMB_H * AMB_W];
    float keep = sAmbReady ? (float)pow(0.5, dt / 15.0) : 0.0f;
    int x;
    int y;
    int c;

    for (y = 0; y < AMB_H; y++) {
        for (x = 0; x < AMB_W; x++) {
            u32 sum[3] = { 0, 0, 0 };
            int i;
            int j;

            for (j = 0; j < SH / AMB_H; j++) {
                const u32 *row = &gHostScreen[(y * (SH / AMB_H) + j) * SW + x * (SW / AMB_W)];

                for (i = 0; i < SW / AMB_W; i++) {
                    sum[0] += (row[i] >> 16) & 0xFF;
                    sum[1] += (row[i] >> 8) & 0xFF;
                    sum[2] += row[i] & 0xFF;
                }
            }
            for (c = 0; c < 3; c++) {
                now[y][x][c] = sum[c] / (float)((SW / AMB_W) * (SH / AMB_H));
            }
        }
    }
    BlurAmbient(now);
    for (y = 0; y < AMB_H; y++) {
        for (x = 0; x < AMB_W; x++) {
            u32 out = 0;

            for (c = 0; c < 3; c++) {
                sAmb[y][x][c] = sAmb[y][x][c] * keep + now[y][x][c] * (1 - keep);
                out = out << 8 | (u32)(sAmb[y][x][c] + 0.5f);
            }
            if (gHostSettings.colors && sLut != NULL) {
                out = sLut[(out >> 18 & 0x3F) << 12 | (out >> 10 & 0x3F) << 6 | (out >> 2 & 0x3F)];
            }
            px[y * AMB_W + x] = 0xFF000000 | out;
        }
    }
    sAmbReady = 1;
    if (sAmbTex == NULL) {
        sAmbTex = SDL_CreateTexture(sRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, AMB_W, AMB_H);
        if (sAmbTex == NULL) {
            return;
        }
        SDL_SetTextureScaleMode(sAmbTex, SDL_ScaleModeLinear);
        SDL_SetTextureColorMod(sAmbTex, 160, 160, 160);
    }
    SDL_UpdateTexture(sAmbTex, NULL, px, AMB_W * 4);
}

/* The picture's shape, zoomed to cover the whole window, centred on it. */
static void DrawAmbient(int ww, int wh)
{
    SDL_Rect r;

    if (sAmbTex == NULL || !sAmbReady) {
        return;
    }
    if ((long)ww * SH > (long)wh * SW) {
        r.w = ww;
        r.h = ww * SH / SW;
    } else {
        r.h = wh;
        r.w = wh * SW / SH;
    }
    r.x = (ww - r.w) / 2;
    r.y = (wh - r.h) / 2;
    SDL_RenderCopy(sRenderer, sAmbTex, NULL, &r);
}

static void DrawBorders(SDL_Rect dst)
{
    int ww;
    int wh;

    SDL_GetRendererOutputSize(sRenderer, &ww, &wh);
    if (dst.w >= ww && dst.h >= wh) {
        return; /* no borders */
    }
    switch (gHostSettings.borders) {
    case HOST_BORDERS_AMBIENT:
        DrawAmbient(ww, wh);
        break;
    case HOST_BORDERS_THEMED:
        HostBordersDrawThemed(sRenderer, dst, ww, wh);
        break;
    default:
        break;
    }
}

static int sMenuPeek;

/* One picture to the window: the game's (`src`, w x h, the game's own
 * frame or an interpolated one), then what goes over it. */
static void Present(const u32 *src, int w, int h, int ownFrame, double dt, int menu, const char *banner)
{
    SDL_Rect dst = GameRect();
    int scale = (dst.h / SH + 1) / 2 > 1 ? (dst.h / SH + 1) / 2 : 1;
    Uint64 now;

    if (src != NULL) {
        UploadPicture(src, w, h, ownFrame, dt);
        if (gHostSettings.borders == HOST_BORDERS_AMBIENT) {
            UpdateAmbient(dt);
        }
    }
    SDL_SetRenderDrawColor(sRenderer, 0, 0, 0, 255);
    SDL_RenderClear(sRenderer);
    DrawBorders(dst);
    if (sGameTex != NULL) {
        DrawGame(dst);
        DrawMasks(dst);
    }
    if (menu && !sMenuPeek) {
        DrawMenu(dst, NULL);
    } else {
        if (banner != NULL) {
            DrawMenu(dst, banner);
        }
        if (gHostSettings.showFps) {
            DrawFps(dst, scale);
        }
        if (gHostSettings.inputDisplay) {
            DrawPad(dst, scale, gHostSettings.inputDisplay == 2);
        }
        DrawToast(dst, scale);
    }
    SDL_RenderPresent(sRenderer);
    sFpsCount++;
    now = SDL_GetPerformanceCounter();
    if (now - sFpsStart >= SDL_GetPerformanceFrequency() / 2) {
        sFpsShown = (u32)((sFpsCount * SDL_GetPerformanceFrequency() + (now - sFpsStart) / 2) / (now - sFpsStart));
        sFpsCount = 0;
        sFpsStart = now;
    }
}

/* --- F12 ------------------------------------------------------------------ */

/* The window's picture as the renderer drew it (or the game's, if it
 * can't be read back), in the data folder's screenshots/. */
static void Screenshot(void)
{
    static u32 sShots;
    int w;
    int h;
    u32 *pixels;
    char dir[1100];
    char path[1200];
    FILE *f;

    SDL_GetRendererOutputSize(sRenderer, &w, &h);
    pixels = malloc((size_t)w * h * 4);
    if (pixels == NULL || w > 4096 ||
        SDL_RenderReadPixels(sRenderer, NULL, SDL_PIXELFORMAT_RGB888, pixels, w * 4) != 0) {
        free(pixels);
        pixels = NULL;
    }
    snprintf(dir, sizeof(dir), "%sscreenshots", HostConfigDataDir());
#ifdef _WIN32
    _mkdir(dir);
#else
    mkdir(dir, 0755);
#endif
    do {
        snprintf(path, sizeof(path), "%s/crashbandicootxs-%u.png", dir, ++sShots);
        f = fopen(path, "rb");
        if (f != NULL) {
            fclose(f);
        }
    } while (f != NULL);
    if (pixels != NULL ? HostWritePng(path, pixels, w, h) : HostWritePng(path, gHostScreen, SW, SH)) {
        printf("screenshot %s before frame %u\n", path, HostFrame() + 1);
        fflush(stdout);
    }
    free(pixels);
}

/* --- Input ---------------------------------------------------------------- */

static int AxisHeld(SDL_GameController *pad, int v, int threshold)
{
    Sint16 a = SDL_GameControllerGetAxis(pad, (SDL_GameControllerAxis)((v & 0xFF) >> 1));

    return (v & 1) ? a > threshold : a < -threshold;
}

/* Is binding `b` held (keyboard `useKeys`, pads `usePads`)? */
static int Held(int b, int useKeys, int usePads)
{
    const Uint8 *state = SDL_GetKeyboardState(NULL);
    int threshold = gHostSettings.deadzone * 32767 / 100;
    int s;
    int i;

    for (s = 0; s < HOST_BIND_SLOTS; s++) {
        int key = gHostSettings.keys[b][s];

        if (useKeys && key > 0 && key < SDL_NUM_SCANCODES && state[key]) {
            return 1;
        }
    }
    if (!usePads) {
        return 0;
    }
    for (i = 0; i < SDL_NumJoysticks(); i++) {
        SDL_GameController *pad;

        if (!SDL_IsGameController(i)) {
            continue;
        }
        pad = SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(i));
        if (pad == NULL) {
            continue;
        }
        for (s = 0; s < HOST_BIND_SLOTS; s++) {
            int v = gHostSettings.pad[b][s];

            if (HOST_PAD_IS_BUTTON(v) && SDL_GameControllerGetButton(pad, (SDL_GameControllerButton)(v & 0xFF))) {
                return 1;
            }
            if (HOST_PAD_IS_AXIS(v) && AxisHeld(pad, v, threshold)) {
                return 1;
            }
        }
    }
    return 0;
}

static u16 GameKeys(void)
{
    static const u16 bits[HOST_BIND_GBA_COUNT] = {
        DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT, A_BUTTON, B_BUTTON, L_BUTTON, R_BUTTON, START_BUTTON, SELECT_BUTTON,
    };
    u16 keys = 0;
    int b;

    for (b = 0; b < HOST_BIND_GBA_COUNT; b++) {
        if (Held(b, 1, 1)) {
            keys |= bits[b];
        }
    }
    return keys;
}

static int KeyIs(int scancode, int bind)
{
    int s;

    for (s = 0; s < HOST_BIND_SLOTS; s++) {
        if (gHostSettings.keys[bind][s] == scancode) {
            return 1;
        }
    }
    return 0;
}

static int PadIs(int v, int bind)
{
    int s;

    for (s = 0; s < HOST_BIND_SLOTS; s++) {
        if (gHostSettings.pad[bind][s] == v) {
            return 1;
        }
    }
    return 0;
}

static void ToggleFullscreen(void)
{
    gHostSettings.fullscreen = !gHostSettings.fullscreen;
    ApplyWindow();
    HostConfigSave();
}

/* The menu's directions from a pad: held, they repeat. */
static int sRepeatAction = -1;
static Uint32 sRepeatAt;

static int PadDirection(void)
{
    static const int binds[4] = { HOST_BIND_UP, HOST_BIND_DOWN, HOST_BIND_LEFT, HOST_BIND_RIGHT };
    static const int actions[4] = { HOST_UI_UP, HOST_UI_DOWN, HOST_UI_LEFT, HOST_UI_RIGHT };
    int i;

    for (i = 0; i < 4; i++) {
        if (Held(binds[i], 0, 1)) {
            return actions[i];
        }
    }
    return -1;
}

/* Axis captures: an axis already pushed when the capture started doesn't
 * count until it has come back. */
static int sAxisWasOut[SDL_CONTROLLER_AXIS_MAX];

static void MenuEvent(const SDL_Event *e)
{
    int capturing = HostUiCapturing();

    if (e->type == SDL_KEYDOWN) {
        SDL_Scancode k = e->key.keysym.scancode;

        if (capturing) {
            if (k == SDL_SCANCODE_ESCAPE) {
                HostUiCaptureCancel();
            } else if (capturing == 1 && !e->key.repeat) {
                HostUiCaptured(k);
            }
            return;
        }
        if (KeyIs(k, HOST_BIND_MENU) && !e->key.repeat) {
            HostUiInput(HOST_UI_BACK);
            while (HostUiIsOpen()) {
                HostUiInput(HOST_UI_BACK);
            }
            return;
        }
        if (k == SDL_SCANCODE_UP || KeyIs(k, HOST_BIND_UP)) {
            HostUiInput(HOST_UI_UP);
        } else if (k == SDL_SCANCODE_DOWN || KeyIs(k, HOST_BIND_DOWN)) {
            HostUiInput(HOST_UI_DOWN);
        } else if (k == SDL_SCANCODE_LEFT || KeyIs(k, HOST_BIND_LEFT)) {
            HostUiInput(HOST_UI_LEFT);
        } else if (k == SDL_SCANCODE_RIGHT || KeyIs(k, HOST_BIND_RIGHT)) {
            HostUiInput(HOST_UI_RIGHT);
        } else if (e->key.repeat) {
            /* only the directions repeat */
        } else if (k == SDL_SCANCODE_RETURN || k == SDL_SCANCODE_KP_ENTER || k == SDL_SCANCODE_SPACE ||
                   KeyIs(k, HOST_BIND_A) || KeyIs(k, HOST_BIND_START)) {
            HostUiInput(HOST_UI_OK);
        } else if (k == SDL_SCANCODE_ESCAPE || k == SDL_SCANCODE_BACKSPACE || KeyIs(k, HOST_BIND_B)) {
            HostUiInput(HOST_UI_BACK);
        } else if (k == SDL_SCANCODE_DELETE) {
            HostUiInput(HOST_UI_CLEAR);
        }
        return;
    }
    if (e->type == SDL_CONTROLLERBUTTONDOWN) {
        int v = HOST_PAD_BUTTON(e->cbutton.button);

        if (capturing == 2) {
            HostUiCaptured(v);
            return;
        }
        if (capturing) {
            return;
        }
        if (PadIs(v, HOST_BIND_MENU)) {
            while (HostUiIsOpen()) {
                HostUiInput(HOST_UI_BACK);
            }
        } else if (PadIs(v, HOST_BIND_A) || PadIs(v, HOST_BIND_START)) {
            HostUiInput(HOST_UI_OK);
        } else if (PadIs(v, HOST_BIND_B)) {
            HostUiInput(HOST_UI_BACK);
        } else if (e->cbutton.button == SDL_CONTROLLER_BUTTON_X) {
            HostUiInput(HOST_UI_CLEAR);
        }
        return;
    }
    if (e->type == SDL_CONTROLLERAXISMOTION && capturing == 2 && e->caxis.axis < SDL_CONTROLLER_AXIS_MAX) {
        int out = e->caxis.value > 24000 || e->caxis.value < -24000;

        if (out && !sAxisWasOut[e->caxis.axis]) {
            HostUiCaptured(HOST_PAD_AXIS(e->caxis.axis, e->caxis.value > 0));
        }
        sAxisWasOut[e->caxis.axis] = out || (sAxisWasOut[e->caxis.axis] &&
                                             (e->caxis.value > 8000 || e->caxis.value < -8000));
    }
}

/* The events every loop reads: the window's, the pads', the hotkeys. */
static void CommonEvent(const SDL_Event *e, int *shot)
{
    switch (e->type) {
    case SDL_QUIT:
        sQuit = 1;
        break;
    case SDL_WINDOWEVENT:
        if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
            sFocused = 0;
        } else if (e->window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
            sFocused = 1;
        }
        break;
    case SDL_KEYDOWN:
        if (e->key.repeat || HostUiCapturing()) {
            break;
        }
        if ((e->key.keysym.scancode == SDL_SCANCODE_RETURN && (e->key.keysym.mod & KMOD_ALT)) ||
            KeyIs(e->key.keysym.scancode, HOST_BIND_FULLSCREEN)) {
            ToggleFullscreen();
        } else if (KeyIs(e->key.keysym.scancode, HOST_BIND_SCREENSHOT)) {
            *shot = 1;
        } else if (!HostUiIsOpen() && KeyIs(e->key.keysym.scancode, HOST_BIND_MENU)) {
            sMenuRequest = 1;
        }
        break;
    case SDL_CONTROLLERBUTTONDOWN:
        if (!HostUiIsOpen() && !HostUiCapturing() && PadIs(HOST_PAD_BUTTON(e->cbutton.button), HOST_BIND_MENU)) {
            sMenuRequest = 1;
        }
        break;
    case SDL_CONTROLLERDEVICEADDED:
        SDL_GameControllerOpen(e->cdevice.which);
        break;
    case SDL_CONTROLLERDEVICEREMOVED:
        {
            SDL_GameController *pad = SDL_GameControllerFromInstanceID(e->cdevice.which);

            if (pad != NULL) {
                SDL_GameControllerClose(pad);
            }
            break;
        }
    default:
        break;
    }
}

static void CheckQuit(void)
{
    if (sQuit) {
        HostConfigSave();
        HostExit(0, "quit");
    }
}

/* --- Pacing --------------------------------------------------------------- */

static void Pace(int fast)
{
    Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 now;

    sFraction += sFrameTicks;
    sDeadline += sFraction >> 8;
    sFraction &= 0xFF;
    now = SDL_GetPerformanceCounter();
    if (fast || now > sDeadline + 4 * (sFrameTicks >> 8)) {
        /* Running fast, or far behind (a slow host, a stop in a
         * debugger): start again from now. */
        sDeadline = now;
        return;
    }
    while (now < sDeadline) {
        Uint64 left = (sDeadline - now) * 1000 / freq;

        if (left > 2) {
            SDL_Delay((Uint32)(left - 1));
        }
        now = SDL_GetPerformanceCounter();
    }
}

static void SleepUntil(Uint64 when)
{
    Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 now = SDL_GetPerformanceCounter();

    while (now < when) {
        Uint64 left = (when - now) * 1000 / freq;

        if (left > 2) {
            SDL_Delay((Uint32)(left - 1));
        } else if (left > 0) {
            SDL_Delay(0);
        }
        now = SDL_GetPerformanceCounter();
    }
}

/* The game stops here while the menu is open or the window has lost the
 * focus: the last picture stays (under the menu, or with "paused"), the
 * sound stops, and the pacing starts again from now after. */
static void Hold(int *shot)
{
    int paused = 0;
    int menuHeld = 0;

    for (;;) {
        SDL_Event e;
        int menu;

        if (sMenuRequest) {
            sMenuRequest = 0;
            HostUiOpen();
        }
        menu = HostUiIsOpen();
        paused = !menu && gHostSettings.pauseUnfocused && !sFocused;
        if (!menu && !paused) {
            break;
        }
        if (!menuHeld) {
            menuHeld = 1;
            if (sAudio != 0) {
                SDL_PauseAudioDevice(sAudio, 1);
            }
        }
        while (SDL_PollEvent(&e)) {
            CommonEvent(&e, shot);
            if (HostUiIsOpen()) {
                MenuEvent(&e);
            }
        }
        CheckQuit();
        /* Discord's status stays current while the game is held (the
         * settings menu, or paused in the background). */
        HostDiscordFrame();
        if (HostUiIsOpen() && !HostUiCapturing()) {
            int dir = PadDirection();
            Uint32 now = SDL_GetTicks();

            if (dir != sRepeatAction) {
                sRepeatAction = dir;
                if (dir >= 0) {
                    HostUiInput(dir);
                }
                sRepeatAt = now + 380;
            } else if (dir >= 0 && (Sint32)(now - sRepeatAt) >= 0) {
                HostUiInput(dir);
                sRepeatAt = now + 90;
            }
            sMenuPeek = Held(HOST_BIND_R, 1, 1);
        } else {
            sMenuPeek = 0;
        }
        if (*shot) {
            Screenshot();
            *shot = 0;
        }
        Present(NULL, 0, 0, 0, 1, HostUiIsOpen(), paused ? "paused" : NULL);
        /* About 60 pictures a second, vsync or not (the menu's messages
         * count its frames). */
        {
            static Uint64 last;
            Uint64 frame = SDL_GetPerformanceFrequency() / 60;

            if (last != 0 && SDL_GetPerformanceCounter() - last < 2 * frame) {
                SleepUntil(last + frame);
            }
            last = SDL_GetPerformanceCounter();
        }
    }
    if (menuHeld) {
        sKeys = 0;
        sRepeatAction = -1;
        if (sAudio != 0) {
            SDL_ClearQueuedAudio(sAudio);
            SDL_PauseAudioDevice(sAudio, 0);
        }
        sDeadline = SDL_GetPerformanceCounter();
        sFraction = 0;
        HostInterpReset();
        HostConfigSave();
    }
}

static Uint64 sLastPresent;

static double SinceLastPresent(Uint64 now)
{
    double dt = sLastPresent ? (double)(now - sLastPresent) * 60.0 / SDL_GetPerformanceFrequency() : 1.0;

    sLastPresent = now;
    return dt;
}

void HostFrontendVBlank(void)
{
    SDL_Event e;
    int shot = 0;
    int fast;
    int fps = ActiveFps();

    /* The picture game frame N reads its keys under (--screens's FRAME.png),
     * finished before that frame. */
    if (gHostOptions.screenshotAt != 0 && HostFrame() + 1 == gHostOptions.screenshotAt) {
        shot = 1;
    }
    while (SDL_PollEvent(&e)) {
        CommonEvent(&e, &shot);
    }
    CheckQuit();
    HostDiscordFrame();
    Hold(&shot);
    fast = Held(HOST_BIND_FAST_FORWARD, 1, 1);

    if (fps <= 60 || fast) {
        static Uint64 lastFast;
        Uint64 now = SDL_GetPerformanceCounter();

        /* Fast forward shows at most 60 pictures a second. */
        if (!fast || now - lastFast >= SDL_GetPerformanceFrequency() / 60) {
            lastFast = now;
            Present(gHostScreen, SW, SH, 1, SinceLastPresent(now), 0, NULL);
        }
        if (shot) {
            Screenshot();
        }
        HostInterpReset();
        sKeys = GameKeys();
        Pace(fast);
        return;
    }

    /* Frame interpolation: the frame just drawn is kept, and the pictures
     * between it and the one before are shown until this frame's time is
     * up. */
    HostInterpCapture();
    {
        Uint64 freq = SDL_GetPerformanceFrequency();
        Uint64 start = sDeadline;
        Uint64 period;
        Uint64 step = freq / fps;
        Uint64 next;
        SDL_Rect dst = GameRect();
        int scale = (dst.h + SH - 1) / SH;
        size_t need;

        sFraction += sFrameTicks;
        sDeadline += sFraction >> 8;
        sFraction &= 0xFF;
        period = sDeadline - start;
        if (SDL_GetPerformanceCounter() > sDeadline + 4 * period) {
            start = SDL_GetPerformanceCounter();
            sDeadline = start + period;
        }
        if (scale < 2) {
            scale = 2;
        }
        if (scale > 4) {
            scale = 4;
        }
        need = (size_t)SW * scale * SH * scale;
        if (need > sHdSize) {
            free(sHd);
            sHd = malloc(need * 4);
            sHdSize = need;
            if (sHd == NULL) {
                Fail("out of memory");
            }
        }
        next = SDL_GetPerformanceCounter();
        for (;;) {
            Uint64 now = SDL_GetPerformanceCounter();
            double alpha;

            if (now >= sDeadline) {
                break;
            }
            alpha = (double)(now - start) / (double)period;
            if (alpha < 0) {
                alpha = 0;
            }
            if (HostInterpReady()) {
                HostInterpRender(alpha, scale, sHd, Parallel);
                Present(sHd, SW * scale, SH * scale, 0, SinceLastPresent(now), 0, NULL);
            } else {
                Present(gHostScreen, SW, SH, 1, SinceLastPresent(now), 0, NULL);
            }
            if (shot) {
                Screenshot();
                shot = 0;
            }
            while (SDL_PollEvent(&e)) {
                CommonEvent(&e, &shot);
            }
            if (sQuit || sMenuRequest || (!sFocused && gHostSettings.pauseUnfocused)) {
                break; /* the next VBlank sees to it */
            }
            next += step;
            if (next < SDL_GetPerformanceCounter()) {
                next = SDL_GetPerformanceCounter();
            }
            if (next >= sDeadline) {
                break;
            }
            if (!sVsyncOn || fps < HostFrontendRefreshRate() - 1) {
                SleepUntil(next);
            }
        }
        SleepUntil(sDeadline);
    }
    sKeys = GameKeys();
}

u16 HostFrontendKeys(void)
{
    return sKeys;
}

#endif /* HOST_SDL */
