/*
  https://www.roxxxtar.com/bmc
  Licensed under the MIT license. See LICENSE file in the project root for full license information.

  Library to read one or more 74HC165 multiplexers

  This library will read each pin of the mux and store it in RAM

  It was designed and tested for PJRC Teensy boards only.

  Use at your own risk.

  2025 Edited by K. Jörg, @Barsk
  https://github.com/barsk/Arduino_FFB_Yoke 
*/
#ifndef Rox74HC165_h
#define Rox74HC165_h

#include <Arduino.h>
#include <digitalWriteFast.h>
// #include "FastShiftIn.h"

#define ROXMUX_74HC165_DELAY 1


template <uint8_t _muxCount>
class Rox74HC165 {
  private:
    const uint8_t clkPin  = MUX_YOKE_CLK; // from defines.h
    const uint8_t loadPin = MUX_YOKE_PL;
    const uint8_t dataPin = MUX_YOKE_OUT;
    uint8_t states[_muxCount];
    // FastShiftIn* FSI;
    
  public:
    Rox74HC165(){}
    // ~Rox74HC165(){
    //   if (FSI != NULL){
    //     delete FSI;
    //   }
    // }
    void begin() { //uint8_t t_data, uint8_t t_load, uint8_t t_clk){
      // if(t_clk==t_load || t_clk==t_data || t_load==t_data){
      //   Serial.println(F("invalid 74HC165 pins used"));
      //   while(1);
      // }
      
      // clkPin = t_clk;
      // loadPin = t_load;
      // dataPin = t_data;
      // FSI = new FastShiftIn(dataPin,clkPin, LSBFIRST);

      pinMode(clkPin, OUTPUT);
      pinMode(loadPin, OUTPUT);
      pinMode(dataPin, INPUT);
      digitalWrite(clkPin, LOW);

      memset(states, 0, sizeof(states[0])*_muxCount); 
    }

    uint16_t getLength(){
      return _muxCount*8;
    }

    void update(){ // NOTE! Added direct #define constants since const didn't work(!?). See digitalWriteFast docs
      digitalWriteFast(MUX_YOKE_PL, LOW);
      delayMicroseconds(ROXMUX_74HC165_DELAY);
      digitalWriteFast(MUX_YOKE_PL, HIGH);
      for(uint8_t mux = 0; mux < _muxCount; mux++){
        for(int i = 7; i >= 0; i--){
          uint8_t bit = digitalReadFast(MUX_YOKE_OUT);
          bitWrite(states[mux], i, bit);
          digitalWriteFast(MUX_YOKE_CLK, HIGH);

          delayMicroseconds(ROXMUX_74HC165_DELAY);

          digitalWriteFast(MUX_YOKE_CLK, LOW);
        }
      }
      // for(uint8_t mux = 0; mux < _muxCount; mux++){
      // //  states[mux] = shiftIn(dataPin, clkPin, LSBFIRST);
      //  states[mux] = FSI->readLSBFIRST(); // optimized fastShiftIn() method by Rob Tillaart
      // }
    }

    // return FALSE/LOW if bit is set (pin is grounded)
    bool read(uint16_t n){
      if(n >= (_muxCount*8)){
        return HIGH;
      }
      return bitRead(states[(n>>3)], (n&0x07));
    }

    // Get all pin states for two muxes in one 16 bit unsigned int
    uint16_t get2MuxPinStates() {
      return ~(states[0] | (states[1]<<8));
    }

    // bool readPin(uint16_t n){
    //   return read(n);
    // }
};

template <uint8_t _muxinCount>
class RoxMUXIN16 : public Rox74HC165 <_muxinCount*2>{};


#endif
