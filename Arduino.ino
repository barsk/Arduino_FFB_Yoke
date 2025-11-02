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

/***************
  Pin setup
****************/
void arduinoSetup() {

  // Pitch motor driver Pins
  pinMode(PITCH_EN, OUTPUT);
  pinMode(PITCH_U_PWM, OUTPUT);
  pinMode(PITCH_D_PWM, OUTPUT);

  // Roll motor driver  Pins
  pinMode(ROLL_EN, OUTPUT);
  pinMode(ROLL_R_PWM, OUTPUT);
  pinMode(ROLL_L_PWM, OUTPUT);

  // Buzzer pin
  pinMode(BUZZER_PIN, OUTPUT);

  // Multiplexer Yoke Buttons
  pinMode(MUX_YOKE_OUT, INPUT);
  pinMode(MUX_YOKE_PL, OUTPUT);
  pinMode(MUX_YOKE_CLK, OUTPUT);

  // IR Sensor pins
  pinMode(IR_ROLL_LEFT, INPUT);
  pinMode(IR_ROLL_RIGHT, INPUT);
  pinMode(IR_PITCH_UP, INPUT);
  pinMode(IR_PITCH_DOWN, INPUT);

  // Calibration Button pin
  pinMode(CALIB_BUTTON_PIN, INPUT);


  ////////////////////////////
  // define pin default states
  digitalWrite(BUZZER_PIN, LOW);

  // Pitch
  digitalWrite(PITCH_EN, LOW);
  digitalWrite(PITCH_U_PWM, LOW);
  digitalWrite(PITCH_D_PWM, LOW);
  //Roll
  digitalWrite(ROLL_EN, LOW);
  digitalWrite(ROLL_R_PWM, LOW);
  digitalWrite(ROLL_L_PWM, LOW);

  // Multiplexer

  digitalWrite(MUX_YOKE_PL, HIGH);
  digitalWrite(MUX_YOKE_CLK, LOW);

  // not for all Arduinos!
  // This sets the PWM Speed to maximun for noise reduction

  // Timer1: pins 9 & 10
  TCCR1B = _BV(CS10);  // change the PWM frequencey to 31.25kHz - pins 9 & 10

  // Timer4: pin 13 & 6
  TCCR4B = _BV(CS40);  // change the PWM frequencey to 31.25kHz - pin 13 & 6

  //Timer3: pin 5

  TCCR3B = _BV(CS30);  // Change the PWM frequency to 31.25kHz - pin 5
}  //ArduinoSetup

/**************************
  Enables the motordrivers
****************************/
void enableMotors() {
  digitalWrite(PITCH_EN, HIGH);
  digitalWrite(ROLL_EN, HIGH);
}  //EnableMotors

/***************************
  Disables the motordrivers
****************************/
void disableMotors() {
  digitalWrite(PITCH_EN, LOW);
  digitalWrite(ROLL_EN, LOW);

  analogWrite(ROLL_L_PWM, 0);  // stop left
  analogWrite(ROLL_R_PWM, 0);  // stop right

  analogWrite(PITCH_U_PWM, 0);  // stop up
  analogWrite(PITCH_D_PWM, 0);  // stop down
}  //DisableMotors


