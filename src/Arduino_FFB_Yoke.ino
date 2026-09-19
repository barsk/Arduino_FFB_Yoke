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

// Magnetic rotary encoders. FastAS5600 skips most register-pointer writes on angle reads
// (see FastAS5600.h); Axis still holds them as AS5600*.
#include "FastAS5600.h"
FastAS5600 rollEncoder;  
FastAS5600 pitchEncoder;
byte ROLL_CHANNEL = 0;
byte PITCH_CHANNEL = 1;

BeepManager beepManager(BUZZER_PIN);  // Instanciate the BeepManager

// variables for calculation
unsigned long lastEffectsUpdate = 0;  // count millis for next effect calculation

#if defined(FFB_SERIAL_TRACE) || defined(FFB_STACK_TRACE)
// ---- stack high-water mark -------------------------------------------------------
// Flash has been budgeted all along; RAM never was. 2125 B of 2560 is static, so only
// ~435 B is shared between the main-loop force chain (loop -> updateEffects -> getForce
// -> forceCalculator -> getEffectForce -> ConditionForceCalculator/ApplyEnvelope, float
// temps throughout) and a USB ISR that nests ~6 frames on top of it (USB_COM_vect ->
// USB_Setup -> PluggableUSB().setup -> DynamicHID_::setup -> SetReport -> CreateNewEffect).
// TelemFFB's startup is exactly when that ISR path is busiest - and it is when the two
// reverted optimisations (Rev 8.13, Rev 8.15) both reset the MCU.
//
// paintStack() fills the gap above .bss with a known byte; stackFreeMin() counts how many
// are still untouched, i.e. the closest the stack has ever come to .bss. A value near 0
// means we are overflowing into .bss and the resets are explained.
extern uint8_t __bss_end;
#define STACK_PAINT_BYTE 0xC5

static void paintStack()
{
  uint8_t *p   = &__bss_end;
  uint8_t *top = (uint8_t *)SP - 64;   // margin: never touch our own frame
  while (p < top) *p++ = STACK_PAINT_BYTE;
}

uint16_t stackFreeMin()
{
  const uint8_t *p = &__bss_end;
  uint16_t n = 0;
  while (n < 2048 && *p == STACK_PAINT_BYTE) { n++; p++; }
  return n;
}

// Previous session's watermark, captured at boot before the slot is re-armed. Reported in
// every P line, so after a crash the FIRST lines of the next session say how close the
// stack came in the session that died - which a live reading can never tell us.
uint16_t stackPrevSession = 0xFFFF;
static uint16_t stackMinSeen = 0xFFFF;

// Called on a timer, not every pass: stackFreeMin() scans up to 434 B (~80 us).
static void recordStackWatermark()
{
  uint16_t now = stackFreeMin();
  if (now < stackMinSeen)
  {
    stackMinSeen = now;
    EEPROM.put(EEPROM_STACK_WATERMARK_INDEX, now);   // put() skips unchanged bytes
  }
}
// ----------------------------------------------------------------------------------

// Main-loop passes since the last 'F'/'P' trace line. x4 = loop Hz. This is the ceiling on
// how fast host effect reports can be accepted: the OUT endpoint is double-banked, so
// each drain point takes at most 2 reports per pass.
volatile uint16_t loopCount = 0;
uint8_t resetFlags = 0;   // MCUSR as latched at boot; see setup()

// An interrupt whose vector is not populated lands on avr-libc's __bad_interrupt, which
// JUMPS TO ADDRESS 0. That restarts the sketch, resets uptime, sets no MCUSR flag and
// leaves the stack untouched - i.e. it produces exactly the signature we are looking at,
// and it is the commonest cause of it on AVR. Defining BADISR_vect replaces that jump
// with this handler, so instead of restarting, the yoke survives and counts the event.
// If the crash disappears and this counter climbs, the fault is named.
volatile uint16_t badIsrCount = 0;
ISR(BADISR_vect) { badIsrCount++; }
#endif

typedef struct {
int16_t lastPos;                        // position from last loop
int16_t lastVel;                        // velocity from last loop (counts/ms << VEL_SHIFT, clamped)
int16_t lastAccel;                      // acceleration from last loop (clamped)
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
  Axis(ROLL_L_PWM, ROLL_R_PWM, ROLL_EN, true, &rollEncoder, ROLL_CHANNEL, &i2c_mux, &mux, &beepManager, adjPwmMin[MEM_ROLL]),
  Axis(PITCH_U_PWM, PITCH_D_PWM, PITCH_EN, false, &pitchEncoder, PITCH_CHANNEL, &i2c_mux, &mux, &beepManager, adjPwmMin[MEM_PITCH])
};

Communication comm(&beepManager, gains, adjPwmMin, axis, maxVelocityPcnt);

/********************************
     initial setup
*******************************/
// Explicit prototypes. Arduino's .ino auto-prototype generator is fragile: adding the
// stack-instrumentation block above was enough to make it stop emitting these entirely -
// the profile build still compiled, the release build did not ("not declared in this
// scope" for functions in this very file). Declaring them by hand removes the dependency.
void arduinoSetup();          // Arduino.ino
void enableMotors();          // Arduino.ino
void disableMotors();         // Arduino.ino
void setupJoystick();         // joystick.ino
void setupDefaults();         // joystick.ino
void setRangeJoystick();      // joystick.ino
void updateEffects(bool recalculate);   // joystick.ino
bool isEepromDataValid();     // EEPROM.ino
void writeSettingsToEeprom(); // EEPROM.ino
void readSettingsFromEeprom();// EEPROM.ino
void fullCalibration();       // this file, defined below setup()
void readEEPromCalib();       // this file
void failedCalibration();     // this file
void readEncoderPos();        // this file

void setup() {
#if defined(FFB_SERIAL_TRACE) || defined(FFB_STACK_TRACE)
  // MCUSR latches WHY the last reset happened, and it is the one fact that separates the
  // two remaining candidates: a watchdog fire (the Arduino CDC core arms a 120 ms WDT for
  // the 1200-baud bootloader touch - the only watchdog in the system) from a jump to 0
  // caused by a smashed return address, which sets no flag at all. Read it before the
  // register can be disturbed, then clear it so the NEXT reset reports only itself.
  //   bit0 PORF power-on   bit1 EXTRF external   bit2 BORF brown-out   bit3 WDRF watchdog
  // A Caterina bootloader may consume it first; a constant 0 here means exactly that, and
  // that is worth knowing too.
  resetFlags = MCUSR;
  MCUSR = 0;
#ifdef FFB_STACK_TRACE
  // Stack instrumentation belongs to the S line now; the F line stopped carrying the two
  // figures when force/velocity peaks took their place. Left out of a serial-only build,
  // the paint, the 50 ms scan and the EEPROM slot all drop out with it.
  paintStack();     // must be first: everything below this uses stack
  // Capture what the PREVIOUS session got down to, then re-arm the slot.
  EEPROM.get(EEPROM_STACK_WATERMARK_INDEX, stackPrevSession);
  EEPROM.put(EEPROM_STACK_WATERMARK_INDEX, (uint16_t)0xFFFF);
#endif
#endif
  arduinoSetup();   // setup for Arduino itself (pins)
  Serial.begin(SERIAL_BAUD);  // init serial
  // Cap Stream::readBytes(). Default is 1000 ms: a single stray byte on the CDC port
  // (COM-port enumeration, the VPForce Configurator scanning ports, a leftover serial
  // monitor) makes serialEvent()'s 4-byte read block that long every 100 ms -> the whole
  // FFB loop freezes for ~1 s at a time. A real command's bytes arrive within ~1 ms.
  Serial.setTimeout(15);
  Wire.begin(); // I2C Wire communication
  Wire.setClock(400000); // 400 kHz fast-mode: AS5600 + TCA9548 both support it.
                         // Two muxed encoder reads dominate loop(); at 100 kHz they
                         // capped the force/torque update near ~1 kHz. Revert to
                         // 100000 if long encoder wiring / weak pull-ups misbehave.

  i2c_mux.begin();
  i2c_mux.selectChannel(ROLL_CHANNEL);
  rollEncoder.begin(); 
  rollEncoder.setDirection(AS5600_CLOCK_WISE);
  // CONF is volatile: the filter is re-applied on every boot.  reloadPointer() is what
  // FastAS5600.h asks for after any non-ANGLE register access.  See ENCODER_SLOW_FILTER.
  rollEncoder.setSlowFilter(ENCODER_SLOW_FILTER);
  rollEncoder.reloadPointer();  

  i2c_mux.selectChannel(PITCH_CHANNEL);
  pitchEncoder.begin(); 
  pitchEncoder.setDirection(AS5600_CLOCK_WISE);
  pitchEncoder.setSlowFilter(ENCODER_SLOW_FILTER);
  pitchEncoder.reloadPointer();  

  // if serial debug, no motors enabled
#ifndef SERIAL_DEBUG
  if (digitalReadFast(CALIB_BUTTON_PIN) ||  !isEepromDataValid()) {
    fullCalibration();
  } else {
    readEEPromCalib();
  }

  setupJoystick();  // Joystick and settings

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
  Serial.print(encoderPos[MEM_PITCH]); //Serial.print(F(":")); Serial.print(axis[MEM_PITCH].config.iMin + axis[MEM_PITCH].config.softLock_range);

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
  // Drain the USB OUT endpoint at several points in the pass, not just once inside
  // updateEffects(). The endpoint is double-banked (EP_DOUBLE_64), so ONE drain point
  // can accept at most 2 reports per loop pass - the host refills the banks on 1 ms USB
  // frame boundaries but nothing looks at them until the next pass. That made effect
  // delivery scale with the LOOP RATE (measured ~200 reports/s), so once TelemFFB wanted
  // more than that the surplus queued host-side and the backlog grew for as long as the
  // demand lasted: latency that builds with time in flight and with effect count, and a
  // stop-on-pause that lands seconds late. The I2C encoder/button reads below are the
  // slow part of the pass, so draining around them is where it pays.
  Joystick.getUSBPID();                     // drain OUT endpoint
  mux.updateJoystickButtons();              // get Joystick buttons
  Joystick.getUSBPID();                     // drain again - updateJoystickButtons is slow (I2C mux)
  readEncoderPos();
  Joystick.sendState(); // send joystick values to system
  updateEffects(true);                      // update/calculate new effect paraeters (drains again)

  for (byte i = MEM_ROLL; i <= MEM_PITCH; i++) {
    axis[i].applyForce(forces[i], encoderPos[i], physicsData[i].lastVel);
  }
#if defined(FFB_SERIAL_TRACE) || defined(FFB_STACK_TRACE)
  loopCount++;
#endif
#ifdef FFB_STACK_TRACE
  { // ~50 ms: fine enough to catch a dive shortly before a crash, cheap enough to ignore
    static unsigned long swLast = 0;
    if (currentMillis - swLast >= 50) { swLast = currentMillis; recordStackWatermark(); }
  }   // reported by the ~1 Hz S line below
#endif
#ifdef FFB_STACK_TRACE
  { // The S line: the same two stack figures the F line carries, but emitted from loop()
    // instead of from inside getEffectForce(). The F line only appears while a host is
    // actually driving effects - which is exactly not the case in the seconds after a
    // reset, when prevMin is the one number worth having. This one repeats regardless,
    // so the terminal can be attached at any time instead of being raced against boot.
    static unsigned long stLast = 0;
    if (currentMillis - stLast >= FFB_STACK_TRACE_MS)
    {
      stLast = currentMillis;
      Serial.write('S');
      Serial.print(stackFreeMin());              Serial.write(',');
      Serial.print(stackPrevSession);            Serial.write(',');
      Serial.print(currentMillis / 1000UL);      // uptime s: a reset shows as this dropping
      Serial.write(',');
      Serial.print(resetFlags);                  // why the LAST reset happened - see setup()
      Serial.write(',');
      Serial.print(badIsrCount);                 // unhandled interrupt vectors caught, not taken
      Serial.write(',');
      { extern volatile uint16_t shortReportCount;   // D5, PIDReportHandler.cpp
        Serial.println(shortReportCount); }      // reports shorter than the struct they feed
    }
  }
#endif
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
