// Local NX2Apple bridge. Included by the Mac front end and haptics sink.
#pragma once
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <dispatch/dispatch.h>
#include "hle_haptics.h"

static int rpduo_enabled(void) {
    const char *setting = getenv("DM_RP_DUO");
    return setting && !strcmp(setting, "1");
}

static int rpduo_address(struct sockaddr_un *addr, const char *name) {
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

static int rpduo_socket(void) {
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd >= 0 && (fcntl(fd, F_SETFL, O_NONBLOCK) || fcntl(fd, F_SETFD, FD_CLOEXEC))) {
        close(fd);
        return -1;
    }
    return fd;
}

static void rpduo_raw_write(dm_haptic_state state) {
    static int fd = -1;
    static uint32_t seq;
    static uint64_t source_epoch;
    struct sockaddr_un addr;
    if (!rpduo_address(&addr, "haptics.sock")) return;
    if (fd < 0) fd = rpduo_socket();
    if (fd < 0) return;
    unsigned char frame[64] = {'R','P','H','1',1,0,(unsigned char)state.player,0};
    uint32_t serial = ++seq, ttl = 200000;
    uint64_t now = clock_gettime_nsec_np(CLOCK_MONOTONIC) / 1000;
    memcpy(frame + 8, &serial, 4);
    if (!source_epoch) source_epoch = now;
    uint64_t epoch = source_epoch + state.epoch;
    memcpy(frame + 12, &epoch, 8);
    memcpy(frame + 20, &now, 8);
    memcpy(frame + 28, &ttl, 4);
    _Static_assert(sizeof state.motor == 32, "raw motor payload");
    memcpy(frame + 32, state.motor, 32);
    sendto(fd, frame, sizeof frame, 0, (struct sockaddr *)&addr, sizeof addr);
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

static uint64_t rpduo_switch_buttons(rpduo_pad_frame pad) {
    const uint16_t map[] = {0x2000, 0x1000, 0x8000, 0x4000, 0x40, 0x80, 0x100, 0x200,
                            0, 0, 0x10, 0x20, 0x04, 0x01, 0x08, 0x02};
    uint64_t buttons = 0;
    for (unsigned i = 0; i < sizeof map / sizeof *map; i++)
        if (pad.buttons & map[i]) buttons |= UINT64_C(1) << i;
    if (pad.lt >= 32) buttons |= DM_BTN_ZL;
    if (pad.rt >= 32) buttons |= DM_BTN_ZR;
    return buttons;
}
