#pragma once

#include <Uefi.h>

#define MAX_ROMS         64
#define MAX_ROM_NAME     128
#define MAX_ROM_PATH     256

//
// Renders a full-screen ROM selection menu on top of the GOP and blocks
// until the user picks a file or cancels. On success, fills OutAsciiPath
// with a null-terminated ASCII path suitable for uefi_read_file (e.g.
// "ROM\\Contra.nes") and returns 0. Returns a negative value on cancel
// or when no ROMs were found.
//
INTN rom_browser_select(
    OUT CHAR8 *OutAsciiPath,
    IN  UINTN OutAsciiPathSize
    );
