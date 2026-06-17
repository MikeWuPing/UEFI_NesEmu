#pragma once

#include "cpu6502.h"
#include "ppu.h"
#include "mmu.h"
#include "mapper.h"
#include "../Uefi/uefi_gfx.h"
#include "../Uefi/uefi_timer.h"


// frame rate in Hz
#define NTSC_FRAME_RATE 60
#define PAL_FRAME_RATE 50

// turbo keys toggle rate (Hz)
// value should be a factor of FRAME_RATE
// and should never exceed FRAME_RATE for best result
#define NTSC_TURBO_RATE 30
#define PAL_TURBO_RATE 25

// sleep time when emulator is paused in milliseconds
#define IDLE_SLEEP 50


typedef struct Emulator {
    c6502 cpu;
    PPU ppu;
    Memory mem;
    Mapper mapper;
    GraphicsContext g_ctx;
    Timer timer;

    TVSystem type;

    uint64_t time_diff;

    uint8_t exit;
    uint8_t pause;
} Emulator;


void init_emulator(Emulator *emulator, const char *rom_path);
void reset_emulator(Emulator *emulator);
void run_emulator(Emulator *emulator);
void free_emulator(Emulator *emulator);
