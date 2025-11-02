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

/*
  This is the MAIN FILE and has to be open in an Directory called "Arduino_FFB_Yoke". Otherwise the building of code will not work.
  This version is usable for Hardware v1.3 with Arduino Micro (original) and Hardware v2.0 with Arduino pro Micro
  See the defines.h for pin and cpu definitions definitions
*/
/*************************
  Includes
**************************/
#include "src/Joystick.h"  // Joystick and FFB Library (many changes made) by https://github.com/jmriego/Fino
#include <AS5600.h>          // Magnetic encoder library
#include <TCA9548.h>         // I2C channel multiplexer, the roll and pitch magnetic encoders goes through here
#include <EEPROM.h>        // https://docs.arduino.cc/learn/built-in-libraries/eeprom

#include "defines.h"
#include "Multiplexer.h"
#include "Axis.h"  // Include the header file
#include "BeepManager.h"
#include "Communication.h"

/*************************
  Variables
// **************************/
unsigned long nextUpdateMillis = 0;    // count millis for next mux update
unsigned long currentMillis;            // millis for the current loop

byte maxVelocityPcnt = 60;             // Percentage of axis max velocity allowed

int16_t forces[MEM_AXES] = { 0, 0 };        // stored forces
Gains gains[FFB_AXIS_COUNT];                // stored gain parameters
EffectParams effects[MEM_AXES];             // stored effect parameters

int16_t adjForceMax[MEM_AXES] = { 0, 0 };  // stored max adjusted force
byte adjPwmMin[MEM_AXES] = { default_ROLL_PWM_MIN, default_PITCH_PWM_MIN };       // stored start pwm (motor power) on force != 0

// TCA9548 I2C multiplexer (actually a switch)
TCA9548 i2c_mux(0x70);

// Magnetic rotary encoders
AS5600 rollEncoder;  
AS5600 pitchEncoder;
byte ROLL_CHANNEL = 0;
byte PITCH_CHANNEL = 1;

BeepManager beepManager(BUZZER_PIN);  // Instanciate the BeepManager

// variables for calculation
unsigned long lastEffectsUpdate = 0;  // count millis for next effect calculation

typedef struct {
int16_t lastPos;                        // X value from last loop
int16_t lastVel;                     // Velocity X value from last loop
int16_t lastAccel;                   // Acceleration X value from last loop
} PhysicsData;

PhysicsData physicsData[MEM_AXES];
int32_t encoderPos[MEM_AXES];
// float angularSpeed[MEM_AXES];

Joystick_ Joystick(            // define Joystick parameters
  JOYSTICK_DEFAULT_REPORT_ID,  
  JOYSTICK_TYPE_JOYSTICK,      // type Joystick
  12, 1,                       // Button Count, Hat Switch Count
  true, true, false);           // X, Y, Z
  // false, false, false,         // Rx, Ry, Rz
  // false, false);               // rudder, throttle

Multiplexer mux(&Joystick);   // class for mutiplexers

Axis axis[MEM_AXES] ={
  Axis(ROLL_L_PWM, ROLL_R_PWM, true, &rollEncoder, ROLL_CHANNEL, &i2c_mux, &mux, &beepManager, adjPwmMin[MEM_ROLL]),
  Axis(PITCH_U_PWM, PITCH_D_PWM, false, &pitchEncoder, PITCH_CHANNEL, &i2c_mux, &mux, &beepManager, adjPwmMin[MEM_PITCH])
};

Communication comm(&beepManager, gains, adjPwmMin, axis, maxVelocityPcnt);

/********************************
     initial setup
*******************************/
void setup() {
  arduinoSetup();   // setup for Arduino itself (pins)
  Serial.begin(SERIAL_BAUD);  // init serial
  Wire.begin(); // I2C Wire communication

  i2c_mux.begin();
  i2c_mux.selectChannel(ROLL_CHANNEL);
  rollEncoder.begin(); 
  rollEncoder.setDirection(AS5600_CLOCK_WISE);  

  i2c_mux.selectChannel(PITCH_CHANNEL);
  pitchEncoder.begin(); 
  pitchEncoder.setDirection(AS5600_CLOCK_WISE);  

  // if serial debug, no motors enabled
#ifndef SERIAL_DEBUG
  if (digitalReadFast(CALIB_BUTTON_PIN) ||  !isEepromDataValid()) {
    fullCalibration();
  } else {
    readEEPromCalib();
  }

  // readEncoderPos();
  setupJoystick();  // Joystick setup
  Joystick.begin(false);
  axis[MEM_ROLL].setMAxVelocityFromPcnt(maxVelocityPcnt);
  axis[MEM_PITCH].setMAxVelocityFromPcnt(maxVelocityPcnt);
#endif
}  //setup

/***************************
      main loop
****************************/
void loop() {
  // if serial debug mode than only display pins, mux and counters
#ifdef SERIAL_DEBUG

  readEncoderPos();

  Serial.print(F("IR_L:"));
  Serial.print(!digitalReadFast(IR_ROLL_LEFT));
  Serial.print(F(", IR_R:"));
  Serial.print(!digitalReadFast(IR_ROLL_RIGHT));
  Serial.print(F(", IR_DN:"));
  Serial.print(!digitalRead(IR_PITCH_DOWN));
  Serial.print(F(", IR_UP:"));
  Serial.print(!digitalRead(IR_PITCH_UP));

  Serial.print(F(", X-axis:"));
  Serial.print(encoderPos[MEM_ROLL]); //Serial.print(F(":")); Serial.print(axis[MEM_ROLL].config.iMin);
  Serial.print(F(", Y-axis:"));
  Serial.print(encoderPos[MEM_PITCH]); //Serial.print(F(":")); Serial.print(axis[MEM_PITCH].config.iMin + SOFT_LOCK_Y);

  mux.updateJoystickButtons();              // get Joystick buttons

  Serial.println();
  delay(250);
#else
  currentMillis = millis();

  if (currentMillis >= nextUpdateMillis) {
    comm.serialEvent();

    // Calibrate?
    if (digitalReadFast(CALIB_BUTTON_PIN)) {
      fullCalibration();
    }
    nextUpdateMillis = currentMillis + 100;
  }
  mux.updateJoystickButtons();              // get Joystick buttons
  readEncoderPos();
  Joystick.sendState(); // send joystick values to system
  updateEffects(true);                      // update/calculate new effect paraeters

  for (byte i = MEM_ROLL; i <= MEM_PITCH; i++) {
    axis[i].applyForce(forces[i], encoderPos[i], physicsData[i].lastVel);
  }
  // Serial.println(millis() - currentMillis);
#endif
} // loop

void readEncoderPos() {
  // Read encoder Positions
    for (byte i = MEM_ROLL; i <= MEM_PITCH; i++) {
      encoderPos[i] = axis[i].getCumulativePos();
      // angularSpeed[i] = axis[i]->readAngularSpeed();
    }
}

/***************************
  Calibration
****************************/
void fullCalibration() {
  enableMotors();             // enable motors
  for (byte i = MEM_ROLL; i <= MEM_PITCH; i++) {
    if (!axis[i].calibrate()) {
      failedCalibration();
      break;
    }
  }

  setRangeJoystick();  // Set Joystick range
}

void readEEPromCalib() {
  enableMotors();
  for (byte i = MEM_ROLL; i <= MEM_PITCH; i++) {
    if (!axis[i].EEPromCalibration()) {
      failedCalibration();
      return;
    }
  }
  setRangeJoystick(); // Set Joystick range
  axis[MEM_PITCH].centerAxis();
}

void failedCalibration() {
  beepManager.beep(250, 300); // Calibration error beep
  disableMotors();
}

