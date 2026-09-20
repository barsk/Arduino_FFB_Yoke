# Changelog

Notable changes to the SimInvent FFB Yoke firmware and the Yoke Tool.

Versions here describe the **firmware**; the Yoke Tool ships alongside it and is
noted where it changed. Figures are measured on the SparkFun Pro Micro
(ATmega32u4) build .

---

## [1.0-RC1] - 2026-09-19

Release candidate. The force loop runs about **three times as fast** under a full
effect load, multi-second FFB command lag is gone, several effect bugs that only
appeared under that load are fixed, and the low end - where stiction rather than
the force command decides what the pilot feels - was rebuilt around measurements.

Most of this came out of driving the yoke from SimInvent-TelemFFB over DirectInput, which
stacks 10-17 simultaneous effects and reached problems that bench testing with
one or two effects never did. The later half came out of tuning the condition
effects against it: damper, friction and inertia are all derived from encoder
position, and nearly every fault found there was a constant that silently
depended on the loop rate.

The pitch drivetrain is selectable, so the earlier planetary-gear build is
supported by a one-line edit.

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
| 10-11 | 80-96 Hz | **248-260 Hz** |
| 15-16 | ~80 Hz | **196-208 Hz** |
| idle | ~850 Hz | **~980 Hz** |

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
- **Constant and periodic waveforms are computed once per effect**, not once per
  axis. The calculator, the envelope and their 32-bit divides ran twice over for
  the same number; only the per-axis gain and direction actually differ. The
  per-type gain is now indexed by effect type, guarded by a compile-time check on
  the `Gains` field order. Output is bit-identical.
- **Two more 32-bit divides removed from the per-effect path.** The phase offset
  skips its divide when phase is 0 - nearly every effect TelemFFB sends - and the
  envelope fast path tests `magnitude * gain >= 255` instead of dividing by 255 to
  compare the result with zero. Same predicates, about 33 us each.
- The idle figure above comes from the encoder read in *Position sensing*; the
  rest is these two changes.

### Motor drive

Five changes attacking the same problem from different sides: what the axis does
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
  previous behaviour. It stays on by default at `1200`: with Motor Start PWM near
  breakaway, turning it off would tighten the held dead band but let small forces
  such as propeller rumble ride the full offset - the inflation the knee was
  introduced to stop.
- **`PWM_KNEE_FLOOR_PCT` trades the knee's dead zone against a pop.** Ramping the
  offset from zero leaves the smallest commands below breakaway, so nothing moves
  at all: on its own the knee swaps a hard grab for a soft dead zone. The floor
  starts that ramp at a percentage of `pwmMin` instead. `0` is the full soft ramp
  (widest dead zone, no pop), `100` is exactly the old hard step, and `75`
  removes most of the dead zone while leaving a pop of only about
  `pwmMin * 0.2` counts as the axis frees. `80` is the default, settled by feel on
  the yoke.
- **Stribeck friction feedforward** (`ENABLE_FRICTION_FF`). Knee
  and floor can only trade one artefact for the other, because neither knows
  whether the axis is already moving. Static friction is far higher than kinetic,
  so the real fix is velocity-aware: a boost in the *commanded* direction,
  strongest at standstill and fading as `VS / (VS + |v|)`, using the velocity
  `applyForce()` is already given - no new sensing. Weak effects break the axis
  loose without a large standing offset, and the static-to-kinetic drop behind
  the pop is covered too. `FRIC_FF_STATIC` (6 PWM counts) is the peak boost, added
  on top of the knee mapping. What must stay below real breakaway is the drive the
  tiniest force gets - floor x Motor Start PWM plus the boost - or the axis crawls
  or hums hands-off near centre; with the defaults below that is 40 / 37 counts on pitch / roll,
  11 / 8 under measured breakaway. `FRIC_FF_VS` (200, in the speed
  limiter's `<< VEL_SHIFT` units) is the half-boost velocity, deliberately low so
  the assist stays near standstill; `FRIC_FF_MIN_FORCE` (40) gates out rounding
  noise. It used to crash and reboot the yoke. That was never the stack or memory:
  `COAST_AT_IDLE` released the bridge with current still in the winding, and
  friction feedforward, driving harder at small forces, made that happen far more
  often. The brake-before-coast below fixed it. *Hardware-confirmed stable with
  `COAST_BRAKE_MS` in place.*
- **Low-end defaults from measurement, settled by feel.** Bench sweeps on the 20 A
  supply (sheets in `reference/`) put breakaway at PWM 51 on pitch and PWM 45 on
  roll. The defaults were then tuned on the yoke with friction feedforward on:
  Motor Start PWM **43 pitch / 39 roll** (was 26 / 25), knee `1200`, floor `80`,
  `FRIC_FF_STATIC 6`. Motor Start PWM just under breakaway lets a held force reach
  movement early - modelled from the measured curves, the dead band falls from
  about 25% to 8% of the sim's force range on pitch and from 14% to 3% on roll -
  while the knee keeps small forces such as propeller rumble from riding the full
  offset. The tiniest force gets 41 / 38 counts, 10 / 7 under breakaway, so the
  yoke holds still near centre hands-off. Total gain defaults to **70 pitch / 100
  roll** (was 45 / 70). At the real 125 mm grip that leaves pitch about 1.25-1.45x
  roll's force for the same sim magnitude; a pitch gain near 40 would balance them.
  New defaults only reach a yoke with fresh or reset EEPROM; stored settings are
  kept.
- **The friction feedforward is set per axis.** `FRIC_FF_VS` is in encoder counts
  and `FRIC_FF_STATIC` in PWM counts, so both depend on the drivetrain and cannot
  be one shared number. They are now `FRIC_FF_*_ROLL` and `FRIC_FF_*_PITCH`; the
  pitch trio lives in the drivetrain block described below.
- **The bridge is released at idle** (`COAST_AT_IDLE`). With nothing commanded,
  `driveMotor()` left both bridge inputs low - motor terminals shorted, so the
  axis was electrically braked at rest. That dragged on every hand input and
  fought all of the above. `EN` now goes low at zero command and the axis coasts.
  Comment it out for the old always-braked behaviour, which settles more firmly
  hands-off.

  It brakes for a moment before letting go (`COAST_BRAKE_MS`, 10 ms). Releasing
  `EN` the instant the force reached zero dropped the bridge while the winding
  still carried current, and that current's only path was through the FETs' body
  diodes back into the 24 V rail. A switch-mode PSU cannot absorb it: on a 20 A
  supply, stopping a strong effect tripped the PSU's over-voltage protection
  every time, and the same spike exceeds the BTS7960's ~27 V rating. A 10 A
  supply had hidden it by capping the current, and with it the stored energy.
  Both low-sides are now held on for 10 ms first, so the current dies away inside
  the motor, and then the bridge is released as before. Comment it out to restore
  the instant release. *Hardware-confirmed: the PSU no longer trips on stop.*
  The same release had been crashing the yoke whenever friction feedforward was
  on, and the brake cured that too - so keep `COAST_BRAKE_MS` whenever
  `ENABLE_FRICTION_FF` is enabled.

The knee, floor, friction feedforward and coast settings are compile-time in
`defines.h` rather than settings-struct fields, so retuning them costs no
`FIRMWARE_VERSION` bump. Motor Start PWM and gain are per-axis Yoke Tool settings
stored in EEPROM; the values above are their defaults. *Bench-measured on the
775-motor build - confirm by feel.*

### Position sensing

- **The angle the force loop reads is about 8x fresher.** The AS5600 powers up with
  its slow filter at 16x, which settles in **2.2 ms** - roughly a whole loop pass of
  lag on every position that the spring, damper, friction and inertia are derived
  from. `ENCODER_SLOW_FILTER` sets it to 2x: **0.286 ms**, at the cost of RMS noise
  rising from 0.015 to 0.043 degrees, still under half of one 12-bit count. The
  config register is volatile, so it is re-applied at every boot.
- **One I2C transaction less per axis per pass.** The datasheet (v1-06) exempts
  ANGLE, RAW ANGLE and MAGNITUDE from the address-pointer auto-increment while the
  pointer sits on the register's high byte, so a re-read needs no pointer write.
  `FastAS5600` skips it, taking a full read every eighth read and after any I2C
  error - which bounds how long a sensor that reset on its own could be misread.
  Worth about 130 Hz at idle.
- **Velocity, acceleration and the friction delta are measured over a fixed
  window.** They were differentiated per loop pass, which tied all three to the
  loop rate: one encoder count of jitter read as 64 velocity / 640 acceleration
  units at ~1 kHz idle against 16 / 40 at ~250 Hz in flight. Damper, inertia and
  friction therefore changed character with effect count, and inertia was mostly
  differentiated noise. `PHYSICS_SAMPLE_MS` (10 ms) pins it: the jitter floor is
  6.4 units for both, whatever the loop is doing. **The `*MaxVelocity`,
  `*MaxAcceleration` and `*MaxPositionChange` references are in those units**, so a
  custom value has to be rescaled if the window is changed.

### Condition effects

- **Damper, inertia and friction no longer soften as effects stack up.** Their
  output filter applied a fixed step per pass derived from an assumed 500 Hz loop,
  so the real cutoff followed the loop rate: about 3.9 Hz at idle, 1.0 Hz at ten
  effects, 0.79 Hz at fifteen - a 5x spread, and never the 2 Hz it claimed. The
  coefficient is now derived from the **measured** loop period, 16 times a second.
  Damper and friction hold a ~15 ms time constant across the whole range where they
  used to vary between 41 and 202 ms; inertia holds ~41-45 ms.
- **Each effect type has its own cutoff** - `DAMPER_LPF_HZ` and `FRICTION_LPF_HZ` at
  12 Hz, `INERTIA_LPF_HZ` at 8 Hz. Damper and friction should track the hand, and
  that lag is what made a quick jab feel weaker than a steady sweep at the same
  speed. Acceleration is a second difference of a quantised position, so inertia
  keeps more smoothing.
- **No burst of vibration when a host connects.** The filters were left unconfigured
  until the first measurement window, which put raw, unfiltered damper force on the
  motors for a fraction of a second - enough to buzz the yoke audibly. They are now
  seeded at startup for the fastest loop the firmware reaches, and the window is
  measured whether or not a host is driving effects.
- **A latent way to lose damper, inertia and friction entirely.** A
  default-constructed filter had a coefficient of 0, which is not "unfiltered" but
  "output frozen at zero". Nothing ever hit it, because `begin()` always configured
  the filters first, but the default is now pass-through.
- **Inertia references were about 10x too high.** They carried the `<< VEL_SHIFT`
  shorthand that belongs to the velocity references, so inertia saturated only on
  the initial jerk of a movement and went quiet through the rest of it. They are
  plain acceleration numbers now, with the rule of thumb in `defines.h`.

### Pitch drivetrain

- **`NEW_PITCH_CONF_14T` selects the drivetrain.** `true` is the current 14T pulleys
  with no planetary gear; `false` is the previous two 30T pulleys with the 1:3.7
  planetary. It is a plain edit in `defines.h`, so the Arduino IDE needs no build
  flags, and everything that differs between the two sits in one block. The 30T
  column is scaled from the measured 14T values by two ratios: **x0.47** on
  everything count-based - the encoder magnet rides the idler pulley, so 4096 counts
  span one belt circumference, 70 mm on 14T against 150 mm on 30T - and **x0.58** on
  everything force-based, 3.7/23.87 mm against 1/11.14 mm at the carriage. Those
  figures are derived, not measured on that hardware. **Switching drivetrain:
  re-run calibration**, since stored travel is in encoder counts and the scale
  changes, and reset the Settings Tool to defaults, since pitch gain and Motor Start
  PWM live in EEPROM and the compile-time values only seed a fresh one.

### Defaults shipped in RC1

Compile-time, in `defines.h`, unless the table says otherwise.

| Setting | Roll | Pitch (14T) | Pitch (30T + 1:3.7) |
|---|---|---|---|
| Motor Start PWM *(Settings Tool, EEPROM)* | 39 | 43 | 25 |
| Total gain *(Settings Tool, EEPROM)* | 100 | 70 | 40 |
| `default_damperMaxVelocity_*` | `5 << VEL_SHIFT` | `24 << VEL_SHIFT` | `11 << VEL_SHIFT` |
| `default_frictionMaxPositionChange_*` | 50 | 200 | 93 |
| `default_inertiaMaxAcceleration_*` | 140 | 500 | 233 |
| `MAX_VELOCITY_*` | `15 << VEL_SHIFT` | `25 << VEL_SHIFT` | `12 << VEL_SHIFT` |
| `FRIC_FF_STATIC_*` | 6 | 6 | 3 |
| `FRIC_FF_VS_*` | 200 | 200 | 94 |
| `FRIC_FF_MIN_FORCE_*` | 40 | 40 | 40 |

Shared by both axes: `PWM_KNEE_FORCE 1200`, `PWM_KNEE_FLOOR_PCT 80`,
`PHYSICS_SAMPLE_MS 10`, `ENCODER_SLOW_FILTER 3` (2x), `DAMPER_LPF_HZ 12`,
`FRICTION_LPF_HZ 12`, `INERTIA_LPF_HZ 8`, `COAST_AT_IDLE` with `COAST_BRAKE_MS 10`,
`ENABLE_FRICTION_FF` on, default effect gain 100 and default spring gain 50.

### Memory and build

| build | flash | free | RAM |
|---|---|---|---|
| release | 27 848 / 28 672 | 824 | 2 125 / 2 560 |
| `FFB_SERIAL_TRACE` | 28 606 | 66 | 2 145 |
| `FFB_STACK_TRACE` | 28 598 | 74 | 2 145 |
| both together | 29 184 | **does not fit** | - |

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
  `F<nPlaying>,<rxReports>,<loops>,<fxPeak>,<fyPeak>,<vxPeak>,<vyPeak>`. `loops` x4
  is the loop
  rate, `rxReports` x4 shows how close USB delivery is running to its drain
  ceiling. `fxPeak`/`fyPeak` are the peak per-axis force over the window, exactly
  as handed to the motor stage, and `vxPeak`/`vyPeak` the peak axis velocity in the
  units the `*MaxVelocity` and `*MaxAcceleration` references use - peak against
  peak, which is what makes them comparable and what those references are set
  against. Field legend in `defines.h`.
- **Stack instrumentation is `FFB_STACK_TRACE` only.** The stack paint, its 50 ms
  scan, the EEPROM watermark and the `S` line that reports them - including
  `prevMin`, the post-mortem reading from the session *before* a crash - no longer
  build into the serial-trace configuration. Stack was ruled out as the cause of
  those resets (it was the bridge release, see *Motor drive*), and the two trace
  builds no longer fit in flash together.
- The development-only `FFB_PROFILE` probe was removed once its questions were
  answered. `platformio.ini` records what it measured, so it does not get rebuilt
  to re-answer them.

---

## [0.9]

Initial beta release.
