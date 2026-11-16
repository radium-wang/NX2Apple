# RP Duo streaming integration

This branch preserves the three components of the tested prototype. The host
routes player-one input directly into NX2Apple games through a local Unix socket.
The browser Gamepad API and other macOS applications require a system controller
and will not see this route. The game sends its original bands to Artemis through
Sunshine's paired and encrypted control channel. Artemis selects the available
actuator backend, including a Droidplay-style intensity fallback.

## Apply and build

Use separate working copies and apply the supplied patches and matching new files
from `source/` at each checkout root. Check the patches with `git apply --check`
before applying. The Artemis patch preserves the original CRLF Java file endings.

| Component | Tested source |
| --- | --- |
| Sunshine | `srikanthamsa/sunshine-macos-gamepads` at `b14f6ab21eb2081e8fb276bad280f1c5f1f1a5e8` |
| Artemis | `ClassicOldSong/moonlight-android` at `4de0227fb6ae5c9ad9f7cc486aed7c3571f5f62f` |
| moonlight-common-c | Artemis submodule at `ad329b240f18826f320ce6a99226b36354b86b59` |
| NX2Apple host | This fork's main at `e0ce357`, its engine pin `c78d7375bea9c862679ec294a6a69ff3e22e18d0` and patch series through 0101, plus the supplied tested-baseline patch |

For the host, sync the NX2Apple engine, apply `nx2apple-tested-baseline.patch`
then `nx2apple.patch` in the engine checkout. Apply `moonlight-common-c.patch`
inside the Artemis submodule. Build each component using its normal build
system. The Artemis debug build uses the `:app:assembleNonRoot_gameDebug` task.
This branch keeps the integration opt-in and outside the default patch series.

Set `controller = enabled` and `gamepad_driver = none` in Sunshine, and launch
the rebuilt Mac game with `DM_RP_DUO=1`. Connect with the patched Artemis.
See [the wire format](PROTOCOL.md) for the local socket and transport details.

## RP Duo verification

- Desktop and Breath of the Wild game video streamed via DeskPad and Apple hardware encoding.
- All 16 independent controller checks passed. The controller-event-to-Mac
  median was 8.08 ms and p95 was 10.69 ms across 32 samples, with approximately
  ±3.75 ms clock alignment uncertainty. Press-to-display latency was not measured.
- The no-game haptics probe produced two long and three short effects. Android
  reported 320, 320, 70, 85 and 80 ms. The actual game also triggered a 203 ms effect.
- RP Duo firmware 1.0.0.108 reported no Android frequency envelopes. The test
  used the intensity fallback. Frequency-controlled motor playback remains unverified.

The tests use the same local sockets as the production bridge. Keep the
game closed when running the independent input receiver and test one controller
at a time. Android event device numbers vary by device and boot.
