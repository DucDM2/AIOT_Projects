// Cam bien APM10 gia lap (UART + I2C) va dong ho gia de test driver ma khong can phan cung.
#pragma once

#include <string.h>

#include "apm10_hal.h"
#include "apm10_protocol.h"

namespace fake {

using namespace apm10;

class FakeClock : public IClock {
 public:
  uint32_t now = 0;
  uint32_t totalDelay = 0;
  uint32_t millis() override { return now; }
  void delayMs(uint32_t ms) override {
    now += ms;
    totalDelay += ms;
  }
};

// Cam bien UART gia lap: nhan lenh, tra loi sau responseDelayMs, moi byte mat byteTimeMs (1200 baud ~ 8.3 ms).
class FakeUartSensor : public IUartPort {
 public:
  explicit FakeUartSensor(FakeClock& clock) : clock_(clock) {}

  // --- Gia tri do va hanh vi (test tuy chinh) ---
  uint16_t pm1 = 10, pm25 = 20, pm10 = 30;
  uint8_t lenStyleAll = 0x02;     // byte LEN trong phan hoi lenh 0x01 (datasheet ghi 02)
  uint8_t respCmdByte = 0x00;     // byte CMD trong phan hoi (datasheet ghi 00)
  bool onlyPm25ForAll = false;    // firmware chi tra 2 byte cho lenh 0x01
  bool silent = false;            // khong tra loi
  int corruptIndex = -1;          // XOR 1 byte cua phan hoi
  uint8_t corruptMask = 0x01;
  int truncateTo = -1;            // chi gui n byte dau
  uint32_t responseDelayMs = 10;
  uint32_t byteTimeMs = 9;
  uint8_t garbage[128];
  size_t garbageLen = 0;          // rac gui truoc khung phan hoi

  // --- Ghi nhan ---
  uint8_t lastCmd[16];
  size_t lastCmdLen = 0;
  int commandsReceived = 0;

  // Day san byte "cu" vao buffer nhan (mo phong byte sot lai tu truoc)
  void injectStale(const uint8_t* d, size_t n) {
    for (size_t i = 0; i < n; ++i) push(d[i], clock_.now);
  }

  size_t write(const uint8_t* data, size_t len) override {
    lastCmdLen = len < sizeof(lastCmd) ? len : sizeof(lastCmd);
    memcpy(lastCmd, data, lastCmdLen);
    ++commandsReceived;
    // Lenh hop le: FE A5 00 CMD CS
    if (len != 5 || data[0] != 0xFE || data[1] != 0xA5 || data[2] != 0x00) return len;
    if (sumLow8(&data[1], 3) != data[4]) return len;
    const uint8_t cmd = data[3];
    if (cmd != kCmdReadAll && cmd != kCmdReadPm25) return len;
    if (silent) return len;

    uint8_t resp[16];
    size_t n = 0;
    resp[n++] = 0xFE;
    resp[n++] = 0xA5;
    const bool six = (cmd == kCmdReadAll) && !onlyPm25ForAll;
    resp[n++] = six ? lenStyleAll : 0x02;
    resp[n++] = respCmdByte;
    if (six) {
      put16(resp, n, pm1);
      put16(resp, n, pm25);
      put16(resp, n, pm10);
    } else {
      put16(resp, n, pm25);
    }
    resp[n] = sumLow8(&resp[1], n - 1);
    ++n;
    if (corruptIndex >= 0 && (size_t)corruptIndex < n) resp[corruptIndex] ^= corruptMask;
    if (truncateTo >= 0 && (size_t)truncateTo < n) n = (size_t)truncateTo;

    // Lenh 5 byte truyen mat ~5 byte-time, sau do cam bien xu ly roi tra loi
    uint32_t t = clock_.now + 5 * byteTimeMs + responseDelayMs;
    for (size_t i = 0; i < garbageLen; ++i) push(garbage[i], t += byteTimeMs);
    for (size_t i = 0; i < n; ++i) push(resp[i], t += byteTimeMs);
    return len;
  }

  int read() override {
    if (head_ == tail_ || times_[head_] > clock_.now) return -1;
    const uint8_t b = bytes_[head_];
    head_ = (head_ + 1) % kCap;
    return b;
  }

  void flushInput() override {
    while (head_ != tail_ && times_[head_] <= clock_.now) head_ = (head_ + 1) % kCap;
  }

 private:
  static constexpr size_t kCap = 512;
  static void put16(uint8_t* b, size_t& n, uint16_t v) {
    b[n++] = (uint8_t)(v >> 8);
    b[n++] = (uint8_t)v;
  }
  void push(uint8_t b, uint32_t t) {
    bytes_[tail_] = b;
    times_[tail_] = t;
    tail_ = (tail_ + 1) % kCap;
  }
  FakeClock& clock_;
  uint8_t bytes_[kCap];
  uint32_t times_[kCap];
  size_t head_ = 0, tail_ = 0;
};

// Cam bien I2C gia lap theo datasheet: dia chi 0x08, Start/Stop/Read, 30 byte kem CRC.
class FakeI2cSensor : public II2cBus {
 public:
  uint16_t pm1 = 5, pm25 = 9, pm10 = 14;
  bool present = true;
  bool measuring = false;
  int corruptIndex = -1;  // XOR 1 byte du lieu doc ra
  size_t shortRead = 0;   // >0: chi tra ve n byte
  bool lastWasReadCmd = false;
  int startCount = 0, stopCount = 0, readCount = 0;
  uint8_t lastWrite[16];
  size_t lastWriteLen = 0;

  bool write(uint8_t addr, const uint8_t* data, size_t len) override {
    if (!present || addr != kI2cAddress) return false;
    lastWriteLen = len < sizeof(lastWrite) ? len : sizeof(lastWrite);
    if (len) memcpy(lastWrite, data, lastWriteLen);
    lastWasReadCmd = false;
    if (len == 0) return true;  // probe
    if (len == sizeof(kI2cCmdStart) && memcmp(data, kI2cCmdStart, len) == 0) {
      measuring = true;
      ++startCount;
      return true;
    }
    if (len == sizeof(kI2cCmdStop) && memcmp(data, kI2cCmdStop, len) == 0) {
      measuring = false;
      ++stopCount;
      return true;
    }
    if (len == sizeof(kI2cCmdRead) && memcmp(data, kI2cCmdRead, len) == 0) {
      lastWasReadCmd = true;
      return true;
    }
    return false;  // lenh la -> NACK
  }

  size_t read(uint8_t addr, uint8_t* data, size_t len) override {
    if (!present || addr != kI2cAddress || !measuring || !lastWasReadCmd) return 0;
    ++readCount;
    uint8_t buf[kI2cDataLen];
    const uint16_t words[kI2cWordCount] = {pm1, pm25, 0, pm10, 0, 0, 0, 0, 0, 0};
    for (size_t w = 0; w < kI2cWordCount; ++w) {
      buf[w * 3] = (uint8_t)(words[w] >> 8);
      buf[w * 3 + 1] = (uint8_t)words[w];
      buf[w * 3 + 2] = crc8(&buf[w * 3], 2);
    }
    if (corruptIndex >= 0 && (size_t)corruptIndex < kI2cDataLen) buf[corruptIndex] ^= 0x01;
    size_t n = len < kI2cDataLen ? len : kI2cDataLen;
    if (shortRead > 0 && shortRead < n) n = shortRead;
    memcpy(data, buf, n);
    return n;
  }
};

}  // namespace fake
