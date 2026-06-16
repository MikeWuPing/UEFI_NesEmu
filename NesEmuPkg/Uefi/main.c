#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include "../Emu/emulator.h"
#include "../Emu/utils.h"
#include "rom_browser.h"
#include "uefi_gfx.h"
#include "uefi_input.h"
#include "uefi_timer.h"

#ifdef NESEMU_AUTORUN
STATIC CONST CHAR8 g_autorun_path[] = "ROM\\Contra (USA).nes";
#endif

//
// Top-level flow:
//
//   1. Initialise input + GOP
//   2. Show the ROM browser; user picks a .nes file or ESC to quit
//   3. Spin up the emulator for that ROM
//   4. When the emulator returns (ESC during play), go back to step 2
//
// The browser and the emulator both use the same GOP, so we rebind
// g_active_ctx on each transition. SetMode on the same mode is a no-op
// in OVMF, which keeps flicker off the critical path.
//
STATIC
VOID
EnterBrowserAndPlay (
    VOID
    )
{
    // The browser and the emulator each instantiate their own
    // GraphicsContext. free_emulator() clears g_active_ctx, which would
    // make the next browser iteration silently render nothing. To keep
    // things simple we re-establish a long-lived browser context here on
    // every loop iteration.
    GraphicsContext *BootCtx = AllocateZeroPool(sizeof(GraphicsContext));
    if (BootCtx == NULL) {
        return;
    }

    for (;;) {
        BootCtx->width  = 640;
        BootCtx->height = 480;
        BootCtx->scale  = 1;
        get_graphics_context(BootCtx);

        CHAR8 Path[256];
        SetMem(Path, sizeof(Path), 0);
        INTN Pick = rom_browser_select(Path, sizeof(Path));
        if (Pick < 0) {
            // User cancelled (only happens if no ROMs found).
            break;
        }

        LOG(INFO, "Selected ROM: %a", Path);

        Emulator Emu;
        init_emulator(&Emu, Path);
        run_emulator(&Emu);
        free_emulator(&Emu);

        // Re-arm the browser context so the next rom_browser_select()
        // actually has a valid g_active_ctx to draw on.
    }

    free_graphics(BootCtx);
    FreePool(BootCtx);
}

VOID NesEmuMain(VOID) {
    LOG(INFO, "NesEmu UEFI boot");
    uefi_input_init();

#ifdef NESEMU_AUTORUN
    // Smoke test path: skip the browser entirely and boot straight into a
    // pre-set ROM. Drives the whole CPU/PPU/MMU/Mapper pipeline so the
    // serial log captures any LOG(ERROR, ...) coming out of the emulator.
    LOG(INFO, "AUTORUN: %a", g_autorun_path);
    Emulator Emu;
    SetMem(&Emu, sizeof(Emu), 0);
    init_emulator(&Emu, g_autorun_path);
    run_emulator(&Emu);
    free_emulator(&Emu);
    LOG(INFO, "NesEmu AUTORUN shutdown");
    return;
#else
    EnterBrowserAndPlay();
    LOG(INFO, "NesEmu UEFI shutdown");
#endif
}
