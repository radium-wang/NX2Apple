// Timestamp blue/red changes from Android screenrecord's continuous RGB frames.
#include <stdint.h>
#include <stdio.h>
#include <time.h>

static long long monotonic_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000000000 + t.tv_nsec;
}

int main(void) {
    uint32_t header[5];
    unsigned char rgb[160*90*3];
    int previous = -2;
    while (fread(header, sizeof header, 1, stdin) == 1) {
        if (header[0] != sizeof rgb + 16 || header[1] != 160 || header[2] != 90 ||
            header[3] != 480 || header[4] != 3) return 2;
        if (fread(rgb, sizeof rgb, 1, stdin) != 1) return 3;
        const unsigned char *p = rgb + (45*160+80)*3;
        int color = p[0] > 160 && p[2] < 100 ? 1 : p[0] < 100 && p[2] > 110 ? 0 : -1;
        if (color != previous) {
            printf("%d %lld\n", color, monotonic_ns());
            fflush(stdout);
            previous = color;
        }
    }
}
