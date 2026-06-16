#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/PrintLib.h>
#include "../Emu/utils.h"

extern VOID NesEmuMain(VOID);

//
// UEFI application entry point. Delegates immediately to the NES emulator
// main loop so the entry stub stays trivial and side-effect free.
//
EFI_STATUS
EFIAPI
UefiMain (
    IN EFI_HANDLE        ImageHandle,
    IN EFI_SYSTEM_TABLE  *SystemTable
    )
{
    SystemTable->ConOut->OutputString(SystemTable->ConOut,
        L"[NesEmu] UefiMain reached\r\n");
    NesEmuMain();
    SystemTable->ConOut->OutputString(SystemTable->ConOut,
        L"[NesEmu] NesEmuMain returned\r\n");

    // NesEmu switches the GOP into a graphics mode, which leaves the
    // parent shell's ConOut attached to GraphicsConsole. Switching back
    // to text mode (mode 0 = 80x25) makes the shell prompt readable
    // again instead of freezing the screen.
    SystemTable->ConOut->SetMode(SystemTable->ConOut, 0);
    SystemTable->ConOut->ClearScreen(SystemTable->ConOut);
    SystemTable->ConOut->EnableCursor(SystemTable->ConOut, TRUE);
    return EFI_SUCCESS;
}
