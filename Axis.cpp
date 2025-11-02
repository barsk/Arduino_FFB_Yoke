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

#include "Axis.h"

// int freeRAM() {
//   extern int __heap_start, *__brkval;
//   int v;
//   return (int)&v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
// }

Axis::Axis(byte motorPinB, byte motorPinF, bool isRoll, AS5600* encoderPtr,
  byte channel, TCA9548* i2c_muxPtr, Multiplexer* multiplexerPtr, BeepManager* beepManagerPtr, byte& adjPwmMinPtr) //byte& maxVel,
  : motorPinBack(motorPinB), motorPinForw(motorPinF), blIsRoll(isRoll), encoder(encoderPtr),
  i2c_channel(channel), i2c_mux(i2c_muxPtr), multiplexer(multiplexerPtr), beepManager(beepManagerPtr), pwmMin(adjPwmMinPtr), // maxVelocityPcnt(maxVel)
  speedLimitActive(false) {

  lastMovementTime = millis();

  if (blIsRoll) {
    maxIncrement = CALIBRATION_MAX_INCREMENT_X;
    config.softLock_range = SOFT_LOCK_X; // not used, (but will be overwritten by EEPROM value in setup())
    config.softLock_hyst = SOFT_LOCK_BUFFER_X;
    config.softlock_force = SOFT_LOCK_FORCE_X;
    config.maxVelocity =  MAX_VELOCITY_X;
    // config.maxVelocity = (uint16_t)(maxVelocityPcnt / 100.0f * MAX_VELOCITY_X);
  } else {
    maxIncrement = CALIBRATION_MAX_INCREMENT_X;
    config.softLock_range = SOFT_LOCK_Y; // will be overwritten by EEPROM value in setup()
    config.softLock_hyst = SOFT_LOCK_BUFFER_Y;
    config.softlock_force = SOFT_LOCK_FORCE_Y;
    config.maxVelocity = MAX_VELOCITY_Y;
  }
}

void Axis::setPwmMin(byte min) {
  pwmMin = min;
}

// Method to move the motor in a given direction
void Axis::driveMotor(bool forwardDir) {

  if (forwardDir) {
    analogWrite(motorPinBack, 0);
    analogWrite(motorPinForw, pwmSpeed);
  } else {
    analogWrite(motorPinBack, pwmSpeed);
    analogWrite(motorPinForw, 0);
  }
  // Serial.print(F("Dir: "));
  // Serial.print(forwardDir);
  // Serial.print(F(", pwm="));
  // Serial.println(pwmSpeed);
}

// Method to stop the motor
void Axis::stopMotor() {
  analogWrite(motorPinBack, 0);
  analogWrite(motorPinForw, 0);
}



// read multiplexer for end stops
void Axis::readEndStops() {
  if (blIsRoll) {
    blLimitStart = !digitalReadFast(IR_ROLL_LEFT);
    blLimitEnd = !digitalReadFast(IR_ROLL_RIGHT);
  } else {
    blLimitStart = !digitalReadFast(IR_PITCH_DOWN);
    blLimitEnd = !digitalReadFast(IR_PITCH_UP);
 }
}

// read the encoder cummulative position (includes revolutions)
int32_t Axis::getCumulativePos() {
  i2c_mux->selectChannel(i2c_channel);
  return encoder->getCumulativePosition();
}

// // read the encoder absolute position (0..4096)
int32_t Axis::getAbsolutePos() {
  i2c_mux->selectChannel(i2c_channel);
  return encoder->readAngle();
}

// Speed in RPM, cannot be used due to Flash ROM size
// float Axis::readAngularSpeed() { // No update mode. Call readEncoder() before! 
//   // i2c_mux->selectChannel(i2c_channel);
//   return encoder->getAngularSpeed(AS5600_MODE_RPM, false);
// }

// Reset the encoder counter to position
int32_t Axis::resetEncoder(int32_t position) {
  i2c_mux->selectChannel(i2c_channel);
  encoder->resetCumulativePosition(position);
  return position;
}

// Full calibration method for the axis
bool Axis::calibrate() {
  int16_t motorDelay = blIsRoll ? CALIBRATION_MOTOR_DELAY_X : CALIBRATION_MOTOR_DELAY_Y;
  int32_t lastEncoderValue;
  
  readEndStops();  // Update end switch states

  Serial.print(F("Calibrate "));
  Serial.println(blIsRoll ? F("Roll") : F("Pitch"));

  //**********************************************************************
  // Step 1: Move towards the first roll left/ pitch down (farthest) end switch 
  //**********************************************************************
  pwmSpeed = 20;
  if (!slowMove(BACKWARD)) {
    return false;
  }

  //End switch was hit
  delay(motorDelay); // Wait for bounce off endstop to stabilize   
  lastEncoderValue = getAbsolutePos(); // min pos, absolute for roll
  resetEncoder(); // Start pos reset to zero at this position 
  stopMotor();  // Stop the motor   

    // Remember roll absolute min pos(offset)
  // Cannot do absolute pos for pitch since we have more than one revolution of axis there
  if (blIsRoll) {
    EEPROM.put(EEPROM_ENCODER_X_OFFSET_INDEX, lastEncoderValue + RANGE_LIMITER_X);
    // Serial.print("Offset: "); Serial.println(lastEncoderValue + RANGE_LIMITER_X);
  }
       
  // Determine which end switch was triggered
  if(blLimitEnd) // wrong one!
  {
    Serial.println(F("Motor inverted!"));
    return false;
  }
 
  //**********************************************************************
  // Step 2: Move to the roll right/pitch up (nearest) switch
  //**********************************************************************
  if (!slowMove(FORWARD)) {
    return false;
  }
  delay(motorDelay); // Wait for bounce off endstop to stabilize 
  lastEncoderValue = getCumulativePos(); // max pos, i.e range from 0 to max for travel
 
  // apply limiter to assure we get full range
  int32_t rangeLimiter = blIsRoll ? RANGE_LIMITER_X : RANGE_LIMITER_Y;

   // calculate iMin and iMax based on the encoder value
  config.iMax = lastEncoderValue / 2 - rangeLimiter; // Set iMax as half of the maximum encoder value
  config.iMin = -config.iMax; // Set iMin as the negative value of iMax
  resetEncoder(config.iMax + rangeLimiter);  // Axis is at max, Set the encoder to zero at middle pos, was + deadzone
  stopMotor();  // Stop the motor 
  EEPROM.put(blIsRoll ? EEPROM_ENCODER_X_MAX_INDEX : EEPROM_ENCODER_Y_MAX_INDEX, config.iMax);

  Serial.print(F("Min-Max: "));
  Serial.print(config.iMin);
  Serial.print(";");
  Serial.println(config.iMax);  

  //**********************************************************************
  // 4. Move axis to the middle (encoder pos zero)
  //**********************************************************************
  centerAxis();
  Serial.println(F("Cal done!"));
  return true;
}

bool Axis::EEPromCalibration() {
  if (blIsRoll) {
    rollEEPromCalibration();
    return true;
  } else {
    return pitchEEPromCalibration();
  }
}

// Read calibration data from EEPROM
void Axis::rollEEPromCalibration() {
  int32_t offset, max, currPos;
  EEPROM.get(EEPROM_ENCODER_X_OFFSET_INDEX, offset);
  EEPROM.get(EEPROM_ENCODER_X_MAX_INDEX, max);

  currPos = getAbsolutePos() - offset;
  if (currPos < 0) {
    currPos += 4096;
  }
  
  currPos = currPos- max;
  resetEncoder(currPos);
  config.iMin = -max;
  config.iMax = max;
}

// Reuse calibration data from EEPROM
bool Axis::pitchEEPromCalibration() {
  EEPROM.get(EEPROM_ENCODER_Y_MAX_INDEX, config.iMax);
  config.iMin = -config.iMax; // Set iMin as the negative value of iMax

  pwmSpeed = 10;
  lastMovementTime = millis();
  if (!slowMove(BACKWARD)) {
    return false;
  }
  delay(CALIBRATION_MOTOR_DELAY_Y); // Wait for possible bounce off endstop with power still on
  resetEncoder(config.iMin - RANGE_LIMITER_Y);  // Set encoder to match min pos
  stopMotor();
  
  return true;
}

// Helper method to manage slow motor movement. targetPos is optional
// Note. The caller is responsible to call stopMotor(), unless if timeout
bool Axis::slowMove(bool direction, int32_t targetPos) {
  int32_t lastEncoderValue = getCumulativePos();
  int32_t currentEncoderValue;
  lastMovementTime = millis();
  bool limitSwitch = false;
  bool playBeep = (targetPos > -1);

  while (!limitSwitch) {
    readEndStops(); // Update end switch states
    limitSwitch = (direction == FORWARD) ? blLimitEnd : blLimitStart;
    
    currentEncoderValue = getCumulativePos();

    // stop at target?
    if (targetPos > -1) {
      if ((direction == FORWARD && currentEncoderValue >= targetPos) || (direction == BACKWARD && currentEncoderValue <=  targetPos))  {
       break; 
      }
    }

    // Check for speed increase
    if (abs(currentEncoderValue - lastEncoderValue) <= maxIncrement) {
      if (pwmSpeed < CALIBRATION_MAX_PWM) pwmSpeed++;
    } else {
      lastMovementTime = millis();
      if (abs(currentEncoderValue - lastEncoderValue) > maxIncrement + 20) { // too fast?
        pwmSpeed--;
      }
    }
    lastEncoderValue = currentEncoderValue;

    // Constrain to make sure...
    pwmSpeed = constrain(pwmSpeed, 0, CALIBRATION_MAX_PWM);
    driveMotor(direction);

    // Movement Timeout?
    if (millis() - lastMovementTime >= CALIBRATION_AXIS_MOVEMENT_TIMEOUT) {
      stopMotor();
      Serial.println(F("Timeout!"));
      return false;
    }
    delay(CALIBRATION_WHILE_DELAY);
  }
  if (playBeep) beepManager->beep(1000, 15); // happy beep
  return true;
}

void Axis::centerAxis(bool resetPwmSpeed) {
  if (resetPwmSpeed) pwmSpeed = 20;
  // Serial.println("Move axis to zero");
  int32_t pos = getCumulativePos();

  if (pos > 0) {
    slowMove(BACKWARD, 0);
  } else {
    slowMove(FORWARD, 0);
    return;
  }
  stopMotor();
}

/******************************************************
  calculates the motor pwmSpeeds and move
******************************************************/
void Axis::applyForce(int16_t gForce, int32_t& pos, int16_t& velocity) {

  readEndStops(); // get limit switches

  // SOFT STOP?
  if (!blIsRoll) {
    if (pos <= config.iMin + config.softLock_range) {
      gForce = calcSoftLockForce(gForce, -pos, config.softLock_hyst, -config.iMin - config.softLock_range, -config.iMin, -config.softlock_force);
    } else if (pos >= config.iMax - config.softLock_range) {
      gForce = -calcSoftLockForce(-gForce, pos, config.softLock_hyst, config.iMax - config.softLock_range, config.iMax, -config.softlock_force);
    }
  }

  // if position is on end switch and pushing then stop the motor
  if (gForce < 0 && blLimitStart) {
    gForce = -ENDSTOP_HOLD_FORCE;
  } else if (gForce > 0 && blLimitEnd) {
    gForce = ENDSTOP_HOLD_FORCE;
  }

  #ifdef ENABLE_SPEED_LIMITER
  if ((velocity > config.maxVelocity && gForce > 0) || (velocity < -config.maxVelocity && gForce < 0)) {  // Too fast and force pushing 
    speedLimitActive = true;
    // Serial.print(F("Speed constraint:"));
    // Serial.println(velocity);
  }
  if (speedLimitActive) {
      if (abs(velocity) <= config.maxVelocity - 5) {
      speedLimitActive = false;
      // Serial.println(F("Reenabling"));
    } else {
      gForce = 0;
    }
  }
  #endif

  if (abs(gForce) > 5) { // only apply pwmMin if gforce active, remove rounding errors etc
    pwmSpeed = map(abs(gForce), 0, 10000, pwmMin, 255);
  } else {
    pwmSpeed = 0;
  }

  driveMotor(gForce > 0);
}

// Calc power from positive side (iMax). For opposite end reverse (mirror) inputs with negative signs
int16_t Axis::calcSoftLockForce(int16_t force, int32_t pos, int32_t &hysteresis, int32_t sl_start, int32_t sl_end, int16_t force_max) {
  if (force >= 0 && force_max < 0) { // No force or force pulling AWAY from endstop, i.e Spring force
    // Serial.print(F(" !AGAINST! "));
    // Ramp power from current force to max through hysteresis zone
    if (pos <= sl_start + hysteresis) { // Inside hysteresis zone
      // Serial.print(F(" !HYST! "));
      force = map(pos, sl_start, sl_start + hysteresis, force, -force_max);
    } else {
      force = -force_max;
    }
  } else { // Force pushing TOWARDS endstop
    // Serial.print(F(" !TOWARDS! "));
    if (pos <= sl_start + hysteresis) {  // Inside hysteresis zone
      // Serial.print(F(" !HYST! "));
      force = 0; // hysteris, no force
    } else { // Beyond hysteresis, ramp to max
      force = map(pos, sl_start + hysteresis, sl_end, 0, -force_max);
    }
  }
  // Serial.print(F(" force="));
  // Serial.println(force);
  return force;
}

// SoftLock range calculation from travel range percentage of full (iMax)
void Axis::setSoftLockRangeFromRangePcnt(byte travelRangePcnt) {
  config.softLock_range = (int32_t)((100.0f - travelRangePcnt)/100.0f * config.iMax);
}

byte Axis::getRangePcntFromSoftLockRange() {
  return (uint8_t)(100.0f - 100.0f * config.softLock_range / config.iMax);
}

void Axis::setMAxVelocityFromPcnt(byte maxVel) {
  config.maxVelocity = (uint16_t)(maxVel / 100.0f * (blIsRoll ? MAX_VELOCITY_X : MAX_VELOCITY_Y));
}

