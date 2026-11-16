// Feed the host's real vibration state machine through the direct wire encoder.
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "rpduo_haptics_mac.h"

static unsigned updates;
static unsigned char packet[64];
static float band(unsigned offset) {
    float v;
    memcpy(&v, packet + offset, sizeof v);
    return v;
}
static void pack(dm_haptic_state state) {
    if (state.player == 0)
        rpduo_raw_pack(packet, state, ++updates, UINT64_C(0x102030405060708) + state.epoch,
                       UINT64_C(0x0102030405060708), RPDUO_DIRECT_HAPTICS_TTL_US);
}
int main(void) {
    dm_haptics_init(pack, 1);
    dm_haptics_route(0, 1);
    const dm_vibration_update two_motors[] = {
        {.player = 0, .motor = 0, .value = {0.0078125f, 112.f, .625f, 267.f}},
        {.player = 0, .motor = 1, .value = {0.03125f, 180.f, .125f, 385.f}},
    };
    dm_haptics_send(two_motors, 2);
    assert(updates == 3);
    assert(!memcmp(packet, "RPH1\1\0\0\0\3\0\0\0", 12));
    assert(!memcmp(packet + 20, "\10\7\6\5\4\3\2\1\220\137\1\0", 12)); // little-endian source time and 90000 µs hold
    assert(band(32) == 0.0078125f && band(36) == 112.f && band(40) == .625f && band(44) == 267.f);
    assert(band(48) == 0.03125f && band(52) == 180.f && band(56) == .125f && band(60) == 385.f);
    dm_haptics_send(two_motors, 2);
    assert(updates == 3); // Identical re-sends need no wire edge.
    dm_haptics_suspend(DM_HAPTICS_INACTIVE, 1);
    assert(updates == 4);
    assert(band(32) == 0 && band(40) == 0 && band(48) == 0 && band(56) == 0);
    dm_haptics_send(two_motors, 2);
    assert(updates == 4); // Suspension cannot restart a prior effect.
    dm_haptics_suspend(DM_HAPTICS_INACTIVE, 0);
    assert(updates == 5);
    dm_haptics_send(two_motors, 2);
    assert(updates == 6 && band(32) == 0.0078125f);
    dm_haptics_set_enabled(0);
    assert(updates == 7 && band(32) == 0);
}
