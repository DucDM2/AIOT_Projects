#include "acd10_driver.h"

#include <string.h>

namespace acd10 {

// ======================= I2C =======================

bool Acd10I2c::probe() { return bus_.write(addr_, nullptr, 0); }

Status Acd10I2c::query(const uint8_t* cmd, size_t cmdLen, size_t respLen, uint32_t delayMs) {
  rawLen_ = 0;
  if (respLen > sizeof(raw_)) return Status::InvalidArgument;
  if (!bus_.write(addr_, cmd, cmdLen)) return Status::I2cNack;
  if (delayMs > 0) clock_.delayMs(delayMs);
  rawLen_ = bus_.read(addr_, raw_, respLen);
  if (rawLen_ == 0) return Status::NoResponse;
  if (rawLen_ < respLen) return Status::ShortRead;
  return Status::Ok;
}

Status Acd10I2c::readCo2(Measurement& out) {
  const Status st = query(kI2cCmdReadCo2, sizeof(kI2cCmdReadCo2), kI2cCo2DataLen, readDelayMs_);
  if (st != Status::Ok) return st;
  return decodeI2cCo2(raw_, rawLen_, out);
}

Status Acd10I2c::readWord(const uint8_t* cmd2, uint16_t& value) {
  const Status st = query(cmd2, 2, kI2cWordLen, kCmdDelayMs);
  if (st != Status::Ok) return st;
  return decodeI2cWord(raw_, rawLen_, value);
}

Status Acd10I2c::readAscii(const uint8_t* cmd2, char* out, size_t cap) {
  if (out == nullptr || cap <= kAsciiLen) return Status::InvalidArgument;
  out[0] = '\0';
  const Status st = query(cmd2, 2, kAsciiLen, kCmdDelayMs);
  if (st != Status::Ok) return st;
  copyAscii(raw_, rawLen_, out, cap);
  return Status::Ok;
}

Status Acd10I2c::readCalibrationMode(uint8_t& mode) {
  uint16_t v = 0;
  const Status st = readWord(kI2cCmdCalMode, v);
  if (st != Status::Ok) return st;
  if (v != kCalModeManual && v != kCalModeAuto) return Status::BadFrame;
  mode = static_cast<uint8_t>(v);
  return Status::Ok;
}

Status Acd10I2c::writeThenVerify(const uint8_t* cmd, size_t cmdLen, uint16_t expected) {
  rawLen_ = 0;
  if (!bus_.write(addr_, cmd, cmdLen)) return Status::I2cNack;
  clock_.delayMs(kCmdDelayMs);
  // Datasheet: lenh doc lai dung 2 byte dau cua lenh ghi, va phai di ngay sau lenh ghi.
  uint16_t v = 0;
  const Status st = readWord(cmd, v);
  if (st != Status::Ok) return st;
  return v == expected ? Status::Ok : Status::VerifyFailed;
}

Status Acd10I2c::setCalibrationMode(uint8_t mode) {
  uint8_t cmd[kI2cSetCmdLen];
  if (buildI2cSetCalMode(mode, cmd, sizeof(cmd)) == 0) return Status::InvalidArgument;
  return writeThenVerify(cmd, sizeof(cmd), mode);
}

Status Acd10I2c::manualCalibrate(uint16_t ppm) {
  uint8_t cmd[kI2cSetCmdLen];
  if (buildI2cManualCal(ppm, cmd, sizeof(cmd)) == 0) return Status::InvalidArgument;
  return writeThenVerify(cmd, sizeof(cmd), ppm);
}

Status Acd10I2c::factoryReset() {
  uint8_t cmd[kI2cFactoryCmdLen];
  buildI2cFactoryReset(cmd, sizeof(cmd));
  return writeThenVerify(cmd, sizeof(cmd), 0x0001);  // cam bien tra 00 01 khi khoi phuc thanh cong
}

// ======================= UART =======================

Status Acd10Uart::transact(uint8_t expectedCmd, uint8_t expectedLen, UartFrame& frame) {
  rawLen_ = 0;
  elapsedMs_ = 0;
  port_.flushInput();  // bo byte cu con sot lai de khong ghep nham khung

  if (cmdLen_ == 0) return Status::InvalidArgument;
  if (port_.write(cmd_, cmdLen_) != cmdLen_) return Status::WriteFailed;

  const uint32_t start = clock_.millis();
  UartParseResult r;
  while (static_cast<uint32_t>(clock_.millis() - start) < timeoutMs_) {
    const int b = port_.read();
    if (b < 0) {
      clock_.delayMs(1);
      continue;
    }
    if (rawLen_ == sizeof(raw_)) {  // day buffer: bo nua dau (rac), giu nua sau
      const size_t keep = sizeof(raw_) / 2;
      memmove(raw_, raw_ + (sizeof(raw_) - keep), keep);
      rawLen_ = keep;
    }
    raw_[rawLen_++] = static_cast<uint8_t>(b);
    r = parseUartResponse(raw_, rawLen_, expectedCmd, expectedLen, frame);
    if (r.status == Status::Ok) {
      elapsedMs_ = clock_.millis() - start;
      return Status::Ok;
    }
  }
  elapsedMs_ = clock_.millis() - start;
  if (rawLen_ == 0) return Status::NoResponse;
  return parseUartResponse(raw_, rawLen_, expectedCmd, expectedLen, frame).status;
}

Status Acd10Uart::readCo2(Measurement& out) {
  cmdLen_ = buildUartFrame(kUartCmdReadCo2, nullptr, 0, cmd_, sizeof(cmd_));
  UartFrame f;
  const Status st = transact(kUartCmdReadCo2, kUartLenReadCo2, f);
  if (st != Status::Ok) return st;
  return decodeUartCo2(f, out);
}

Status Acd10Uart::ackCommand(uint8_t cmd) {
  // Phan hoi xac nhan: FE A6 00 CMD CS. Bat buoc dung CMD (khong dung phuong an du phong).
  if (cmdLen_ == 0) return Status::InvalidArgument;
  UartFrame f;
  return transact(cmd, kUartAnyLen, f);
}

Status Acd10Uart::manualCalibrate(uint16_t ppm) {
  cmdLen_ = buildUartManualCal(ppm, cmd_, sizeof(cmd_));
  return ackCommand(kUartCmdManualCal);
}

Status Acd10Uart::setCalibrationMode(uint8_t mode) {
  cmdLen_ = buildUartSetCalMode(mode, cmd_, sizeof(cmd_));
  return ackCommand(kUartCmdSetCalMode);
}

Status Acd10Uart::factoryReset() {
  cmdLen_ = buildUartFrame(kUartCmdFactoryReset, nullptr, 0, cmd_, sizeof(cmd_));
  return ackCommand(kUartCmdFactoryReset);
}

Status Acd10Uart::readAscii(uint8_t cmd, uint8_t expectedLen, char* out, size_t cap) {
  if (out == nullptr || cap <= kAsciiLen) return Status::InvalidArgument;
  out[0] = '\0';
  cmdLen_ = buildUartFrame(cmd, nullptr, 0, cmd_, sizeof(cmd_));
  UartFrame f;
  const Status st = transact(cmd, expectedLen, f);
  if (st != Status::Ok) return st;
  copyAscii(f.data, f.len < kAsciiLen ? f.len : kAsciiLen, out, cap);
  return Status::Ok;
}

Status Acd10Uart::readVersion(char* out, size_t cap) { return readAscii(kUartCmdVersion, kUartLenVersion, out, cap); }

Status Acd10Uart::readSerial(char* out, size_t cap) { return readAscii(kUartCmdSerial, kUartLenSerial, out, cap); }

}  // namespace acd10
