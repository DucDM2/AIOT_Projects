// Adapter noi driver APM10 voi HardwareSerial / TwoWire cua Arduino (chi bien dich tren mach).
#pragma once

#ifdef ARDUINO

#include <Arduino.h>
#include <Wire.h>

#include "apm10_hal.h"

namespace apm10 {

class ArduinoClock : public IClock {
 public:
  uint32_t millis() override { return ::millis(); }
  void delayMs(uint32_t ms) override { ::delay(ms); }
};

class ArduinoUartPort : public IUartPort {
 public:
  explicit ArduinoUartPort(HardwareSerial& serial) : serial_(serial) {}
  size_t write(const uint8_t* data, size_t len) override {
    const size_t n = serial_.write(data, len);
    serial_.flush();  // cho gui xong lenh (5 byte o 1200 baud ~ 42 ms)
    return n;
  }
  int read() override { return serial_.read(); }
  void flushInput() override {
    while (serial_.available() > 0) serial_.read();
  }

 private:
  HardwareSerial& serial_;
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
    return wire_.endTransmission(true) == 0;  // co STOP truoc khi doc (theo gian do datasheet)
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

}  // namespace apm10

#endif  // ARDUINO
