#include "src/platform/macos/rpduo_audio_buffer.h"

#include <cassert>
#include <cstdio>

using platf::rpduo_audio::buffer_t;

namespace {
  buffer_t::block_t audio_block(int block_number) {
    buffer_t::block_t block;
    for (std::size_t index = 0; index < block.size(); index += 2) {
      const auto frame = block_number * static_cast<int>(block.size() / 2) + static_cast<int>(index / 2);
      block[index] = static_cast<std::int16_t>(1000 + (frame % 6000));
      block[index + 1] = static_cast<std::int16_t>(-block[index]);
    }
    return block;
  }

  void producer_jitter_preserves_every_stereo_sample() {
    buffer_t queue;
    std::array<float, buffer_t::block_samples> output;
    constexpr std::array jitter_ms {2, 14, 17, 2, 0, 13, 0, 8, 4, 14, 2};
    int sent = 0;
    int played = 0;
    int first_frame_at = 0;
    for (int time = 5; time <= 5000; time += 5) {
      // A producer can send multiple blocks after waking late. Preserve datagram order.
      while (sent * 5 + jitter_ms[sent % jitter_ms.size()] <= time) {
        queue.push(audio_block(sent++));
      }
      queue.read(output);
      if (!first_frame_at && output.back() != 0.0f) {
        first_frame_at = time;
        ++played;
      } else if (first_frame_at) {
        const auto expected = audio_block(played++);
        for (std::size_t sample = 0; sample < output.size(); ++sample) {
          assert(output[sample] == expected[sample] / 32768.0f);
        }
      }
    }
    assert(first_frame_at >= 20 && first_frame_at <= 50);
    assert(played >= 990);
    assert(queue.underruns == 0 && queue.overruns == 0);
    std::printf("Ordered stereo: %d blocks with 0–17 ms producer jitter, no gaps, startup %d ms\n", played, first_frame_at);
  }

  void signal_loss_fades_and_refills() {
    buffer_t queue;
    std::array<float, buffer_t::block_samples> output;
    buffer_t::block_t signal;
    signal.fill(16384);
    for (std::size_t block = 0; block < buffer_t::prebuffer_blocks; ++block) {
      queue.push(signal);
    }
    for (std::size_t block = 0; block < buffer_t::prebuffer_blocks; ++block) {
      queue.read(output);
      assert(output.back() == 0.5f);
    }
    queue.read(output);
    assert(output.front() == 0.5f && output.back() == 0.0f);
    assert(queue.underruns == 1);
    for (std::size_t block = 1; block < buffer_t::prebuffer_blocks; ++block) {
      queue.push(signal);
      queue.read(output);
      assert(output.front() == 0.0f && output.back() == 0.0f);
    }
    queue.push(signal);
    queue.read(output);
    assert(output.front() == 0.0f && output.back() == 0.5f);
    assert(queue.underruns == 1);
  }

  void overrun_discards_the_oldest_audio() {
    buffer_t queue;
    std::array<float, buffer_t::block_samples> output;
    for (std::size_t block = 0; block <= buffer_t::maximum_blocks; ++block) {
      queue.push(audio_block(block));
    }
    assert(queue.overruns == 1);
    queue.read(output);
    const auto first_recent_block = buffer_t::maximum_blocks - buffer_t::prebuffer_blocks + 1;
    assert(output.back() == audio_block(first_recent_block).back() / 32768.0f);
  }

  void sustained_pressure_shortens_continuous_stereo_audio() {
    buffer_t queue;
    std::array<float, buffer_t::block_samples> output;
    for (std::size_t block = 0; block < buffer_t::prebuffer_blocks; ++block) {
      queue.push(audio_block(block));
    }
    queue.read(output);
    for (std::size_t block = buffer_t::prebuffer_blocks; block < 15; ++block) {
      queue.push(audio_block(block));
    }
    queue.read(output);
    assert(queue.smoothed_frames == 48 && queue.overruns == 0);
    const auto next = audio_block(2);
    assert(output.back() == next[(48 * buffer_t::channels) - 1] / 32768.0f);
    for (std::size_t index = 0; index + 2 < output.size(); index += buffer_t::channels) {
      assert(output[index] == -output[index + 1]);
      // A 1 ms skip on this waveform would jump 48 sample values. The crossfade stays continuous.
      assert((output[index + 2] - output[index]) * 32768.0f < 2.1f);
    }
  }
}  // namespace

int main() {
  producer_jitter_preserves_every_stereo_sample();
  signal_loss_fades_and_refills();
  overrun_discards_the_oldest_audio();
  sustained_pressure_shortens_continuous_stereo_audio();
}
