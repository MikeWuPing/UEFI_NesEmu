#pragma once

#include <stdint.h>

//
// Opaque timer wrapper used by the emulator core. The implementation lives in
// uefi_timer.c and uses TSC + gBS->Stall to deliver sub-millisecond accuracy.
//
typedef struct Timer {
    void *timer;
} Timer;


void init_timer(Timer *timer, uint64_t period);
void mark_start(Timer *timer);
void mark_end(Timer *timer);
int adjusted_wait(Timer *timer);
int wait(uint64_t period_ms);
uint64_t get_diff_ms(Timer *timer);
void release_timer(Timer *timer);
