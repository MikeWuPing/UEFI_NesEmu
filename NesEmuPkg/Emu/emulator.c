#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/BaseMemoryLib.h>

#include "emulator.h"
#include "controller.h"
#include "../Uefi/uefi_gfx.h"
#include "../Uefi/uefi_input.h"
#include "../Uefi/uefi_timer.h"

#include "mapper.h"
#include "utils.h"

static uint64_t PERIOD;
static uint16_t TURBO_SKIP;

void init_emulator(Emulator *emulator, const char *rom_path) {
    SetMem(emulator, sizeof(Emulator), 0);
    load_file((char *)rom_path, NULL, &emulator->mapper);
    emulator->type = emulator->mapper.type;
    emulator->mapper.emulator = emulator;
    if (emulator->type == PAL) {
        PERIOD = 20000000ULL;  // 1e9 / 50 fps
        TURBO_SKIP = PAL_FRAME_RATE / PAL_TURBO_RATE;
    } else {
        PERIOD = 16666667ULL;  // 1e9 / 60 fps
        TURBO_SKIP = NTSC_FRAME_RATE / NTSC_TURBO_RATE;
    }

    GraphicsContext *g_ctx = &emulator->g_ctx;
    g_ctx->width = 256;
    g_ctx->height = 240;
    g_ctx->scale = 2;
    g_ctx->is_tv = 0;

#if NAMETABLE_MODE
    g_ctx->width = 512;
    g_ctx->height = 480;
    g_ctx->scale = 1;
    if (emulator->mapper.is_nsf) {
        LOG(ERROR, "Can't run NSF Player in Nametable mode");
        quit(EXIT_FAILURE);
    }
    LOG(DEBUG, "RENDERING IN NAMETABLE MODE");
#endif
    get_graphics_context(g_ctx);

    init_mem(emulator);
    init_ppu(emulator);
    init_cpu(emulator);
    init_timer(&emulator->timer, PERIOD);

    emulator->exit = 0;
    emulator->pause = 0;
}


void run_emulator(Emulator *emulator) {
    struct JoyPad *joy1 = &emulator->mem.joy1;
    struct JoyPad *joy2 = &emulator->mem.joy2;
    struct PPU *ppu = &emulator->ppu;
    struct c6502 *cpu = &emulator->cpu;
    struct GraphicsContext *g_ctx = &emulator->g_ctx;
    struct Timer *timer = &emulator->timer;
    Timer frame_timer;
    init_timer(&frame_timer, PERIOD);
    mark_start(&frame_timer);

    while (!emulator->exit) {
#if PROFILE
        if (PROFILE_STOP_FRAME && ppu->frames >= PROFILE_STOP_FRAME)
            break;
#endif
        mark_start(timer);

        // Drain the UEFI keyboard queue and apply decay so that released
        // keys fall back to zero shortly after the user lets go.
        uefi_input_poll();
        uefi_input_decay();

        joy1->status = uefi_input_status(0);
        joy2->status = uefi_input_status(1);

        // ESC quits the emulator back to the ROM browser.
        if (uefi_input_exit_requested()) {
            emulator->exit = 1;
        }
        if (uefi_input_pause_toggled()) {
            emulator->pause ^= 1;
        }
        if (uefi_input_reset_requested()) {
            reset_emulator(emulator);
        }

        // trigger turbo events
        if (ppu->frames % TURBO_SKIP == 0) {
            turbo_trigger(joy1);
            turbo_trigger(joy2);
        }

        if (!emulator->pause) {
            // if ppu.render is set a frame is complete
            if (emulator->type == NTSC) {
                while (!ppu->render) {
                    execute_ppu(ppu);
                    execute_ppu(ppu);
                    execute_ppu(ppu);
                    execute(cpu);
                }
            } else {
                // PAL
                uint8_t check = 0;
                while (!ppu->render) {
                    execute_ppu(ppu);
                    execute_ppu(ppu);
                    execute_ppu(ppu);
                    check++;
                    if (check == 5) {
                        // on the fifth run execute an extra ppu clock
                        // this produces 3.2 scanlines per cpu clock
                        execute_ppu(ppu);
                        check = 0;
                    }
                    execute(cpu);
                }
            }
#if NAMETABLE_MODE
            render_name_tables(ppu, ppu->screen);
#endif
            render_graphics(g_ctx, ppu->screen);
            ppu->render = 0;
            mark_end(timer);
            adjusted_wait(timer);
        } else {
            wait(IDLE_SLEEP);
        }
    }

    mark_end(&frame_timer);
    emulator->time_diff = get_diff_ms(&frame_timer);
    release_timer(&frame_timer);
}

void reset_emulator(Emulator *emulator) {
    LOG(INFO, "Resetting emulator");
    reset_cpu(&emulator->cpu);
    reset_ppu(&emulator->ppu);
    if (emulator->mapper.reset != NULL) {
        emulator->mapper.reset(&emulator->mapper);
    }
}


void free_emulator(Emulator *emulator) {
    LOG(DEBUG, "Starting emulator clean up");
    exit_ppu(&emulator->ppu);
    free_mapper(&emulator->mapper);
    free_graphics(&emulator->g_ctx);
    release_timer(&emulator->timer);
    LOG(DEBUG, "Emulator session successfully terminated");
}
