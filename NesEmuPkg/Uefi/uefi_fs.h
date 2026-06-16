#pragma once

#include <stdint.h>
#include <Uefi.h>

//
// Searches every EFI_SIMPLE_FILE_SYSTEM_PROTOCOL currently available and
// returns the contents of the first file that matches `path` (relative to
// that volume's root).  On success the buffer is allocated with AllocatePool
// and ownership transfers to the caller; on failure *buffer is left as NULL
// and the function returns a negative value.
//
long long uefi_read_file(const char *path, uint8_t **buffer);

//
// Enumerate files matching `extension` (case-insensitive ASCII, e.g. ".nes")
// under `Directory` (e.g. L"ROM").  For each match the callback is invoked
// with the directory path, the leaf file name, and the caller context.
// Returns the total match count, or a negative value on protocol error.
//
typedef void (*RomEnumeratorCallback)(
    IN CONST CHAR16 *Directory,
    IN CONST CHAR16 *LeafName,
    IN VOID         *Ctx
    );

int uefi_enumerate_files(
    IN CONST CHAR16            *Directory,
    IN CONST CHAR8             *Extension,
    IN RomEnumeratorCallback   Callback,
    IN VOID                    *Ctx
    );
