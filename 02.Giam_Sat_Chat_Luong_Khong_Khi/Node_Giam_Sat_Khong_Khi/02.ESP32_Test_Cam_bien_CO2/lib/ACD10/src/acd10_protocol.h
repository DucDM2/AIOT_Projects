// Giao thuc cam bien CO2 ASAIR (Aosong) ACD10 - phan thuan C++, khong phu thuoc Arduino.
// Theo datasheet ACD10 ban tieng Trung 04/2022 (ban day du I2C + UART; ban tieng Anh 07/2021 chi co UART):
//   I2C  : dia chi 7-bit 0x2A, toi da 100 kHz. Moi word 16-bit kem 1 byte CRC8 (poly 0x31, init 0xFF)
//          Doc CO2  : ghi 03 00 -> doc 9 byte: PPM3 PPM2 CRC  PPM1 PPM0 CRC  TEMP1 TEMP0 CRC
//                     CO2 (ppm) = PPM3..PPM0 (32-bit, big-endian), nhiet do = TEMP1*256+TEMP0 (gia tri tho)
//          Che do hieu chuan : ghi 53 06 00 MODE CRC (0 = thu cong, 1 = tu dong) | doc: ghi 53 06 -> 3 byte
//          Hieu chuan 1 diem : ghi 52 04 HI LO CRC (ppm)                       | doc: ghi 52 04 -> 3 byte
//          Khoi phuc goc     : ghi 52 02 00                                     | doc: ghi 52 02 -> 00 01 CRC
//          Phien ban phan mem: ghi D1 00 -> 10 ky tu ASCII | So serial: ghi D2 01 -> 10 ky tu ASCII
//   UART : 1200 baud, 8N1. Khung FE A6 LEN CMD DATA.. CS ; CS = byte thap cua (A6+LEN+CMD+DATA)
//          Doc CO2  : FE A6 00 01 A7          -> FE A6 04 01 D1 D2 D3 D4 CS ; CO2 = D1*256+D2
//          Hieu chuan 1 diem : FE A6 02 03 D1 D2 CS -> FE A6 00 03 A9
//          Che do hieu chuan : FE A6 02 04 00 D1 CS -> FE A6 00 04 AA
//          Khoi phuc goc     : FE A6 00 05 AB       -> FE A6 00 05 AB
//          Phien ban : FE A6 00 1E C4 -> FE A6 0B 1E D1..D11 CS | So serial: FE A6 00 1F C5 -> FE A6 0A 1F D1..D10 CS
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace acd10 {

// ---------------- Thong so chung ----------------
constexpr uint32_t kMinPpm = 400;        // dai do theo datasheet
constexpr uint32_t kMaxPpm = 5000;
constexpr uint32_t kPreheatMs = 120000;  // thoi gian lam nong sau khi cap nguon
constexpr uint32_t kRefreshMs = 2000;    // chu ky cap nhat du lieu (ban 2022)
constexpr uint8_t kCalModeManual = 0;
constexpr uint8_t kCalModeAuto = 1;      // mac dinh xuat xuong
constexpr size_t kAsciiLen = 10;         // do dai chuoi phien ban / so serial

// ---------------- UART ----------------
constexpr uint32_t kUartBaud = 1200;
constexpr uint8_t kUartHeader = 0xFE;
constexpr uint8_t kUartFixedCode = 0xA6;
constexpr uint8_t kUartCmdReadCo2 = 0x01;
constexpr uint8_t kUartCmdManualCal = 0x03;
constexpr uint8_t kUartCmdSetCalMode = 0x04;
constexpr uint8_t kUartCmdFactoryReset = 0x05;
constexpr uint8_t kUartCmdVersion = 0x1E;
constexpr uint8_t kUartCmdSerial = 0x1F;
constexpr uint8_t kUartLenReadCo2 = 4;   // so byte data trong phan hoi
constexpr uint8_t kUartLenVersion = 11;
constexpr uint8_t kUartLenSerial = 10;
constexpr uint8_t kUartLenAck = 0;
constexpr uint8_t kUartAnyLen = 0xFF;    // khong kiem tra do dai mong doi
constexpr size_t kUartMaxData = 16;      // lon nhat trong datasheet la 11; LEN > 16 chac chan la rac
constexpr size_t kUartOverhead = 5;      // FE A6 LEN CMD ... CS
constexpr size_t kUartMaxFrame = kUartOverhead + kUartMaxData;

// ---------------- I2C ----------------
constexpr uint8_t kI2cAddress = 0x2A;
constexpr uint32_t kI2cMaxFreqHz = 100000;
constexpr uint8_t kI2cCmdReadCo2[] = {0x03, 0x00};
constexpr uint8_t kI2cCmdCalMode[] = {0x53, 0x06};
constexpr uint8_t kI2cCmdManualCal[] = {0x52, 0x04};
constexpr uint8_t kI2cCmdFactoryReset[] = {0x52, 0x02};
constexpr uint8_t kI2cCmdVersion[] = {0xD1, 0x00};
constexpr uint8_t kI2cCmdSerial[] = {0xD2, 0x01};
constexpr size_t kI2cCo2DataLen = 9;     // 3 word x (2 byte + 1 CRC)
constexpr size_t kI2cWordLen = 3;        // 2 byte + CRC
constexpr size_t kI2cSetCmdLen = 5;      // CMD_H CMD_L DATA_H DATA_L CRC
constexpr size_t kI2cFactoryCmdLen = 3;  // 52 02 00

enum class Status : uint8_t {
  Ok = 0,
  NoResponse,        // khong nhan duoc byte nao
  Incomplete,        // co dau khung nhung thieu byte
  BadChecksum,       // UART checksum sai
  BadFrame,          // co du lieu nhung khong tim thay khung hop le
  CrcError,          // I2C CRC sai
  ShortRead,         // I2C doc thieu byte
  I2cNack,           // cam bien khong ACK tren I2C
  WriteFailed,       // gui lenh that bai
  VerifyFailed,      // doc lai sau khi ghi khong khop
  InvalidArgument,
  Count_             // so luong trang thai (dung cho thong ke)
};

const char* statusToString(Status s);

// Canh bao: du lieu van hop le ve mat giao thuc nhung can chu y.
enum Warning : uint8_t {
  kWarnNone = 0,
  kWarnOutOfRange = 1 << 0,      // CO2 ngoai dai 400~5000 ppm (binh thuong trong luc lam nong)
  kWarnLengthMismatch = 1 << 1,  // UART: byte LEN khac datasheet
  kWarnCmdMismatch = 1 << 2,     // UART: byte CMD trong phan hoi khac lenh da gui
};

struct Measurement {
  uint32_t co2_ppm = 0;
  uint16_t temp_raw = 0;   // chi co o I2C; datasheet khong cho cong thuc doi ra do C
  bool has_temp = false;
  uint8_t warnings = kWarnNone;
};

// CRC8 theo datasheet: init 0xFF, poly 0x31 (x8 + x5 + x4 + 1). Vi du: CRC(00 00) = 0x81
uint8_t crc8(const uint8_t* data, size_t len);

// Checksum UART: byte thap cua tong cac byte
uint8_t sumLow8(const uint8_t* data, size_t len);

// Kiem tra tinh hop ly, tra ve co canh bao (kWarnOutOfRange).
uint8_t checkPlausibility(const Measurement& m);

// ---------------- UART ----------------

// Tao khung lenh UART: FE A6 n cmd data[n] CS. Tra ve so byte da ghi, 0 neu loi.
size_t buildUartFrame(uint8_t cmd, const uint8_t* data, size_t n, uint8_t* out, size_t cap);
size_t buildUartManualCal(uint16_t ppm, uint8_t* out, size_t cap);
size_t buildUartSetCalMode(uint8_t mode, uint8_t* out, size_t cap);

struct UartFrame {
  uint8_t cmd = 0;
  uint8_t len = 0;
  uint8_t data[kUartMaxData] = {0};
};

struct UartParseResult {
  Status status = Status::NoResponse;
  size_t frameStart = 0;     // vi tri byte FE cua khung hop le
  size_t frameLen = 0;       // do dai khung hop le
  bool cmdMismatch = false;  // khung hop le nhung byte CMD khac lenh da gui
};

// Tim khung phan hoi trong buf.
//   expectedCmd : lenh da gui. Khung co CMD trung duoc uu tien.
//   expectedLen : so byte data mong doi (kUartAnyLen = bo qua). Neu khong co khung trung CMD,
//                 chap nhan khung dung checksum co dung expectedLen (cmdMismatch = true).
// `out` chi bi ghi khi status == Ok.
UartParseResult parseUartResponse(const uint8_t* buf, size_t len, uint8_t expectedCmd, uint8_t expectedLen,
                                  UartFrame& out);

// Giai ma khung phan hoi lenh doc CO2. `out` chi bi ghi khi tra ve Ok.
Status decodeUartCo2(const UartFrame& f, Measurement& out);

// ---------------- I2C ----------------

// Lenh ghi 2 byte data kem CRC: CMD_H CMD_L HI LO CRC(HI LO)
size_t buildI2cSetCalMode(uint8_t mode, uint8_t* out, size_t cap);
size_t buildI2cManualCal(uint16_t ppm, uint8_t* out, size_t cap);
size_t buildI2cFactoryReset(uint8_t* out, size_t cap);

// Giai ma 9 byte du lieu doc CO2. `out` chi bi ghi khi tra ve Ok.
Status decodeI2cCo2(const uint8_t* buf, size_t len, Measurement& out);

// Giai ma 1 word (2 byte + CRC). `value` chi bi ghi khi tra ve Ok.
Status decodeI2cWord(const uint8_t* buf, size_t len, uint16_t& value);

// Chep chuoi ASCII tu cam bien ra chuoi C: ky tu khong in duoc -> '.', bo NUL/khoang trang cuoi.
// Luon ket thuc bang '\0' (neu cap > 0). Tra ve do dai chuoi.
size_t copyAscii(const uint8_t* src, size_t n, char* dst, size_t cap);

}  // namespace acd10
