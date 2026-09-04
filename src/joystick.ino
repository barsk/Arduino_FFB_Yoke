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


/******************************************
  setup joystick and initialisation
*******************************************/
void setupJoystick() {
   setupDefaults();

  if(isEepromDataValid()) {
    readSettingsFromEeprom();
  }else{
    writeSettingsToEeprom(); // store defaults
  }

  setRangeJoystick();  // HID axis range + spring position scale, from the now-loaded softLock_range
  Joystick.setGains(gains);
}

// default values
void setupDefaults(){
  gains[MEM_ROLL].totalGain = default_ROLL_TOT_GAIN;
  gains[MEM_PITCH].totalGain = default_PITCH_TOT_GAIN;

  maxVelocityPcnt = DEFAULT_VELOCITY_PCNT;

  effects[MEM_ROLL].frictionMaxPositionChange = default_frictionMaxPositionChange_ROLL;
  effects[MEM_ROLL].inertiaMaxAcceleration = default_inertiaMaxAcceleration_ROLL;
  effects[MEM_ROLL].damperMaxVelocity = default_damperMaxVelocity_ROLL;

  effects[MEM_PITCH].frictionMaxPositionChange = default_frictionMaxPositionChange_PITCH;
  effects[MEM_PITCH].inertiaMaxAcceleration = default_inertiaMaxAcceleration_PITCH;
  effects[MEM_PITCH].damperMaxVelocity = default_damperMaxVelocity_PITCH;

  adjPwmMin[MEM_ROLL] = default_ROLL_PWM_MIN;
  adjPwmMin[MEM_PITCH] = default_PITCH_PWM_MIN;

  // Travel limit / end cushion (0 for roll on this HW = full range, no cushion).
  // No-op if calibration hasn't run yet; setRangeJoystick() re-derives after.
  axis[MEM_ROLL].setSoftLockRangeFromRangePcnt(default_ROLL_TRAVEL_RANGE_PCNT);
  axis[MEM_PITCH].setSoftLockRangeFromRangePcnt(default_PITCH_TRAVEL_RANGE_PCNT);

  for (byte i = MEM_ROLL; i <= MEM_PITCH; i++) {
    gains[i].constantGain = default_gain;
    gains[i].rampGain = default_gain;
    gains[i].squareGain = default_gain;
    gains[i].sineGain = default_gain;
    gains[i].triangleGain = default_gain;
    gains[i].sawtoothdownGain = default_gain;
    gains[i].sawtoothupGain = default_gain;
    gains[i].springGain = default_gain;
    gains[i].damperGain = default_gain;
    gains[i].inertiaGain = default_gain;
    gains[i].frictionGain = default_friction_gain;
    gains[i].defaultSpringGain = default_spring_gain;
  }
}

// Set the HID-reported range for each axis to its working travel (mechanical
// range minus the softLock_range cushion), and keep each spring/condition
// effect's position scale (springMaxPosition) in lock-step with it - otherwise
// the spring reaches "centre" at a different deflection than the axis reports.
void setRangeJoystick() {
  for (byte i = MEM_ROLL; i <= MEM_PITCH; i++) {
    int32_t half = axis[i].config.iMax - axis[i].config.softLock_range;
    if (half < 0) half = 0;
    if (half > 32767) half = 32767;              // springMaxPosition / axis range are int16
    effects[i].springMaxPosition = (int16_t)half;
  }
  Joystick.setXAxisRange(-effects[MEM_ROLL].springMaxPosition,  effects[MEM_ROLL].springMaxPosition);
  Joystick.setYAxisRange(-effects[MEM_PITCH].springMaxPosition, effects[MEM_PITCH].springMaxPosition);
}

void updateEffects(bool recalculate) {
  //If you need to use the spring effect, set the following parameters.`Position` is the current position of the force feedback axis.
  //For example, connect the encoder with the action axis,the current encoder value is `Positon` and the max encoder value is `MaxPosition`.

   unsigned long currentMillis = millis();
  int16_t diffTime = currentMillis - lastEffectsUpdate;

  for (byte i = MEM_ROLL; i <= MEM_PITCH; i++) {
    // springMaxPosition is set in setRangeJoystick() (in lock-step with the HID
    // axis range); here we only feed the live position.
    effects[i].springPosition = encoderPos[i];

    if (diffTime > 0 && recalculate) {
      lastEffectsUpdate = currentMillis;
      int32_t positionChange = encoderPos[i] - physicsData[i].lastPos;
      // E5: scale BEFORE dividing so gentle inputs don't quantise to 0. Pure integer.
      // vel is counts/ms << VEL_SHIFT (see defines.h); accel inherits the same scale.
      // Clamp both to int16 - a violent slam saturates, which is the correct behaviour
      // for damper/inertia (they resist), and keeps EffectParams compact.
      int32_t vel = ((int32_t)positionChange << VEL_SHIFT) / diffTime;
      vel = constrain(vel, -32767, 32767);
      int32_t accel = ((vel - physicsData[i].lastVel) * 10) / diffTime;
      accel = constrain(accel, -32767, 32767);

      effects[i].frictionPositionChange = constrain(positionChange, -32767, 32767);  // raw delta
      effects[i].inertiaAcceleration = accel;
      effects[i].damperVelocity = vel;

      physicsData[i].lastPos = encoderPos[i];
      physicsData[i].lastAccel = accel;
      physicsData[i].lastVel = vel;
    }
  }
  
  Joystick.setXAxis(encoderPos[MEM_ROLL]);
  Joystick.setYAxis(encoderPos[MEM_PITCH]);
  Joystick.setEffectParams(effects);
  Joystick.getForce(forces);
}