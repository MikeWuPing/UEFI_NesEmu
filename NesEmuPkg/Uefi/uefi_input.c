#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/SimpleTextIn.h>

#include "uefi_input.h"
#include "../Emu/controller.h"
#include "../Emu/utils.h"

#define KEY_HOLD_TICKS  3   // ~3 frames at 60Hz (~50ms) before a key ages out

typedef struct {
    UINT16 Pressed;          // current NES button mask (player 1 only, player 2 unused)
    UINT8  Age[16];          // frames since each bit was last refreshed
} PlayerInput;

STATIC PlayerInput g_players[2];
STATIC EFI_SIMPLE_TEXT_INPUT_PROTOCOL *g_conin = NULL;
STATIC UINT64 g_tsc_freq_hz = 0;

// Sticky one-shot events
STATIC UINT8 g_exit_requested;
STATIC UINT8 g_pause_toggled;
STATIC UINT8 g_reset_requested;
STATIC UINT16 g_last_ascii;

STATIC
UINT16
MapKeyToNesMask (
    IN EFI_INPUT_KEY *Key
    )
{
    UINT16 Mask = 0;
    switch (Key->ScanCode) {
        case SCAN_UP:    Mask |= UP;    break;
        case SCAN_DOWN:  Mask |= DOWN;  break;
        case SCAN_LEFT:  Mask |= LEFT;  break;
        case SCAN_RIGHT: Mask |= RIGHT; break;
        default: break;
    }
    CHAR16 ch = Key->UnicodeChar;
    if (ch >= L'a' && ch <= L'z') ch = (CHAR16)(ch - L'a' + L'A');
    switch (ch) {
        case L'\r': // Enter
        case L'\n':
            Mask |= START;
            break;
        case L' ':
            Mask |= SELECT;
            break;
        case L'Z':
            Mask |= BUTTON_A;
            break;
        case L'X':
            Mask |= BUTTON_B;
            break;
        case L'A':
            Mask |= TURBO_A | BUTTON_A;
            break;
        case L'S':
            Mask |= TURBO_B | BUTTON_B;
            break;
        default:
            break;
    }
    return Mask;
}

void uefi_input_init(void) {
    if (g_conin == NULL) {
        g_conin = gST->ConIn;
    }
    SetMem(g_players, sizeof(g_players), 0);
    g_exit_requested = 0;
    g_pause_toggled = 0;
    g_reset_requested = 0;
    g_last_ascii = 0;

    // Drain any queued keys at startup.
    if (g_conin != NULL) {
        EFI_INPUT_KEY Key;
        while (g_conin->ReadKeyStroke(g_conin, &Key) == EFI_SUCCESS) {
            // discard
        }
    }
}

void uefi_input_poll(void) {
    if (g_conin == NULL) {
        g_conin = gST->ConIn;
        if (g_conin == NULL) return;
    }

    EFI_INPUT_KEY Key;
    while (g_conin->ReadKeyStroke(g_conin, &Key) == EFI_SUCCESS) {
        UINT16 Mask = MapKeyToNesMask(&Key);
        if (Mask) {
            g_players[0].Pressed |= Mask;
            for (UINTN b = 0; b < 16; b++) {
                if (Mask & (1u << b)) {
                    g_players[0].Age[b] = KEY_HOLD_TICKS;
                }
            }
        }
        // Track side-channel events.
        if (Key.ScanCode == SCAN_ESC) {
            g_exit_requested = 1;
        }
        if (Key.ScanCode == SCAN_F5) {
            g_reset_requested = 1;
        }
        CHAR16 ch = Key.UnicodeChar;
        if (ch == L'P' || ch == L'p') {
            g_pause_toggled = 1;
        }
        if (ch >= 0x20 && ch < 0x80) {
            g_last_ascii = ch;
        }
    }
}

void uefi_input_decay(void) {
    for (UINTN b = 0; b < 16; b++) {
        if (g_players[0].Pressed & (1u << b)) {
            if (g_players[0].Age[b] > 0) {
                g_players[0].Age[b]--;
            } else {
                g_players[0].Pressed &= (UINT16)~(1u << b);
            }
        }
    }
}

uint16_t uefi_input_status(int player) {
    if (player < 0 || player >= 2) return 0;
    return g_players[player].Pressed;
}

int uefi_input_exit_requested(void) {
    int v = g_exit_requested;
    g_exit_requested = 0;
    return v;
}

int uefi_input_pause_toggled(void) {
    int v = g_pause_toggled;
    g_pause_toggled = 0;
    return v;
}

int uefi_input_reset_requested(void) {
    int v = g_reset_requested;
    g_reset_requested = 0;
    return v;
}

uint16_t uefi_input_last_ascii(void) {
    UINT16 v = g_last_ascii;
    g_last_ascii = 0;
    return v;
}

int uefi_input_read_key(void *OutKey) {
    if (g_conin == NULL) {
        g_conin = gST->ConIn;
        if (g_conin == NULL) return 0;
    }
    EFI_INPUT_KEY *Key = (EFI_INPUT_KEY *)OutKey;
    if (Key == NULL) return 0;
    EFI_STATUS Status = g_conin->ReadKeyStroke(g_conin, Key);
    if (Status == EFI_SUCCESS) {
        return 1;
    }
    return 0;
}
