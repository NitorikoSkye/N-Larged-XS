/* Widescreen as a setting (docs/platforms.md, "Widescreen"): the screen's
 * width is fixed when the game is built, so the game is built twice, the
 * usual 240x160 one and a 288x160 one (platform.mk's HOST_WIDE), and the
 * launcher makes both. The "widescreen" setting says which one to play:
 *
 * - At start (HostWideHandover, from sdl.c's HostFrontendInit), the build
 *   the setting doesn't ask for hands over to the other one, with the same
 *   arguments.
 * - Changing it in the menu (HostWideSwitch) saves the settings and quits;
 *   HostExit (main.c) then starts the other build (HostWideRelaunch).
 *
 * On Linux the other build replaces this process (execv: the same pid, as
 * RecompLauncher or a terminal sees it). On Windows the other build is
 * started and waited for, and its exit code is this one's, so whoever
 * started the game still sees it running until it ends.
 *
 * The other build is the file beside this one named as its pair below; if
 * it isn't there (the game built by hand, say), the setting is grey and
 * nothing hands over. */

#include "gba/gba.h"
#include "host_platform.h"
#include "host_ui.h"

#ifdef HOST_SDL

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef HOST_WIDE_BG
#define HOST_IS_WIDE 1
#else
#define HOST_IS_WIDE 0
#endif

/* The usual build's name, the wide build's: the launcher's games
 * (launcher.c's GAME_FILE, GAME_WIDE_FILE), then `make`'s. */
static const char *const sPairs[][2] = {
    { "Crash Bandicoot XS.exe", "Crash Bandicoot XS Wide.exe" },
    { "crash-bandicoot-xs", "crash-bandicoot-xs-wide" },
    { "crashbandicootxs_pc", "crashbandicootxs_pc_wide288" },
    { "crashbandicootxs_pc.exe", "crashbandicootxs_pc_wide288.exe" },
};

#define PAIR_COUNT ((int)(sizeof(sPairs) / sizeof(sPairs[0])))

static int sSwitch; /* HostWideSwitch: start the other build at the end */

#ifdef _WIN32

static wchar_t sOther[MAX_PATH * 2];
static int sOtherState; /* 0 not looked, 1 there, -1 not */

static int FindOther(void)
{
    wchar_t self[MAX_PATH * 2];
    DWORD n;
    wchar_t *name;
    int i;

    if (sOtherState != 0) {
        return sOtherState > 0;
    }
    sOtherState = -1;
    n = GetModuleFileNameW(NULL, self, MAX_PATH * 2);
    if (n == 0 || n >= MAX_PATH * 2) {
        return 0;
    }
    name = wcsrchr(self, L'\\');
    name = name != NULL ? name + 1 : self;
    for (i = 0; i < PAIR_COUNT; i++) {
        wchar_t mine[128];
        wchar_t other[128];
        DWORD attrs;

        MultiByteToWideChar(CP_UTF8, 0, sPairs[i][HOST_IS_WIDE], -1, mine, 128);
        MultiByteToWideChar(CP_UTF8, 0, sPairs[i][!HOST_IS_WIDE], -1, other, 128);
        if (_wcsicmp(name, mine) != 0) {
            continue;
        }
        if ((size_t)(name - self) + wcslen(other) + 1 > MAX_PATH * 2) {
            return 0;
        }
        memcpy(sOther, self, (size_t)(name - self) * sizeof(wchar_t));
        wcscpy(sOther + (name - self), other);
        attrs = GetFileAttributesW(sOther);
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            sOtherState = 1;
        }
        break;
    }
    return sOtherState > 0;
}

/* Starts the other build with this one's arguments (the command line
 * after its first word), waits for it and ends with its exit code. */
static void Become(void)
{
    const wchar_t *line = GetCommandLineW();
    wchar_t *cmd;
    size_t len;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD code = 1;

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
    len = wcslen(sOther) + wcslen(line) + 4;
    cmd = malloc(len * sizeof(wchar_t));
    if (cmd == NULL) {
        return;
    }
    /* "other build" + the rest, as given (no swprintf: its %s differs
     * between Windows's C runtimes) */
    cmd[0] = L'"';
    wcscpy(cmd + 1, sOther);
    wcscat(cmd, L"\"");
    wcscat(cmd, line);
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    SetEnvironmentVariableW(L"NLXS_HANDED_OVER", L"1");
    if (!CreateProcessW(sOther, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        free(cmd);
        return; /* this build plays on */
    }
    free(cmd);
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    ExitProcess(code);
}

#else

static char sOther[4096];
static int sOtherState;

static int FindOther(void)
{
    char self[4096];
    ssize_t n;
    char *name;
    int i;

    if (sOtherState != 0) {
        return sOtherState > 0;
    }
    sOtherState = -1;
    n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n <= 0) {
        return 0;
    }
    self[n] = '\0';
    name = strrchr(self, '/');
    name = name != NULL ? name + 1 : self;
    for (i = 0; i < PAIR_COUNT; i++) {
        struct stat st;

        if (strcmp(name, sPairs[i][HOST_IS_WIDE]) != 0) {
            continue;
        }
        snprintf(sOther, sizeof(sOther), "%.*s%s", (int)(name - self), self,
                 sPairs[i][!HOST_IS_WIDE]);
        if (stat(sOther, &st) == 0 && S_ISREG(st.st_mode) && access(sOther, X_OK) == 0) {
            sOtherState = 1;
        }
        break;
    }
    return sOtherState > 0;
}

/* Replaces this process with the other build, with the same arguments. */
static void Become(void)
{
    char **argv = gHostArgv;
    char *first = argv[0];

    setenv("NLXS_HANDED_OVER", "1", 1);
    fflush(NULL);
    argv[0] = sOther;
    execv(sOther, argv);
    argv[0] = first; /* this build plays on */
}

#endif

int HostWideAvailable(void)
{
    return FindOther();
}

void HostWideHandover(void)
{
    const char *done = SDL_getenv("NLXS_HANDED_OVER");

    if (gHostSettings.widescreen == HOST_IS_WIDE || gHostArgv == NULL ||
        (done != NULL && done[0] == '1') || !FindOther()) {
        /* the setting says what this build is (the other one's missing,
         * say, or this one was started on its own) */
        gHostSettings.widescreen = HOST_IS_WIDE;
        return;
    }
    SDL_Quit();
    Become();
    /* it didn't start: this build plays, and says what it is */
    gHostSettings.widescreen = HOST_IS_WIDE;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        exit(1);
    }
}

void HostWideSwitch(void)
{
    if (!FindOther()) {
        return;
    }
    gHostSettings.widescreen = !HOST_IS_WIDE;
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
    Become();
}

#else /* no window: one build, nothing to switch to */

int HostWideAvailable(void)
{
    return 0;
}

void HostWideHandover(void)
{
}

void HostWideSwitch(void)
{
}

void HostWideRelaunch(void)
{
}

#endif /* HOST_SDL */
