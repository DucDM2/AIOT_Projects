// Lop truu tuong phan cung: driver chi lam viec qua cac interface nay,
// nho do chay duoc ca tren ESP32 (Arduino) lan tren may tinh (unit test voi cam bien gia lap).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace acd10 {

class IClock {
 public:
  virtual ~IClock() {}
  virtual uint32_t millis() = 0;
  virtual void delayMs(uint32_t ms) = 0;
};

class IUartPort {
 public:
  virtual ~IUartPort() {}
  virtual size_t write(const uint8_t* data, size_t len) = 0;
  virtual int read() = 0;  // tra ve -1 neu khong co byte
  virtual void flushInput() = 0;
};

class II2cBus {
 public:
  virtual ~II2cBus() {}
  // Ghi len bus (co STOP), tra ve true neu cam bien ACK tat ca cac byte.
  virtual bool write(uint8_t addr, const uint8_t* data, size_t len) = 0;
  // Doc toi da len byte, tra ve so byte doc duoc.
  virtual size_t read(uint8_t addr, uint8_t* data, size_t len) = 0;
};

}  // namespace acd10
