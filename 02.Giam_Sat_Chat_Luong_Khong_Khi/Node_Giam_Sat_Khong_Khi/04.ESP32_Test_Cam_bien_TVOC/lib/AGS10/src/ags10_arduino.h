// Adapter noi driver AGS10 voi TwoWire cua Arduino (chi bien dich tren mach / mo phong firmware).
#pragma once

#ifdef ARDUINO

#include <Arduino.h>
#include <Wire.h>

#include "ags10_hal.h"

namespace ags10 {

class ArduinoClock : public IClock {
 public:
  uint32_t millis() override { return ::millis(); }
  void delayMs(uint32_t ms) override { ::delay(ms); }
};

class ArduinoI2cBus : public II2cBus {
 public:
  explicit ArduinoI2cBus(TwoWire& wire) : wire_(wire) {}
  bool write(uint8_t addr, const uint8_t* data, size_t len) override {
    wire_.beginTransmission(addr);
    if (len > 0 && wire_.write(data, len) != len) {
      wire_.endTransmission(true);
      return false;
    }
    return wire_.endTransmission(true) == 0;  // STOP sau moi lenh (datasheet: cach 30 ms roi moi doc)
  }
  size_t read(uint8_t addr, uint8_t* data, size_t len) override {
    const size_t got = wire_.requestFrom(addr, static_cast<uint8_t>(len));
    size_t n = 0;
    while (n < len && n < got && wire_.available() > 0) data[n++] = static_cast<uint8_t>(wire_.read());
    while (wire_.available() > 0) wire_.read();
    return n;
  }

 private:
  TwoWire& wire_;
};

}  // namespace ags10

#endif  // ARDUINO
