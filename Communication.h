#ifndef COMMUNICATION_H
#define COMMUNICATION_H

#include <stdint.h>
#include <Arduino.h>
#include "BeepManager.h"
#include "Axis.h"
#include "src/Joystick.h"
#include "defines.h"

#define RX_CMD "<RX>"
#define TX_CMD "<TX>"
#define RESET_CMD "<RS>"

void setupDefaults();
void writeSettingsToEeprom();
void setRangeJoystick();

class Communication {
private:
    typedef struct  __attribute__ ((packed)) {
        char startPkt = 0x2; // ASCII code START
        char startPkt2 = '<';
        uint8_t rollTotGain;
        uint8_t rollPwmMin;
        uint8_t pitchTotGain;
        uint8_t pitchPwmMin;
        uint8_t pitchTravelRange;
        uint8_t defaultSpringGain;
        uint8_t maxVelocityPcnt;
        char endPkt = 0x3; // ASCII code END
        char endPkt2 = '>';
    } SettingsDataStruct;

    SettingsDataStruct settingsData;
    BeepManager *beepManager;
    Gains *gains;
    byte *adjPwmMin;
    Axis *axis;
    byte& maxVelocityPcnt;

    void txData();
    void rxData();
    void resetDevice();
    void beep(byte times);
    void failBeep(byte times);

public:
	//constructor
	Communication(BeepManager *ptr, Gains *gainsPtr, byte *adjPwmMinPtr, Axis *axis, byte& maxVelocityPcntVal);
    void serialEvent();
};

#endif