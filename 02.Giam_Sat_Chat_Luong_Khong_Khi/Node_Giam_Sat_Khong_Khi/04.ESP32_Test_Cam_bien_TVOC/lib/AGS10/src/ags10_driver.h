// Driver cam bien TVOC AGS10 (I2C).
// Driver tu dam bao rang buoc thoi gian cua datasheet, nguoi goi khong can tu delay:
//   - moi lenh cach lenh truoc >= 30 ms (ke ca giua ghi thanh ghi va doc du lieu)
//   - doc TVOC / dien tro cach lan truoc >= 1.5 s (doc nhanh hon -> RDY luon = 1, khong lay duoc du lieu)
#pragma once

#include "ags10_hal.h"
#include "ags10_protocol.h"

namespace ags10 {

class Ags10 {
 public:
  Ags10(II2cBus& bus, IClock& clock, uint8_t addr = kDefaultAddress)
      : bus_(bus), clock_(clock), addr_(addr) {}

  uint8_t address() const { return addr_; }

  bool probe();  // cam bien co ACK dia chi khong

  // Doc TVOC (thanh ghi 0x00). Ok: du lieu hop le. NotReady: dang lam nong, `out` chi tham khao.
  Status readTvoc(TvocReading& out);
  // Dien tro cam bien (thanh ghi 0x20), don vi 0.1 kOhm
  Status readResistance(uint32_t& out);
  // Phien ban firmware (thanh ghi 0x11)
  Status readVersion(uint8_t& out);

  // Hieu chuan diem 0 (mat khi mat nguon). Chi lam sau >= 15 phut dat cam bien trong khong khi sach.
  Status zeroPointCalibrateCurrent() { return writeZeroPoint(kZeroPointCurrentResistance); }
  Status zeroPointFactoryReset() { return writeZeroPoint(kZeroPointFactoryReset); }
  // Dat dien tro diem 0 tuy chon (don vi 0.1 kOhm). 0x0000 va 0xFFFF la 2 lenh dac biet o tren.
  Status zeroPointSet(uint16_t value01kOhm) { return writeZeroPoint(value01kOhm); }

  // Doi dia chi I2C (luu vinh vien trong cam bien, co hieu luc ngay). Thanh cong -> driver dung dia chi moi.
  Status setAddress(uint8_t newAddr);

  // Debug: du lieu tho lan doc gan nhat va thoi gian driver da phai cho de dung rang buoc thoi gian
  const uint8_t* lastRaw() const { return raw_; }
  size_t lastRawLen() const { return rawLen_; }
  uint32_t lastWaitMs() const { return waitMs_; }

 private:
  void waitGap(bool acquisition);
  Status readRegister(uint8_t reg, bool acquisition);
  Status writeRegister(uint8_t reg, const uint8_t* data, size_t len);
  Status writeZeroPoint(uint16_t value);

  II2cBus& bus_;
  IClock& clock_;
  uint8_t addr_;
  uint8_t raw_[kFrameLen] = {0};
  size_t rawLen_ = 0;
  uint32_t waitMs_ = 0;
  bool hasBusTime_ = false;
  uint32_t lastBusMs_ = 0;  // thoi diem ket thuc giao dich I2C gan nhat
  bool hasAcqTime_ = false;
  uint32_t lastAcqMs_ = 0;  // thoi diem gui lenh doc TVOC / dien tro gan nhat
};

}  // namespace ags10
