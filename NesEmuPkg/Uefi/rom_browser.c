#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/GraphicsOutput.h>

#include "rom_browser.h"
#include "uefi_gfx.h"
#include "uefi_fs.h"
#include "../Emu/utils.h"
#include "font.h"

#define BROWSER_TITLE     L"NesEmu - Select ROM"
#define BROWSER_HINT      L"\x2191/\x2193: Select   Enter: Play   Esc: Quit"

typedef struct {
    CHAR16 Names[MAX_ROMS][MAX_ROM_NAME];
    CHAR16 Paths[MAX_ROMS][MAX_ROM_PATH];
    INTN   Count;
} RomList;

STATIC
VOID
EFIAPI
BrowserCollectCallback (
    IN CONST CHAR16 *Directory,
    IN CONST CHAR16 *LeafName,
    IN VOID         *Ctx
    )
{
    RomList *List = (RomList *)Ctx;
    if (List->Count >= MAX_ROMS) {
        return;
    }
    UINTN DirLen = StrLen(Directory);
    UINTN LeafLen = StrLen(LeafName);
    if (DirLen + 1 + LeafLen + 1 > MAX_ROM_PATH) {
        return;
    }
    StrCpyS(List->Paths[List->Count], MAX_ROM_PATH, Directory);
    StrCatS(List->Paths[List->Count], MAX_ROM_PATH, L"\\");
    StrCatS(List->Paths[List->Count], MAX_ROM_PATH, LeafName);

    UINTN CopyLen = LeafLen + 1;
    if (CopyLen > MAX_ROM_NAME) CopyLen = MAX_ROM_NAME;
    StrCpyS(List->Names[List->Count], MAX_ROM_NAME, LeafName);

    List->Count++;
}

STATIC
VOID
DrawBrowser (
    IN RomList *List,
    IN INTN    Selected
    )
{
    NesColor black      = {  0,   0,   0, 0};
    NesColor title_color= {255, 255, 255, 0};
    NesColor normal     = {200, 200, 200, 0};
    NesColor highlight  = { 50, 130, 230, 0};
    NesColor highlight_tx = {255, 255, 255, 0};
    NesColor hint       = {140, 140, 140, 0};

    gfx_clear(&black);

    INT32 Scale = 2;
    // Row height is driven by the CJK cell (16 px tall) so ASCII and
    // Chinese filenames share a single comfortable line pitch.
    INT32 RowH = (CN_FONT_HEIGHT + 6) * Scale;       // 44 px at scale=2
    INT32 TitleY = 20;
    INT32 ListX = 40;
    INT32 ListY = TitleY + RowH * 2;                  // 108

    gfx_draw_text(ListX, TitleY, BROWSER_TITLE, &title_color, Scale);

    // Reserve room for the hint line at the bottom of the screen, then
    // derive MaxVisible from the actual GOP mode. The previous hardcoded
    // MaxVisible=13 was sized for the 5x8 font and ended up off-screen
    // after we switched to 16x16 CJK glyphs.
    GraphicsContext *Gfx = gfx_active_context();
    INT32 ScreenH = (Gfx != NULL) ? (INT32)Gfx->screen_height : 480;
    INT32 HintHeight = RowH + 20;
    INT32 UsableH = ScreenH - ListY - HintHeight;
    INTN MaxVisible = UsableH / RowH;
    if (MaxVisible < 1) MaxVisible = 1;

    INTN First = 0;
    if (List->Count > MaxVisible) {
        // Keep the selection centered once it goes past the middle of the
        // viewport; clamp at top/bottom so the list doesn't scroll past
        // either end.
        if (Selected >= List->Count - MaxVisible / 2) {
            First = List->Count - MaxVisible;
        } else if (Selected >= MaxVisible / 2) {
            First = Selected - MaxVisible / 2;
        }
        if (First < 0) First = 0;
    }
    INTN Last = First + MaxVisible;
    if (Last > List->Count) Last = List->Count;

    // Selection bar spans the full screen width minus a small gutter.
    INT32 BarWidth = 600;

    for (INTN i = First; i < Last; i++) {
        INT32 Y = ListY + (INT32)((i - First) * RowH);
        NesColor *TxColor = (i == Selected) ? &highlight_tx : &normal;
        if (i == Selected) {
            gfx_fill_rect(ListX - 6, Y - 3,
                          BarWidth, RowH, &highlight);
        }
        // Pass the raw UTF-16 filename; font_draw_char handles ASCII / CJK
        // dispatch, unknown glyphs render as a filled box.
        gfx_draw_text(ListX, Y, List->Names[i], TxColor, Scale);
    }

    INT32 HintY = ListY + MaxVisible * RowH + 20;
    if (HintY + RowH > ScreenH) {
        HintY = ScreenH - RowH - 4;
    }
    gfx_draw_text(ListX, HintY, BROWSER_HINT, &hint, Scale);
}

INTN rom_browser_select(
    OUT CHAR8 *OutAsciiPath,
    IN  UINTN OutAsciiPathSize
    )
{
    if (OutAsciiPath == NULL || OutAsciiPathSize == 0) {
        return -1;
    }

    RomList *List = AllocateZeroPool(sizeof(RomList));
    if (List == NULL) {
        return -1;
    }
    INTN Found = uefi_enumerate_files(L"ROM", ".nes", BrowserCollectCallback, List);
    LOG(INFO, "ROM browser found %d entries", Found);
    if (Found <= 0) {
        FreePool(List);
        return -1;
    }

    INTN Selected = 0;
    DrawBrowser(List, Selected);

    EFI_EVENT WaitEvent = gST->ConIn->WaitForKey;

    for (;;) {
        UINTN Index;
        EFI_STATUS Status = gBS->WaitForEvent(1, &WaitEvent, &Index);
        if (EFI_ERROR(Status)) {
            continue;
        }
        EFI_INPUT_KEY Key;
        Status = gST->ConIn->ReadKeyStroke(gST->ConIn, &Key);
        if (EFI_ERROR(Status)) {
            continue;
        }

        if (Key.ScanCode == SCAN_UP) {
            if (Selected > 0) {
                Selected--;
                DrawBrowser(List, Selected);
            }
            // Stop at the top instead of wrapping around; the wrap was
            // surprising in actual use.
        } else if (Key.ScanCode == SCAN_DOWN) {
            if (Selected < List->Count - 1) {
                Selected++;
                DrawBrowser(List, Selected);
            }
            // Stop at the bottom for the same reason.
        } else if (Key.ScanCode == SCAN_ESC) {
            // ESC in the browser is a no-op: the user already saw the
            // list, and exiting to the shell leaves the screen in an
            // awkward state because ConOut's graphics-console driver
            // isn't always cleanly restored by OVMF. To quit, close QEMU
            // (Ctrl-A X) or reboot.
            DrawBrowser(List, Selected);
        } else if (Key.UnicodeChar == L'\r' || Key.UnicodeChar == L'\n') {
            // Confirm - convert wide path to ASCII
            UINTN i;
            for (i = 0; i + 1 < OutAsciiPathSize && List->Paths[Selected][i] != 0; i++) {
                CHAR16 c = List->Paths[Selected][i];
                OutAsciiPath[i] = (c < 0x80) ? (CHAR8)c : '?';
            }
            OutAsciiPath[i] = 0;
            INTN Ret = 0;
            FreePool(List);
            return Ret;
        }
    }
}
