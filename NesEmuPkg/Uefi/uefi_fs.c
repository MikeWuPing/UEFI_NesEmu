#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>

#include "uefi_fs.h"
#include "../Emu/utils.h"
#include <stdint.h>

#define MAX_ROM_LIST 256
#define DIR_READ_BUF_SIZE  (SIZE_OF_EFI_FILE_INFO + 256 * sizeof(CHAR16))

STATIC
BOOLEAN
EndsWithIgnoreCaseW (
    IN CONST CHAR16 *Haystack,
    IN CONST CHAR8  *NeedleAscii
    )
{
    if (Haystack == NULL || NeedleAscii == NULL) {
        return FALSE;
    }
    UINTN HL = StrLen(Haystack);
    UINTN NL = AsciiStrLen(NeedleAscii);
    if (NL == 0 || NL > HL) {
        return FALSE;
    }
    for (UINTN i = 0; i < NL; i++) {
        CHAR16 hc = Haystack[HL - NL + i];
        CHAR8  nc = NeedleAscii[i];
        if (hc >= L'A' && hc <= L'Z') hc = (CHAR16)(hc - L'A' + L'a');
        CHAR8 ncl = (nc >= 'A' && nc <= 'Z') ? (CHAR8)(nc - 'A' + 'a') : nc;
        if (hc != (CHAR16)(UINT8)ncl) {
            return FALSE;
        }
    }
    return TRUE;
}

long long uefi_read_file(const char *path, uint8_t **buffer) {
    *buffer = NULL;
    if (path == NULL) {
        return -1;
    }

    UINTN AL = AsciiStrLen(path);
    CHAR16 *WPath = AllocatePool((AL + 1) * sizeof(CHAR16));
    if (WPath == NULL) {
        return -1;
    }
    for (UINTN i = 0; i <= AL; i++) {
        WPath[i] = (CHAR16)(UINT8)path[i];
    }

    EFI_STATUS Status;
    UINTN NumHandles;
    EFI_HANDLE *Handles;
    Status = gBS->LocateHandleBuffer(
        ByProtocol,
        &gEfiSimpleFileSystemProtocolGuid,
        NULL,
        &NumHandles,
        &Handles);
    if (EFI_ERROR(Status)) {
        FreePool(WPath);
        LOG(ERROR, "LocateHandleBuffer(SimpleFs) failed: %r", Status);
        return -1;
    }

    long long Result = -1;
    for (UINTN i = 0; i < NumHandles; i++) {
        EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs = NULL;
        Status = gBS->HandleProtocol(
            Handles[i],
            &gEfiSimpleFileSystemProtocolGuid,
            (VOID **)&Fs);
        if (EFI_ERROR(Status) || Fs == NULL) {
            continue;
        }

        EFI_FILE_PROTOCOL *Root = NULL;
        Status = Fs->OpenVolume(Fs, &Root);
        if (EFI_ERROR(Status) || Root == NULL) {
            continue;
        }

        EFI_FILE_PROTOCOL *File = NULL;
        Status = Root->Open(Root, &File, WPath, EFI_FILE_MODE_READ, 0);
        Root->Close(Root);
        if (EFI_ERROR(Status) || File == NULL) {
            continue;
        }

        UINTN InfoSize = 0;
        EFI_FILE_INFO *Info = NULL;
        Status = File->GetInfo(File, &gEfiFileInfoGuid, &InfoSize, NULL);
        if (Status == EFI_BUFFER_TOO_SMALL && InfoSize > 0) {
            Info = AllocatePool(InfoSize);
            if (Info != NULL) {
                Status = File->GetInfo(File, &gEfiFileInfoGuid, &InfoSize, Info);
            }
        }
        if (EFI_ERROR(Status) || Info == NULL) {
            File->Close(File);
            if (Info != NULL) FreePool(Info);
            continue;
        }

        UINT64 FileSize = Info->FileSize;
        FreePool(Info);

        UINT8 *Buf = AllocatePool((UINTN)FileSize);
        if (Buf == NULL) {
            File->Close(File);
            continue;
        }

        UINTN ReadSize = (UINTN)FileSize;
        Status = File->Read(File, &ReadSize, Buf);
        File->Close(File);
        if (EFI_ERROR(Status) || ReadSize != (UINTN)FileSize) {
            FreePool(Buf);
            continue;
        }

        *buffer = Buf;
        Result = (long long)FileSize;
        LOG(INFO, "Loaded %a (%llu bytes) from volume #%llu", path, FileSize, (UINT64)i);
        break;
    }

    FreePool(Handles);
    FreePool(WPath);
    if (Result < 0) {
        LOG(ERROR, "ROM %a not found on any mounted volume", path);
    }
    return Result;
}

int uefi_enumerate_files(
    IN CONST CHAR16            *Directory,
    IN CONST CHAR8             *Extension,
    IN RomEnumeratorCallback   Callback,
    IN VOID                    *Ctx
    )
{
    if (Callback == NULL) {
        return -1;
    }

    EFI_STATUS Status;
    UINTN NumHandles;
    EFI_HANDLE *Handles;
    Status = gBS->LocateHandleBuffer(
        ByProtocol,
        &gEfiSimpleFileSystemProtocolGuid,
        NULL,
        &NumHandles,
        &Handles);
    if (EFI_ERROR(Status)) {
        return -1;
    }

    int TotalMatches = 0;
    UINT8 *EntryBuf = AllocatePool(DIR_READ_BUF_SIZE);
    if (EntryBuf == NULL) {
        FreePool(Handles);
        return -1;
    }

    for (UINTN i = 0; i < NumHandles && TotalMatches < MAX_ROM_LIST; i++) {
        EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs = NULL;
        Status = gBS->HandleProtocol(
            Handles[i],
            &gEfiSimpleFileSystemProtocolGuid,
            (VOID **)&Fs);
        if (EFI_ERROR(Status) || Fs == NULL) {
            continue;
        }

        EFI_FILE_PROTOCOL *Root = NULL;
        Status = Fs->OpenVolume(Fs, &Root);
        if (EFI_ERROR(Status) || Root == NULL) {
            continue;
        }

        EFI_FILE_PROTOCOL *Dir = NULL;
        Status = Root->Open(Root, &Dir, (CHAR16 *)Directory, EFI_FILE_MODE_READ, EFI_FILE_DIRECTORY);
        Root->Close(Root);
        if (EFI_ERROR(Status) || Dir == NULL) {
            continue;
        }

        for (;;) {
            UINTN ThisRead = DIR_READ_BUF_SIZE;
            Status = Dir->Read(Dir, &ThisRead, EntryBuf);
            if (EFI_ERROR(Status) || ThisRead == 0) {
                break;
            }
            EFI_FILE_INFO *Info = (EFI_FILE_INFO *)EntryBuf;
            if ((Info->Attribute & EFI_FILE_DIRECTORY) != 0) {
                continue;
            }
            if (!EndsWithIgnoreCaseW(Info->FileName, Extension)) {
                continue;
            }
            Callback(Directory, Info->FileName, Ctx);
            TotalMatches++;
            if (TotalMatches >= MAX_ROM_LIST) {
                break;
            }
        }

        Dir->Close(Dir);
    }

    FreePool(EntryBuf);
    FreePool(Handles);
    return TotalMatches;
}
