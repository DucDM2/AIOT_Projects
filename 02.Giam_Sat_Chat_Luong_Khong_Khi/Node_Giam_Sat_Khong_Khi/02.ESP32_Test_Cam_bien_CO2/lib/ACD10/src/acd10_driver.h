// Driver ACD10 cho 2 che do giao tiep: I2C (chan SET de ho - mac dinh) va UART (chan SET noi GND).
#pragma once

#include "acd10_hal.h"
#include "acd10_protocol.h"

namespace acd10 {

class Acd10I2c {
 public:
  // Datasheet khong ghi thoi gian cho giua lenh doc va luc lay du lieu.
  // 80 ms la gia tri thu vien RobTillaart/ACD10 dung va da chay tren mach that.
  static constexpr uint32_t kDefaultReadDelayMs = 80;
  // Datasheet: sau lenh ghi (che do / hieu chuan) phai cho > 5 ms moi duoc doc lai.
  static constexpr uint32_t kCmdDelayMs = 10;

  Acd10I2c(II2cBus& bus, IClock& clock, uint8_t addr = kI2cAddress,
           uint32_t readDelayMs = kDefaultReadDelayMs)
      : bus_(bus), clock_(clock), addr_(addr), readDelayMs_(readDelayMs) {}

  bool probe();  // cam bien co ACK dia chi khong
  Status readCo2(Measurement& out);

  Status readCalibrationMode(uint8_t& mode);
  // Cac lenh ghi deu doc lai de xac nhan (VerifyFailed neu khong khop).
  Status setCalibrationMode(uint8_t mode);
  Status manualCalibrate(uint16_t ppm);  // 400..5000 ppm
  Status factoryReset();

  // Chuoi 10 ky tu ASCII, cap >= 11
  Status readVersion(char* out, size_t cap) { return readAscii(kI2cCmdVersion, out, cap); }
  Status readSerial(char* out, size_t cap) { return readAscii(kI2cCmdSerial, out, cap); }

  // Du lieu tho nhan duoc o lan doc gan nhat (de debug tren Serial Monitor)
  const uint8_t* lastRaw() const { return raw_; }
  size_t lastRawLen() const { return rawLen_; }

 private:
  Status query(const uint8_t* cmd, size_t cmdLen, size_t respLen, uint32_t delayMs);
  Status readWord(const uint8_t* cmd2, uint16_t& value);
  Status readAscii(const uint8_t* cmd2, char* out, size_t cap);
  Status writeThenVerify(const uint8_t* cmd, size_t cmdLen, uint16_t expected);

  II2cBus& bus_;
  IClock& clock_;
  uint8_t addr_;
  uint32_t readDelayMs_;
  uint8_t raw_[16] = {0};
  size_t rawLen_ = 0;
};

class Acd10Uart {
 public:
  // Khung dai nhat (16 byte) o 1200 baud mat ~135 ms; de du cho cam bien xu ly.
  static constexpr uint32_t kDefaultTimeoutMs = 1000;

  Acd10Uart(IUartPort& port, IClock& clock, uint32_t timeoutMs = kDefaultTimeoutMs)
      : port_(port), clock_(clock), timeoutMs_(timeoutMs) {}

  Status readCo2(Measurement& out);
  Status manualCalibrate(uint16_t ppm);  // 400..5000 ppm
  Status setCalibrationMode(uint8_t mode);
  Status factoryReset();
  Status readVersion(char* out, size_t cap);
  Status readSerial(char* out, size_t cap);

  const uint8_t* lastRaw() const { return raw_; }
  size_t lastRawLen() const { return rawLen_; }
  const uint8_t* lastCommand() const { return cmd_; }
  size_t lastCommandLen() const { return cmdLen_; }
  uint32_t lastElapsedMs() const { return elapsedMs_; }

 private:
  // Gui cmd_[0..cmdLen_) roi cho khung phan hoi.
  Status transact(uint8_t expectedCmd, uint8_t expectedLen, UartFrame& frame);
  Status ackCommand(uint8_t cmd);
  Status readAscii(uint8_t cmd, uint8_t expectedLen, char* out, size_t cap);

  IUartPort& port_;
  IClock& clock_;
  uint32_t timeoutMs_;
  uint8_t cmd_[kUartMaxFrame] = {0};
  size_t cmdLen_ = 0;
  uint8_t raw_[64] = {0};
  size_t rawLen_ = 0;
  uint32_t elapsedMs_ = 0;
};

}  // namespace acd10
