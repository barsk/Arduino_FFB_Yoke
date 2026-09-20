# Tools and documents

| File | What it is |
|---|---|
| `pwm-torque-measurement.csv` | Round 1 of the PWM-vs-torque bench sweep, both axes: the magnitude grid with its PWM and duty worked out, the required firmware and tool setup, and the geometry constants for converting a scale reading to motor torque. Readings above magnitude ~6000 are limited by its 10 A supply, which sagged to about 12 V at full duty. |
| `pwm-torque-measurement-round2.csv` | Round 2: pitch only, push direction, on the 20 A supply and after the pitch belt was slackened. Round 1's pitch readings sit alongside each row. Peak 4,42 kg from cold at full magnitude - 43,4 N at the handle. |
| `pwm-torque-measurement-round3-roll.csv` | Round 3: the roll axis from 4000 to 10000 on the 20 A supply, re-measuring the range where round 1's supply had folded back to ~12 V. Blank sheet, ready to fill. |
| `pid1_01.pdf` | USB Device Class Definition for Physical Interface Devices (PID) 1.0, the official Microsoft Force Feedback specification the firmware's FFB layer implements. |

Both sheets are semicolon-separated with a decimal comma.

`FFBTestTool.exe` used to live here; it is now in
[`../tool/other/`](../tool/other/) with its licence and source pointer.

---

## Reading an effect's direction

Getting this right takes two documents, and neither is enough on its own.

- **[`pid1_01.pdf`](pid1_01.pdf) - USB PID 1.0** gives the **encoding**. An effect's
  direction arrives in the Set Effect report as one byte per axis, spanning 0-360 degrees
  over 0-255, so one count is about 1.41 degrees. The firmware holds it as
  `effect.direction[0]`.
- **[Microsoft - *Effect Direction*](<https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee417536(v=vs.85)>)**
  gives the **meaning**: an effect's direction is *the direction the force comes from*, so
  the force applied to the device is the opposite of the angle. It also fixes the frame -
  0 degrees points away from the user, and the angle increases clockwise.

The PID spec tells you where the number is and how it is scaled. The Microsoft page tells
you which way it points. With only the first, the natural reading is that the angle is the
way the force pushes, and that is exactly 180 degrees wrong. This project made that
mistake, changed working code to "fix" it, and had to revert; a spec-compliant wheel is
what finally settled it.

### The compass

![Effect direction: a compass with 0 degrees at the top increasing clockwise, and an
example showing direction 90 degrees pushing the device to the left](../Images/effect-direction.svg)

Angles are laid out like a compass bearing: **0 degrees at the top, increasing clockwise** -
0 north, 90 east, 180 south, 270 west. North is away from the user.

### What each direction does

| Direction | Force comes from | Device is pushed | Yoke / stick | Wheel |
|---|---|---|---|---|
| **0&deg;** north | ahead of you | backwards, toward you | pulls back - nose-up | no component on its axis - see below |
| **90&deg;** east | your right | to the left | rolls left - left wing down | turns counter-clockwise - steers left |
| **180&deg;** south | behind you | forwards, away from you | pushes forward - nose-down | no component on its axis - see below |
| **270&deg;** west | your left | to the right | rolls right - right wing down | turns clockwise - steers right |

Angles in between divide between the axes like any vector: 45 degrees comes from the
north-east, so it pulls back *and* rolls left, each at about 71 % of the magnitude.

### Single-axis devices are not covered by any of this

A device with one force axis - a wheel - can only feel the component along that axis. At 90
and 270 degrees that is the whole force. At 0 and 180 it is **zero**, and that is where the
convention runs out: a direction vector of zero has no direction, so what the device does
next is not defined by the specification at all. It is up to the driver and the firmware,
and it differs between devices.

Measured here on a Logitech RS50, 2026-09-12: **0 and 180 produced full force, identical to
90.** FFBTestTool's log confirms it sent `dir=[0]` for both and that DirectInput accepted the
call without an error, so the substitution happens somewhere on the Logitech side. Whether
that is the driver or the wheel's firmware is not visible from the DirectInput API; telling
them apart would need a capture of the HID PID Set Effect report.

Do not generalise from that. Another wheel may go quiet, or hold its previous direction, or
refuse the call. The only safe assumption is that 0 and 180 are undefined on a one-axis
device, and that if you need to know, you test that device.

Two further traps on single-axis devices:

- **Polar angles need two axes to begin with.** A genuinely single-axis effect takes a
  Cartesian +/-1, not an angle.
- **DirectInput normalises the direction vector** - only its direction matters, not its
  length. On one axis that collapses to a sign, so 45 degrees cannot be expressed as "71 %
  of full" through the direction. An application that wants a proportional force there has
  to scale the effect's *magnitude* itself.

Two axes, as on the yoke, has none of this ambiguity: the X component at 0 and 180 degrees
is genuinely 0, it is sent as 0, and the axis stays still.

### In the firmware

For polar angle `t`, the "comes from" vector is `(sin t, -cos t)`, and the force applied to
the axes is its opposite:

```
Fx = -sin t     negative = left
Fy = +cos t     positive = toward you, nose-up
```

That is what `getAngleRatio()` in [`../src/src/Joystick.cpp`](../src/src/Joystick.cpp)
builds, and the comment above it records the convention so the same mistake cannot be made
twice. FFBTestTool sends the DirectInput angle unchanged, so its 90 degree button pushes
the device left - matching TelemFFB and other DirectInput tools.

