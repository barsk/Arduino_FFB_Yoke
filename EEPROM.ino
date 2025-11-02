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

#include "defines.h"

/******************************************
  Check if EEPROM data is valid
*******************************************/
bool isEepromDataValid() {
  int32_t max;
  EEPROM.get(EEPROM_ENCODER_X_MAX_INDEX, max); // if -1, then calibration data has not been written

  // magic number + version
  return (EEPROM.read(EEPROM_DATA_AVAILABLE_INDEX) == EEPROM_DATA_MAGIC_NUMBER &&
   EEPROM.read(EEPROM_FIRMWARE_VERSION_INDEX) == FIRMWARE_VERSION  && max != -1); 
} 

/******************************************
  write all settings to eeprom
*******************************************/
void writeSettingsToEeprom() {
  // int eeAddress;
  EEPROM.update(EEPROM_MAX_VELOCITY_PCNT_INDEX, maxVelocityPcnt);
  EEPROM.update(EEPROM_ADJ_PWM_MIN_X_INDEX, adjPwmMin[MEM_ROLL]);
  EEPROM.update(EEPROM_ADJ_PWM_MIN_Y_INDEX, adjPwmMin[MEM_PITCH]);
  // WriteEepromByteArray((eeAddress = EEPROM_ADJ_PWM_MIN_X_INDEX), adjPwmMin, MEM_AXES);
  
  int eeAddress = EEPROM_TOTAL_GAIN_X_INDEX; // start address
  for (byte i = 0; i < MEM_AXES; i++) {
    EEPROM.update(eeAddress++, gains[i].totalGain);
    EEPROM.update(eeAddress++, gains[i].defaultSpringGain);
    // EEPROM.update(eeAddress++, (uint8_t)(100.0f - 100.0f * axis[i].config.softLock_range / axis[i].config.iMax));

    // Actually only Y-axis (pitch) is used, but we store both axis
    EEPROM.update(eeAddress++, axis[i].getRangePcntFromSoftLockRange());
  }
    // set flag to indicate that data is valid and available
  EEPROM.update(EEPROM_DATA_AVAILABLE_INDEX, EEPROM_DATA_MAGIC_NUMBER); // magic number
  EEPROM.update(EEPROM_FIRMWARE_VERSION_INDEX, FIRMWARE_VERSION);
  
  // WriteEepromByteArray((eeAddress = EEPROM_TOTAL_GAIN_X_INDEX, gains, MEM_AXES);
  // eeAddress = EEPROM_DATA_INDEX:
  // for (byte i = 0; i < MEM_AXES; i++) {
  //   EEPROM.put(eeAddress, effects[i]);
  //   eeAddress += sizeof(EffectParams);
  // }
  // // write gains
  // for (byte i = 0; i < MEM_AXES; i++) {
  //   EEPROM.put(eeAddress, gains[i]);
  //   eeAddress += sizeof(Gains);
  // }
} 

/******************************************
  read all settings from eeprom
*******************************************/
void readSettingsFromEeprom() {
  // int eeAddress;
  maxVelocityPcnt = EEPROM.read(EEPROM_MAX_VELOCITY_PCNT_INDEX);
  adjPwmMin[MEM_ROLL] = EEPROM.read(EEPROM_ADJ_PWM_MIN_X_INDEX);
  adjPwmMin[MEM_PITCH] = EEPROM.read(EEPROM_ADJ_PWM_MIN_Y_INDEX);
  // ReadEepromByteArray((eeAddress = EEPROM_ADJ_PWM_MIN_X_INDEX), adjPwmMin, MEM_AXES);
  
  int eeAddress = EEPROM_TOTAL_GAIN_X_INDEX; // start address
  for (byte i = 0; i < MEM_AXES; i++) {
    gains[i].totalGain = EEPROM.read(eeAddress++);
    gains[i].defaultSpringGain = EEPROM.read(eeAddress++);
    byte travelRange =  EEPROM.read(eeAddress++);

    // Actually only Y-axis (pitch) is used, but we store both axis
    axis[i].setSoftLockRangeFromRangePcnt(travelRange);
    // axis[i].config.softLock_range = (int32_t)((100.0f - travelRange)/100.0f * axis[i].config.iMax);
  }


  // // start address
  // int eeAddress = EEPROM_DATA_INDEX;
  // // read data  
  // ReadEepromInt16Array(eeAddress, adjForceMax, MEM_AXES);
  // // eeAddress += 4;
  // ReadEepromByteArray(eeAddress, adjPwmMin, MEM_AXES);
  // ReadEepromByteArray(eeAddress, adjPwmMax, MEM_AXES);
  // // eeAddress += 2;
  // //read effects
  // for (byte i = 0; i < MEM_AXES; i++) {
  //   EEPROM.get(eeAddress, effects[i]);
  //   eeAddress += sizeof(EffectParams);
  // }
  // // read gains
  // for (byte i = 0; i < MEM_AXES; i++) {
  //   EEPROM.get(eeAddress, gains[i]);
  //   eeAddress += sizeof(Gains);
  // }
}

/******************************************
  writes an byte array to the eeprom
*******************************************/
void WriteEepromByteArray(int &myAddress, byte myValues[], byte arraySize) {
  for (byte i = 0; i < arraySize; i++) {
    EEPROM.put(myAddress, myValues[i]);
    myAddress += sizeof(byte);
  }
} //WriteEepromByteArray

/******************************************
  reads an byte array  from eeprom
*******************************************/
void ReadEepromByteArray(int &myAddress, byte myArray[], byte arraySize) {
  for (byte i = 0; i < arraySize; i++) {
    EEPROM.get(myAddress, myArray[i]);
    myAddress += sizeof(byte);
  }
} //ReadEepromByteArray

/******************************************
  writes an int16_t array to the eeprom
*******************************************/
void WriteEepromInt16Array(int &myAddress, int16_t myValues[], byte arraySize) {
  for (byte i = 0; i < arraySize; i++) {
    EEPROM.put(myAddress, myValues[i]);
    myAddress += sizeof(int16_t);
  }
} //WriteEepromInt16Array

/******************************************
  reads an int16_t array  from eeprom
*******************************************/
void ReadEepromInt16Array(int &myAddress, int16_t myArray[], byte arraySize) {
  for (byte i = 0; i < arraySize; i++) {
    EEPROM.get(myAddress, myArray[i]);
    myAddress += sizeof(int16_t);
  }
} //ReadEepromInt16Array

/******************************************
  writes an int16_t value to the eeprom
*******************************************/
void WriteEepromInt16(int &myAddress, int16_t myValue) {
    EEPROM.put(myAddress, myValue);
    myAddress += sizeof(int16_t);
} //WriteEepromInt16

/******************************************
  reads an int16_t value  from eeprom
*******************************************/
void ReadEepromInt16(int &myAddress, int16_t myValue) {
    EEPROM.get(myAddress, myValue);
    myAddress += sizeof(int16_t);
} //WriteEepromInt16
