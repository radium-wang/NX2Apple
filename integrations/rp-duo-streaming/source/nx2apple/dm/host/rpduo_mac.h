// Local NX2Apple bridge. Included by the Mac front end and haptics sink.
#pragma once
#include <sys/file.h>
#include "rpduo_haptics_mac.h"

static int rpduo_enabled(void) {
    const char *setting = getenv("DM_RP_DUO");
    return setting && !strcmp(setting, "1");
}

static void rpduo_raw_write(dm_haptic_state state) {
    (void)rpduo_raw_send(state, 0, rpduo_haptics_now_us());
}

static void rpduo_raw_refresh(dispatch_queue_t q, dm_haptic_state state, uint64_t version, const uint64_t *current) {
    if (version != *current || state.epoch != dm_haptics_epoch(state.player)) return;
    rpduo_raw_write(state);
    if (state.motor[0].amplitude_low || state.motor[0].amplitude_high ||
        state.motor[1].amplitude_low || state.motor[1].amplitude_high) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 60 * NSEC_PER_MSEC), q, ^{
            rpduo_raw_refresh(q, state, version, current);
        });
    }
}

typedef struct {
    uint32_t magic;
    uint16_t buttons;
    uint8_t lt, rt;
    int16_t lx, ly, rx, ry;
} rpduo_pad_frame;

_Static_assert(sizeof(rpduo_pad_frame) == 16, "local pad payload");

static inline uint64_t rpduo_switch_buttons(rpduo_pad_frame pad) {
    const uint16_t map[] = {0x2000, 0x1000, 0x8000, 0x4000, 0x40, 0x80, 0x100, 0x200,
                            0, 0, 0x10, 0x20, 0x04, 0x01, 0x08, 0x02};
    uint64_t buttons = 0;
    for (unsigned i = 0; i < sizeof map / sizeof *map; i++)
        if (pad.buttons & map[i]) buttons |= UINT64_C(1) << i;
    if (pad.lt >= 32) buttons |= DM_BTN_ZL;
    if (pad.rt >= 32) buttons |= DM_BTN_ZR;
    return buttons;
}
