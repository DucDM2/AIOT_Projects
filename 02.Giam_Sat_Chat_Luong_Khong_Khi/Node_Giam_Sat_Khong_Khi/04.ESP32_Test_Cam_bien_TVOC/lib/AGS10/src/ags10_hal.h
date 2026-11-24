// Lop truu tuong phan cung: driver chi lam viec qua cac interface nay,
// nho do chay duoc ca tren ESP32 (Arduino) lan tren may tinh (unit test voi cam bien gia lap).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ags10 {

class IClock {
 public:
  virtual ~IClock() {}
  virtual uint32_t millis() = 0;
  virtual void delayMs(uint32_t ms) = 0;
};

class II2cBus {
 public:
  virtual ~II2cBus() {}
  // Ghi len bus (ket thuc bang STOP), tra ve true neu cam bien ACK tat ca cac byte.
  // len = 0 -> chi gui dia chi (probe).
  virtual bool write(uint8_t addr, const uint8_t* data, size_t len) = 0;
  // Doc toi da len byte, tra ve so byte doc duoc (0 neu cam bien khong ACK dia chi).
  virtual size_t read(uint8_t addr, uint8_t* data, size_t len) = 0;
};

}  // namespace ags10
