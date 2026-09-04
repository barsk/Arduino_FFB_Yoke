# Changelog

Notable changes to the SimInvent FFB Yoke firmware and the Yoke Tool.

Versions here describe the **firmware**; the Yoke Tool ships alongside it and is
noted where it changed. Figures are measured on the SparkFun Pro Micro
(ATmega32u4) build unless stated otherwise.

---

## [1.0-RC1] - 2026-09-04

Release candidate. The headline is that the force loop runs about **twice as
fast**, multi-second FFB command lag is gone, and several effect bugs that only
appeared under a full sim effect load are fixed.

Most of this came out of driving the yoke from TelemFFB over DirectInput, which
stacks 10-17 simultaneous effects and reached problems that bench testing with
one or two effects never did.

### Force feedback correctness

- **Effects no longer cancel or distort each other under load.** `forceCalculator`
  summed every playing effect into an `int16_t`. A full MSFS aircraft puts 10+
  effects on the pitch axis (G-force, elevator droop, AoA, deceleration and
  touchdown all pull the same way), so the sum wrapped past +/-32767 *before* the
  final clamp. Symptom: an effect did nothing while others played, but worked
  correctly on its own, and periodics went jerky. Now accumulated in `int32_t`,
  with the clamp applied only at the end.
- **Roll spring reaches full travel.** Roll's spring and condition effects
  saturated at roughly 29% of physical travel while pitch was fine: a pitch-only
  soft-lock margin was applied to roll, shrinking the spring's reference range
  without shrinking the reported HID axis range. Travel limits and soft lock are
  now handled uniformly for both axes, and the HID axis range and the spring's
  centre are set in lock-step so they cannot drift apart again.
- **PID layer reviewed against the USB PID 1.0 specification** and its findings
  implemented. Full audit and per-item status in
  `reference/PID-implementation-review.md`.
- **Trigger-button gating fixed.** DirectInput sends `triggerButton = 0xFF`
  (`DIEB_NOTRIGGER`) on ordinary effects. That was read as "button 255 is held",
  which suppressed every effect. Only a real button index gates playback now.
- **Triangle and Sawtooth** agree in sign with Constant, Sine and Square, and are
  guarded against a zero period.
- **Effect direction follows the DirectInput convention** - an effect's direction
  is where the force *comes from*, and the applied force is its opposite.
  Verified against a spec-compliant wheel and against TelemFFB's own usage.

### Latency

- **Multi-second FFB command lag removed.** Latency grew with time in flight and
  with effect count, and a stop-on-pause could land seconds late. Cause: the USB
  interrupt OUT endpoint is double-banked, so one `RecvfromUsb()` accepts at most
  two reports - and it ran **once per main-loop pass**. Delivery was capped at
  twice the loop rate, so any surplus queued host-side and grew for as long as
  the demand lasted. The endpoint is now drained at three points per pass, around
  the slow I2C encoder and button reads.
  *User-confirmed: "lag is gone, pausing also stops the effects instantly."*
- **A stalled position report can no longer stall the force loop.** The Arduino
  core's `USB_Send()` blocks - it spins in `delay(1)` for up to 250 ms while the
  IN bank is still full - and joystick position was sent ahead of the effect
  update on every pass. A position report is worthless once superseded, so it is
  dropped when the bank is busy rather than waited on.
- **A stray byte on the serial port no longer freezes the yoke.**
  `Stream::readBytes()` defaults to a 1000 ms timeout, so a single noise byte
  (port enumeration, a leftover serial monitor, another tool scanning ports)
  blocked the settings read for a full second, every 100 ms. Capped at 15 ms; a
  real command's bytes arrive within about 1 ms.

### Performance

Force-loop rate at a matched effect count, which is also the FFB update rate:

| effects | 0.9 | 1.0-RC1 |
|---|---|---|
| 10-11 | 80-96 Hz | **174-188 Hz** |
| idle | ~850 Hz | ~850 Hz |

- **Sine lookup table replaces `sin()` in the force loop** - a 65-entry quarter
  wave in PROGMEM, Q15, linearly interpolated. `avr-libc`'s `sin()` costs about
  150 us per call in software float and was the largest single cost in the loop.
  Worth roughly **+45%** loop rate on its own, measured against the previous
  build. Interpolation is not optional here: nearest-point lookup leaves a 4.7%
  amplitude staircase, twelve times coarser than the 8-bit PWM output can
  resolve, and it is felt in periodic effects. Interpolated, worst-case error is
  0.0095% - about 40 times finer than the output stage - verified exhaustively
  across all 65536 phase values.
- **Direction projection cached** rather than recomputed per effect per axis, and
  only the component the effect type actually reads is calculated.
- **Float divides removed from the hot path.** Per-effect and per-axis gains use
  a reciprocal multiply, and the condition calculator does the same for its fixed
  scale factors. A float divide is roughly 400 cycles on AVR.
- **Envelope fast path** for the common case of no attack and no fade.
- Assorted hoisting in `forceCalculator`: `millis()` read once per pass, an early
  exit for effects that are not playing, and a guarded modulo in place of an
  unconditional 32-bit division.

### Motor drive

- **Low-end linearity.** PWM stepped straight to `pwmMin` (about 10% drive) the
  instant any force appeared, so small effects were over-driven and the axis
  broke loose with a lurch. Below a configurable knee the mapping now ramps from
  a fraction of `pwmMin`, so weak effects feel weak. Tunable in `defines.h`
  (`PWM_KNEE_FORCE`, `PWM_KNEE_FLOOR_PCT`); a knee of 0 restores the previous
  behaviour. *Bench-tuned, still being evaluated.*

### Memory and build

Release build: **27600 / 28672 bytes flash, 2111 / 2560 bytes RAM.**

- **HID report descriptor frozen into PROGMEM.** The joystick half was rebuilt
  byte-by-byte at every boot into a 150-byte RAM buffer that then stayed
  allocated for the life of the device - to re-derive a constant. Now an 84-byte
  PROGMEM array, verified two independent ways against the builder's output.
  **-150 bytes RAM, -314 bytes flash.**
- **Linker relaxation enabled** (`-Wl,--relax`, via `relax.py`), converting
  `call`/`jmp` to `rcall`/`rjmp` where the target is in range. PlatformIO does
  not forward this from `build_flags` to the link step, hence the pre-script.
  **-558 bytes flash.**

### Diagnostics and tooling

- **Build identity is queryable.** The Yoke Tool shows which firmware is running
  - variant name plus compile date and time - in its status bar on connect and on
  every Read. New `<BI>` command in the settings protocol, and a `FW_VARIANT`
  string in `defines.h` to bump per build. Deliberately a query rather than a
  banner: the port is shared with the tool, so anything the firmware volunteers
  would land in the middle of that conversation. Compatible in both directions -
  an older tool never sends `<BI>`, and older firmware simply does not answer.
- **`FFB_SERIAL_TRACE` build** emits a compact CSV trace of effect operations plus
  a 4 Hz force-loop line,
  `F<nPlaying>,<rxReports>,<loops>,<stackFree>,<prevMin>`. `loops` x4 is the loop
  rate, `rxReports` x4 shows how close USB delivery is running to its drain
  ceiling, and `prevMin` is a post-mortem stack watermark read back from EEPROM
  at boot - the only way to see how close a session that *crashed* came. Field
  legend in `defines.h`.
- The development-only `FFB_PROFILE` probe was removed once its questions were
  answered. `platformio.ini` records what it measured, so it does not get rebuilt
  to re-answer them.

---

## [0.9]

Initial beta release.
