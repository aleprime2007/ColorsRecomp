#ifndef COLORSRECOMP_DISC_H
#define COLORSRECOMP_DISC_H

#include "core/types.h"
#include <stdbool.h>

// Disc image service.
//
// The game reads the disc through IOS /dev/di. We reconstruct a virtual disc
// from the extracted files:
//   offset 0x000000  boot.bin          (32-byte disc ID + boot info)
//   offset 0x000440  bi2.bin
//   offset 0x002440  apploader.img
//   offset 0x80D900  fst.bin           (boot.bin +0x424 = FST offset>>2)
//   ...              every FST file entry at its own disc offset
//
// FST entry file offsets/lengths come straight from game/sys/fst.bin; each
// length matches exactly one extracted file under game/files.

void disc_init(void);

// Read `length` bytes at `offset` into `dst`. Unmapped bytes are zero-filled.
// Returns true if at least one byte came from a mapped region.
bool disc_read(u32 offset, u32 length, u8* dst);

// The raw FST image (for the apploader-style "load FST into MEM1" step).
const u8* disc_fst(u32* size);

// 32-byte disc boot header ("SNCE8P...").
const u8* disc_disk_id(void);

// Diagnostics: describe the region covering `offset` (NULL if none).
const char* disc_region_name(u32 offset);

#endif
