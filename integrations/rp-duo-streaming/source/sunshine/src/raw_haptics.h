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

/** @brief Discard late active state and subtract host queue time from its hold. Stops always pass. */
static inline int rp_haptics_apply_host_age(unsigned char *p, uint64_t now_us) {
  const uint64_t source = rp_haptics_u32(p + 20) | (uint64_t) rp_haptics_u32(p + 24) << 32;
  int active = 0;
  for (int i = 32; i < RP_HAPTICS_SIZE; i += 8) {
    active |= (rp_haptics_u32(p + i) & 0x7fffffffu) != 0;
  }
  const uint32_t hold = rp_haptics_u32(p + 28);
  if (active && (!source || source > now_us || now_us - source >= hold || now_us - source > 55000)) {
    return 0;
  }
  const uint32_t remaining = active ? hold - (uint32_t) (now_us - source) : hold;
  for (int i = 0; i < 4; ++i) {
    p[28 + i] = (unsigned char) (remaining >> (i * 8));
  }
  // The client estimates only link transit from the forwarder's timestamp.
  for (int i = 0; i < 8; ++i) {
    p[20 + i] = (unsigned char) (now_us >> (i * 8));
  }
  return 1;
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
