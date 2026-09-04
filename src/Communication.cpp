#include "Communication.h"

Communication::Communication(BeepManager *ptr, Gains *gainsPtr,  byte *adjPwmMinPtr, Axis *axisPtr, byte& maxVelocityPcntVal): 
  beepManager(ptr), gains(gainsPtr), adjPwmMin(adjPwmMinPtr), axis(axisPtr), maxVelocityPcnt(maxVelocityPcntVal) {}

void Communication::serialEvent() {
  // Every command is "<..>". Drop anything that isn't the start of one so a stray/noise
  // byte can't sit in the buffer forcing readBytes() to block for the Stream timeout
  // (which would freeze the FFB loop). Only engage once a whole 4-byte command is buffered.
  while (Serial.available() > 0 && Serial.peek() != '<')
    Serial.read();

  if (Serial.available() < 4) return;

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
  } else if (strcmp(cmd, BUILD_CMD) == 0) { // Which firmware is this?
    txBuildInfo();
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
  settingsData.defaultSpringGain = gains[MEM_ROLL].defaultSpringGain; // roll and pitch share same value, we use ROLL
  settingsData.maxVelocityPcnt = maxVelocityPcnt; // roll and pitch share same value, we use ROLL

  Serial.flush();
  Serial.write((const byte*)&settingsData, sizeof(SettingsDataStruct));
  beep(1);
}

// Build identity, on request only.
//
// Its own command rather than extra fields on SettingsDataStruct: the tool reads that
// struct as a fixed-size block, so growing it would break every existing build of the
// tool.  A tool that predates this command simply never sends <BI>, and a tool that
// knows about it gets an answer - so both directions stay compatible.
//
// ASCII, newline-terminated, so the host reads to '\n' rather than a fixed length -
// the variant name is free-form and the compiler's date/time format is not ours to fix.
// No beep: this is a passive query, not a settings change.
void Communication::txBuildInfo() {
  Serial.print(F(BUILD_CMD FW_VARIANT " " __DATE__ " " __TIME__ "\n"));
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