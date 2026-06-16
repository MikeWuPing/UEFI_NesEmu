#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>

#include "uefi_timer.h"

#define NS_PER_S  1000000000ULL
#define US_PER_S  1000000ULL

typedef struct TimerInternal {
    UINT64 period_ns;
    UINT64 tsc_start;
    UINT64 tsc_end;
    UINT64 tsc_diff_ns;
} TimerInternal;

STATIC UINT64 g_tsc_freq_hz = 0;

STATIC
UINT64
CalibrateTsc (
    VOID
    )
{
    UINT64 T0 = AsmReadTsc();
    gBS->Stall(1000);  // 1 ms
    UINT64 T1 = AsmReadTsc();
    UINT64 Delta = T1 - T0;
    if (Delta == 0) {
        return 1000000;  // 1 MHz fallback if TSC unsupported
    }
    return Delta * 1000;  // ticks per second (Hz)
}

void init_timer(Timer *timer, uint64_t period) {
    if (g_tsc_freq_hz == 0) {
        g_tsc_freq_hz = CalibrateTsc();
    }
    TimerInternal *t = AllocateZeroPool(sizeof(TimerInternal));
    timer->timer = t;
    t->period_ns = period;
    t->tsc_start = 0;
    t->tsc_end = 0;
    t->tsc_diff_ns = 0;
}

void mark_start(Timer *timer) {
    TimerInternal *t = (TimerInternal *)timer->timer;
    if (t == NULL) return;
    t->tsc_start = AsmReadTsc();
}

void mark_end(Timer *timer) {
    TimerInternal *t = (TimerInternal *)timer->timer;
    if (t == NULL) return;
    t->tsc_end = AsmReadTsc();
    UINT64 Ticks = t->tsc_end - t->tsc_start;
    // Guard against TSC rollover or uninitialized calibration.
    if (g_tsc_freq_hz == 0) {
        t->tsc_diff_ns = 0;
        return;
    }
    t->tsc_diff_ns = DivU64x64Remainder(Ticks * NS_PER_S, g_tsc_freq_hz, NULL);
}

int adjusted_wait(Timer *timer) {
    TimerInternal *t = (TimerInternal *)timer->timer;
    if (t == NULL) return 0;
    if (g_tsc_freq_hz == 0) {
        return 0;
    }
    INT64 RemainingNs = (INT64)t->period_ns - (INT64)t->tsc_diff_ns;
    if (RemainingNs <= 0) {
        return 0;
    }
    UINT32 Us = (UINT32)DivU64x32((UINT64)RemainingNs, 1000);
    if (Us == 0) {
        return 0;
    }
    gBS->Stall(Us);
    return 0;
}

int wait(uint64_t period_ms) {
    gBS->Stall((UINT32)(period_ms * 1000ULL));
    return 0;
}

uint64_t get_diff_ms(Timer *timer) {
    TimerInternal *t = (TimerInternal *)timer->timer;
    if (t == NULL) return 0;
    return DivU64x32(t->tsc_diff_ns, 1000000);
}

void release_timer(Timer *timer) {
    if (timer->timer != NULL) {
        FreePool(timer->timer);
        timer->timer = NULL;
    }
}
