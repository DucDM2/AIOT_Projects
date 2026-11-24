// Giao thuc cam bien TVOC ASAIR (Aosong) AGS10 - phan thuan C++, khong phu thuoc Arduino.
// Theo datasheet AGS10 (Aosong, 06/2022):
//   I2C slave, dia chi 7-bit 0x1A (byte ghi/doc 0x34/0x35), toc do <= 15 kHz, keo len 2k~10k.
//   Doc thanh ghi : S 0x34 Reg P  ->  (cho 30 ms)  ->  S 0x35 Data1..Data4 CRC P
//   Ghi thanh ghi : S 0x34 Reg Data1..Data4 CRC P
//   CRC8 tren Data1..Data4: init 0xFF, poly 0x31, khong dao bit, khong XOR cuoi.
//
//   Thanh ghi  Lenh                          Tra ve        Thoi gian xu ly
//   0x00       Doc TVOC                      5 byte        1500 ms (khong doc nhanh hon 1.5 s)
//   0x01       Hieu chuan diem 0 (ghi)       -             30 ms
//   0x11       Doc phien ban firmware        5 byte        30 ms
//   0x20       Doc dien tro cam bien         5 byte        1500 ms
//   0x21       Doi dia chi I2C (ghi)         -             30 ms
//   Giua 2 lenh ghi/doc lien tiep phai cach nhau toi thieu 30 ms.
//
//   Du lieu TVOC: Data1 = STATUS, Data2..Data4 = TVOC (ppb, big-endian 24 bit)
//     STATUS bit0   RDY : 0 = du lieu san sang, 1 = dang lam nong / chua san sang
//     STATUS bit3:1 CH  : don vi, 000 = ppb (gia tri khac khong duoc datasheet dinh nghia)
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ags10 {

constexpr uint8_t kDefaultAddress = 0x1A;
constexpr uint32_t kMaxI2cFreqHz = 15000;

constexpr uint8_t kRegTvoc = 0x00;
constexpr uint8_t kRegCalibration = 0x01;
constexpr uint8_t kRegVersion = 0x11;
constexpr uint8_t kRegResistance = 0x20;
constexpr uint8_t kRegAddress = 0x21;

constexpr size_t kFrameLen = 5;  // 4 byte data + 1 byte CRC (moi lenh doc va ghi)

constexpr uint32_t kCommandGapMs = 30;        // khoang cach toi thieu giua 2 lenh
constexpr uint32_t kAcquisitionGapMs = 1500;  // khoang cach toi thieu giua 2 lan doc TVOC / dien tro
constexpr uint32_t kSamplingPeriodMs = 2000;  // chu ky lay mau khuyen dung (>= 2 s)
constexpr uint32_t kPreheatMs = 120000;       // thoi gian lam nong sau khi cap nguon

constexpr uint32_t kMaxTvocPpb = 99999;  // dai do

constexpr uint8_t kStatusRdyMask = 0x01;
constexpr uint8_t kStatusUnitMask = 0x0E;
constexpr uint8_t kStatusUnitPpb = 0x00;

// Gia tri Data3/Data4 cua lenh hieu chuan diem 0
constexpr uint16_t kZeroPointFactoryReset = 0xFFFF;   // ve diem 0 cua nha san xuat
constexpr uint16_t kZeroPointCurrentResistance = 0x0000;  // lay dien tro hien tai lam diem 0

enum class Status : uint8_t {
  Ok = 0,
  NotReady,         // CRC dung nhung bit RDY = 1 (dang lam nong hoac doc qua day)
  NoResponse,       // cam bien khong tra byte nao
  ShortRead,        // doc thieu byte
  CrcError,         // CRC sai
  I2cNack,          // cam bien khong ACK khi ghi
  UnsupportedUnit,  // truong CH trong STATUS khac ppb
  InvalidArgument,
  Count_            // so luong trang thai (dung cho thong ke)
};

const char* statusToString(Status s);

// Canh bao: du lieu hop le ve mat giao thuc nhung can chu y.
enum Warning : uint8_t {
  kWarnNone = 0,
  kWarnOutOfRange = 1 << 0,  // TVOC > 99999 ppb
};

struct TvocReading {
  uint32_t tvocPpb = 0;
  uint8_t status = 0;  // byte STATUS nguyen goc
  bool ready = false;  // RDY = 0
  uint8_t warnings = kWarnNone;
};

// CRC8 theo datasheet: init 0xFF, poly 0x31 (x8 + x5 + x4 + 1)
uint8_t crc8(const uint8_t* data, size_t len);

// Kiem tra khung 5 byte (do dai + CRC). Tra ve Ok / ShortRead / CrcError / InvalidArgument.
Status checkFrame(const uint8_t* buf, size_t len);

// Giai ma khung TVOC. `out` duoc ghi khi tra ve Ok hoac NotReady (gia tri luc lam nong chi tham khao).
Status decodeTvoc(const uint8_t* buf, size_t len, TvocReading& out);

// Dien tro cam bien, don vi 0.1 kOhm. `out` chi bi ghi khi tra ve Ok.
Status decodeResistance(const uint8_t* buf, size_t len, uint32_t& out);

// Phien ban firmware (byte Data4). `out` chi bi ghi khi tra ve Ok.
Status decodeVersion(const uint8_t* buf, size_t len, uint8_t& out);

// Tao 5 byte du lieu cho lenh hieu chuan diem 0: 00 0C hi lo CRC. Tra ve so byte (5) hoac 0 neu loi.
size_t buildZeroPointData(uint16_t value, uint8_t* out, size_t cap);

// Tao 5 byte du lieu cho lenh doi dia chi: New ~New New ~New CRC.
// Chi chap nhan dia chi 7-bit thong thuong 0x08..0x77. Tra ve 5 hoac 0 neu loi.
size_t buildAddressData(uint8_t newAddr, uint8_t* out, size_t cap);

}  // namespace ags10
