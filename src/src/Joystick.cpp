/*
  Joystick.cpp

  Copyright (c) 2015-2017, Matthew Heironimus

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

  2025 Edited by K. Jörg, @Barsk
  https://github.com/barsk/Arduino_FFB_Yoke
*/

#include "Joystick.h"
#include "FFBDescriptor.h"
#include "filters.h"
#include<avr/pgmspace.h>
#if defined(_USING_DYNAMIC_HID)

#define JOYSTICK_REPORT_ID_INDEX 7
#define JOYSTICK_AXIS_MINIMUM -32767
#define JOYSTICK_AXIS_MAXIMUM 32767
#define JOYSTICK_SIMULATOR_MINIMUM -32767
#define JOYSTICK_SIMULATOR_MAXIMUM 32767

#define JOYSTICK_INCLUDE_X_AXIS  B00000001
#define JOYSTICK_INCLUDE_Y_AXIS  B00000010
#define JOYSTICK_INCLUDE_Z_AXIS  B00000100
// #define JOYSTICK_INCLUDE_RX_AXIS B00001000
// #define JOYSTICK_INCLUDE_RY_AXIS B00010000
// #define JOYSTICK_INCLUDE_RZ_AXIS B00100000

// #define JOYSTICK_INCLUDE_RUDDER      B00000001
// #define JOYSTICK_INCLUDE_THROTTLE    B00000010
// #define JOYSTICK_INCLUDE_ACCELERATOR B00000100

const float cutoff_freq_damper   = 2.0;  //Cutoff frequency in Hz
const float sampling_time_damper = 0.002; //Sampling time in seconds.
LowPassFilter damperFilter[FFB_AXIS_COUNT];
LowPassFilter inertiaFilter[FFB_AXIS_COUNT];
LowPassFilter frictionFilter[FFB_AXIS_COUNT];

// Gain bytes are percent (0..100). `x / 100.0f` costs a float DIVIDE (__divsf3, ~400
// cycles); `x * 0.01f` is a float multiply (~100). This is evaluated per effect PER AXIS
// on every loop pass, so the divide was pure overhead. Compile-time constant - the
// compiler folds it - and the last-ulp difference vs /100.0f is far below the 8-bit PWM.
static const float GAIN_PCT = 0.01f;

// Quarter-wave sine table, Q15, RAM-resident: no PROGMEM, no pgm_read_*.  RAM rather
// than flash because the PROGMEM version hung during effect creation (Rev 8.15/8.19)
// and the RAM one does not; the cause was never found, so this is empirical.
// 130 B of RAM, leaving ~323 B against a measured 225 B stack peak.
static const int16_t sinLUT[65] PROGMEM = {
       0,    804,   1608,   2410,   3212,   4011,   4808,   5602,   6393,   7179,
    7962,   8739,   9512,  10278,  11039,  11793,  12539,  13279,  14010,  14732,
   15446,  16151,  16846,  17530,  18204,  18868,  19519,  20159,  20787,  21403,
   22005,  22594,  23170,  23731,  24279,  24811,  25329,  25832,  26319,  26790,
   27245,  27683,  28105,  28510,  28898,  29268,  29621,  29956,  30273,  30571,
   30852,  31113,  31356,  31580,  31785,  31971,  32137,  32285,  32412,  32521,
   32609,  32678,  32728,  32757,  32767,
};

// sin(phase * 360/256 deg) as Q15; phase 0..255 spans one full turn.
static int16_t sinQ15(uint8_t phase)
{
    uint8_t idx = phase & 0x3F;
    // Cast BEFORE negating.  pgm_read_word() yields uint16_t, and on AVR int is also
    // 16 bits, so an uncast uint16_t promotes to UNSIGNED int: -x is then modular
    // arithmetic on an unsigned, and the result only lands on the right int16_t via an
    // out-of-range unsigned->signed conversion.  GCC defines that as wrapping so it does
    // work here, but it is implementation-defined rather than guaranteed.  Casting first
    // negates a signed value, which is simply correct.
    switch (phase >> 6)
    {
        case 0:  return  (int16_t)pgm_read_word(&sinLUT[idx]);
        case 1:  return  (int16_t)pgm_read_word(&sinLUT[64 - idx]);
        case 2:  return -(int16_t)pgm_read_word(&sinLUT[idx]);
        default: return -(int16_t)pgm_read_word(&sinLUT[64 - idx]);
    }
}

// Same, on an 8.8 phase, linearly interpolated between table points.
//
// Nearest-point lookup is NOT good enough here, which the 33-entry trial showed the hard
// way: it left a 4.7 % amplitude staircase against an 8-bit PWM step of 0.39 %, i.e. a
// dozen output levels of error, and it was audible/feelable in periodic effects.  One
// multiply-add drops that to 0.011 % - below what the output stage can render - and is
// still a fraction of the ~150 us an avr-libc sin() costs.
static int16_t sinQ15i(uint16_t phase16)
{
    uint8_t ph   = (uint8_t)(phase16 >> 8);
    uint8_t frac = (uint8_t)(phase16 & 0xFF);
    int16_t a = sinQ15(ph);
    int16_t b = sinQ15((uint8_t)(ph + 1));
    // +128 rounds rather than truncates; (b - a) fits int16 since adjacent points
    // differ by at most one step of the quarter wave.
    return a + (int16_t)((((int32_t)(b - a) * frac) + 128) >> 8);
}

// Joystick half of the HID report descriptor: 12 buttons + 1 hat + X/Y (16-bit each),
// Report ID 1. This is exactly what the stock Matthew Heironimus runtime builder emitted
// for Joystick_(0x01, JOYSTICK_TYPE_JOYSTICK, 12, 1, true, true, false) - captured once
// (reference/gen_joydesc.py) and frozen here so ~1 KB of flash + 150 B of static RAM
// (the old build buffer) are not spent re-deriving a constant at every boot. The
// COLLECTION (Application) opened here is deliberately left unclosed - the trailing 0xC0
// of pidReportDescriptor closes it (the two halves are concatenated by getDescriptor()).
static const uint8_t joyReportDescriptor[] PROGMEM = {
  0x05, 0x01, 0x09, 0x04, 0xA1, 0x01, 0x09, 0x01, 0x85, 0x01, 0x05, 0x09,
  0x19, 0x01, 0x29, 0x0C, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x0C,
  0x55, 0x00, 0x65, 0x00, 0x81, 0x02, 0x75, 0x01, 0x95, 0x04, 0x81, 0x03,
  0x05, 0x01, 0x09, 0x39, 0x15, 0x00, 0x25, 0x07, 0x35, 0x00, 0x46, 0x3B,
  0x01, 0x65, 0x14, 0x75, 0x04, 0x95, 0x01, 0x81, 0x02, 0x75, 0x01, 0x95,
  0x04, 0x81, 0x03, 0x09, 0x01, 0x16, 0x01, 0x80, 0x26, 0xFF, 0x7F, 0x75,
  0x10, 0x95, 0x02, 0xA1, 0x00, 0x09, 0x30, 0x09, 0x31, 0x81, 0x02, 0xC0,
};


Joystick_::Joystick_(
	uint8_t hidReportId,
	uint8_t joystickType,
    uint8_t buttonCount,
	uint8_t hatSwitchCount,
	bool includeXAxis,
	bool includeYAxis,
	bool includeZAxis
	// bool includeRxAxis,
	// bool includeRyAxis,
	// bool includeRzAxis,
	// bool includeRudder,
	// bool includeThrottle
)
{
    // Set the USB HID Report ID
    _hidReportId = hidReportId;

    // Save Joystick Settings
    _buttonCount = buttonCount;
	_hatSwitchCount = hatSwitchCount;
	_includeAxisFlags = 0;
	_includeAxisFlags |= (includeXAxis ? JOYSTICK_INCLUDE_X_AXIS : 0);
	_includeAxisFlags |= (includeYAxis ? JOYSTICK_INCLUDE_Y_AXIS : 0);
	_includeAxisFlags |= (includeZAxis ? JOYSTICK_INCLUDE_Z_AXIS : 0);
	// _includeAxisFlags |= (includeRxAxis ? JOYSTICK_INCLUDE_RX_AXIS : 0);
	// _includeAxisFlags |= (includeRyAxis ? JOYSTICK_INCLUDE_RY_AXIS : 0);
	// _includeAxisFlags |= (includeRzAxis ? JOYSTICK_INCLUDE_RZ_AXIS : 0);
	// _includeSimulatorFlags = 0;
	// _includeSimulatorFlags |= (includeRudder ? JOYSTICK_INCLUDE_RUDDER : 0);
	// _includeSimulatorFlags |= (includeThrottle ? JOYSTICK_INCLUDE_THROTTLE : 0);
	
    // The HID report descriptor is the compile-time constant `joyReportDescriptor`
    // (PROGMEM, top of file). The ctor args have been fixed since this became a 2-axis
    // yoke, so the runtime byte-by-byte builder that used to live here - writing a 150 B
    // static-RAM buffer that then stayed allocated forever - was re-deriving a constant
    // at every boot. Dropping it: -150 B RAM, -314 B flash. Regenerate the array with
    // reference/gen_joydesc.py if the button / hat / axis layout ever changes (the
    // total descriptor size feeds D_HIDREPORT, so it must match what the host expects).
	uint8_t axisCount = (includeXAxis == true)
		+  (includeYAxis == true)
		+  (includeZAxis == true);

	// Register the joystick descriptor + the static FFB descriptor - both PROGMEM now
	// (inProgMem = true; getDescriptor() sends node->data with TRANSFER_PGM). Static
	// local node, no heap: the 3 `new`s that used to be here were the ONLY malloc() call
	// sites in the firmware, so dropping them let --gc-sections remove malloc/free
	// entirely.
	static DynamicHIDSubDescriptor node(joyReportDescriptor, sizeof(joyReportDescriptor),
	                                    pidReportDescriptor, pidReportDescriptorSize, true);
	DynamicHID().AppendDescriptor(&node);

    // Setup Joystick State
	if (buttonCount > 0) {
		_buttonValuesArraySize = (_buttonCount + 7) / 8;
		if (_buttonValuesArraySize > sizeof(_buttonValues))
			_buttonValuesArraySize = sizeof(_buttonValues);   // clamp: fixed 4-byte bitmap = 32 buttons
	}

	// Calculate HID Report Size
	_hidReportSize = _buttonValuesArraySize;
	_hidReportSize += (_hatSwitchCount > 0);
	_hidReportSize += (axisCount * 2);

	// Initalize Joystick State
	_xAxis = 0;
	_yAxis = 0;
	_zAxis = 0;
	for (int index = 0; index < JOYSTICK_HATSWITCH_COUNT_MAXIMUM; index++)
	{
		_hatSwitchValues[index] = JOYSTICK_HATSWITCH_RELEASE;
	}
	for (uint8_t index = 0; index < _buttonValuesArraySize; index++)
	{
		_buttonValues[index] = 0;
	}
}

void Joystick_::begin(bool initAutoSendState)
{
	_autoSendState = initAutoSendState;
	sendState();
    for (int i=0; i < FFB_AXIS_COUNT; ++i)
    {
        damperFilter[i] = LowPassFilter(cutoff_freq_damper, sampling_time_damper);
        inertiaFilter[i] = LowPassFilter(cutoff_freq_damper, sampling_time_damper);
        frictionFilter[i] = LowPassFilter(cutoff_freq_damper, sampling_time_damper);
    }
}

void Joystick_::getUSBPID()
{
	DynamicHID().RecvfromUsb();
}

void Joystick_::getForce(int16_t* forces) {
	DynamicHID().RecvfromUsb(); 
	forceCalculator(forces);
}

float Joystick_::getAngleRatio(volatile TEffectState& effect, int axis)
{
    // bug #3: sin()/cos() are ~150 us each here (soft-float, no FPU) and this is called for
    // every directional effect, every axis, every force-loop pass - 10+ stacked effects made
    // the loop visibly steppy and backed up the USB OUT queue (effects lagging seconds).
    // Cache the last direction's unit vector: forceCalculator() walks one effect's axes
    // back-to-back and most effects share a direction (every pitch effect = 180), so this
    // hits almost every call. cos(a) is taken as sin(a + HALF_PI) so cos() drops out of the
    // build (flash is at the limit).
    static int16_t cachedDir = -1;               // last direction[0] seen; -1 = cold
    static float   cachedRatio[FFB_AXIS_COUNT];   // [0] = -sin(dir), [1] = cos(dir)

    uint8_t dir = effect.direction[0];
    if ((int16_t)dir != cachedDir)
    {
        cachedRatio[0] = -sinQ15(dir) * (1.0f / 32767.0f);
        cachedRatio[1] =  sinQ15((uint8_t)(dir + 64)) * (1.0f / 32767.0f);
        cachedDir = (int16_t)dir;
    }

    // DirectInput/PID convention (MS "Effect Direction"): the effect's direction is the
    // direction the force COMES FROM; the applied force is its opposite.  Direction 0deg =
    // north (away from the user) -> force pulls the stick toward the user (+Y).  90deg = east
    // -> force pushes LEFT (-X).  So the applied-force unit vector is -(sin, -cos) = (-sin, cos).
    // (Stock Fino/YukMing formula - a 2025 "G1" change wrongly inverted it to (sin, -cos);
    // reverted.  The spurious '-' the wave calculators carried to cancel it is still removed -
    // they match constant/sine/square directly.)
    return cachedRatio[axis == 1 ? 1 : 0];
}

int16_t Joystick_::getEffectForce(volatile TEffectState& effect, EffectParams _effect_params, uint8_t axis){
    float angle_ratio;
    uint8_t condition = 0;
    if (effect.enableAxis == DIRECTION_ENABLE && effect.conditionReportsCount > 1)
    {
        angle_ratio = 1.0;
        condition = axis;
    }
    else
    {
        angle_ratio = getAngleRatio(effect, axis);
    }

	// if (effect.state == MEFFECTSTATE_PLAYING)
    // {
    //    Serial.print("axis ");
    //    Serial.print(axis);
    //    Serial.print(" dX ");
    //    Serial.print(effect.direction[0] * 360.0 / 255.0);
    //    Serial.print(" dY ");
    //    Serial.print(effect.direction[1] * 360.0 / 255.0);
	//     Serial.print(" angle_ratio ");
    //    Serial.println(angle_ratio);
    // }

	int32_t force = 0;   // wide: a periodic with a large offset can exceed int16 mid-calc
	switch (effect.effectType)
    {
	    case USB_EFFECT_CONSTANT://1
	        force = ConstantForceCalculator(effect) * (float)(m_gains[axis].constantGain * GAIN_PCT) * angle_ratio;
	        break;
	    case USB_EFFECT_RAMP://2
	    	force = RampForceCalculator(effect) * (float)(m_gains[axis].rampGain * GAIN_PCT) * angle_ratio;
	    	break;
	    case USB_EFFECT_SQUARE://3
	    	force = SquareForceCalculator(effect) * (float)(m_gains[axis].squareGain * GAIN_PCT) * angle_ratio;
	    	break;
	    case USB_EFFECT_SINE://4
	    	force = SinForceCalculator(effect) * (float)(m_gains[axis].sineGain * GAIN_PCT) * angle_ratio;
	    	break;
	    case USB_EFFECT_TRIANGLE://5
	    	force = TriangleForceCalculator(effect) * (float)(m_gains[axis].triangleGain * GAIN_PCT) * angle_ratio;
	    	break;
	    case USB_EFFECT_SAWTOOTHDOWN://6
	    	force = SawtoothDownForceCalculator(effect) * (float)(m_gains[axis].sawtoothdownGain * GAIN_PCT) * angle_ratio;
	    	break;
	    case USB_EFFECT_SAWTOOTHUP://7
	    	force = SawtoothUpForceCalculator(effect) * (float)(m_gains[axis].sawtoothupGain * GAIN_PCT) * angle_ratio;
	    	break;
	    case USB_EFFECT_SPRING://8
	    	force = ConditionForceCalculator(effect, NormalizeRange(_effect_params.springPosition, m_effect_params[axis].springMaxPosition), condition) * angle_ratio * (float)(m_gains[axis].springGain * GAIN_PCT);
	    	break;
	    case USB_EFFECT_DAMPER://9
	    	force = ConditionForceCalculator(effect, NormalizeRange(_effect_params.damperVelocity, m_effect_params[axis].damperMaxVelocity), condition) * angle_ratio;
	    	force = force * (float)(m_gains[axis].damperGain * GAIN_PCT);
            force = damperFilter[axis].update(force);
	    	break;
	    case USB_EFFECT_INERTIA://10
	    	// E4: was a half-implemented 2-branch form that only reacted to negative
	    	// acceleration and keyed direction off friction's metric. Inertia resists
	    	// acceleration symmetrically - same shape as damper, metric = acceleration.
	    	force = ConditionForceCalculator(effect, NormalizeRange(_effect_params.inertiaAcceleration, m_effect_params[axis].inertiaMaxAcceleration), condition) * angle_ratio * (float)(m_gains[axis].inertiaGain * GAIN_PCT);
            force = inertiaFilter[axis].update(force);
	    	break;
	    case USB_EFFECT_FRICTION://11
	    	force = ConditionForceCalculator(effect, NormalizeRange(_effect_params.frictionPositionChange, m_effect_params[axis].frictionMaxPositionChange), condition) * angle_ratio * (float)(m_gains[axis].frictionGain * GAIN_PCT);
            force = frictionFilter[axis].update(force);
	    	break;
	    }

#ifdef _serialPrintForces
		  Serial.print(axis);Serial.print(": [");
		  Serial.print(effect.effectType);
		  Serial.print("]: ");
		  Serial.println(force);				
		  Serial.print(",");
#endif
		return (int16_t)constrain(force, -10000L, 10000L);
}

void Joystick_::forceCalculator(int16_t* forces) {
    // Accumulate wide. Each getEffectForce() is clamped to +-10000, but a full MSFS
    // aircraft in TelemFFB stacks 10+ effects on the SAME axis (G-force, elevator droop,
    // AoA, deceleration, touchdown are all direction 180 = pitch). Their sum blows past
    // int16 and wraps *before* the final constrain, scrambling the result - which looked
    // like "the touchdown effect does nothing when other effects are playing".
    int32_t acc[FFB_AXIS_COUNT] = { 0 };

#ifdef _serialPrintForces
	  Serial.print("!");
	  Serial.print(SERIAL_CMD_DEBUG_FORCE_VALUES);
	  Serial.print("|");
#endif

    // If the device is in default auto spring effect lets calculate it
    if (DynamicHID().pidReportHandler.deviceState == MDEVICESTATE_SPRING)
    {
        for (int axis = 0; axis < FFB_AXIS_COUNT; ++axis)
        {
			acc[axis] = (int32_t)(NormalizeRange(m_effect_params[axis].springPosition, m_effect_params[axis].springMaxPosition) * -10000.0f * m_gains[axis].defaultSpringGain * GAIN_PCT * m_gains[axis].totalGain * GAIN_PCT);

#ifdef _serialPrintForces
			Serial.print(axis);
			Serial.print(F(" FORCE: "));
			Serial.println((long)acc[axis]);
#endif
        } // axis
    } // MDEVICESTATE_SPRING
    else
    {
        // Hoisted out of the per-effect loop below. millis() was being called once PER
        // EFFECT - it disables interrupts to read a 4-byte volatile - and evaluating the
        // whole sweep against a single timestamp is also more correct than letting it
        // drift between effects. Same for the pidReportHandler indirection.
        PIDReportHandler& pid  = DynamicHID().pidReportHandler;
        const uint32_t     now = millis();
        const bool notPaused   = !pid.deviceState;

	    for (int id = 1; id <= MAX_EFFECTS; id++) {   // C2: IDs are 1..MAX_EFFECTS
	    	volatile TEffectState& effect = pid.g_EffectStates[id];

            // Cheapest rejection first. Most slots are free or stopped, and the whole
            // time/trigger preamble below - including an int32 modulo, the single most
            // expensive operation in it - was being paid for every one of them. Only
            // elapsedTime is written here, and only a PLAYING effect ever reads it
            // (StartEffect resets it), so skipping non-playing slots is safe.
            if (!(effect.state & MEFFECTSTATE_PLAYING)) continue;

            // Signed time since Start, so "still in start-delay" is representable.
            int32_t t = (int32_t)(now - effect.startTime);
            // duration == max value of the field means INFINITE (PID 1.0 convention).
            bool infinite = (effect.duration >= USB_DURATION_INFINITE) ||
                            (effect.totalDuration == USB_DURATION_INFINITE);
            // For looping / infinite effects, reduce within one (duration + startDelay) cycle.
            uint16_t denom = effect.duration + effect.startDelay;   // D3: guard against %0
            if (denom > 0 && (infinite || (t < (int32_t)effect.totalDuration)))
            {
                // t %= denom is a __divmodsi4 call (~530 cycles, ~33 us) and it runs
                // for every effect on every pass once effects age past one cycle (32.8 s
                // for an infinite effect) - the profile probe caught exactly that, the
                // preamble bucket stepping 13.4 -> 46.8 us/effect.
                //
                // REVERTED an "advance effect.startTime by whole cycles" version that
                // replaced this: CreateNewEffect() runs in USB ISR context and memsets the
                // whole TEffectState, so having the main loop WRITE startTime mid-sweep
                // races with the ISR zeroing it. The old code only ever read startTime.
                // Any future fix here must not write shared effect state from the loop -
                // derive a local epoch instead, or make the reduction read-only.
                if (t >= (int32_t)denom) t %= denom;
            }
            t -= effect.startDelay;                       // negative while still delayed
            effect.elapsedTime = (t < 0) ? 0 : (uint16_t)t;

            // B3: an effect bound to a REAL trigger button only plays while that button is
            // held.  0 (unset) and 0xFF (DIEB_NOTRIGGER - what DirectInput sends for "no
            // trigger", on every ordinary effect) both mean "always play", not "button 255".
            bool triggerOk = true;
            if (effect.triggerButton != 0 && effect.triggerButton != 0xFF &&
                effect.triggerButton <= _buttonCount)
            {
                uint8_t b = effect.triggerButton - 1;    // descriptor: Trigger Button is 1-based
                triggerOk = (_buttonValues[b >> 3] & (1 << (b & 7))) != 0;
            }

	    	if (triggerOk &&                                             // state checked above
	    	    (t >= 0) &&                                              // reached its startDelay
	    	    (infinite || t <= (int32_t)effect.duration) &&           // not finished
	    	    notPaused)
            {
                // if this is a directional conditional calculate the conditional parameters
                // as the length in the direction of its angle. This is the same as the dot product of the vectors
                EffectParams direction_effect_params;
                if (effect.conditionReportsCount == 1)
                {
                    // Project ONLY the metric this effect type actually reads.
                    // getEffectForce uses exactly one of these four - spring->position,
                    // damper->velocity, inertia->acceleration, friction->positionChange -
                    // but all four were computed here, each an int16 -> float -> int16
                    // round trip, x2 axes: 8 float chains where 2 are needed, so ~75 % was
                    // calculated and thrown away. The switch is hoisted out of the axis
                    // loop too, so it dispatches once instead of once per axis (this
                    // assumes FFB_AXIS_COUNT == 2, as getAngleRatio and the rest already do).
                    const float ar0 = getAngleRatio(effect, 0);
                    const float ar1 = getAngleRatio(effect, 1);
                    switch (effect.effectType)
                    {
                        case USB_EFFECT_SPRING:
                            direction_effect_params.springPosition =
                                m_effect_params[0].springPosition * ar0 + m_effect_params[1].springPosition * ar1;
                            break;
                        case USB_EFFECT_DAMPER:
                            direction_effect_params.damperVelocity =
                                m_effect_params[0].damperVelocity * ar0 + m_effect_params[1].damperVelocity * ar1;
                            break;
                        case USB_EFFECT_INERTIA:
                            direction_effect_params.inertiaAcceleration =
                                m_effect_params[0].inertiaAcceleration * ar0 + m_effect_params[1].inertiaAcceleration * ar1;
                            break;
                        case USB_EFFECT_FRICTION:
                            direction_effect_params.frictionPositionChange =
                                m_effect_params[0].frictionPositionChange * ar0 + m_effect_params[1].frictionPositionChange * ar1;
                            break;
                        default: break;   // non-condition types never read these
                    }
                }

                for (int axis = 0; axis < FFB_AXIS_COUNT; ++axis)
                {
                    if (effect.enableAxis == DIRECTION_ENABLE || effect.enableAxis & (1 << axis))
                    {
                        acc[axis] += (int32_t)getEffectForce(effect, effect.conditionReportsCount == 1 ? direction_effect_params : m_effect_params[axis], axis);
                    }
                }
            }
	    }
		
#ifdef _serialPrintForces
		  Serial.println();
#endif

    }

    // B1: apply the host PID Device Gain (0..255, 255 = full - set in the ctor so games
    // that never send Set Device Gain are unaffected). Shift, not /255, to stay cheap.
    // Stays int32 until the final constrain so a stacked-effect sum can't wrap here either.
    uint8_t devGain = DynamicHID().pidReportHandler.deviceGain.gain;
    for (int axis = 0; axis < FFB_AXIS_COUNT; ++axis)
    {
        int32_t f = (int32_t)((float)(m_gains[axis].totalGain * GAIN_PCT) * acc[axis]);  // per-axis total gain
        f = (f * devGain) >> 8;                                                      // host device gain
        forces[axis] = (int16_t)constrain(f, -10000L, 10000L);
    }

#ifdef FFB_SERIAL_TRACE
    extern volatile uint16_t loopCount;   // these three live in Arduino_FFB_Yoke.ino
    extern uint16_t stackFreeMin();
    extern uint16_t stackPrevSession;
    // ~4 Hz snapshot: F<nPlaying>,<rxReports>,<loops>
    //   rxReports  OUT reports accepted in this ~250 ms window.  x4 = reports/s.
    //   loops      main-loop passes in the same window.          x4 = loop Hz.
    // The wire ceiling is ~1000 reports/s (bInterval=1), but the REAL ceiling is the
    // drain rate: the OUT endpoint is double-banked, so each RecvfromUsb() call takes at
    // most 2 reports, and they only run once per loop pass.  If rxReports tracks
    // 2 x loops (or n_drain_points x loops) we are drain-limited and the host's surplus
    // is queuing up host-side; if rxReports sits well below that, the host simply is not
    // sending more and the latency is elsewhere.
    // (fx/fy dropped from this line to fit the trace build in flash.)
    static uint32_t ftLast = 0;
    if ((uint32_t)millis() - ftLast >= 250)
    {
        ftLast = millis();
        ftTag('F', DynamicHID().pidReportHandler.playingCount());
        ftV(DynamicHID().pidReportHandler.rxReportCount);
        DynamicHID().pidReportHandler.rxReportCount = 0;
        ftV(loopCount);
        loopCount = 0;
        // The stack watermark moved here when the P line went. recordStackWatermark()
        // samples it every 50 ms and persists a new low to EEPROM; without a reader it
        // would be a RAM scan nobody looks at. prevMin is the post-mortem field - the
        // previous session's low, read back at boot - and is the only way to see how
        // close a session that CRASHED came, which a live reading never can.
        ftV((long)stackFreeMin());
        ftEnd((long)stackPrevSession);
    }
#endif

}

int16_t Joystick_::ConstantForceCalculator(volatile TEffectState& effect) 
{
	// float tempforce = (float)effect.magnitude * effect.gain / 255;
	// return (int16_t)tempforce;

	return ApplyEnvelope(effect, (int32_t)effect.magnitude);
}

int16_t Joystick_::RampForceCalculator(volatile TEffectState& effect)
{
	if (effect.duration == 0) return effect.endMagnitude;   // D3: guard /0
	// Integer, multiply-before-divide - no need for float here.
	int32_t span = (int32_t)(effect.endMagnitude - effect.startMagnitude);
	int32_t t = effect.elapsedTime; if (t > effect.duration) t = effect.duration;
	return (int16_t)(effect.startMagnitude + span * t / effect.duration);
}

// Time offset within one period corresponding to the effect's phase.
// E2: (uint32_t) forces a widening multiply - phase*period overflows 16-bit int otherwise.
static inline uint16_t phaseTimeOf(volatile TEffectState& effect, uint16_t period)
{
	return (uint16_t)(((uint32_t)effect.phase * period) / 36000u);
}
// E7: position within the current period, 0..period-1. Folding phase in here means the
// waveform never phase-jumps when elapsedTime wraps, and phase gets its full effect (E1).
static inline uint16_t periodPos(volatile TEffectState& effect, uint16_t period)
{
	return (uint16_t)(((uint32_t)effect.elapsedTime + phaseTimeOf(effect, period)) % period);
}

int16_t Joystick_::SquareForceCalculator(volatile TEffectState& effect)
{
	int16_t offset = effect.offset * 2;
	int16_t magnitude = effect.magnitude;
	uint16_t period = effect.period;
	if (period == 0) return ApplyEnvelope(effect, offset + magnitude);   // D3
	uint16_t reminder = periodPos(effect, period);
	int16_t tempforce = (reminder > (period / 2)) ? (offset - magnitude) : (offset + magnitude);
	return ApplyEnvelope(effect, tempforce);
}

int16_t Joystick_::SinForceCalculator(volatile TEffectState& effect)
{
	int16_t offset = effect.offset * 2;
	int16_t magnitude = effect.magnitude;
	uint16_t period = effect.period;
	uint16_t phase16 = 0;
	if (period != 0)
	{
		// E1: phase is folded into periodPos() so it shifts the whole cycle (was a
		//     stray +phase/36000 term, ~1 rad max). E7: periodPos() stays bounded.
		phase16 = (uint16_t)(((uint32_t)periodPos(effect, period) << 16) / period);
	}
	// DO NOT replace this sin() with a lookup table without reading Rev 8.15-8.21 first.
	// A PROGMEM quarter-wave LUT was tried twice - at both sin() sites, then at this one
	// alone - and BOTH reproduced a hang during effect creation (TelemFFB's threads block
	// inside dib_effect_update/dib_poll). The table was verified correct four ways and
	// --relax, stack exhaustion and the getAngleRatio half were all eliminated. The cause
	// is still unknown, so this is a known-good baseline, not a missed optimisation.
	int32_t tempforce = (((int32_t)sinQ15i(phase16) * magnitude) >> 15) + offset;
	return ApplyEnvelope(effect, tempforce);
}

int16_t Joystick_::TriangleForceCalculator(volatile TEffectState& effect)
{
	int16_t offset = effect.offset * 2;
	int16_t magnitude = effect.magnitude;               // 0..10000 (clamped in SetPeriodic)
	int16_t minMagnitude = offset - magnitude;
	uint16_t period = effect.period;
	if (period == 0) return ApplyEnvelope(effect, (int16_t)(offset + magnitude));   // D3 (period==0 guard). No negation - see the non-degenerate return below.
	uint16_t reminder = periodPos(effect, period);
	uint16_t half = period / 2;
	uint16_t r = (reminder <= half) ? reminder : (uint16_t)(period - reminder);   // rising 0..half
	// E6: multiply-before-divide. Peak-to-peak is 2*magnitude over 0..half.
	// (int32_t)magnitude * 4 <= 40000; * r (<= period/2 <= 16383) <= 6.6e8 -> fits int32.
	int32_t tempforce = ((int32_t)magnitude * 4 * r) / period + minMagnitude;
	return ApplyEnvelope(effect, (int16_t)tempforce);   // no negation: Fino's spurious - made waves inverted vs constant/sine/square
}

int16_t Joystick_::SawtoothDownForceCalculator(volatile TEffectState& effect)
{
	int16_t offset = effect.offset * 2;
	int16_t magnitude = effect.magnitude;
	int16_t minMagnitude = offset - magnitude;
	uint16_t period = effect.period;
	if (period == 0) return ApplyEnvelope(effect, (int16_t)(offset + magnitude));   // D3 (period==0 guard). No negation - see the non-degenerate return below.
	uint16_t reminder = periodPos(effect, period);
	// E6: ramp maxMag -> minMag over the period; multiply-before-divide.
	// (int32_t)magnitude*2 <= 20000; * (period - reminder) (<= 32767) -> <= 6.6e8.
	int32_t tempforce = ((int32_t)magnitude * 2 * (period - reminder)) / period + minMagnitude;
	return ApplyEnvelope(effect, (int16_t)tempforce);   // no negation: Fino's spurious - made waves inverted vs constant/sine/square
}

int16_t Joystick_::SawtoothUpForceCalculator(volatile TEffectState& effect)
{
	int16_t offset = effect.offset * 2;
	int16_t magnitude = effect.magnitude;
	int16_t minMagnitude = offset - magnitude;
	uint16_t period = effect.period;
	if (period == 0) return ApplyEnvelope(effect, (int16_t)(offset - magnitude));   // D3 (period==0 guard). No negation - see the non-degenerate return below.
	uint16_t reminder = periodPos(effect, period);
	// E6: ramp minMag -> maxMag over the period; multiply-before-divide.
	int32_t tempforce = ((int32_t)magnitude * 2 * reminder) / period + minMagnitude;
	return ApplyEnvelope(effect, (int16_t)tempforce);   // no negation: Fino's spurious - made waves inverted vs constant/sine/square
}

int16_t Joystick_::ConditionForceCalculator(volatile TEffectState& effect, float metric, uint8_t conditionReport)
{
    volatile TEffectCondition& c = effect.conditions[conditionReport];

    // E3: metric is normalised (-1..1); the condition parameters are raw -10000..10000.
    // Bring the offset/deadband boundaries into the same normalised space ONCE, and use
    // them in both the compare and the force term (was: raw in the compare, /10000 in the
    // body - so any non-zero offset/deadband misbehaved).
    // Reciprocal multiplies, not divides: these run per condition effect per axis every
    // loop pass, and conditions (spring/damper/inertia/friction) are the priciest effects.
    float offNeg = (c.cpOffset - (float)c.deadBand) * (1.0f / 10000.0f);
    float offPos = (c.cpOffset + (float)c.deadBand) * (1.0f / 10000.0f);

    float tempForce;
    if (metric < offNeg) {
        tempForce = (metric - offNeg) * c.negativeCoefficient;
        if (tempForce < -(float)c.negativeSaturation) tempForce = -(float)c.negativeSaturation;
    }
    else if (metric > offPos) {
        tempForce = (metric - offPos) * c.positiveCoefficient;
        if (tempForce > (float)c.positiveSaturation) tempForce = (float)c.positiveSaturation;
    }
    else return 0;

    tempForce = -tempForce * effect.gain * (1.0f / 255.0f);
    return (int16_t)tempForce;
}

float Joystick_::NormalizeRange(int16_t x, int16_t maxValue) {
    if (maxValue == 0) return 0.0f;   // D3: guard /0 (uncalibrated axis / zero max-param)
    float value = (float)x / maxValue;
    return (value > 1.0f) ? 1.0f : (value < -1.0f ? -1.0f : value);
}

int16_t  Joystick_::ApplyGain(uint16_t value, uint8_t gain)
{
	int32_t value_32 = value;
	return ((value_32 * gain) / 255);
}

int16_t Joystick_::ApplyEnvelope(volatile TEffectState& effect, int16_t value)
{
	// Fast path: no envelope. With attackTime and fadeTime both 0 neither shaping branch
	// below can run, so the body reduces to (magnitude * value) / magnitude == value, and
	// the only thing that survives is the magnitude==0 guard. TelemFFB almost never sets
	// an envelope, so this is the common case for every effect on every axis on every
	// loop pass - and it skips two more ApplyGain() calls plus two int32 divides.
	if (effect.attackTime == 0 && effect.fadeTime == 0)
		return ApplyGain(effect.magnitude, effect.gain) ? value : 0;

	int32_t magnitude = ApplyGain(effect.magnitude, effect.gain);
	if (magnitude == 0) return 0;                 // D3: guard the final /magnitude
	int32_t attackLevel = ApplyGain(effect.attackLevel, effect.gain);
	int32_t fadeLevel = ApplyGain(effect.fadeLevel, effect.gain);
	int32_t newValue = magnitude;
	int32_t attackTime = effect.attackTime;
	int32_t fadeTime = effect.fadeTime;
	int32_t elapsedTime = effect.elapsedTime;
	int32_t duration = effect.duration;

	if (attackTime > 0 && elapsedTime < attackTime)   // D3: guard /attackTime
	{
		newValue = (magnitude - attackLevel) * elapsedTime;
		newValue /= attackTime;
		newValue += attackLevel;
	}
	// D3: guard /fadeTime (A1 makes fadeTime real). Spec: an INFINITE effect has attack then
	// an infinite sustain - no fade - so skip it when duration is the INFINITE marker.
	if (fadeTime > 0 && effect.duration < USB_DURATION_INFINITE && elapsedTime > (duration - fadeTime))
	{
		newValue = (magnitude - fadeLevel) * (duration - elapsedTime);
		newValue /= fadeTime;
		newValue += fadeLevel;
	}

	newValue *= value;
	newValue /= magnitude;
	return newValue;
}

void Joystick_::end()
{
}

void Joystick_::setButton(uint8_t button, uint8_t value)
{
	if (value == 0)
	{
		releaseButton(button);
	}
	else
	{
		pressButton(button);
	}
}
void Joystick_::pressButton(uint8_t button)
{
    if (button >= _buttonCount) return;

    int index = button / 8;
    int bit = button % 8;

	bitSet(_buttonValues[index], bit);
	if (_autoSendState) sendState();
}
void Joystick_::releaseButton(uint8_t button)
{
    if (button >= _buttonCount) return;

    int index = button / 8;
    int bit = button % 8;

    bitClear(_buttonValues[index], bit);
	if (_autoSendState) sendState();
}

void Joystick_::setXAxis(int16_t value)
{
	_xAxis = value;
	if (_autoSendState) sendState();
}
void Joystick_::setYAxis(int16_t value)
{
	_yAxis = value;
	if (_autoSendState) sendState();
}
void Joystick_::setZAxis(int16_t value)
{
	_zAxis = value;
	if (_autoSendState) sendState();
}

// void Joystick_::setRudder(int16_t value)
// {
// 	_rudder = value;
// 	if (_autoSendState) sendState();
// }
// void Joystick_::setThrottle(int16_t value)
// {
// 	_throttle = value;
// 	if (_autoSendState) sendState();
// }

void Joystick_::setHatSwitch(int8_t hatSwitchIndex, int16_t value)
{
	if (hatSwitchIndex >= _hatSwitchCount) return;
	
	_hatSwitchValues[hatSwitchIndex] = value;
	if (_autoSendState) sendState();
}

int Joystick_::buildAndSet16BitValue(bool includeValue, int16_t value, int16_t valueMinimum, int16_t valueMaximum, int16_t actualMinimum, int16_t actualMaximum, uint8_t dataLocation[]) 
{
	int16_t convertedValue;
	uint8_t highByte;
	uint8_t lowByte;
	int16_t realMinimum = min(valueMinimum, valueMaximum);
	int16_t realMaximum = max(valueMinimum, valueMaximum);

	if (includeValue == false) return 0;

	if (value < realMinimum) {
		value = realMinimum;
	}
	if (value > realMaximum) {
		value = realMaximum;
	}

	if (valueMinimum > valueMaximum) {
		// Values go from a larger number to a smaller number (e.g. 1024 to 0)
		value = realMaximum - value + realMinimum;
	}

	convertedValue = map(value, realMinimum, realMaximum, actualMinimum, actualMaximum);

	highByte = (uint8_t)(convertedValue >> 8);
	lowByte = (uint8_t)(convertedValue & 0x00FF);
	
	dataLocation[0] = lowByte;
	dataLocation[1] = highByte;
	
	return 2;
}

int Joystick_::buildAndSetAxisValue(bool includeAxis, int16_t axisValue, int16_t axisMinimum, int16_t axisMaximum, uint8_t dataLocation[])
{
	return buildAndSet16BitValue(includeAxis, axisValue, axisMinimum, axisMaximum, JOYSTICK_AXIS_MINIMUM, JOYSTICK_AXIS_MAXIMUM, dataLocation);
}

void Joystick_::sendState()
{
	uint8_t data[_hidReportSize];
	int index = 0;
	
	// Load Button State
	for (; index < _buttonValuesArraySize; index++)
	{
		data[index] = _buttonValues[index];		
	}

	// Set Hat Switch Values
	if (_hatSwitchCount > 0) {
		
		// Calculate hat-switch values
		uint8_t convertedHatSwitch[JOYSTICK_HATSWITCH_COUNT_MAXIMUM];
		for (int hatSwitchIndex = 0; hatSwitchIndex < JOYSTICK_HATSWITCH_COUNT_MAXIMUM; hatSwitchIndex++)
		{
			if (_hatSwitchValues[hatSwitchIndex] < 0)
			{
				convertedHatSwitch[hatSwitchIndex] = 8;
			}
			else
			{
				convertedHatSwitch[hatSwitchIndex] = (_hatSwitchValues[hatSwitchIndex] % 360) / 45;
			}			
		}

		// Pack hat-switch states into a single byte
		data[index++] = (convertedHatSwitch[1] << 4) | (B00001111 & convertedHatSwitch[0]);
	
	} // Hat Switches

	// Set Axis Values
	index += buildAndSetAxisValue(_includeAxisFlags & JOYSTICK_INCLUDE_X_AXIS, _xAxis, _xAxisMinimum, _xAxisMaximum, &(data[index]));
	index += buildAndSetAxisValue(_includeAxisFlags & JOYSTICK_INCLUDE_Y_AXIS, _yAxis, _yAxisMinimum, _yAxisMaximum, &(data[index]));
	index += buildAndSetAxisValue(_includeAxisFlags & JOYSTICK_INCLUDE_Z_AXIS, _zAxis, _zAxisMinimum, _zAxisMaximum, &(data[index]));
	// index += buildAndSetAxisValue(_includeAxisFlags & JOYSTICK_INCLUDE_RX_AXIS, _xAxisRotation, _rxAxisMinimum, _rxAxisMaximum, &(data[index]));
	// index += buildAndSetAxisValue(_includeAxisFlags & JOYSTICK_INCLUDE_RY_AXIS, _yAxisRotation, _ryAxisMinimum, _ryAxisMaximum, &(data[index]));
	// index += buildAndSetAxisValue(_includeAxisFlags & JOYSTICK_INCLUDE_RZ_AXIS, _zAxisRotation, _rzAxisMinimum, _rzAxisMaximum, &(data[index]));
	
	// Set Simulation Values
	// index += buildAndSetSimulationValue(_includeSimulatorFlags & JOYSTICK_INCLUDE_RUDDER, _rudder, _rudderMinimum, _rudderMaximum, &(data[index]));
	// index += buildAndSetSimulationValue(_includeSimulatorFlags & JOYSTICK_INCLUDE_THROTTLE, _throttle, _throttleMinimum, _throttleMaximum, &(data[index]));

	DynamicHID().SendReport(_hidReportId, data, _hidReportSize);
}

#endif
