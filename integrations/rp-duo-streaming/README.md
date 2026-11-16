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

## System controller mode

Apply `sunshine-system-ojd.patch` alongside `sunshine.patch` to reproduce
the tested OpenJoystickDriver report mapping and rumble handling. Install
the signed free OpenJoystickDriver and use its local socket with a
Sunshine-only read/control token. Keep the token in the local configuration
and leave the WebSocket disabled. Select `gamepad_driver = virtualhid`
and keep `controller = enabled`. The tested fork selects OpenJoystickDriver
when its endpoint and token are configured. Existing paired Artemis clients
send the same controller packets.

Run NX2Apple with `DM_RP_DUO=0` for system-only tests. This prevents the
opt-in raw path from duplicating the normal GameController haptic output.
The Artemis patch recognizes the Duo's native and Xbox modes (2022:3001 and
2022:3002) for routing ordinary vibration to the built-in vibrator.

The macOS observer and Edge 9slab page detected a standard Xbox Wireless
Controller. The separate `tests/gamepad.html` exercised the browser Gamepad
API and produced a 306 ms effect from a 300 ms request. Direct Xbox HID
output reports produced 49, 52 and 55 ms pulses. Breath of the Wild loaded
a disposable save with the system controller, accepted a sustained
diagonal stick plus jump, and sent two haptic effects during travel that
played on Duo for 107 and 117 ms. The 9slab page's own vibration button did
not produce a matched motor event in the test. This mode uses ordinary
rumble through macOS; it does not expose programmable frequency on the Duo.

On a 1920×1080, 60 Hz DeskPad stream, device input submission to macOS
GameController callback took 10.18 ms median, 18.41 ms p95 and 21.63 ms max
over 32 samples (clock alignment uncertainty ±3.87 ms). The independent
browser page changed color in response to D-pad input. Continuous Android
compositor sampling of 16 changes yielded a 76.5 ms median and a 60.6–94.3 ms
range. This includes browser work and the sampling overhead; it does not
measure panel scanout or the timing of the same action inside a game.

`tests/observe.swift` listens to system GameController events.
`tests/measure.py` uses the Android NDK-built `PadLatencyAndroid.c` input
sender and the macOS observer. `tests/measure-display.py` additionally
uses the Android NDK-built `screen-frames.c` to observe the test page on
the streaming display. These test programs are installed temporarily in
Android's local debug directory and do not change the game or firmware.
