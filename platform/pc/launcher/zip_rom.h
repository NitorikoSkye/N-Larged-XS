#ifndef GUARD_PC_LAUNCHER_ZIP_ROM_H
#define GUARD_PC_LAUNCHER_ZIP_ROM_H

#include <stddef.h>

/* zip_rom.c: a ROM kept in a .zip. */

/* Does `data` start as a zip archive does? */
int ZipIsArchive(const unsigned char *data, size_t size);

/* The archive's .gba (or its only file), unpacked and its CRC-32
 * checked; NULL if there is none or it can't be read. malloc'd. */
unsigned char *ZipRom(const unsigned char *zip, size_t zipSize, size_t *romSize, size_t limit);

#endif
