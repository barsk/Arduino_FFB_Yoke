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

  Joystick.setGains(gains);
}

// default values
void setupDefaults(){
  gains[MEM_ROLL].totalGain = default_PITCH_TOT_GAIN;
  gains[MEM_PITCH].totalGain = default_ROLL_TOT_GAIN;

  maxVelocityPcnt = DEFAULT_VELOCITY_PCNT;

  effects[MEM_ROLL].frictionMaxPositionChange = default_frictionMaxPositionChange_ROLL;
  effects[MEM_ROLL].inertiaMaxAcceleration = default_inertiaMaxAcceleration_ROLL;
  effects[MEM_ROLL].damperMaxVelocity = default_damperMaxVelocity_ROLL;

  effects[MEM_PITCH].frictionMaxPositionChange = default_frictionMaxPositionChange_PITCH;
  effects[MEM_PITCH].inertiaMaxAcceleration = default_inertiaMaxAcceleration_PITCH;
  effects[MEM_PITCH].damperMaxVelocity = default_damperMaxVelocity_PITCH;

  adjPwmMin[MEM_ROLL] = default_ROLL_PWM_MIN;
  adjPwmMin[MEM_PITCH] = default_PITCH_PWM_MIN;

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

void setRangeJoystick() {
  // Joystick.setXAxisRange(axis[MEM_ROLL]->config.iMin + SOFT_LOCK_X, axis[MEM_ROLL]->config.iMax - SOFT_LOCK_X);
  Joystick.setXAxisRange(axis[MEM_ROLL].config.iMin, axis[MEM_ROLL].config.iMax);
  Joystick.setYAxisRange(axis[MEM_PITCH].config.iMin + SOFT_LOCK_Y, axis[MEM_PITCH].config.iMax - SOFT_LOCK_Y);
}

void updateEffects(bool recalculate) {
  //If you need to use the spring effect, set the following parameters.`Position` is the current position of the force feedback axis.
  //For example, connect the encoder with the action axis,the current encoder value is `Positon` and the max encoder value is `MaxPosition`.

   unsigned long currentMillis = millis();
  int16_t diffTime = currentMillis - lastEffectsUpdate;

  for (byte i = MEM_ROLL; i <= MEM_PITCH; i++) {
    effects[i].springMaxPosition = axis[i].config.iMax - axis[i].config.softLock_range;
    effects[i].springPosition = encoderPos[i];

    int16_t positionChange, accel, vel;
    if (diffTime > 0 && recalculate) {
      lastEffectsUpdate = currentMillis;
      positionChange = encoderPos[i] - physicsData[i].lastPos;
      vel = positionChange / diffTime;
      accel = ((vel - physicsData[i].lastVel) * 10) / diffTime;

      //If you need to use the friction effect, set the following parameters.`PositionChange`
      //is the position difference of the force feedback axis.
      effects[i].frictionPositionChange = positionChange;

      //If you need to use the damper effect, set the following parameters.`Velocity` is the current velocity of the force feedback axis.
      effects[i].inertiaAcceleration = accel;

      //If you need to use the inertia effect, set the following parameters.`Acceleration` is the current acceleration of the force feedback axis.
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
