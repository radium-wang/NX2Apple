# RP Duo raw haptic extension v1

This is a private fork extension, not an upstream Moonlight API. The receiver
advertises `a=x-rpduo-haptics.version:1` in SDP. Sunshine must never send it to a
client that did not opt in. It travels in the existing authenticated, encrypted
control channel as message 0x5f10. Ordinary rumble is a separate message.

The 64-byte payload uses little endian integers and IEEE-754 binary32 floats:

| Offset | Field |
|---|---|
| 0 | ASCII RPH1 |
| 4 | uint16 version, 1 |
| 6 | uint16 player, 0 through 3 |
| 8 | uint32 sequence, wrapping |
| 12 | uint64 source epoch |
| 20 | uint64 source monotonic microseconds |
| 28 | uint32 maximum hold microseconds, 1 through 250000 |
| 32 | Left low amplitude, low frequency Hz, high amplitude, high frequency Hz |
| 48 | Right low amplitude, low frequency Hz, high amplitude, high frequency Hz |

Amplitudes are 0..1, frequencies 0..2000 Hz, all floats finite. Stop is zero
amplitude on both bands of both motors. Refresh active state within its hold
interval. On disconnect, focus loss or hold timeout the receiver must stop.
Receiver playback uses its local clock. The client measures excess transit
relative to the lowest clock offset observed in the current session, without
assuming synchronized clocks. Epoch and sequence preserve source ordering. A new connection
resets receiver ordering state. The raw receiver must retain all eight floats;
playing only a mixed amplitude is explicitly a fallback, not HD playback.

NX2Apple emits game snapshots before controller-specific rendering.
Artemis retains all eight floats. Its Android output selects a single
supported frequency on capable firmware, or uses Droidplay's intensity
curve when the system does not expose frequency control.

The direct-video host uses a separate owner-only `direct-haptics.sock`.
On builds with the independent direct rumble feature, the system controller
still supplies input. The player-one game haptics are sent before the
virtual Xbox or Core Haptics render them. Direct snapshots are refreshed
every 30 ms with a 90 ms hold. Sunshine drops locally queued active
snapshots older than 55 ms, removes local queue time from the hold, and
stamps the frame with the forwarder's time. The client estimates added
transit from recent session timestamps. Ordered stop snapshots still
release the motor after a prolonged stall.

On Mac, the same owner-only directory contains pad.sock for player-one
input. Each 16-byte little-endian snapshot contains ASCII RPD1, the
uint16 Moonlight digital button mask, two uint8 triggers, and four
int16 stick axes. The paired, opted-in encrypted stream produces
snapshots and refreshes held input every 75 ms. The game merges this
source with its other input sources and releases it after 250 ms
without a fresh snapshot, or when the game loses focus.
