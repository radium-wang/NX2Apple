// Exercise the NX2Apple raw-state producer over a live stream without a game.
#include "rpduo_mac.h"

static void forward_player_one(dm_haptic_state state) {
    if (state.player == 0) rpduo_raw_write(state);
}

static void play(float frequency, unsigned milliseconds) {
    dm_vibration_update effect = {.player = 0, .motor = 0, .value = {0.55f, frequency, 0, 320}};
    dm_haptics_send(&effect, 1);
    dm_haptic_state snapshot = {.player = 0, .epoch = dm_haptics_epoch(0)};
    snapshot.motor[0] = dm_haptics_actual(0, 0);
    snapshot.motor[1] = dm_haptics_actual(0, 1);
    printf("NX2Apple: %.2f Hz, %.2f amplitude, %u ms\n", frequency,
           snapshot.motor[0].amplitude_low, milliseconds);
    for (unsigned elapsed = 0; elapsed < milliseconds;) {
        unsigned step = milliseconds - elapsed;
        if (step > 60) step = 60;
        usleep(step * 1000);
        elapsed += step;
        if (elapsed < milliseconds) rpduo_raw_write(snapshot);
    }
    dm_haptics_clear(0);
}

int main(void) {
    struct sockaddr_un addr;
    struct stat st;
    if (!rpduo_address(&addr, "haptics.sock") || lstat(addr.sun_path, &st) || !S_ISSOCK(st.st_mode)) {
        fputs("Start the RP Duo stream before running the probe.\n", stderr);
        return 1;
    }
    dm_haptics_init(forward_player_one, 1);
    dm_haptics_route(0, 1);
    play(130.81f, 300);
    usleep(400000);
    play(196.0f, 300);
    usleep(400000);
    for (int i = 0; i < 3; i++) {
        play(164.81f, 70);
        usleep(130000);
    }
    return 0;
}
