/*
  FastAS5600: an AS5600 whose angle reads mostly skip the register-pointer write.

  The main loop reads the ANGLE register (0x0E/0x0F) of both encoders on every pass, and
  each read is three I2C transactions on this bus: the TCA9548 channel select, a write
  that loads the AS5600's register pointer with 0x0E, and the 2-byte read. The AS5600
  datasheet exempts ANGLE, RAW ANGLE and MAGNITUDE from the pointer's automatic increment
  on reads, as long as the pointer sits on the register's high byte - so once it has been
  loaded with 0x0E, a plain 2-byte read returns ANGLE again. That drops the pointer write:
  the same 12-bit reading, one transaction fewer per axis.

  The library's full read, which reloads the pointer, is still used for every
  FAST_READS+1-th angle read (bounding how long a sensor that reset on its own, its
  pointer back at 0x00, could be misread) and for the first read after an I2C error.

  Only ANGLE is ever read after begin() in this firmware - readAngle(),
  getCumulativePosition() and resetCumulativePosition() all go through it, and begin()
  runs before the first read. Flash is too tight to guard the other register accessors,
  so any NEW access to another AS5600 register must call reloadPointer() afterwards.
*/
#pragma once
#include <AS5600.h>

class FastAS5600 : public AS5600
{
public:
  using AS5600::AS5600;

  // Call after any access to an AS5600 register other than ANGLE.
  void reloadPointer() { _fastLeft = 0; }

protected:
  uint16_t readReg2(uint8_t reg) override
  {
    if (reg != ANGLE_REG || _fastLeft == 0)
    {
      // Not an angle read, or a reload is due: the library's full read, whose write
      // leaves the pointer on reg.
      uint16_t value = AS5600::readReg2(reg);
      _fastLeft = (reg == ANGLE_REG && _error == AS5600_OK) ? FAST_READS : 0;
      return value;
    }
    _fastLeft--;
    _error = AS5600_OK;
    if (_wire->requestFrom(_address, (uint8_t)2) != 2)
    {
      _error = AS5600_ERROR_I2C_READ_3;
      _fastLeft = 0;                       // reload the pointer on the next read
      return 0;
    }
    uint16_t data = _wire->read();
    data <<= 8;
    data += _wire->read();
    return data;
  }

private:
  enum : uint8_t {
    ANGLE_REG  = 0x0E,   // ANGLE high byte (AS5600_ANGLE is private to AS5600.cpp)
    FAST_READS = 7       // pointer-less angle reads between full reads
  };
  uint8_t _fastLeft = 0; // 0 = the next angle read reloads the pointer
};
