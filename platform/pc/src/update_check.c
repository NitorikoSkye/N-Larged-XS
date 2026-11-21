/* The update check (docs/platforms.md, "Update check"; General's "update
 * check", off by default): once a launch, the port's latest release on
 * GitHub against this build's HOST_PORT_VERSION; a newer one puts
 * "update available" in the title screen's top right corner
 * (stats_hooks.cpp draws it), and a click or a tap on it opens the
 * releases page.
 *
 * Nothing touches the network while the setting is off: the check starts
 * on the first frame the setting is on (at launch, or when it is turned
 * on), and only once a launch. It asks GitHub's API for the latest
 * release (api.github.com/repos/OWNER/REPO/releases/latest, its
 * "tag_name") on a thread of its own:
 * - Windows: WinHTTP (the system's own HTTPS, its proxy settings);
 * - Linux: libcurl, if the system has it (opened at run time, so the
 *   game doesn't need it), or else the curl command;
 * - Android: the app (stream.c's message 10 asks, its 11 answers, with
 *   the tag's three numbers), which has the network permission.
 * A failure (no network, GitHub unreachable, an answer without a tag)
 * shows nothing.
 *
 * Headless runs never check; HOST_UPDATE_LATEST=vX.Y.Z there stands in
 * for GitHub's answer (the tests and screenshots). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gba/gba.h"
#include "host_platform.h"
#include "host_ui.h"

#define UPDATE_OWNER_REPO "NitorikoSkye/N-Larged-XS"
#define UPDATE_API_URL    "https://api.github.com/repos/" UPDATE_OWNER_REPO "/releases/latest"
#define UPDATE_PAGE_URL   "https://github.com/" UPDATE_OWNER_REPO "/releases"
#define UPDATE_AGENT      "N-Larged-XS-update-check"

enum {
    UPDATE_IDLE,     /* not checked (the setting off, so far) */
    UPDATE_CHECKING, /* the thread (or the app) is asking */
    UPDATE_CURRENT,  /* this build is the latest, or newer */
    UPDATE_NEWER,    /* a newer release: shown */
    UPDATE_FAILED,
};

static volatile int sState = UPDATE_IDLE;

/* "v0.52.1" (or "0.52"): major, minor, patch; 0 if it isn't a version. */
int HostParseVersion(const char *s, int out[3])
{
    int n = 0;

    out[0] = out[1] = out[2] = 0;
    if (s == NULL) {
        return 0;
    }
    while (*s == ' ' || *s == '"') {
        s++;
    }
    if (*s == 'v' || *s == 'V') {
        s++;
    }
    while (n < 3 && *s >= '0' && *s <= '9') {
        int v = 0;

        while (*s >= '0' && *s <= '9') {
            v = v * 10 + (*s - '0');
            s++;
        }
        out[n++] = v;
        if (*s != '.') {
            break;
        }
        s++;
    }
    return n >= 2;
}

/* The latest release's version, from GitHub: newer than this build? */
static void Answer(const int latest[3])
{
    int mine[3];
    int i;
    int newer = 0;

    HostParseVersion(HOST_PORT_VERSION, mine);
    for (i = 0; i < 3; i++) {
        if (latest[i] != mine[i]) {
            newer = latest[i] > mine[i];
            break;
        }
    }
    __sync_synchronize();
    sState = newer ? UPDATE_NEWER : UPDATE_CURRENT;
    HostLogf("update check: latest %d.%d.%d, this %s: %s\n", latest[0], latest[1], latest[2],
             HOST_PORT_VERSION, newer ? "update available" : "up to date");
}

/* The tag in the API's JSON ("tag_name": "v0.52"), as numbers. */
static int ParseTag(const char *json, int out[3])
{
    const char *at = json != NULL ? strstr(json, "\"tag_name\"") : NULL;

    if (at == NULL) {
        return 0;
    }
    at = strchr(at + 10, ':');
    if (at == NULL) {
        return 0;
    }
    at++;
    while (*at == ' ' || *at == '\t' || *at == '\r' || *at == '\n') {
        at++;
    }
    return HostParseVersion(at, out);
}

static void Failed(const char *why)
{
    __sync_synchronize();
    sState = UPDATE_FAILED;
    HostLogf("update check: %s\n", why);
}

#if defined(HOST_SDL) && !defined(HOST_ANDROID)

#include <SDL.h>

/* The answer's body, up to this much (the tag is near its start). */
#define BODY_MAX (256 * 1024)

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>

static char *Fetch(void)
{
    HINTERNET session;
    HINTERNET connect = NULL;
    HINTERNET request = NULL;
    char *body = NULL;
    DWORD len = 0;
    DWORD status = 0;
    DWORD size = sizeof(status);

    session = WinHttpOpen(L"" UPDATE_AGENT, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == NULL) {
        /* before Windows 8.1: the default proxy settings */
        session = WinHttpOpen(L"" UPDATE_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (session == NULL) {
        return NULL;
    }
    WinHttpSetTimeouts(session, 10000, 10000, 10000, 15000);
    connect = WinHttpConnect(session, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (connect != NULL) {
        request = WinHttpOpenRequest(
            connect, L"GET", L"/repos/" UPDATE_OWNER_REPO L"/releases/latest", NULL,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    }
    if (request != NULL &&
        WinHttpSendRequest(request, L"Accept: application/vnd.github+json\r\n", (DWORD)-1L,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, NULL) &&
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                            WINHTTP_NO_HEADER_INDEX) &&
        status == 200) {
        body = malloc(BODY_MAX + 1);
        while (body != NULL && len < BODY_MAX) {
            DWORD got = 0;

            if (!WinHttpReadData(request, body + len, BODY_MAX - len, &got) || got == 0) {
                break;
            }
            len += got;
        }
        if (body != NULL) {
            body[len] = '\0';
        }
    }
    if (request != NULL) {
        WinHttpCloseHandle(request);
    }
    if (connect != NULL) {
        WinHttpCloseHandle(connect);
    }
    WinHttpCloseHandle(session);
    return body;
}

#else /* Linux */

#include <dlfcn.h>

struct Body {
    char *data;
    size_t len;
};

static size_t Write(char *ptr, size_t size, size_t n, void *user)
{
    struct Body *b = user;
    size_t add = size * n;

    if (b->len + add > BODY_MAX) {
        add = BODY_MAX - b->len;
    }
    memcpy(b->data + b->len, ptr, add);
    b->len += add;
    b->data[b->len] = '\0';
    return size * n; /* the rest is dropped, not an error */
}

/* libcurl's few functions, by name (curl/curl.h's option numbers). */
#define CURLOPT_WRITEDATA      10001
#define CURLOPT_URL            10002
#define CURLOPT_USERAGENT      10018
#define CURLOPT_HTTPHEADER     10023
#define CURLOPT_WRITEFUNCTION  20011
#define CURLOPT_FOLLOWLOCATION 52
#define CURLOPT_FAILONERROR    45
#define CURLOPT_NOSIGNAL       99
#define CURLOPT_TIMEOUT        13
#define CURLOPT_CONNECTTIMEOUT 78

static char *FetchLibcurl(void)
{
    static const char *const names[] = { "libcurl.so.4", "libcurl-gnutls.so.4", "libcurl.so" };
    void *lib = NULL;
    void *(*easyInit)(void);
    int (*setopt)(void *, int, ...);
    int (*perform)(void *);
    void (*cleanup)(void *);
    void *(*listAppend)(void *, const char *);
    void (*listFree)(void *);
    struct Body b;
    void *curl;
    void *headers = NULL;
    size_t i;
    int rc;

    for (i = 0; i < sizeof(names) / sizeof(names[0]) && lib == NULL; i++) {
        lib = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
    }
    if (lib == NULL) {
        return NULL;
    }
    *(void **)&easyInit = dlsym(lib, "curl_easy_init");
    *(void **)&setopt = dlsym(lib, "curl_easy_setopt");
    *(void **)&perform = dlsym(lib, "curl_easy_perform");
    *(void **)&cleanup = dlsym(lib, "curl_easy_cleanup");
    *(void **)&listAppend = dlsym(lib, "curl_slist_append");
    *(void **)&listFree = dlsym(lib, "curl_slist_free_all");
    if (easyInit == NULL || setopt == NULL || perform == NULL || cleanup == NULL ||
        (curl = easyInit()) == NULL) {
        dlclose(lib);
        return NULL;
    }
    b.data = malloc(BODY_MAX + 1);
    b.len = 0;
    if (b.data == NULL) {
        cleanup(curl);
        dlclose(lib);
        return NULL;
    }
    b.data[0] = '\0';
    if (listAppend != NULL) {
        headers = listAppend(NULL, "Accept: application/vnd.github+json");
    }
    setopt(curl, CURLOPT_URL, UPDATE_API_URL);
    setopt(curl, CURLOPT_USERAGENT, UPDATE_AGENT);
    if (headers != NULL) {
        setopt(curl, CURLOPT_HTTPHEADER, headers);
    }
    setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    setopt(curl, CURLOPT_FAILONERROR, 1L);
    setopt(curl, CURLOPT_NOSIGNAL, 1L);
    setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    setopt(curl, CURLOPT_TIMEOUT, 20L);
    setopt(curl, CURLOPT_WRITEFUNCTION, Write);
    setopt(curl, CURLOPT_WRITEDATA, &b);
    rc = perform(curl);
    cleanup(curl);
    if (headers != NULL && listFree != NULL) {
        listFree(headers);
    }
    dlclose(lib);
    if (rc != 0) {
        free(b.data);
        return NULL;
    }
    return b.data;
}

/* Without libcurl: the curl command, if there is one. */
static char *FetchCommand(void)
{
    FILE *p = popen("curl -fsSL --connect-timeout 10 --max-time 20 -A " UPDATE_AGENT
                    " -H 'Accept: application/vnd.github+json' " UPDATE_API_URL " 2>/dev/null",
                    "r");
    char *body;
    size_t len = 0;
    size_t got;

    if (p == NULL) {
        return NULL;
    }
    body = malloc(BODY_MAX + 1);
    while (body != NULL && len < BODY_MAX && (got = fread(body + len, 1, BODY_MAX - len, p)) > 0) {
        len += got;
    }
    if (body != NULL) {
        body[len] = '\0';
    }
    if (pclose(p) != 0 && body != NULL) {
        free(body);
        return NULL;
    }
    return body;
}

static char *Fetch(void)
{
    char *body = FetchLibcurl();

    return body != NULL ? body : FetchCommand();
}

#endif

static int Thread(void *unused)
{
    char *body = Fetch();
    int latest[3];

    (void)unused;
    if (body == NULL) {
        Failed("no answer from github");
    } else if (!ParseTag(body, latest)) {
        Failed("no release tag in github's answer");
    } else {
        Answer(latest);
    }
    free(body);
    return 0;
}

static void Start(void)
{
    SDL_Thread *t = SDL_CreateThread(Thread, "update check", NULL);

    if (t == NULL) {
        Failed("no thread");
        return;
    }
    SDL_DetachThread(t);
}

void HostUpdateOpenPage(void)
{
    if (SDL_OpenURL(UPDATE_PAGE_URL) != 0) {
        HostLogf("update check: can't open %s: %s\n", UPDATE_PAGE_URL, SDL_GetError());
    }
}

#elif defined(HOST_ANDROID)

extern int gHostStream;
void HostStreamUpdate(int op); /* stream.c: 0 check, 1 open the page */

static void Start(void)
{
    if (!gHostStream) {
        Failed("no app");
        return;
    }
    HostStreamUpdate(0);
}

void HostUpdateOpenPage(void)
{
    HostStreamUpdate(1);
}

/* The app's answer (stream.c's message 11): the latest tag's numbers, or
 * 0xFFFF for none. */
void HostUpdateAppAnswer(int major, int minor, int patch)
{
    int latest[3];

    if (sState != UPDATE_CHECKING) {
        return;
    }
    if (major == 0xFFFF) {
        Failed("the app got no answer from github");
        return;
    }
    latest[0] = major;
    latest[1] = minor;
    latest[2] = patch;
    Answer(latest);
}

#else /* no window: headless only */

static void Start(void)
{
    Failed("no network in this build");
}

void HostUpdateOpenPage(void)
{
    HostLogf("update check: open %s\n", UPDATE_PAGE_URL);
}

#endif

/* Once a frame (stats.c's HostStatsFrame, from the keys' read): starts
 * the check the first frame the setting is on. */
void HostUpdateFrame(void)
{
    static int sTest = -1;

    if (sState != UPDATE_IDLE) {
        return;
    }
    if (gHostOptions.headless) {
        int latest[3];

        if (sTest < 0) {
            const char *e = getenv("HOST_UPDATE_LATEST");

            sTest = e != NULL && HostParseVersion(e, latest);
            if (sTest) {
                Answer(latest);
            }
        }
        return;
    }
    if (!gHostSettings.updateCheck) {
        return;
    }
    sState = UPDATE_CHECKING;
    Start();
}

int HostUpdateAvailable(void)
{
    return sState == UPDATE_NEWER;
}
