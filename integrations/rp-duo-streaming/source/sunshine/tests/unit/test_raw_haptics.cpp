/**
 * @file tests/unit/test_raw_haptics.cpp
 * @brief Regression coverage for raw motor state validation.
 */
// test includes
#include "../tests_common.h"

// standard includes
#include <array>
#include <cstring>
#include <limits>

// local includes
#include <src/raw_haptics.h>

TEST(RawHaptics, PreservesBandsAndRejectsMalformedSnapshots) {
  std::array<unsigned char, RP_HAPTICS_SIZE> bytes {};
  std::memcpy(bytes.data(), "RPH1", 4);
  bytes[4] = 1;
  bytes[28] = 1;
  EXPECT_TRUE(rp_haptics_valid(bytes.data(), bytes.size()));
  EXPECT_FALSE(rp_haptics_valid(bytes.data(), bytes.size() - 1));
  bytes[4] = 2;
  EXPECT_FALSE(rp_haptics_valid(bytes.data(), bytes.size()));
  bytes[4] = 1;
  bytes[6] = 4;
  EXPECT_FALSE(rp_haptics_valid(bytes.data(), bytes.size()));
  bytes[6] = 0;
  for (float invalid : {-1.0f, 1.01f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
    std::memcpy(bytes.data() + 32, &invalid, 4);
    EXPECT_FALSE(rp_haptics_valid(bytes.data(), bytes.size()));
  }
  float amplitude = 0.5f;
  std::memcpy(bytes.data() + 32, &amplitude, 4);
  float frequency = 164.81f;
  std::memcpy(bytes.data() + 36, &frequency, 4);
  EXPECT_TRUE(rp_haptics_valid(bytes.data(), bytes.size()));
  uint32_t expected_bits;
  std::memcpy(&expected_bits, &frequency, sizeof(expected_bits));
  EXPECT_EQ(rp_haptics_u32(bytes.data() + 36), expected_bits);
  frequency = 2001.0f;
  std::memcpy(bytes.data() + 36, &frequency, 4);
  EXPECT_FALSE(rp_haptics_valid(bytes.data(), bytes.size()));
  bytes[28] = 0;
  EXPECT_FALSE(rp_haptics_valid(bytes.data(), bytes.size()));
}
