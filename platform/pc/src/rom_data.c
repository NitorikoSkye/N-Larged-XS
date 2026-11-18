/* The Android core's game data (docs/platforms.md, "Android"; HOST_ANDROID
 * builds only). The core in the APK has the game's data (everything the
 * game's and the port's objects keep in .rodata, .data and .data.rel.ro:
 * the graphics, the levels, the music, the texts, which come from the
 * ROM) blanked out, so that the app can be published; the app makes the
 * data back from the player's ROM (platform/android/tools/
 * make_core_package.py's package) into a file, and names it in
 * CRASHXS_DATA. Before anything of the game runs (a constructor that
 * runs first), the core writes it back where it belongs.
 *
 * The file (little-endian): "NLXSDAT1", u32 region count, u8[20] the
 * blanked core's build id (the package's: a file for another core is
 * refused), then each region's u64 address and u32 length, then the
 * regions' bytes in order. A region in a read-only segment is made
 * writable for the copy and read-only again (its pages are the process's
 * own copy then; nothing there ever runs).
 *
 * Without CRASHXS_DATA (the tests, a core that wasn't blanked) nothing
 * happens. */

#ifdef HOST_ANDROID

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <elf.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <unistd.h>

#include "gba/gba.h"
#include "host_platform.h"

/* The build id the package was made for (make_core_package.py writes
 * it here in the blanked core; zero in a core that was never blanked). */
const volatile u8 gHostDataBuildId[20] __attribute__((used, aligned(8))) = { 'N', 'L', 'X', 'S', 'B', 'I', 'D' };

static void Fail(const char *why)
{
    fprintf(stderr, "core: game data: %s\n", why);
    fflush(stderr);
    _exit(70);
}

static void ReadAll(FILE *f, void *p, size_t n)
{
    if (fread(p, 1, n, f) != n) {
        Fail("the file is cut short");
    }
}

__attribute__((constructor(101))) static void LoadGameData(void)
{
    const char *path = getenv("CRASHXS_DATA");
    long page = sysconf(_SC_PAGESIZE);
    u8 head[32];
    u32 count;
    u32 i;
    u64 *addr;
    u32 *len;
    FILE *f;

    if (path == NULL || path[0] == '\0') {
        return;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        Fail("can't open the file");
    }
    ReadAll(f, head, sizeof(head));
    if (memcmp(head, "NLXSDAT1", 8) != 0) {
        Fail("not a game data file");
    }
    if (memcmp(head + 12, (const void *)gHostDataBuildId, 20) != 0) {
        Fail("made for another build of the core");
    }
    memcpy(&count, head + 8, 4);
    addr = malloc(count * sizeof(u64));
    len = malloc(count * sizeof(u32));
    if (addr == NULL || len == NULL) {
        Fail("out of memory");
    }
    for (i = 0; i < count; i++) {
        ReadAll(f, &addr[i], 8);
        ReadAll(f, &len[i], 4);
    }
    for (i = 0; i < count; i++) {
        uintptr_t start = (uintptr_t)addr[i] & ~(uintptr_t)(page - 1);
        uintptr_t end = ((uintptr_t)addr[i] + len[i] + page - 1) & ~(uintptr_t)(page - 1);

        if (mprotect((void *)start, end - start, PROT_READ | PROT_WRITE) != 0) {
            Fail("can't write the core's data pages");
        }
        ReadAll(f, (void *)(uintptr_t)addr[i], len[i]);
    }
    /* back to read-only where nothing writable shares the pages (the
     * program headers say which segments are written) */
    for (i = 0; i < count; i++) {
        const Elf64_Phdr *ph = (const Elf64_Phdr *)getauxval(AT_PHDR);
        size_t n = getauxval(AT_PHNUM);
        uintptr_t start = (uintptr_t)addr[i] & ~(uintptr_t)(page - 1);
        uintptr_t end = ((uintptr_t)addr[i] + len[i] + page - 1) & ~(uintptr_t)(page - 1);
        int shared = ph == NULL;
        size_t k;

        for (k = 0; ph != NULL && k < n; k++) {
            uintptr_t a = (uintptr_t)ph[k].p_vaddr & ~(uintptr_t)(page - 1);
            uintptr_t b = (uintptr_t)(ph[k].p_vaddr + ph[k].p_memsz);

            if (ph[k].p_type == PT_LOAD && (ph[k].p_flags & PF_W) && a < end && b > start) {
                shared = 1;
            }
        }
        if (!shared) {
            mprotect((void *)start, end - start, PROT_READ);
        }
    }
    fclose(f);
    free(addr);
    free(len);
}

#endif /* HOST_ANDROID */
