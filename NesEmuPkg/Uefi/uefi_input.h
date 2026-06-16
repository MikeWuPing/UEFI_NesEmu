#pragma once

#include <stdint.h>

//
// UEFI keyboard front-end. The emulator core still polls JoyPad->status, but
// the SDL-driven update_joypad path is gone; instead, emulator.c calls these
// helpers once per frame to:
//
//   1. drain the UEFI keyboard queue (uefi_input_poll)
//   2. expire stale "pressed" bits so releasing a key actually releases it
//      (UEFI simple-text-in has no key-up events, so we age each bit out
//       a few frames after we last saw it)
//   3. read back the resolved NES button mask (uefi_input_status)
//
// ESC, F5 and the pause toggle are sticky "events" rather than held-state,
// so they have dedicated query-and-clear accessors.
//
void uefi_input_init(void);

void uefi_input_poll(void);
void uefi_input_decay(void);

uint16_t uefi_input_status(int player);

int uefi_input_exit_requested(void);
int uefi_input_pause_toggled(void);
int uefi_input_reset_requested(void);

//
// Returns the most recently pressed character (used by the ROM browser to
// filter by name) or 0 if none.
//
uint16_t uefi_input_last_ascii(void);

//
// Non-blocking single-key poll. Returns 1 if a key was available and fills
// *OutKey, 0 otherwise. Convenience wrapper around ConIn->ReadKeyStroke.
//
int uefi_input_read_key(void *OutKey);
