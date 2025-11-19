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

#define FIRMWARE_VERSION 1  // one byte, max 254

/*****************************
 Uncomment for Serial Debug, motors are disabled, debug data will be written to Serial Monitor
*****************************/
// #define SERIAL_DEBUG

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
#define EEPROM_DATA_MAGIC_NUMBER   0b10101010 // Magic number to indicate if valid data is written

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
#define EEPROM_TRAVEL_RANGE_X_INDEX 19  // Not used ATM.

#define EEPROM_TOTAL_GAIN_Y_INDEX 20
#define EEPROM_TRAVEL_RANGE_Y_INDEX 21 
#define EEPROM_DEFAULT_SPRING_FORCE_Y_INDEX 22

#define EEPROM_DATA_INDEX 25              // eeprom start address for data (not used)

// Default vaules for gains and effect if nothing saved into eeprom
#define default_gain 100
#define default_friction_gain 100
#define default_spring_gain 40

#define default_frictionMaxPositionChange_ROLL 40
#define default_inertiaMaxAcceleration_ROLL 30
#define default_damperMaxVelocity_ROLL 15

#define default_frictionMaxPositionChange_PITCH 60
#define default_inertiaMaxAcceleration_PITCH 40
#define default_damperMaxVelocity_PITCH 25

// Speed limit settings
#define ENABLE_SPEED_LIMITER // To remove function
#define MAX_VELOCITY_X 15
#define MAX_VELOCITY_Y 25
#define VELOCITY_HYSTERESIS 5 // max velocity - this value to reenable 
#define DEFAULT_VELOCITY_PCNT 60 // default percentage of MAX velocity

#define default_PITCH_TOT_GAIN 50
#define default_PITCH_PWM_MAX 255
#define default_PITCH_PWM_MIN 43

#define default_ROLL_TOT_GAIN 80
#define default_ROLL_PWM_MAX 255
#define default_ROLL_PWM_MIN 37

// Limit range from the absolute max found from calib to assure full range is given
#define EXTREMITY_LIMITER_X 0 
#define EXTREMITY_LIMITER_Y 20 

// Force to use to *gently* hold the yoke to the endstop if a force is pushing it there
// If using 0 as force, we can get bouncing towards the endstop (if hands off)
// This force should be just enough to get the motor working, to high and there will be heat!!!
// Range is 0-10000
#define ENDSTOP_HOLD_FORCE 300

// SOFT LOCK Settings
// Roll AXIS (NOT USED!)

#define SOFT_LOCK_X 50 // Encoder steps away from physical endstop
#define SOFT_LOCK_FORCE_X 10000
#define SOFT_LOCK_BUFFER_X 40

// Pitch AXIS (USED!)
#define SOFT_LOCK_Y 500 // Encoder steps away from physical endstop
#define SOFT_LOCK_FORCE_Y 10000
#define SOFT_LOCK_BUFFER_Y 80

/******************************************
   Calibration Constants
*******************************************/
#define CALIBRATION_MOTOR_DELAY_X 600
#define CALIBRATION_MOTOR_DELAY_Y 250
#define CALIBRATION_MAX_PWM 58                        
#define CALIBRATION_MAX_INCREMENT_Y 35                // Maximum positional delta change per loop (WHILE_DELAY)
#define CALIBRATION_MAX_INCREMENT_X 45                // Maximum positional delta change per loop (WHILE_DELAY)
#define CALIBRATION_AXIS_MOVEMENT_TIMEOUT 2000           // Timeout of 4 seconds for no movement
#define CALIBRATION_TIMEOUT 20000                        // Timeout of 20 seconds for calibration
#define CALIBRATION_SPEED_INCREMENT 2                   // the speed is increased until movement, this is added to speed then movement indicates 
#define CALIBRATION_WHILE_DELAY 15                        // waitdelay inside while of movement to give Arduino time. Change will change speed!
#define CALIBRATION_WHILE_DELAY_MOTOR_STOPS 30           // waitdelay when motor stops to give him time to stops
#define CALIBRATION_DELAY_MOVE_OUT_OF_ENDSTOP 100        // If asix is on endstop on start od´f calibration it will move out of and wait shot before continue

// if defined will catch digitalWritefast() calls that are not fast
// #define THROW_ERROR_IF_NOT_FAST 

#endif
