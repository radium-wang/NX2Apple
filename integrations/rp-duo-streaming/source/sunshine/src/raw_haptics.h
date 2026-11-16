/**
 * @file raw_haptics.h
 * @brief Versioned RP Duo raw haptic transport. No platform rumble conversion.
 */
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

/** @brief Private control message; only sent after x-rpduo-haptics.version=1. */
#define RP_HAPTICS_MESSAGE 0x5f10
/** @brief Fixed wire size, independent of host ABI and byte order. */
#define RP_HAPTICS_SIZE 64

/** @brief Header and float fields decoded without unaligned access. */
static inline uint32_t rp_haptics_u32(const unsigned char *p) {
  return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}

/** @brief Validate version, player, watchdog duration and both raw motor bands. */
static inline int rp_haptics_valid(const unsigned char *p, unsigned n) {
  if (n != RP_HAPTICS_SIZE || memcmp(p, "RPH1", 4) || p[4] != 1 || p[5] || p[6] >= 4 || p[7]) {
    return 0;
  }
  uint32_t ttl = rp_haptics_u32(p + 28);
  if (!ttl || ttl > 250000) {
    return 0;
  }
  for (int i = 0; i < 8; i++) {
    uint32_t bits = rp_haptics_u32(p + 32 + i * 4);
    float f;
    memcpy(&f, &bits, 4);
    if (!isfinite(f) || f < 0 || f > ((i & 1) ? 2000.0f : 1.0f)) {
      return 0;
    }
  }
  return 1;
}
