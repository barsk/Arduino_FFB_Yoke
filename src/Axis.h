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

#ifndef AXIS_H
#define AXIS_H

#include <Arduino.h>
#include "Multiplexer.h"
#include "Beepmanager.h"
#include "defines.h"
#include <AS5600.h>
#include <TCA9548.h>   
#include <EEPROM.h>    
#include <digitalWriteFast.h>

#define FORWARD true
#define BACKWARD false

// Structure to hold the configuration of the axis
typedef struct {
    int32_t iMin;          // Minimum value
    int32_t iMax;          // Maximum value
    int32_t softLock_range; // soft_lock range
    int32_t softLock_hyst;  // length of hysterersis zone
    int16_t softlock_force; // end stop force
    int16_t maxVelocity; 
} AxisConfiguration;

class Axis {
private:
    const Multiplexer* multiplexer;  // Pointer to a multiplexer object
    TCA9548* i2c_mux;           // Pointer to I2C multiplexer TCA9548    
    const byte motorPinBack;          // Pin for left/down motor control
    const byte motorPinForw;         // Pin for right/up motor control
    const bool blIsRoll;            // is Roll or Pitch
    bool blLimitStart;
    bool blLimitEnd;
    byte& pwmMin;           // Min pwm to start range from, max is 255
    bool speedLimitActive; 
    AS5600* encoder;          // Pointer to the encoder object
    byte i2c_channel;         // Channel on TCA9548 for the encoder          
    byte pwmSpeed;                 // Current force  of the motor
    unsigned long lastMovementTime;  // Timestamp of the last movement
    BeepManager* beepManager;
    byte maxIncrement;     // Target position increment to allow for speed measurement

    // Calibration start time
    unsigned long calibrationStartTime; 
    void driveMotor(bool direction);

    bool slowMove(bool direction, int32_t targetPos = -1);
    void readEndStops();

    // Reset current encoder position to pos  
    int32_t resetEncoder(int32_t position = 0);

    int16_t calcSoftLockForce(int16_t force, int32_t pos, int32_t &hysteresis, int32_t sl_start, int32_t sl_end, int16_t force_max);
    void rollEEPromCalibration();
    bool pitchEEPromCalibration();

public:
    // Constructor to initialize motor pins and end switches
    Axis(byte motorPinBack, byte motorPinForw, bool isRoll, AS5600* encoderPtr, byte i2c_channel,
        TCA9548* i2c_muxPtr, Multiplexer* multiplexerPtr, BeepManager *beepManager, byte& adjPwmMinPtr);

    // Axis configuration object
    AxisConfiguration config;
    
    void applyForce(int16_t force, int32_t &pos, int16_t &angularSpeed);

    // Read encoder cumulative position
    int32_t getCumulativePos();

    // Read encoder absolute position
    int32_t getAbsolutePos();

    // Read encoder angular speed in RPM
    float readAngularSpeed();

    void setPwmMin(byte min);

    // Method to center
    void centerAxis(bool resetPwmSpeed = false);

    // Method to stop the motor
    void stopMotor();

    // Calibration method for the axis
    bool calibrate();

    // Calibration method for the axis
    bool EEPromCalibration();

    // SoftLock range calculation from travel range percentage of full (iMax)
    void setSoftLockRangeFromRangePcnt(byte travelRangePcnt);
    byte getRangePcntFromSoftLockRange();

    void setMAxVelocityFromPcnt(byte maxVel);
};

#endif
