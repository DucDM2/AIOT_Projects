// Cam bien AGS10 gia lap theo datasheet va dong ho gia, dung chung cho test driver va mo phong firmware.
// Mo phong ca cac rang buoc ma cam bien that bat buoc:
//   - I2C > 15 kHz       -> khong ACK
//   - 2 lenh cach < 30 ms -> khong ACK (dem gapViolations)
//   - doc TVOC/dien tro cach < 1.5 s -> RDY = 1 o lan doc do (dem acqViolations), giong mo ta datasheet
//   - CRC lenh ghi sai   -> khong ACK
#pragma once

#include <string.h>

#include "ags10_hal.h"
#include "ags10_protocol.h"

namespace fake {

using namespace ags10;

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

class FakeAgs10 : public II2cBus {
 public:
  explicit FakeAgs10(IClock& clock) : clock_(clock), powerOnMs(clock.millis()) {}

  // --- Cau hinh / gia tri do ---
  uint8_t address = kDefaultAddress;
  bool present = true;
  uint32_t busFreqHz = 10000;
  uint32_t preheatMs = kPreheatMs;
  uint32_t tvocPpb = 150;
  uint32_t preheatTvocPpb = 1234;  // gia tri cam bien tra ve luc dang lam nong (khong dung)
  uint32_t resistance = 0x1CBC;    // 0.1 kOhm
  uint8_t version = 0x0B;
  uint8_t unitBits = 0;            // truong CH (bit 3:1) cua STATUS
  int corruptIndex = -1;           // XOR 1 byte cua khung tra ve
  uint8_t corruptMask = 0x01;
  size_t shortRead = 0;            // >0: chi tra n byte
  bool enforceTiming = true;

  // --- Ghi nhan ---
  IClock& clock_;
  uint32_t powerOnMs;
  int gapViolations = 0, acqViolations = 0, freqViolations = 0;
  int probes = 0, tvocReads = 0, resistanceReads = 0, versionReads = 0;
  int zeroPointWrites = 0, addressWrites = 0, rejectedWrites = 0;
  uint16_t zeroPointValue = 0;
  uint8_t lastWrite[8];
  size_t lastWriteLen = 0;
  uint32_t lastRegWriteMs = 0, lastReadMs = 0;
  uint32_t minAcqIntervalMs = 0xFFFFFFFFUL;
  uint32_t minRegToReadMs = 0xFFFFFFFFUL;

  // Goi tu bus mo phong khi ket thuc giao dich (STOP) de do khoang cach STOP -> START chinh xac
  void busStop() {
    lastStopMs_ = clock_.millis();
    hasStop_ = true;
  }

  bool write(uint8_t addr, const uint8_t* data, size_t len) override {
    if (!present || addr != address) return false;
    if (!startTransaction()) return false;
    lastWriteLen = len < sizeof(lastWrite) ? len : sizeof(lastWrite);
    if (len) memcpy(lastWrite, data, lastWriteLen);
    if (len == 0) {
      ++probes;
      return true;
    }
    const uint8_t reg = data[0];
    const uint32_t now = clock_.millis();
    if (len == 1) {
      if (reg != kRegTvoc && reg != kRegVersion && reg != kRegResistance) return reject();
      reg_ = reg;
      regValid_ = true;
      lastRegWriteMs = now;
      if (reg == kRegTvoc || reg == kRegResistance) {
        if (hasAcq_) {
          const uint32_t gap = now - lastAcqMs_;
          if (gap < minAcqIntervalMs) minAcqIntervalMs = gap;
          if (gap < kAcquisitionGapMs) {
            ++acqViolations;
            stuck_ = true;
          }
        }
        hasAcq_ = true;
        lastAcqMs_ = now;
      }
      return true;
    }
    if (len != 1 + kFrameLen || crc8(&data[1], 4) != data[5]) return reject();
    if (reg == kRegCalibration && data[1] == 0x00 && data[2] == 0x0C) {
      zeroPointValue = static_cast<uint16_t>((data[3] << 8) | data[4]);
      ++zeroPointWrites;
      return true;
    }
    if (reg == kRegAddress && data[1] == data[3] && data[2] == data[4] &&
        data[2] == static_cast<uint8_t>(~data[1])) {
      address = data[1];
      ++addressWrites;
      return true;
    }
    return reject();
  }

  size_t read(uint8_t addr, uint8_t* data, size_t len) override {
    if (!present || addr != address) return 0;
    if (!startTransaction()) return 0;
    if (!regValid_) return 0;
    const uint32_t now = clock_.millis();
    const uint32_t regToRead = now - lastRegWriteMs;
    if (regToRead < minRegToReadMs) minRegToReadMs = regToRead;
    lastReadMs = now;

    uint8_t f[kFrameLen] = {0};
    if (reg_ == kRegTvoc) {
      ++tvocReads;
      const bool ready = (now - powerOnMs >= preheatMs) && !stuck_;
      const uint32_t v = ready ? tvocPpb : preheatTvocPpb;
      f[0] = static_cast<uint8_t>((unitBits & kStatusUnitMask) | (ready ? 0 : kStatusRdyMask));
      f[1] = static_cast<uint8_t>(v >> 16);
      f[2] = static_cast<uint8_t>(v >> 8);
      f[3] = static_cast<uint8_t>(v);
      stuck_ = false;
    } else if (reg_ == kRegResistance) {
      ++resistanceReads;
      f[0] = static_cast<uint8_t>(resistance >> 24);
      f[1] = static_cast<uint8_t>(resistance >> 16);
      f[2] = static_cast<uint8_t>(resistance >> 8);
      f[3] = static_cast<uint8_t>(resistance);
    } else {
      ++versionReads;
      f[3] = version;
    }
    f[4] = crc8(f, 4);
    if (corruptIndex >= 0 && static_cast<size_t>(corruptIndex) < kFrameLen) f[corruptIndex] ^= corruptMask;
    reg_ = kRegTvoc;  // datasheet: sau khi doc, Reg mac dinh ve 0x00
    size_t n = len < kFrameLen ? len : kFrameLen;
    if (shortRead > 0 && shortRead < n) n = shortRead;
    memcpy(data, f, n);
    return n;
  }

 private:
  bool startTransaction() {
    const uint32_t now = clock_.millis();
    if (busFreqHz > kMaxI2cFreqHz) {
      ++freqViolations;
      return false;
    }
    if (enforceTiming && hasStop_ && now - lastStopMs_ < kCommandGapMs) {
      ++gapViolations;
      busStop();
      return false;
    }
    busStop();  // test driver: giao dich tuc thoi, START = STOP
    return true;
  }
  bool reject() {
    ++rejectedWrites;
    return false;
  }

  uint8_t reg_ = kRegTvoc;
  bool regValid_ = true;  // datasheet: Reg = 0x00 sau khi cap nguon (cho phep doc truc tiep)
  bool hasStop_ = false;
  uint32_t lastStopMs_ = 0;
  bool hasAcq_ = false;
  uint32_t lastAcqMs_ = 0;
  bool stuck_ = false;
};

}  // namespace fake
