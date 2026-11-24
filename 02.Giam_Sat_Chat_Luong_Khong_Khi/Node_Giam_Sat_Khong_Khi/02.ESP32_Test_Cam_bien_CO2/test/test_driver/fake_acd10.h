// Cam bien ACD10 gia lap (I2C + UART) va dong ho gia de test driver ma khong can phan cung.
// Hanh vi lam theo datasheet ACD10 ban 04/2022.
#pragma once

#include <string.h>

#include "acd10_hal.h"
#include "acd10_protocol.h"

namespace fake {

using namespace acd10;

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

// Trang thai ben trong cam bien, dung chung cho 2 che do
struct SensorState {
  uint32_t co2 = 650;
  uint16_t temp = 0x0BB8;
  uint8_t calMode = kCalModeAuto;
  uint16_t manualCal = 0;
  uint16_t factoryResult = 0;
  int calibrations = 0;
  int factoryResets = 0;
  bool supportsCalMode = true;  // false: mo phong firmware ACK nhung bo qua lenh dat che do
  bool factoryFails = false;
  char version[11] = "V1.0.2.SEN";
  char serial[11] = "2405170042";
};

// ======================= I2C =======================
class FakeI2cSensor : public II2cBus {
 public:
  explicit FakeI2cSensor(FakeClock& clock) : clock_(clock) {}

  SensorState s;
  bool present = true;
  uint32_t minReadDelayMs = 50;  // doc som hon thoi gian nay sau lenh 03 00 -> cam bien NACK
  int corruptIndex = -1;         // XOR 1 byte du lieu doc ra
  size_t shortRead = 0;          // >0: chi tra ve n byte
  uint8_t lastWrite[16];
  size_t lastWriteLen = 0;
  int readCo2Count = 0;
  int writes = 0;

  bool write(uint8_t addr, const uint8_t* data, size_t len) override {
    if (!present || addr != kI2cAddress) return false;
    ++writes;
    lastWriteLen = len < sizeof(lastWrite) ? len : sizeof(lastWrite);
    if (len) memcpy(lastWrite, data, lastWriteLen);
    pending_ = kNone;
    if (len == 0) return true;  // probe
    if (len == 2 && is(data, kI2cCmdReadCo2)) {
      pending_ = kCo2;
      cmdAt_ = clock_.now;
      return true;
    }
    if (len == 2 && is(data, kI2cCmdCalMode)) return pend(kMode);
    if (len == 2 && is(data, kI2cCmdManualCal)) return pend(kCal);
    if (len == 2 && is(data, kI2cCmdFactoryReset)) return pend(kFactory);
    if (len == 2 && is(data, kI2cCmdVersion)) return pend(kVersion);
    if (len == 2 && is(data, kI2cCmdSerial)) return pend(kSerial);
    if (len == 5 && crc8(&data[2], 2) != data[4]) return false;  // CRC sai -> NACK
    const uint16_t v = (uint16_t)((data[2] << 8) | data[3]);
    if (len == 5 && is(data, kI2cCmdCalMode)) {
      if (v > 1) return false;
      if (s.supportsCalMode) s.calMode = (uint8_t)v;
      return true;
    }
    if (len == 5 && is(data, kI2cCmdManualCal)) {
      s.manualCal = v;
      ++s.calibrations;
      return true;
    }
    if (len == 3 && is(data, kI2cCmdFactoryReset) && data[2] == 0x00) {
      ++s.factoryResets;
      s.factoryResult = s.factoryFails ? 0 : 1;
      s.calMode = kCalModeAuto;
      return true;
    }
    return false;  // lenh la -> NACK
  }

  size_t read(uint8_t addr, uint8_t* data, size_t len) override {
    if (!present || addr != kI2cAddress) return 0;
    uint8_t buf[16];
    size_t n = 0;
    switch (pending_) {
      case kCo2:
        if (clock_.now - cmdAt_ < minReadDelayMs) return 0;  // chua do xong
        ++readCo2Count;
        putWord(buf, n, (uint16_t)(s.co2 >> 16));
        putWord(buf, n, (uint16_t)s.co2);
        putWord(buf, n, s.temp);
        break;
      case kMode: putWord(buf, n, s.calMode); break;
      case kCal: putWord(buf, n, s.manualCal); break;
      case kFactory: putWord(buf, n, s.factoryResult); break;
      case kVersion: memcpy(buf, s.version, 10); n = 10; break;
      case kSerial: memcpy(buf, s.serial, 10); n = 10; break;
      case kNone: return 0;
    }
    if (corruptIndex >= 0 && (size_t)corruptIndex < n) buf[corruptIndex] ^= 0x01;
    if (len < n) n = len;
    if (shortRead > 0 && shortRead < n) n = shortRead;
    memcpy(data, buf, n);
    return n;
  }

 private:
  enum Pending { kNone, kCo2, kMode, kCal, kFactory, kVersion, kSerial };
  static bool is(const uint8_t* d, const uint8_t* cmd) { return d[0] == cmd[0] && d[1] == cmd[1]; }
  bool pend(Pending p) {
    pending_ = p;
    return true;
  }
  static void putWord(uint8_t* b, size_t& n, uint16_t v) {
    b[n] = (uint8_t)(v >> 8);
    b[n + 1] = (uint8_t)v;
    b[n + 2] = crc8(&b[n], 2);
    n += 3;
  }
  FakeClock& clock_;
  Pending pending_ = kNone;
  uint32_t cmdAt_ = 0;
};

// ======================= UART =======================
// Nhan lenh, tra loi sau responseDelayMs, moi byte mat byteTimeMs (1200 baud ~ 8.3 ms).
class FakeUartSensor : public IUartPort {
 public:
  explicit FakeUartSensor(FakeClock& clock) : clock_(clock) {}

  SensorState s;
  bool silent = false;           // khong tra loi
  int respCmdOverride = -1;      // >=0: byte CMD trong phan hoi (mo phong firmware khac datasheet)
  int corruptIndex = -1;         // XOR 1 byte cua phan hoi
  uint8_t corruptMask = 0x01;
  int truncateTo = -1;           // chi gui n byte dau
  uint32_t responseDelayMs = 20;
  uint32_t byteTimeMs = 9;
  uint8_t garbage[128];
  size_t garbageLen = 0;         // rac gui truoc khung phan hoi

  uint8_t lastCmd[32];
  size_t lastCmdLen = 0;
  int commandsReceived = 0;
  int validCommands = 0;

  void injectStale(const uint8_t* d, size_t n) {
    for (size_t i = 0; i < n; ++i) push(d[i], clock_.now);
  }

  size_t write(const uint8_t* data, size_t len) override {
    lastCmdLen = len < sizeof(lastCmd) ? len : sizeof(lastCmd);
    memcpy(lastCmd, data, lastCmdLen);
    ++commandsReceived;
    // Lenh hop le: FE A6 n CMD data[n] CS
    if (len < 5 || data[0] != 0xFE || data[1] != 0xA6 || len != (size_t)(5 + data[2])) return len;
    if (sumLow8(&data[1], len - 2) != data[len - 1]) return len;
    ++validCommands;
    const uint8_t cmd = data[3];
    const uint8_t n = data[2];
    uint8_t d[16];
    size_t dn = 0;
    switch (cmd) {
      case kUartCmdReadCo2:
        if (n != 0) return len;
        d[0] = (uint8_t)(s.co2 >> 8);
        d[1] = (uint8_t)s.co2;
        d[2] = 0;
        d[3] = 0;
        dn = 4;
        break;
      case kUartCmdManualCal:
        if (n != 2) return len;
        s.manualCal = (uint16_t)((data[4] << 8) | data[5]);
        ++s.calibrations;
        break;
      case kUartCmdSetCalMode:
        if (n != 2 || data[4] != 0 || data[5] > 1) return len;
        if (s.supportsCalMode) s.calMode = data[5];
        break;
      case kUartCmdFactoryReset:
        if (n != 0) return len;
        ++s.factoryResets;
        s.calMode = kCalModeAuto;
        break;
      case kUartCmdVersion:
        memcpy(d, s.version, 10);
        d[10] = 0;
        dn = 11;
        break;
      case kUartCmdSerial:
        memcpy(d, s.serial, 10);
        dn = 10;
        break;
      default: return len;  // lenh la -> im lang
    }
    if (silent) return len;

    uint8_t resp[24];
    size_t rn = 0;
    resp[rn++] = 0xFE;
    resp[rn++] = 0xA6;
    resp[rn++] = (uint8_t)dn;
    resp[rn++] = respCmdOverride >= 0 ? (uint8_t)respCmdOverride : cmd;
    memcpy(&resp[rn], d, dn);
    rn += dn;
    resp[rn] = sumLow8(&resp[1], rn - 1);
    ++rn;
    if (corruptIndex >= 0 && (size_t)corruptIndex < rn) resp[corruptIndex] ^= corruptMask;
    if (truncateTo >= 0 && (size_t)truncateTo < rn) rn = (size_t)truncateTo;

    // Lenh truyen mat len byte-time, sau do cam bien xu ly roi tra loi
    uint32_t t = clock_.now + (uint32_t)len * byteTimeMs + responseDelayMs;
    for (size_t i = 0; i < garbageLen; ++i) push(garbage[i], t += byteTimeMs);
    for (size_t i = 0; i < rn; ++i) push(resp[i], t += byteTimeMs);
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

}  // namespace fake
