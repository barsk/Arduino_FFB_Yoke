/* 
 Created by A.Eckers aka Gagagu
 http://www.gagagu.de
 https://github.com/gagagu/Arduino_FFB_Yoke
 https://www.youtube.com/@gagagu01

2025 Edited by K. Jörg, @Barsk
https://github.com/barsk/Arduino_FFB_Yoke


  This repository contains code for Arduino projects. 
  The code is provided "as is," without warranty of any kind, either express or implied, 
  including but not limited to the warranties of merchantability, 
  fitness for a particular purpose, or non-infringement. 
  The author(s) make no representations or warranties about the accuracy or completeness of 
  the code or its suitability for your specific use case.

  By using this code, you acknowledge and agree that you are solely responsible for any 
  consequences that may arise from its use. 

  For DIY projects involving electronic and electromechanical moving parts, caution is essential. 
  Ensure that you take the appropriate safety precautions, particularly when working with electricity. 
  Only work with devices if you understand their functionality and potential risks, and always wear 
  appropriate protective equipment. 
  Make sure you are working in a safe, well-lit environment, and that all components are properly installed and secured to avoid injury or damage.

  Special caution is required when building a force feedback device. Unexpected or sudden movements may occur, 
  which could lead to damage to people or other objects. 
  Ensure that all mechanical parts are securely mounted and that the work area is free of obstacles.
  
  By using this project, you acknowledge and agree that you are solely responsible for any consequences that may arise from its use. 
  The author(s) will not be held liable for any damages, injuries, or issues arising from the use of the project, 
  including but not limited to malfunctioning hardware, electrical damage, personal injury, or damage caused by 
  unintended movements of the force feedback device. The responsibility for proper handling, installation, 
  and use of the devices and components lies with the user.
  
  Use at your own risk.
*/

#ifndef DEFINES_H
#define DEFINES_H


// Build identity, reported ONLY when the settings tool asks for it (Communication's
// <BI> command).  Bump this whenever you change something you might flash and later
// have to identify; __DATE__/__TIME__ are stamped by the compiler with no build-script
// help, so two builds of the same variant are still told apart.
#define FW_VARIANT "1.0 RC1"

#define FW_EEPROM_VERSION 3  // one byte, max 254, change this if you make changes to the eeprom data structure or want to force reset of eeprom settings

/*****************************
 Uncomment for Serial Debug, motors are disabled, debug data will be written to Serial Monitor
*****************************/
// #define SERIAL_DEBUG

/**************************************************************************************
  Pitch drivetrain configuration

  true  = current design: 14T motor pulley + 14T encoder pulley, no planetary gear.
  false = previous design: two 30T pulleys + a 1:3.7 planetary gear on the motor.

  Edit the line below - no build flags, so the Arduino IDE handles it like any other
  setting. Everything that differs between the two drivetrains is in this one block;
  the rest of the file is drivetrain-neutral. Two ratios produce all of it:

    counts per mm    The encoder magnet rides the idler pulley, so one turn is one belt
                     circumference: 14T HTD-5M = 70 mm -> 4096/70 = 58.5 counts/mm, 30T
                     = 150 mm -> 27.3 counts/mm. The 30T design therefore reports 0.47x
                     the counts for the same travel, and every count-based reference
                     scales with it: velocity, acceleration, the friction delta, the
                     speed limiter and the calibration increment.

    force at the     F = motor torque x ratio / pulley radius. 14T direct: 1/11.14 mm =
    carriage         89.8 per Nm. 30T + 3.7:1: 3.7/23.87 mm = 155 per Nm - 1.73x the
                     force for the same PWM, so gains and PWM limits scale by 1/1.73.

  The 30T figures are scaled from the measured 14T ones, NOT measured on that hardware.
  Treat them as starting points and bench-check Motor Start PWM and pitch gain first.

  After switching: RE-RUN CALIBRATION - the stored travel is in encoder counts and those
  change scale. Pitch gain and Motor Start PWM live in EEPROM, so a yoke with stored
  settings keeps its old ones until the settings are reset to defaults or set by hand.
**************************************************************************************/
#define NEW_PITCH_CONF_14T true

#if NEW_PITCH_CONF_14T
  #define default_PITCH_PWM_MIN                   43
  #define default_PITCH_TOT_GAIN                  70
  #define default_damperMaxVelocity_PITCH         (24 << VEL_SHIFT)
  #define default_frictionMaxPositionChange_PITCH 200
  #define default_inertiaMaxAcceleration_PITCH    500
  #define MAX_VELOCITY_Y                          (25 << VEL_SHIFT)
  #define CALIBRATION_MOTOR_DELAY_Y               250
  #define CALIBRATION_MAX_PWM_Y                   80
  #define CALIBRATION_MAX_INCREMENT_Y             60
  #define FRIC_FF_STATIC_PITCH                    6
  #define FRIC_FF_VS_PITCH                        200
  #define FRIC_FF_MIN_FORCE_PITCH                 40
#else
  #define default_PITCH_PWM_MIN                   25    // 44 x 0.58
  #define default_PITCH_TOT_GAIN                  40    // 70 x 0.58
  #define default_damperMaxVelocity_PITCH         (11 << VEL_SHIFT)   // 24 x 0.47
  #define default_frictionMaxPositionChange_PITCH 93    // 200 x 0.47
  #define default_inertiaMaxAcceleration_PITCH    233   // 500 x 0.47
  #define MAX_VELOCITY_Y                          (12 << VEL_SHIFT)   // 25 x 0.47
  #define CALIBRATION_MOTOR_DELAY_Y               350   // slower axis, plus gear backlash
  #define CALIBRATION_MAX_PWM_Y                   46    // 80 x 0.58
  #define CALIBRATION_MAX_INCREMENT_Y             28    // 60 x 0.47
  #define FRIC_FF_STATIC_PITCH                    3     // 6 x 0.58, rounded DOWN:
                                                        // creep is the failure mode,
                                                        // and pwmMin is 26 here
  #define FRIC_FF_VS_PITCH                        94    // 200 x 0.47
  #define FRIC_FF_MIN_FORCE_PITCH                 40    // force units: unchanged
#endif

/*****************************
 FFB protocol trace over Serial (115200). Motors stay live. Costs ~380 B flash.
 TOGGLE: `build_flags = -D FFB_SERIAL_TRACE` in platformio.ini (comment out for release).
 Do NOT run the settings/config tool at the same time (shares Serial).

 Compact CSV, first char = tag:
   O<op>,<idx>,<loopCount>      EffectOperation   op 1=start 2=solo 3=stop 4=start-override
   C<type>,<idx>,<loadStatus>   CreateNewEffect   [FFB_SERIAL_TRACE_FULL only]
                                type 1=const 2=ramp 3=sq 4=sin 5=tri 6=sawUp 7=sawDn
                                8=spring 9=damper 10=inertia 11=friction;  loadStatus 1=ok 2=full 3=err
   D<ctrl>,<deviceState>        DeviceControl     [FFB_SERIAL_TRACE_FULL only]
                                ctrl 1=actOn 2=actOff 3=stopAll 4=reset 5=pause 6=cont
   F<nPlaying>,<rxReports>,<loops>,<fxPeak>,<fyPeak>,<vxPeak>,<vyPeak>
                                forceCalculator ~4 Hz  [FFB_SERIAL_TRACE]
                                rxReports = OUT reports accepted in the ~250 ms window; x4 = reports/s.
                                loops     = main-loop passes in the same window; x4 = loop Hz, and
                                            also the FFB force update rate.
                                The wire ceiling is ~1000 reports/s (interrupt OUT, bInterval=1, one
                                report per 1 ms USB frame), but the REAL ceiling is the drain rate:
                                the endpoint is double-banked and RecvfromUsb() only runs at the loop
                                rate, so capacity ~= (drain points) x (reports per drain) x loop Hz.
                                Run that near demand and the queue grows without bound host-side -
                                latency that builds with time in flight. Hence 3 drain points/pass.
                                fxPeak,     peak |per-axis force| since the previous line, 0..10000,
                                fyPeak    = as handed to Axis::applyForce (after the effect gains,
                                            the per-axis total gain and the host device gain).
                                            A peak, not a sample: force swings far faster than this
                                            line, so a snapshot lands wherever the 250 ms boundary
                                            falls and cannot be read against the velocity peak.
                                vxPeak,     peak |axis velocity| since the previous line, in
                                vyPeak    = counts/ms << VEL_SHIFT - the units the
                                            default_damperMaxVelocity_* and
                                            default_inertiaMaxAcceleration_* references use.
                                            Divide by 64 for counts/ms. A reference set equal
                                            to the peak means that effect reaches full
                                            commanded force at that speed; set it lower and it
                                            saturates earlier (heavier), higher and it stays
                                            in its linear region (lighter).

   S<stackFree>,<prevMin>,<upSec>,<mcusr>,<badIsr>  ~1 Hz  [FFB_STACK_TRACE]
                                stackFree = bytes above .bss still holding the boot paint byte, i.e.
                                            the closest the stack has come to .bss THIS session.
                                prevMin   = the same figure from the session BEFORE this boot, out of
                                            EEPROM. After a crash it is the only way to see how close
                                            the session that died came. 65535 = none recorded.
                                Both rode the F line until force and velocity peaks took their place
                                there, so the paint, its 50 ms scan and the EEPROM slot are now built
                                only with FFB_STACK_TRACE. This line repeats forever, so a terminal can
                                be attached whenever - the F line only appears while a host is driving
                                effects, which is exactly not the case in the seconds after a reset.
                                upSec = seconds since boot. Watch it: if it drops back to 0
                                        the yoke reset, which is the event you are hunting.
                                mcusr = MCUSR latched at boot, i.e. WHY the last reset was.
                                        1 PORF power-on   2 EXTRF external (the reset pin)
                                        4 BORF brown-out  8 WDRF watchdog
                                        8 = the watchdog fired. The only WDT in the build is
                                            the Arduino CDC core's 1200-baud bootloader touch
                                            (CDC.cpp), so a WDRF here means either that path
                                            armed it or something hung with it armed.
                                        0 = no flag set, which is the interesting one: it
                                            means execution reached address 0 without a real
                                            reset - a smashed return address or a wild jump.
                                            Note the stack paint CANNOT see that: it measures
                                            how deep the stack went, not writes into a live
                                            frame above SP.
                                        1 = a genuine power cycle, i.e. you unplugged it.
                                        CONTROL: unplug and replug. If that does not show 1,
                                        the bootloader ate the flags and this field is mute.
                                badIsr= interrupts taken on a vector with no handler. Without
                                        a BADISR_vect handler avr-libc sends those to address
                                        0, which restarts the sketch with no MCUSR flag and an
                                        untouched stack - the exact signature above, and the
                                        commonest cause of it on AVR. We define the handler,
                                        so the yoke now survives and counts them instead.
                                        Non-zero = found it. Still crashing with 0 = look
                                        elsewhere (a smashed return address in the force path,
                                        which the USB-ISR memset race can produce).
                                FFB_STACK_TRACE is INDEPENDENT of FFB_SERIAL_TRACE. Use it
                                alone to keep room for whatever code you are trying to
                                reproduce a crash with: measured 28194 B flash / 2125 B RAM
                                against 27606 / 2111 for the same build without it, so
                                ~590 B and 14 B. Most of the flash is Print's integer
                                formatting rather than the line itself - a release build
                                never links it, so the first Serial.print(number) anywhere
                                costs several hundred bytes and the rest are nearly free.
                                Setting both flags has NOT been checked for fit: alone they
                                are 28376 and 28194 of 28672, so together is unlikely.

 Reading it (effects not playing):
   - no C line          -> host never creates an effect (init/handshake failing upstream)
   - D shows 4 but not 3  -> stuck in default-spring; host must send StopAll
*****************************/

#define SERIAL_BAUD 115200  // Communication Speed
#define BUZZER_PIN 4


// IR sensor Pins
#define IR_ROLL_LEFT 0
#define IR_ROLL_RIGHT 1
#define IR_PITCH_UP 15
#define IR_PITCH_DOWN 14

// Calibration Button pin, IR Sensors pins
#define CALIB_BUTTON_PIN 16

// Encoder Pins (via TCA9548 mux)
// On PCB marked as 
#define I2C_SDA 2 // (PIT_A)
#define I2C_SCL 3 // (PIT_B)

// AS5600 slow filter (CONF SF bits): the sensor s own output filter, and so how stale
// the angle the force loop reads is. Datasheet v1-06, settling time / RMS noise:
//   0 = 16x, 2.20 ms, 0.015 deg (power-on default)
//   1 =  8x, 1.10 ms, 0.021 deg
//   2 =  4x, 0.55 ms, 0.030 deg
//   3 =  2x, 0.286 ms, 0.043 deg  <- set here
// 2.2 ms of lag is about a whole force-loop pass (~4-5 ms at 10-15 effects), and the
// spring, damper, friction and friction-FF metrics are all derived from this position.
// The added noise stays under half of one 12-bit count (0.088 deg), so resolution is
// unaffected. CONF is volatile, so setup() re-applies it on every boot; 0 restores the
// stock behaviour. The fast-filter threshold (FTH) is left off.
#define ENCODER_SLOW_FILTER 3


// Pitch Motordriver pins
#define PITCH_EN 7
#define PITCH_U_PWM 5
#define PITCH_D_PWM 6

// Roll Motordriver pins
#define ROLL_EN 8
#define ROLL_R_PWM 10
#define ROLL_L_PWM 9


//ARDUINO_PRO_MICRO
// Multiplexer Yoke Buttons
// A3, A2, A1

#define MUX_YOKE_CLK 19
#define MUX_YOKE_PL 20
#define MUX_YOKE_OUT 21

  // for RoxMux Library
  // used for array sizes not pins!
  // #define MUX_TOTAL_INT 1
  #define MUX_TOTAL_YOKE 2

/*****************************
  Memory array positions for Effects
****************************/
#define MEM_ROLL  0
#define MEM_PITCH  1
#define MEM_AXES  2

/**************************** 
 * EEPROM memory index 
 ****************************/
// Valid data in EEPROM is indicated by a combination of Magic number and version
#define EEPROM_DATA_MAGIC_NUMBER 0b10101010 // Magic number to indicate if valid data is written (used with FIRMWARE_VERSION)

#define EEPROM_DATA_AVAILABLE_INDEX 0     // eeprom address to indicate data available (size 1)
#define EEPROM_FIRMWARE_VERSION_INDEX 1         // version indicator, 1 byte (0-254) 

#define EEPROM_ENCODER_X_OFFSET_INDEX  2      // eeprom address of max encoder pos (size 4)
#define EEPROM_ENCODER_X_MAX_INDEX  6      // eeprom address of max encoder pos (size 4)
#define EEPROM_ENCODER_Y_MAX_INDEX  10     // eeprom address of max encoder pos (size 4)

#define EEPROM_MAX_VELOCITY_PCNT_INDEX 14

#define EEPROM_ADJ_PWM_MIN_X_INDEX 15
#define EEPROM_ADJ_PWM_MIN_Y_INDEX 16

#define EEPROM_TOTAL_GAIN_X_INDEX 17
#define EEPROM_DEFAULT_SPRING_FORCE_X_INDEX 18
#define EEPROM_TRAVEL_RANGE_PCNT_X_INDEX 19  // Not used ATM.

#define EEPROM_TOTAL_GAIN_Y_INDEX 20
#define EEPROM_TRAVEL_RANGE_PCNT_Y_INDEX 21 
#define EEPROM_DEFAULT_SPRING_FORCE_Y_INDEX 22

#define EEPROM_DATA_INDEX 25              // eeprom start address for data (not used)


// Post-mortem stack watermark (uint16; FFB_SERIAL_TRACE or FFB_STACK_TRACE). stackFreeMin() only
// ever decreases, so persisting it lets a build that CRASHES still report how close the
// stack got - the reading we can never read out of a dead session. Cleared at boot after
// the previous session's value has been captured for reporting.
#define EEPROM_STACK_WATERMARK_INDEX 30

// How often the FFB_STACK_TRACE 'S' line is printed, ms. Slow on purpose: it shares the
// port with the settings tool, and the figures it carries move slowly.
#define FFB_STACK_TRACE_MS 1000

// Default vaules for gains and effect if nothing saved into eeprom
#define default_gain 100
#define default_friction_gain 100
#define default_spring_gain 50

// NOTE: velocity/acceleration are computed as (positionChange << VEL_SHIFT) / diffTime
// (see updateEffects() in joystick.ino). VEL_SHIFT = 6 keeps sub-count/ms resolution that
// plain integer division threw away. The *MaxVelocity / *MaxAcceleration references and the
// speed-limiter thresholds below are therefore in the same (<<6) units. friction uses the
// raw positionChange delta and is NOT scaled. These values are compile-time only (not in
// EEPROM / not in the settings tool), so changing the scale needs no FIRMWARE_VERSION bump.
#define VEL_SHIFT 6

// Window over which velocity, acceleration and the friction delta are differentiated,
// in ms. Fixed on purpose: differentiating per loop pass made all three scale with the
// loop rate, so damper/inertia/friction changed character with effect count. One encoder
// count of jitter reads as 64/W velocity units and 640/(W*W) acceleration units - at the
// old per-pass rate that was 64 and 640 at ~1 kHz idle against 16 and 40 at ~250 Hz in
// flight; at W = 8 it is a steady 8 and 10. Larger W = quieter but laggier (the metrics
// trail the hand by up to W ms). The *MaxVelocity / *MaxAcceleration references and
// *MaxPositionChange are all in these units, so changing W rescales them.
#define PHYSICS_SAMPLE_MS 10

// Counts of travel per PHYSICS_SAMPLE_MS window. Scaled up from the 4/28 tuned against
// the old per-pass delta (~4-5 ms in flight) so the feel carries over to the fixed 8 ms
// window; the delta grows with the window, so the reference has to as well.
#define default_frictionMaxPositionChange_ROLL 50
// default_frictionMaxPositionChange_PITCH   // see the pitch drivetrain block above

#define default_damperMaxVelocity_ROLL ( 5 << VEL_SHIFT)
// default_damperMaxVelocity_PITCH   // see the pitch drivetrain block above

// Acceleration, NOT velocity: plain numbers, no << VEL_SHIFT. That shorthand belongs to
// the velocity references above and made these ~10x too high, so inertia bit once on the
// initial jerk and then went quiet. Starting value:
//   reference ~= (velocity change, same units as damperMaxVelocity) x 10 / build-up ms
// A roll sweep building 768 in 60 ms wants ~128; a pitch sweep building 2400 in 80 ms
// wants ~300. Lower saturates on gentler onsets = more apparent mass. The jitter floor
// is 640/(PHYSICS_SAMPLE_MS^2) = ~10 at an 8 ms window, so stay well clear of it.
#define default_inertiaMaxAcceleration_ROLL 140 //120
// default_inertiaMaxAcceleration_PITCH   // see the pitch drivetrain block above //300

// Output smoothing for the condition effects, in Hz (0 = off, force passes through).
// These filter the FORCE the damper/inertia/friction calculators produce, one filter
// per axis. alpha is re-derived from the measured loop period every ~256 ms
// (setConditionFilterRates in Joystick.cpp), so the smoothing no longer drifts with
// effect count: the old single 2 Hz constant assumed a 500 Hz loop and in practice ranfroll
// from ~0.8 Hz (15 effects) to ~3.9 Hz (idle). Time constant is 1/(2*pi*f):
//   12 Hz = 13 ms      4 Hz = 40 ms      2 Hz = 80 ms (the old value)
// Damper and friction should track the hand, so they are filtered lightly - lag here is
// what makes a quick jab feel weaker than a steady sweep at the same speed. Acceleration
// is a second difference of a quantised position and genuinely noisy, so inertia keeps
// more smoothing.
#define DAMPER_LPF_HZ   12
#define INERTIA_LPF_HZ   8
#define FRICTION_LPF_HZ 12



// Speed limit settings
// #define ENABLE_SPEED_LIMITER // comment out to disable
#define MAX_VELOCITY_X (15 << VEL_SHIFT)
// MAX_VELOCITY_Y   // see the pitch drivetrain block above
#define VELOCITY_HYSTERESIS (5 << VEL_SHIFT) // max velocity - this value to reenable
#define DEFAULT_VELOCITY_PCNT 60 // default percentage of MAX velocity
#define DEFAULT_SOFT_LOCK_Y_PCNT 80 // default soft lock range in percentage of full range (iMax - iMin) if not calibrated

// Motor Start PWM, knee and floor defaults. Bench sweeps on the 20 A supply (2026-09-12,
// sheets in reference/) put breakaway at PWM 51 on pitch and 45 on roll; the values were
// then settled by feel on the yoke (2026-09-14) with friction FF on (FRIC_FF_STATIC 8),
// PWM_KNEE_FORCE 1000 and PWM_KNEE_FLOOR_PCT 75:
//  - Motor Start PWM just under breakaway (45 / 40) lets a held force reach movement early:
//    dead band ~8 % of the sim's force range on pitch, ~3 % on roll (25 % / 14 % with the
//    previous 26 / 25).
//  - The knee stops small forces - propeller rumble in particular - riding the full
//    offset. The tiniest force gets floor x Motor Start PWM + the FF boost = 41 / 38
//    counts, still 10 / 7 under breakaway, so the yoke holds still near centre.
//  - Above the knee a held force gets Motor Start PWM + boost = 53 / 48, a few counts
//    over breakaway by design: a force that size is meant to move the yoke.
// If an axis drifts or hums hands-off, lower its Motor Start PWM by 2 in the Yoke Tool.
// Gains: the real grip is 125 mm from the roll axis. There, pitch 55 / roll 100 leaves
// pitch ~1.25-1.45x roll's force for the same sim magnitude; about 40 would balance them.
// Defaults only reach a yoke with fresh or reset EEPROM; stored settings are kept.
// default_PITCH_TOT_GAIN   // see the pitch drivetrain block above
#define default_PITCH_PWM_MAX 255
// default_PITCH_PWM_MIN   // see the pitch drivetrain block above   

#define default_ROLL_TOT_GAIN 100
#define default_ROLL_PWM_MAX 255
#define default_ROLL_PWM_MIN 39    

// Low-end shaping for Axis::applyForce().
// The old mapping stepped PWM straight to pwmMin the instant any force appeared
// (map(|gForce|,0,10000,pwmMin,255)) - so a 5 % effect asked for ~10 % drive and
// the axis broke static friction with a lurch: that exaggerated the low magnitude
// feel. Now the breakaway offset eases in with a quadratic curve over the first
// PWM_KNEE_FORCE units of demanded force; above the knee the mapping is byte-for-
// byte like before. Trade: a small, soft dead zone at the very bottom instead of
// a hard grab. Widen if the low end still is too strong, narrow if centre feels dead.
//   PWM_KNEE_FORCE 0  -> disables KNEE, old hard step (no dead zone, hard grab)
// Default 1000. With Motor Start PWM just under breakaway the knee is what stops small
// forces - propeller rumble in particular - riding the full offset. Turning it off (0)
// tightens the held dead band, but inflates exactly those forces again.
// Units are DirectInput force (0..10000).
#define PWM_KNEE_FORCE 1200

// The knee's dead-zone comes from `off` ramping all the way from 0: the smallest
// commands sit well below breakaway PWM so nothing moves. PWM_KNEE_FLOOR_PCT
// starts `off` at that % of pwmMin instead, so small commands already sit near
// breakaway - shrinks the dead zone, at the cost of a small "pop" as the axis
// frees (the last few counts to breakaway still cross the static->kinetic drop).
//   0   -> full soft ramp from zero (widest dead zone, no pop) - original knee
//   100 -> exactly the old hard step (no dead zone, hard grab)
//   ~70 -> most of the dead zone gone, pop is only ~pwmMin*0.3 counts
// For a dead-zone-free low end without any pop, the real fix is the Stribeck
// friction FF below (velocity-aware, also cancels the kinetic drop).
// Default 75, settled by feel. No effect while PWM_KNEE_FORCE is 0.
#define PWM_KNEE_FLOOR_PCT 80

// --- Optional: Stribeck friction feedforward ------------------------------
// Boosts the motor in the direction of the *commanded* force, strongest at
// standstill and fading as the axis gains speed (static friction >> kinetic).
// Lets weak effects break the axis loose without a big standing pwmMin /
// PWM_KNEE_FORCE offset, so the low end can be tighter and more linear. Uses
// the velocity already passed to Axis::applyForce() - no new sensing.
//   FRIC_FF_STATIC   - peak boost, PWM counts, added on top of the knee mapping.
//                      What must stay below real breakaway is the drive a TINY
//                      force gets - floor x Motor Start PWM + FRIC_FF_STATIC -
//                      or the axis self-crawls / hums hands-off near centre.
//                      Above the knee, Motor Start PWM + boost may exceed
//                      breakaway: a force that large is meant to move the yoke.
//   FRIC_FF_VS       - velocity (<<VEL_SHIFT counts/ms, as the speed limiter)
//                      at which the boost is halved. Default is deliberately
//                      low so the boost is confined to near-standstill; raise
//                      (up to ~256) if the axis chatters as it breaks free.
//   FRIC_FF_MIN_FORCE- noise gate; no boost below this commanded force.
// Bench-tune hands-off: raise FRIC_FF_STATIC until small forces feel alive
// with zero creep or buzz.
#define ENABLE_FRICTION_FF
// Per axis: FRIC_FF_VS is in encoder counts and FRIC_FF_STATIC in PWM counts, so both
// depend on the drivetrain. The pitch trio is in the pitch drivetrain block near the
// top of this file; only roll lives here. FRIC_FF_MIN_FORCE is a commanded-force gate
// (0..10000) and drivetrain-independent, but is kept per axis for symmetry.
#define FRIC_FF_STATIC_ROLL     6
#define FRIC_FF_VS_ROLL         200
#define FRIC_FF_MIN_FORCE_ROLL  40

// At rest (no commanded force) release the H-bridge (EN low) so the motor
// terminals float and the axis coasts. Without this, driveMotor() leaves both
// bridge inputs at 0 = motor shorted = electrically braked at rest, which drags
// on every hand input and fights the low-end feel. Comment out to keep the old
// always-braked behaviour (more hands-off settling, less free movement).
#define COAST_AT_IDLE

// Brake before coasting (only acts with COAST_AT_IDLE). Releasing EN the instant
// the force reaches zero drops the bridge while the winding may still carry
// several amps. That current's only path is through the FETs' body diodes back
// into the 24 V rail; a switch-mode PSU cannot absorb it, so the rail climbs until
// the PSU trips on over-voltage - seen on a 20 A supply every time a strong effect
// stopped (a 10 A supply had capped the current, and with it the energy, below the
// trip). The same spike exceeds the BTS7960's ~27 V operating rating. Holding both
// low-sides on for COAST_BRAKE_MS first lets the current die away inside the
// motor, then the bridge is released as before. The same release was what crashed the
// yoke with ENABLE_FRICTION_FF on: friction FF drives harder at small forces, so it
// released with current far more often. With the brake in place friction FF is stable
// (hardware-confirmed 2026-09-14) - keep this defined whenever friction FF is enabled.
//   COAST_AT_IDLE + COAST_BRAKE_MS  coast at rest, brake briefly at each stop
//   COAST_AT_IDLE alone             coast at rest, release instantly (trips a 20 A PSU,
//                                   and crashes the yoke with friction FF on)
//   neither                         always braked at rest (also cures the trip)
// Raise to 20-30 if the PSU still trips on stop.
#define COAST_BRAKE_MS 10

// Limit range from the absolute max found from calib to assure full range is given
#define EXTREMITY_LIMITER_X 0 
#define EXTREMITY_LIMITER_Y 20 

// Force to use to *gently* hold the yoke to the endstop if a force is pushing it there
// If using 0 as force, we can get bouncing towards the endstop (if hands off)
// This force should be just enough to get the motor working, to high and there will be heat!!!
// Range is 0-10000
#define ENDSTOP_HOLD_FORCE 300 // 300

// SOFT LOCK / travel-limit settings
//
// Both axes support a travel limit: the working range is reduced to
// travelRangePcnt % of the calibrated mechanical range, and the outer margin
// becomes a cushion zone (calcSoftLockForce) that catches the axis before it
// slams into the mechanical endstop. softLock_range = (100-pcnt)/100 * iMax.
// The HID-reported axis range AND the spring/condition position scale both
// shrink by softLock_range together (setRangeJoystick), so effects stay 1:1
// with the reported axis.
//
// travelRangePcnt = 100  -> softLock_range 0 -> full range, cushion disabled.
//
// Roll (X): 100% for this hardware version (no over-rotation). When the
// over-rotation roll HW ships, raise default below and add a rollTravelRange
// field to SettingsDataStruct + the config tool, mirroring pitchTravelRange.
#define default_ROLL_TRAVEL_RANGE_PCNT  100
#define default_PITCH_TRAVEL_RANGE_PCNT DEFAULT_SOFT_LOCK_Y_PCNT  // 80

// Cushion force (0..10000) and hysteresis buffer (encoder steps) per axis.
#define SOFT_LOCK_X 50 // fallback softLock_range when uncalibrated (roll)
#define SOFT_LOCK_FORCE_X 10000
#define SOFT_LOCK_BUFFER_X 40

#define SOFT_LOCK_Y 670 // fallback softLock_range when uncalibrated (pitch)
#define SOFT_LOCK_FORCE_Y 10000
#define SOFT_LOCK_BUFFER_Y 80

/******************************************
   Calibration Constants
*******************************************/
#define CALIBRATION_MOTOR_DELAY_X 600
// CALIBRATION_MOTOR_DELAY_Y   // see the pitch drivetrain block above
#define CALIBRATION_MAX_PWM_X 55
// CALIBRATION_MAX_PWM_Y   // see the pitch drivetrain block above
#define CALIBRATION_MAX_INCREMENT_X 35                // Maximum positional delta change per loop (WHILE_DELAY)                       
// CALIBRATION_MAX_INCREMENT_Y   // see the pitch drivetrain block above                // Maximum positional delta change per loop (WHILE_DELAY)

#define CALIBRATION_AXIS_MOVEMENT_TIMEOUT 2000           // Timeout seconds for no movement
#define CALIBRATION_TIMEOUT 20000                        // Timeout seconds for calibration
#define CALIBRATION_SPEED_INCREMENT 2                   // the speed is increased until movement, this is added to speed then movement indicates 
#define CALIBRATION_WHILE_DELAY 15                        // waitdelay inside while of movement to give Arduino time. Change will change speed!
#define CALIBRATION_WHILE_DELAY_MOTOR_STOPS 30           // waitdelay when motor stops to give motor time to stop
#define CALIBRATION_DELAY_MOVE_OUT_OF_ENDSTOP 100        // If asix is on endstop on start of calibration it will move out of and wait shortly before continue

// if defined will catch digitalWritefast() calls that are not fast
// #define THROW_ERROR_IF_NOT_FAST 

#endif