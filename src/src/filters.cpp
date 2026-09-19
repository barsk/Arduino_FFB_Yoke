#include "filters.h"

#define TWO_PI_F 6.28318530718f

// One-pole low pass, discretised as alpha = dt / (RC + dt) (backward Euler) rather than
// alpha = 1 - exp(-dt/RC).  Same cutoff to first order, and two practical wins on this
// part: no exp() in the build (the avr-libc soft-float one costs a few hundred bytes of
// flash and this was its only runtime caller), and alpha stays below 1 for any dt, so a
// slow loop pass can never make the filter overshoot its input.
static float alphaFor(float cutoffHz, float deltaTime)
{
	if (cutoffHz <= 0.0f) return 1.0f;      // 0 Hz = filter off, pass the input straight through
	if (deltaTime <= 0.0f) return 0.0f;     // no time passed, hold
	float rc = 1.0f / (TWO_PI_F * cutoffHz);
	return deltaTime / (rc + deltaTime);
}

// Pass-through until configured.  It used to default to alpha 0, which is not "unfiltered"
// but "output frozen at 0" - a damper/inertia/friction force of exactly zero, silently,
// if the filters were ever used before Joystick_::begin() configured them.
LowPassFilter::LowPassFilter():
	output(0),
	ePow(1.0f){}

LowPassFilter::LowPassFilter(float iCutOffFrequency, float iDeltaTime):
	output(0),
	ePow(alphaFor(iCutOffFrequency, iDeltaTime))
{
}

float LowPassFilter::update(float input){
	return output += (input - output) * ePow;
}

float LowPassFilter::update(float input, float deltaTime, float cutoffFrequency){
	reconfigureFilter(deltaTime, cutoffFrequency); //Changes ePow accordingly.
	return output += (input - output) * ePow;
}

void LowPassFilter::reconfigureFilter(float deltaTime, float cutoffFrequency){
	ePow = alphaFor(cutoffFrequency, deltaTime);
}
