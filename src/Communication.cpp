#include "Communication.h"

Communication::Communication(BeepManager *ptr, Gains *gainsPtr,  byte *adjPwmMinPtr, Axis *axisPtr, byte& maxVelocityPcntVal): 
  beepManager(ptr), gains(gainsPtr), adjPwmMin(adjPwmMinPtr), axis(axisPtr), maxVelocityPcnt(maxVelocityPcntVal) {}

void Communication::serialEvent() {
  if (Serial.available() > 0) {

    char  cmd[5];
    size_t size = Serial.readBytes(cmd, 4);
    if (size != 4) return;
    cmd[4] = '\0'; // add null char, makes it a string

    if (strcmp(cmd, RX_CMD) == 0) { // Receive data from settings app
      rxData();
    } else if (strcmp(cmd, TX_CMD) == 0) { // Transmit data to settings app
      txData();
    } else if (strcmp(cmd, RESET_CMD) == 0) { // Transmit data to settings app
      resetDevice();
    } 
  }
}

// Receive Settings from app
void Communication::rxData() {
  size_t size = Serial.readBytes((uint8_t*)&settingsData, sizeof(SettingsDataStruct));
  if (size != sizeof(SettingsDataStruct) || settingsData.startPkt != 0x2 || settingsData.endPkt != 0x3) {
    failBeep(1);
    return;
  } 
  gains[MEM_ROLL].totalGain = settingsData.rollTotGain;
  adjPwmMin[MEM_ROLL] = settingsData.rollPwmMin;
  gains[MEM_PITCH].totalGain = settingsData.pitchTotGain;
  adjPwmMin[MEM_PITCH] = settingsData.pitchPwmMin;
  
  axis[MEM_PITCH].setSoftLockRangeFromRangePcnt(settingsData.pitchTravelRange);
  setRangeJoystick();

  maxVelocityPcnt = settingsData.maxVelocityPcnt;
  for (byte i = MEM_ROLL; i <= MEM_PITCH; i++) {
    gains[i].defaultSpringGain = settingsData.defaultSpringGain;
    axis[i].setMAxVelocityFromPcnt(maxVelocityPcnt);
  }

  writeSettingsToEeprom();
  beep(1);
}

// Transmit device values to Settings app
void Communication::txData() {
  settingsData.rollTotGain = gains[MEM_ROLL].totalGain;
  settingsData.rollPwmMin = adjPwmMin[MEM_ROLL];
  settingsData.pitchTotGain = gains[MEM_PITCH].totalGain;
  settingsData.pitchPwmMin = adjPwmMin[MEM_PITCH];
  settingsData.pitchTravelRange = axis[MEM_PITCH].getRangePcntFromSoftLockRange();
  settingsData.defaultSpringGain = gains[MEM_ROLL].defaultSpringGain; // roll and pitch use same value, we use ROLL
  settingsData.maxVelocityPcnt = maxVelocityPcnt; // roll and pitch use same value, we use ROLL

  Serial.flush();
  Serial.write((const byte*)&settingsData, sizeof(SettingsDataStruct));
  beep(1);
}

void Communication::resetDevice() {
  setupDefaults();
  writeSettingsToEeprom();
  beep(2);
}

void Communication::beep(byte times) {
  for (int i =0; i < times; i++) {
    beepManager->beep(1000, 75); // happy beep
    delay(100);
  }
}

  void Communication::failBeep(byte times) {
  for (int i =0; i < times; i++) {
    beepManager->beep(250, 200); // sad beep
    delay(100);
  }
}
