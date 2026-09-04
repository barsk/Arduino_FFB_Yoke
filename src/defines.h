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
   F<nPlaying>,<rxReports>,<loops>,<stackFree>,<prevMin>   forceCalculator ~4 Hz  [FFB_SERIAL_TRACE]
                                rxReports = OUT reports accepted in the ~250 ms window; x4 = reports/s.
                                loops     = main-loop passes in the same window; x4 = loop Hz, and
                                            also the FFB force update rate.
                                The wire ceiling is ~1000 reports/s (interrupt OUT, bInterval=1, one
                                report per 1 ms USB frame), but the REAL ceiling is the drain rate:
                                the endpoint is double-banked and RecvfromUsb() only runs at the loop
                                rate, so capacity ~= (drain points) x (reports per drain) x loop Hz.
                                Run that near demand and the queue grows without bound host-side -
                                latency that builds with time in flight. Hence 3 drain points/pass.
                                stackFree = bytes above .bss still holding the boot paint byte, i.e.
                                            the closest the stack has come to .bss THIS session.
                                prevMin   = the same figure from the session BEFORE this boot, out of
                                            EEPROM. After a crash this is the number that matters: a
                                            live reading can never come from a session that died.
                                            65535 = no previous value recorded.

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


// Post-mortem stack watermark (uint16, FFB_SERIAL_TRACE builds only). stackFreeMin() only
// ever decreases, so persisting it lets a build that CRASHES still report how close the
// stack got - the reading we can never read out of a dead session. Cleared at boot after
// the previous session's value has been captured for reporting.
#define EEPROM_STACK_WATERMARK_INDEX 30

// Default vaules for gains and effect if nothing saved into eeprom
#define default_gain 100
#define default_friction_gain 100
#define default_spring_gain 40

// NOTE: velocity/acceleration are computed as (positionChange << VEL_SHIFT) / diffTime
// (see updateEffects() in joystick.ino). VEL_SHIFT = 6 keeps sub-count/ms resolution that
// plain integer division threw away. The *MaxVelocity / *MaxAcceleration references and the
// speed-limiter thresholds below are therefore in the same (<<6) units. friction uses the
// raw positionChange delta and is NOT scaled. These values are compile-time only (not in
// EEPROM / not in the settings tool), so changing the scale needs no FIRMWARE_VERSION bump.
#define VEL_SHIFT 6

#define default_frictionMaxPositionChange_ROLL 40
#define default_inertiaMaxAcceleration_ROLL (30 << VEL_SHIFT)
#define default_damperMaxVelocity_ROLL (15 << VEL_SHIFT)

#define default_frictionMaxPositionChange_PITCH 60
#define default_inertiaMaxAcceleration_PITCH (40 << VEL_SHIFT)
#define default_damperMaxVelocity_PITCH (25 << VEL_SHIFT)

// Speed limit settings
// #define ENABLE_SPEED_LIMITER // comment out to disable
#define MAX_VELOCITY_X (15 << VEL_SHIFT)
#define MAX_VELOCITY_Y (25 << VEL_SHIFT)
#define VELOCITY_HYSTERESIS (5 << VEL_SHIFT) // max velocity - this value to reenable
#define DEFAULT_VELOCITY_PCNT 60 // default percentage of MAX velocity
#define DEFAULT_SOFT_LOCK_Y_PCNT 80 // default soft lock range in percentage of full range (iMax - iMin) if not calibrated

#define default_PITCH_TOT_GAIN 45
#define default_PITCH_PWM_MAX 255
// #define default_PITCH_PWM_MIN 43
#define default_PITCH_PWM_MIN 26

#define default_ROLL_TOT_GAIN 70
#define default_ROLL_PWM_MAX 255
// #define default_ROLL_PWM_MIN 37
#define default_ROLL_PWM_MIN 25

// Low-end shaping for Axis::applyForce().
// The old mapping stepped PWM straight to pwmMin the instant any force appeared
// (map(|gForce|,0,10000,pwmMin,255)) - so a 5 % effect asked for ~10 % drive and
// the axis broke static friction with a lurch: the "exaggerated at low magnitude"
// feel. Now the breakaway offset eases in with a quadratic curve over the first
// PWM_KNEE_FORCE units of demanded force; above the knee the mapping is byte-for-
// byte the old one. Trade: a small, soft dead zone at the very bottom instead of
// a hard grab. Widen if the low end still grabs, narrow if centre feels dead.
//   PWM_KNEE_FORCE 0  -> restores the old hard step.
// Units are DirectInput force (0..10000). Kept compile-time (not in the settings
// struct) so no FIRMWARE_VERSION bump; promote to a Communication setting later
// if it needs per-unit tuning.
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
#define PWM_KNEE_FLOOR_PCT 75

// --- Optional: Stribeck friction feedforward ------------------------------
// Boosts the motor in the direction of the *commanded* force, strongest at
// standstill and fading as the axis gains speed (static friction >> kinetic).
// Lets weak effects break the axis loose without a big standing pwmMin /
// PWM_KNEE_FORCE offset, so the low end can be tighter and more linear. Uses
// the velocity already passed to Axis::applyForce() - no new sensing.
//   FRIC_FF_STATIC   - peak boost, PWM counts. MUST stay below real breakaway
//                      (~pwmMin) or the axis can self-crawl / hum hands-off.
//   FRIC_FF_VS       - velocity (<<VEL_SHIFT counts/ms, as the speed limiter)
//                      at which the boost is halved. Default is deliberately
//                      low so the boost is confined to near-standstill; raise
//                      (up to ~256) if the axis chatters as it breaks free.
//   FRIC_FF_MIN_FORCE- noise gate; no boost below this commanded force.
// Bench-tune hands-off: raise FRIC_FF_STATIC until small forces feel alive
// with zero creep or buzz. Split _X/_Y in applyForce() if the axes differ.
#define ENABLE_FRICTION_FF
#define FRIC_FF_STATIC     15
#define FRIC_FF_VS         150
#define FRIC_FF_MIN_FORCE  40

// At rest (no commanded force) release the H-bridge (EN low) so the motor
// terminals float and the axis coasts. Without this, driveMotor() leaves both
// bridge inputs at 0 = motor shorted = electrically braked at rest, which drags
// on every hand input and fights the low-end feel. Comment out to keep the old
// always-braked behaviour (more hands-off settling, less free movement).
#define COAST_AT_IDLE

// Limit range from the absolute max found from calib to assure full range is given
#define EXTREMITY_LIMITER_X 0 
#define EXTREMITY_LIMITER_Y 20 

// Force to use to *gently* hold the yoke to the endstop if a force is pushing it there
// If using 0 as force, we can get bouncing towards the endstop (if hands off)
// This force should be just enough to get the motor working, to high and there will be heat!!!
// Range is 0-10000
#define ENDSTOP_HOLD_FORCE 300

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
#define CALIBRATION_MOTOR_DELAY_Y 250
#define CALIBRATION_MAX_PWM_X 55
#define CALIBRATION_MAX_PWM_Y 80
#define CALIBRATION_MAX_INCREMENT_X 35                // Maximum positional delta change per loop (WHILE_DELAY)                       
#define CALIBRATION_MAX_INCREMENT_Y 60                // Maximum positional delta change per loop (WHILE_DELAY)

#define CALIBRATION_AXIS_MOVEMENT_TIMEOUT 2000           // Timeout seconds for no movement
#define CALIBRATION_TIMEOUT 20000                        // Timeout seconds for calibration
#define CALIBRATION_SPEED_INCREMENT 2                   // the speed is increased until movement, this is added to speed then movement indicates 
#define CALIBRATION_WHILE_DELAY 15                        // waitdelay inside while of movement to give Arduino time. Change will change speed!
#define CALIBRATION_WHILE_DELAY_MOTOR_STOPS 30           // waitdelay when motor stops to give motor time to stop
#define CALIBRATION_DELAY_MOVE_OUT_OF_ENDSTOP 100        // If asix is on endstop on start of calibration it will move out of and wait shortly before continue

// if defined will catch digitalWritefast() calls that are not fast
// #define THROW_ERROR_IF_NOT_FAST 

#endif