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
#include "Multiplexer.h"


// Constructor to initialize the Joystick pointer and end switch state pointers
Multiplexer::Multiplexer(Joystick_* joystickPtr) {
    this->joystick = joystickPtr;

    mux_yoke.begin();//MUX_YOKE_OUT, MUX_YOKE_PL, MUX_YOKE_CLK);
    // mux_int.begin(MUX_INT_OUT, MUX_INT_PL, MUX_INT_CLK);

}


uint16_t Multiplexer::getYokeButtonPinStates(){
  return mux_yoke.get2MuxPinStates();
}

// Method to update the joystick buttons based on multiplexer input
void Multiplexer::updateJoystickButtons() {
mux_yoke.update();

#ifdef SERIAL_DEBUG
      Serial.print("  But: ");
      for (uint8_t i = 4, n = mux_yoke.getLength(); i < n; i++) {
        byte data = !mux_yoke.read(i);
        
        Serial.print(i);
        Serial.print("|");
        Serial.print(data);
        Serial.print(" ");
      }
      Serial.print(" Hat_Up:");
      Serial.print(!mux_yoke.read(2));
      Serial.print(", Hat_Dn:");
      Serial.print(!mux_yoke.read(0));
      Serial.print(", Hat_L:");
      Serial.print(!mux_yoke.read(1));
      Serial.print(", Hat_R:");
      Serial.print(!mux_yoke.read(3));
#else
 
  uint16_t reading = mux_yoke.get2MuxPinStates();

  // If any of the switches changed, due to noise or pressing:
  if (reading != lastYokeButtonPinState)
  {
    // reset the debouncing timer
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > DEBOUNCE_MS)
  {
    // whatever the reading is at, it's been there for longer than the debounce
    // delay, so take it as the actual current state:

    // if the button state has changed:
    if (reading != yokeButtonPinState)
    {
      yokeButtonPinState = reading; // remember state
      byte hatSwitchState = 0;
      hatSwitchState |= (!mux_yoke.read(0) << 0);
      hatSwitchState |= (!mux_yoke.read(1) << 1);
      hatSwitchState |= (!mux_yoke.read(2) << 2);
      hatSwitchState |= (!mux_yoke.read(3) << 3);

      switch (hatSwitchState)
      {
      case 0B00000000:
        joystick->setHatSwitch(0, -1); // no direction
        break;
      case 0B00000001:
        joystick->setHatSwitch(0, 180); // up
        break;
      case 0B00000011:
        joystick->setHatSwitch(0, 225); // up right
        break;
      case 0B00000010:
        joystick->setHatSwitch(0, 270); // right
        break;
      case 0B00000110:
        joystick->setHatSwitch(0, 315); // down right
        break;
      case 0B00000100:
        joystick->setHatSwitch(0, 0); // down
        break;
      case 0B00001100:
        joystick->setHatSwitch(0, 45); // down left
        break;
      case 0B00001000:
        joystick->setHatSwitch(0, 90); // left
        break;
      case 0B00001001:
        joystick->setHatSwitch(0, 135); // up left
        break;
      default:
        break; // no change
      }

      for (byte channel = 4; channel < 16; channel++)
      {
        joystick->setButton(channel - 4, !mux_yoke.read(channel));
      }
    }
  }
  // save the reading. Next time through the loop, it'll be the lastButtonState:
  lastYokeButtonPinState = reading;

#endif
}
