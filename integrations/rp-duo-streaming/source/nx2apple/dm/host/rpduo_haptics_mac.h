// Forward each NX2Apple motor and frequency band before adapting it to a controller.
#pragma once
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <dispatch/dispatch.h>
#include "hle_haptics.h"

#define RPDUO_DIRECT_HAPTICS_SOCKET "direct-haptics.sock"
#define RPDUO_DIRECT_HAPTICS_TTL_US 90000u
#define RPDUO_DIRECT_HAPTICS_REFRESH_MS 30u

static inline int rpduo_direct_haptics_enabled(void) {
    const char *setting = getenv("DM_RP_DUO_RAW_HAPTICS");
    return setting && !strcmp(setting, "1");
}

static inline int rpduo_address(struct sockaddr_un *addr, const char *name) {
    char dir[96];
    struct stat st;
    snprintf(dir, sizeof dir, "/private/tmp/rpduo-stream-%u", getuid());
    if (mkdir(dir, 0700) && errno != EEXIST) return 0;
    if (lstat(dir, &st) || !S_ISDIR(st.st_mode) || st.st_uid != getuid() || (st.st_mode & 0777) != 0700) return 0;
    memset(addr, 0, sizeof *addr);
    addr->sun_family = AF_UNIX;
    int n = snprintf(addr->sun_path, sizeof addr->sun_path, "%s/%s", dir, name);
    return n > 0 && (size_t)n < sizeof addr->sun_path;
}

static inline int rpduo_socket(void) {
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd >= 0 && (fcntl(fd, F_SETFL, O_NONBLOCK) || fcntl(fd, F_SETFD, FD_CLOEXEC))) {
        close(fd);
        return -1;
    }
    return fd;
}

static inline uint64_t rpduo_haptics_now_us(void) {
    return clock_gettime_nsec_np(CLOCK_MONOTONIC) / 1000;
}

static inline void rpduo_raw_pack(unsigned char frame[64], dm_haptic_state state, uint32_t serial, uint64_t epoch, uint64_t now, uint32_t ttl) {
    memcpy(frame, "RPH1\1\0\0\0", 8);
    frame[6] = (unsigned char)state.player;
    memcpy(frame + 8, &serial, 4);
    memcpy(frame + 12, &epoch, 8);
    memcpy(frame + 20, &now, 8);
    memcpy(frame + 28, &ttl, 4);
    _Static_assert(sizeof state.motor == 32, "raw motor payload");
    memcpy(frame + 32, state.motor, sizeof state.motor);
}

static inline int rpduo_raw_send(dm_haptic_state state, int direct, uint64_t timestamp_us) {
    static int fds[2] = {-1, -1};
    static uint32_t seq[2];
    static uint64_t source_epoch[2];
    struct sockaddr_un addr;
    if (!rpduo_address(&addr, direct ? RPDUO_DIRECT_HAPTICS_SOCKET : "haptics.sock")) return 0;
    if (fds[direct] < 0) fds[direct] = rpduo_socket();
    if (fds[direct] < 0) return 0;
    if (!source_epoch[direct]) source_epoch[direct] = timestamp_us;
    unsigned char frame[64];
    uint32_t ttl = direct ? RPDUO_DIRECT_HAPTICS_TTL_US : 200000;
    rpduo_raw_pack(frame, state, ++seq[direct], source_epoch[direct] + state.epoch, timestamp_us, ttl);
    return sendto(fds[direct], frame, sizeof frame, 0, (struct sockaddr *)&addr, sizeof addr) == sizeof frame;
}

static inline int rpduo_raw_active(dm_haptic_state state) {
    return state.motor[0].amplitude_low || state.motor[0].amplitude_high ||
           state.motor[1].amplitude_low || state.motor[1].amplitude_high;
}

static void rpduo_direct_haptics_refresh(dispatch_queue_t q, dm_haptic_state state, uint64_t version,
                                         const uint64_t *current, uint64_t timestamp_us) {
    if (version != *current || state.epoch != dm_haptics_epoch(state.player)) return;
    // Stale queued effects must not begin after a dispatch stall. Silence is always allowed through.
    int active = rpduo_raw_active(state);
    uint64_t now = rpduo_haptics_now_us();
    if (timestamp_us > now) timestamp_us = now;
    if (active && now - timestamp_us >= RPDUO_DIRECT_HAPTICS_TTL_US) return;
    static unsigned sent, dropped;
    if (rpduo_raw_send(state, 1, active ? timestamp_us : now)) {
        ++sent;
        if (getenv("DM_RUMBLE_LOG") && (sent <= 10 || sent % 300 == 0))
            fprintf(stderr, "haptics: RP Duo direct packet #%u %s at source_us=%llu age_us=%llu L %.3f/%.1f %.3f/%.1f R %.3f/%.1f %.3f/%.1f\n",
                    sent, active ? "active" : "stop", (unsigned long long)timestamp_us, (unsigned long long)(now - timestamp_us),
                    state.motor[0].amplitude_low, state.motor[0].frequency_low, state.motor[0].amplitude_high, state.motor[0].frequency_high,
                    state.motor[1].amplitude_low, state.motor[1].frequency_low, state.motor[1].amplitude_high, state.motor[1].frequency_high);
    } else {
        ++dropped;
        if (dropped == 1 || (getenv("DM_RUMBLE_LOG") && dropped % 300 == 0))
            fprintf(stderr, "haptics: RP Duo direct socket unavailable (packets dropped %u)\n", dropped);
    }
    if (active) {
        uint64_t due_us = now + RPDUO_DIRECT_HAPTICS_REFRESH_MS * 1000u;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, RPDUO_DIRECT_HAPTICS_REFRESH_MS * NSEC_PER_MSEC), q, ^{
            rpduo_direct_haptics_refresh(q, state, version, current, due_us);
        });
    }
}
