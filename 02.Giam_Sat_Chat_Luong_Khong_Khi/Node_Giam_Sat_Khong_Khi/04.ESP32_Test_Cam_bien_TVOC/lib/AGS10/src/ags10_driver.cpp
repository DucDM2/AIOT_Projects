#include "ags10_driver.h"

namespace ags10 {

// Cho den khi du khoang cach toi thieu ke tu lenh truoc (dung phep tru khong dau -> an toan khi millis() tran).
void Ags10::waitGap(bool acquisition) {
  uint32_t wait = 0;
  const uint32_t now = clock_.millis();
  if (hasBusTime_) {
    const uint32_t since = now - lastBusMs_;
    if (since < kCommandGapMs) wait = kCommandGapMs - since;
  }
  if (acquisition && hasAcqTime_) {
    const uint32_t since = now - lastAcqMs_;
    if (since < kAcquisitionGapMs && kAcquisitionGapMs - since > wait) wait = kAcquisitionGapMs - since;
  }
  if (wait > 0) {
    clock_.delayMs(wait);
    waitMs_ += wait;
  }
}

bool Ags10::probe() {
  waitGap(false);
  const bool ack = bus_.write(addr_, nullptr, 0);
  lastBusMs_ = clock_.millis();
  hasBusTime_ = true;
  return ack;
}

Status Ags10::readRegister(uint8_t reg, bool acquisition) {
  rawLen_ = 0;
  waitMs_ = 0;
  waitGap(acquisition);
  const bool ack = bus_.write(addr_, &reg, 1);
  lastBusMs_ = clock_.millis();
  hasBusTime_ = true;
  if (acquisition) {
    lastAcqMs_ = lastBusMs_;
    hasAcqTime_ = true;
  }
  if (!ack) return Status::I2cNack;

  waitGap(false);  // datasheet: 30 ms giua lenh ghi thanh ghi va lenh doc
  rawLen_ = bus_.read(addr_, raw_, kFrameLen);
  lastBusMs_ = clock_.millis();
  if (rawLen_ == 0) return Status::NoResponse;
  return checkFrame(raw_, rawLen_);
}

Status Ags10::readTvoc(TvocReading& out) {
  const Status st = readRegister(kRegTvoc, true);
  if (st != Status::Ok) return st;
  return decodeTvoc(raw_, rawLen_, out);
}

Status Ags10::readResistance(uint32_t& out) {
  const Status st = readRegister(kRegResistance, true);
  if (st != Status::Ok) return st;
  return decodeResistance(raw_, rawLen_, out);
}

Status Ags10::readVersion(uint8_t& out) {
  const Status st = readRegister(kRegVersion, false);
  if (st != Status::Ok) return st;
  return decodeVersion(raw_, rawLen_, out);
}

Status Ags10::writeRegister(uint8_t reg, const uint8_t* data, size_t len) {
  uint8_t buf[1 + kFrameLen];
  if (data == nullptr || len != kFrameLen) return Status::InvalidArgument;
  buf[0] = reg;
  for (size_t i = 0; i < len; ++i) buf[1 + i] = data[i];
  waitMs_ = 0;
  waitGap(false);
  const bool ack = bus_.write(addr_, buf, sizeof(buf));
  lastBusMs_ = clock_.millis();  // lenh tiep theo tu cho 30 ms (thoi gian xu ly cua cam bien)
  hasBusTime_ = true;
  return ack ? Status::Ok : Status::I2cNack;
}

Status Ags10::writeZeroPoint(uint16_t value) {
  uint8_t data[kFrameLen];
  if (buildZeroPointData(value, data, sizeof(data)) != kFrameLen) return Status::InvalidArgument;
  return writeRegister(kRegCalibration, data, sizeof(data));
}

Status Ags10::setAddress(uint8_t newAddr) {
  uint8_t data[kFrameLen];
  if (buildAddressData(newAddr, data, sizeof(data)) != kFrameLen) return Status::InvalidArgument;
  const Status st = writeRegister(kRegAddress, data, sizeof(data));
  if (st == Status::Ok) addr_ = newAddr;
  return st;
}

}  // namespace ags10
