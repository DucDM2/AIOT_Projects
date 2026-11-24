#include "apm10_driver.h"

#include <string.h>

namespace apm10 {

Status Apm10Uart::transact(uint8_t cmd, Measurement& out) {
  rawLen_ = 0;
  elapsedMs_ = 0;
  port_.flushInput();  // bo byte cu con sot lai de khong ghep nham khung

  if (buildUartCommand(cmd, cmd_, sizeof(cmd_)) != kUartCommandLen) return Status::InvalidArgument;
  if (port_.write(cmd_, kUartCommandLen) != kUartCommandLen) return Status::WriteFailed;

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
    r = parseUartResponse(raw_, rawLen_, cmd, false, out);
    if (r.status == Status::Ok) {
      elapsedMs_ = clock_.millis() - start;
      return Status::Ok;
    }
  }
  elapsedMs_ = clock_.millis() - start;
  if (rawLen_ == 0) return Status::NoResponse;
  // Het thoi gian: thu lan cuoi, cho phep dung byte LEN lam phuong an du phong
  r = parseUartResponse(raw_, rawLen_, cmd, true, out);
  return r.status;
}

bool Apm10I2c::probe() { return bus_.write(addr_, nullptr, 0); }

Status Apm10I2c::startMeasurement() {
  return bus_.write(addr_, kI2cCmdStart, sizeof(kI2cCmdStart)) ? Status::Ok : Status::I2cNack;
}

Status Apm10I2c::stopMeasurement() {
  return bus_.write(addr_, kI2cCmdStop, sizeof(kI2cCmdStop)) ? Status::Ok : Status::I2cNack;
}

Status Apm10I2c::read(Measurement& out) {
  rawLen_ = 0;
  if (!bus_.write(addr_, kI2cCmdRead, sizeof(kI2cCmdRead))) return Status::I2cNack;
  if (readDelayMs_ > 0) clock_.delayMs(readDelayMs_);
  rawLen_ = bus_.read(addr_, raw_, kI2cDataLen);
  if (rawLen_ == 0) return Status::NoResponse;
  return decodeI2cData(raw_, rawLen_, out);
}

}  // namespace apm10
