// Driver APM10 cho 2 che do giao tiep: UART (chan SET tha noi / muc 1) va I2C (chan SET noi GND).
#pragma once

#include "apm10_hal.h"
#include "apm10_protocol.h"

namespace apm10 {

class Apm10Uart {
 public:
  static constexpr uint32_t kDefaultTimeoutMs = 500;  // 11 byte o 1200 baud ~ 92 ms

  Apm10Uart(IUartPort& port, IClock& clock, uint32_t timeoutMs = kDefaultTimeoutMs)
      : port_(port), clock_(clock), timeoutMs_(timeoutMs) {}

  // Doc PM1.0, PM2.5, PM10 (lenh 0x01)
  Status readAll(Measurement& out) { return transact(kCmdReadAll, out); }
  // Doc rieng PM2.5 (lenh 0x00)
  Status readPm25(Measurement& out) { return transact(kCmdReadPm25, out); }

  // Du lieu tho nhan duoc o lan doc gan nhat (de debug tren Serial Monitor)
  const uint8_t* lastRaw() const { return raw_; }
  size_t lastRawLen() const { return rawLen_; }
  const uint8_t* lastCommand() const { return cmd_; }
  uint32_t lastElapsedMs() const { return elapsedMs_; }

 private:
  Status transact(uint8_t cmd, Measurement& out);

  IUartPort& port_;
  IClock& clock_;
  uint32_t timeoutMs_;
  uint8_t cmd_[kUartCommandLen] = {0};
  uint8_t raw_[64] = {0};
  size_t rawLen_ = 0;
  uint32_t elapsedMs_ = 0;
};

class Apm10I2c {
 public:
  static constexpr uint32_t kDefaultReadDelayMs = 5;

  Apm10I2c(II2cBus& bus, IClock& clock, uint8_t addr = kI2cAddress,
           uint32_t readDelayMs = kDefaultReadDelayMs)
      : bus_(bus), clock_(clock), addr_(addr), readDelayMs_(readDelayMs) {}

  bool probe();  // cam bien co ACK dia chi khong
  Status startMeasurement();
  Status stopMeasurement();
  Status read(Measurement& out);

  const uint8_t* lastRaw() const { return raw_; }
  size_t lastRawLen() const { return rawLen_; }

 private:
  II2cBus& bus_;
  IClock& clock_;
  uint8_t addr_;
  uint32_t readDelayMs_;
  uint8_t raw_[kI2cDataLen] = {0};
  size_t rawLen_ = 0;
};

}  // namespace apm10
