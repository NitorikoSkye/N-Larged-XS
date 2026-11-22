/* Widescreen as a setting (docs/platforms.md, "Widescreen"). The game's
 * screen width (DISPLAY_WIDTH, gHostDisplayWidth: gba/defines.h) is 240,
 * the GBA's, or 288. It is set once, before the game's C++ data is: some
 * of that data's positions are worked out from it at start (the logos'
 * and menus' tables, from DISPLAY_ART_X), so EarlyWidth runs ahead of
 * every C++ initializer (constructor priority 101):
 *
 * - --widescreen or --no-widescreen on the command line (main.c reads
 *   them too) decides it for this launch;
 * - otherwise the "widescreen" setting (config.c's HostConfigPeekWidescreen:
 *   the settings.ini HostConfigInit will read), except headless (no
 *   settings read: 240 unless --widescreen).
 * - Changing the setting in the menu (HostWideSwitch) saves the settings
 *   and quits; HostExit (main.c) then starts the game again
 *   (HostWideRelaunch), the same executable with the same arguments. The
 *   width can't change under a running game: its screens, layers and
 *   positions were laid out for the old one.
 *
 * On Linux the new run replaces this process (execv: the same pid, as
 * RecompLauncher or a terminal sees it). On Windows it is started and
 * waited for, and its exit code is this one's, so whoever started the game
 * still sees it running until it ends. */

#include "gba/gba.h"
#include "host_platform.h"
#include "host_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#endif

#ifndef HOST_ANDROID

/* The command line's options that bear on the width: --widescreen 1,
 * --no-widescreen 2 (0: neither); *headless set by --headless. */
static int CommandLineWide(int *headless)
{
    int wide = 0;

    *headless = 0;
#ifdef _WIN32
    {
        const wchar_t *line = GetCommandLineW();
        static const wchar_t *const kNames[] = { L"--widescreen", L"--no-widescreen",
                                                 L"--headless" };
        int i;

        for (i = 0; i < 3; i++) {
            size_t len = wcslen(kNames[i]);
            const wchar_t *at = line;

            while ((at = wcsstr(at, kNames[i])) != NULL) {
                int before = at == line || at[-1] == L' ' || at[-1] == L'\t' || at[-1] == L'"';
                int after =
                    at[len] == L'\0' || at[len] == L' ' || at[len] == L'\t' || at[len] == L'"';

                if (before && after) {
                    if (i == 2) {
                        *headless = 1;
                    } else {
                        wide = i + 1;
                    }
                    break;
                }
                at += len;
            }
        }
    }
#else
    {
        /* NUL-separated arguments */
        static char args[65536];
        FILE *f = fopen("/proc/self/cmdline", "rb");
        size_t n;
        size_t at;

        if (f == NULL) {
            return 0;
        }
        n = fread(args, 1, sizeof(args) - 1, f);
        fclose(f);
        args[n] = '\0';
        for (at = 0; at < n; at += strlen(args + at) + 1) {
            if (strcmp(args + at, "--widescreen") == 0) {
                wide = 1;
            } else if (strcmp(args + at, "--no-widescreen") == 0) {
                wide = 2;
            } else if (strcmp(args + at, "--headless") == 0) {
                *headless = 1;
            }
        }
    }
#endif
    return wide;
}

__attribute__((constructor(101))) static void EarlyWidth(void)
{
    int headless;
    int wide = CommandLineWide(&headless);

    if (wide == 0) {
#ifdef HOST_SDL
        wide = !headless && HostConfigPeekWidescreen() ? 1 : 2;
#else
        wide = 2;
#endif
    }
    gHostDisplayWidth = wide == 1 ? HOST_SCREEN_WIDTH_MAX : HOST_GBA_WIDTH;
}

#endif /* !HOST_ANDROID */

#ifdef HOST_SDL

#include <SDL.h>

#ifndef _WIN32
#include <unistd.h>
#endif

static int sSwitch; /* HostWideSwitch: start again at the end */

#ifdef _WIN32

/* Starts this executable again with its arguments (the command line after
 * its first word), waits for it and ends with its exit code. */
static void Again(void)
{
    wchar_t self[MAX_PATH * 2];
    const wchar_t *line = GetCommandLineW();
    wchar_t *cmd;
    size_t len;
    DWORD n;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD code = 1;

    n = GetModuleFileNameW(NULL, self, MAX_PATH * 2);
    if (n == 0 || n >= MAX_PATH * 2) {
        return;
    }
    /* past the program's name, quoted or not */
    if (*line == L'"') {
        line++;
        while (*line != L'\0' && *line != L'"') {
            line++;
        }
        if (*line == L'"') {
            line++;
        }
    } else {
        while (*line != L'\0' && *line != L' ' && *line != L'\t') {
            line++;
        }
    }
    len = wcslen(self) + wcslen(line) + 4;
    cmd = malloc(len * sizeof(wchar_t));
    if (cmd == NULL) {
        return;
    }
    /* "self" + the rest, as given (no swprintf: its %s differs between
     * Windows's C runtimes) */
    cmd[0] = L'"';
    wcscpy(cmd + 1, self);
    wcscat(cmd, L"\"");
    wcscat(cmd, line);
    /* the width is the setting's now: not this launch's option */
    {
        static const wchar_t *const kOptions[] = { L" --widescreen", L" --no-widescreen" };
        int i;

        for (i = 0; i < 2; i++) {
            size_t olen = wcslen(kOptions[i]);
            wchar_t *at = cmd;

            while ((at = wcsstr(at, kOptions[i])) != NULL) {
                if (at[olen] == L'\0' || at[olen] == L' ' || at[olen] == L'\t') {
                    memmove(at, at + olen, (wcslen(at + olen) + 1) * sizeof(wchar_t));
                } else {
                    at += olen;
                }
            }
        }
    }
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (!CreateProcessW(self, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        free(cmd);
        return;
    }
    free(cmd);
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    ExitProcess(code);
}

#else

/* Replaces this process with this executable again, same arguments. */
static void Again(void)
{
    char self[4096];
    ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    char **argv;
    int i;
    int k = 1;

    if (n <= 0 || gHostArgv == NULL) {
        return;
    }
    self[n] = '\0';
    argv = calloc((size_t)gHostArgc + 1, sizeof(char *));
    if (argv == NULL) {
        return;
    }
    argv[0] = self;
    for (i = 1; i < gHostArgc; i++) {
        /* the width is the setting's now: not this launch's option */
        if (strcmp(gHostArgv[i], "--widescreen") != 0 &&
            strcmp(gHostArgv[i], "--no-widescreen") != 0) {
            argv[k++] = gHostArgv[i];
        }
    }
    fflush(NULL);
    execv(self, argv);
    free(argv);
}

#endif

int HostWideAvailable(void)
{
    return 1;
}

/* After the settings are read (sdl.c): the width was set before main
 * (EarlyWidth); the setting shows it, unless this launch's option chose. */
void HostWideStart(void)
{
    if (gHostOptions.wide == 0) {
        gHostSettings.widescreen = HOST_WIDE;
    }
}

void HostWideSwitch(void)
{
    gHostSettings.widescreen = !HOST_WIDE;
    sSwitch = 1;
    HostFrontendQuit();
}

void HostWideRelaunch(void)
{
    if (!sSwitch) {
        return;
    }
    sSwitch = 0;
    HostDiscordQuit();
    SDL_Quit();
    Again();
    /* it didn't start: the game ends, as it was quitting anyway */
}

#else /* no window: no setting to switch (the width is --widescreen's) */

int HostWideAvailable(void)
{
    return 0;
}

void HostWideStart(void)
{
}

void HostWideSwitch(void)
{
}

void HostWideRelaunch(void)
{
}

#endif /* HOST_SDL */
