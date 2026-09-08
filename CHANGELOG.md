# Changelog

Notable changes to the SimInvent FFB Yoke firmware and the Yoke Tool.

Versions here describe the **firmware**; the Yoke Tool ships alongside it and is
noted where it changed. Figures are measured on the SparkFun Pro Micro
(ATmega32u4) build .

---

## [1.0-RC1] - 2026-09-04

Release candidate. The headline is that the force loop runs about **twice as
fast**, multi-second FFB command lag is gone, and several effect bugs that only
appeared under a full sim effect load are fixed.

Most of this came out of driving the yoke from SimInvent-TelemFFB over DirectInput, which
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
- **The default spring was scaled by total gain twice.** The Settings Tool's default
  spring slider ran through `totalGain` inside its own calculation *and* again in the
  common gain stage that scales every effect, so it delivered **gain squared** - 49% on
  roll (gain 70) and 20% on pitch (gain 45) at a slider setting of 100%. Host-created
  spring effects pick up total gain only once and were unaffected, which is why the same
  spring felt right from a test tool and weak from the tool's own slider. Every other
  effect type applies only its own type gain; the default spring was the sole exception.
  **Upgrading: the default spring is now stronger** - about 1.4x on roll and 2.2x on
  pitch - so a setting that felt right before is roughly the old value multiplied by that
  axis's total gain (pitch 100% -> about 45%).
- **PID layer reviewed against the USB PID 1.0 specification** and all discrepancys fixed.
- **Triangle and Sawtooth** agree in direction with Constant, Sine and Square.
- **Effect direction now correctly follow the DirectInput convention** - an effect's direction
  is where the force *comes from*, and the applied force is its opposite. This was previously handled **inverted**.
  Verified against a spec-compliant commercial FFB wheel and against SimInvent-TelemFFB's own usage.

### Latency

- **Multi-second FFB command lag removed.** Latency grew with time in flight and
  with high effect count, and could cause seconds of lag. Cause: the USB
  interrupt OUT endpoint is double-banked, so one `RecvfromUsb()` accepts at most
  two reports - and it ran **once per main-loop pass**. Delivery was capped at
  twice the loop rate, so any surplus queued host-side and grew for as long as
  the demand lasted. The endpoint is now drained at three points per pass, around
  the slow I2C encoder and button reads.
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

Four changes attacking the same problem from different sides: what the axis does
at very small forces, where dry friction rather than the force command decides
what the pilot feels. This adresses the problems associated with the "Motor Start PWM" 
value from the Yoke Settings Tool. When clamping the usable pwm values to start from a 
higher value to counter the non linear response of a 775 DC motor it means we solve the 
problem of getting the motor to move at low stimuli, but we also get a raised response 
for the whole lower end of forces. This means small forces get unrealistically amplified, 
strong forces work fine. The settings below are used to counter this and create a more
linear feel in the lower force range.

- **Low-end linearity: the breakaway knee.** PWM stepped straight to `pwmMin` 
  (about 10% drive) the instant any force appeared - `map(|force|, 0, 10000,
  pwmMin, 255)` - so a 5% effect asked for 10% drive and the axis broke static
  friction with a lurch. Below `PWM_KNEE_FORCE` (1200 of 10000) the breakaway
  offset now eases in on a quadratic curve instead of stepping; at and above the
  knee the mapping is byte-for-byte the old one. `PWM_KNEE_FORCE 0` restores the
  previous behaviour.
- **`PWM_KNEE_FLOOR_PCT` trades the knee's dead zone against a pop.** Ramping the
  offset from zero leaves the smallest commands below breakaway, so nothing moves
  at all: on its own the knee swaps a hard grab for a soft dead zone. The floor
  starts that ramp at a percentage of `pwmMin` instead. `0` is the full soft ramp
  (widest dead zone, no pop), `100` is exactly the old hard step, and the default
  `75` removes most of the dead zone while leaving a pop of only about
  `pwmMin * 0.3` counts as the axis frees.
- **Stribeck friction feedforward** (`ENABLE_FRICTION_FF`, on by default). Knee
  and floor can only trade one artefact for the other, because neither knows
  whether the axis is already moving. Static friction is far higher than kinetic,
  so the real fix is velocity-aware: a boost in the *commanded* direction,
  strongest at standstill and fading as `VS / (VS + |v|)`, using the velocity
  `applyForce()` is already given - no new sensing. Weak effects break the axis
  loose without a large standing offset, and the static-to-kinetic drop behind
  the pop is covered too. `FRIC_FF_STATIC` (15 PWM counts) is the peak boost and
  must stay below real breakaway or the axis can crawl or hum hands-off;
  `FRIC_FF_VS` (150, in the speed limiter's `<< VEL_SHIFT` units) is the
  half-boost velocity, deliberately low so the assist stays near standstill;
  `FRIC_FF_MIN_FORCE` (40) gates out rounding noise.
- **The bridge is released at idle** (`COAST_AT_IDLE`). With nothing commanded,
  `driveMotor()` left both bridge inputs low - motor terminals shorted, so the
  axis was electrically braked at rest. That dragged on every hand input and
  fought all of the above. `EN` now goes low at zero command and the axis coasts.
  Comment it out for the old always-braked behaviour, which settles more firmly
  hands-off.

All four are compile-time in `defines.h` rather than settings-struct fields, so
retuning them costs no `FIRMWARE_VERSION` bump - promotable to the settings
protocol later if they turn out to need per-unit tuning. *Bench-tuned on the
775-motor build, still being evaluated.*

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
