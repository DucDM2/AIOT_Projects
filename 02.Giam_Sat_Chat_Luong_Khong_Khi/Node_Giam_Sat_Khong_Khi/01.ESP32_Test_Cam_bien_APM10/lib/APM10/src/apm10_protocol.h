// Giao thuc cam bien bui ASAIR (Aosong) APM10 - phan thuan C++, khong phu thuoc Arduino.
// Theo datasheet APM10 (Aosong, 07/2022 ban EN va 07/2021 ban CN):
//   UART : 1200 baud, 8N1. Lenh  FE A5 LEN CMD DATA.. CS ; CS = byte thap cua (A5+LEN+CMD+DATA)
//          CMD 0x00 -> doc PM2.5           : gui FE A5 00 00 A5
//          CMD 0x01 -> doc PM1.0/PM2.5/PM10: gui FE A5 00 01 A6
//   I2C  : dia chi 7-bit 0x08, moi word 16-bit kem 1 byte CRC8 (poly 0x31, init 0xFF)
//          Start : 00 10 05 00 F6 | Stop : 01 04 | Read : 03 00 roi doc 30 byte
//
// Hai cho datasheet ghi mau thuan, parser xu ly theo huong chiu loi:
//   1. Phan hoi UART cho CMD 0x01 ghi "FE A5 02 00 DF11..DF32 CS" (byte LEN = 02 nhung co 6 byte data).
//      -> Parser uu tien do dai mong doi theo lenh da gui, dung checksum de xac nhan khung.
//   2. Bang du lieu I2C ban EN ghi byte 9~11 la "PM1.0" (lap lai), ban CN ghi chi co PM2.5.
//      -> Doc word0 = PM1.0, word1 = PM2.5, word3 = PM10 (thu tu chuan PM1/PM2.5/PM4/PM10).
//         Che do UART la che do duoc khuyen dung de doc du 3 thong so.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace apm10 {

// ---------------- UART ----------------
constexpr uint32_t kUartBaud = 1200;
constexpr uint8_t kUartHeader = 0xFE;
constexpr uint8_t kUartFixedCode = 0xA5;
constexpr uint8_t kCmdReadPm25 = 0x00;
constexpr uint8_t kCmdReadAll = 0x01;
constexpr size_t kUartCommandLen = 5;
constexpr size_t kUartMaxFrameLen = 4 + 6 + 1;  // FE A5 LEN CMD + 6 data + CS

// ---------------- I2C ----------------
constexpr uint8_t kI2cAddress = 0x08;
constexpr size_t kI2cDataLen = 30;  // 10 word x (2 byte + 1 CRC)
constexpr size_t kI2cWordCount = kI2cDataLen / 3;
constexpr uint8_t kI2cCmdStart[] = {0x00, 0x10, 0x05, 0x00, 0xF6};
constexpr uint8_t kI2cCmdStop[] = {0x01, 0x04};
constexpr uint8_t kI2cCmdRead[] = {0x03, 0x00};

// Dai do theo datasheet: 0 ~ 1000 ug/m3
constexpr uint16_t kMaxConcentration = 1000;

enum class Status : uint8_t {
  Ok = 0,
  NoResponse,       // khong nhan duoc byte nao
  Incomplete,       // co dau khung nhung thieu byte
  BadChecksum,      // UART checksum sai
  BadFrame,         // co du lieu nhung khong tim thay khung hop le
  CrcError,         // I2C CRC sai o word PM
  ShortRead,        // I2C doc thieu byte
  I2cNack,          // cam bien khong ACK tren I2C
  WriteFailed,      // gui lenh that bai
  InvalidArgument,
  Count_            // so luong trang thai (dung cho thong ke)
};

const char* statusToString(Status s);

// Canh bao: du lieu van hop le ve mat giao thuc nhung can chu y.
enum Warning : uint8_t {
  kWarnNone = 0,
  kWarnOutOfRange = 1 << 0,      // gia tri > 1000 ug/m3
  kWarnOrder = 1 << 1,           // PM1.0 > PM2.5 hoac PM2.5 > PM10 (vo ly ve vat ly)
  kWarnLengthMismatch = 1 << 2,  // byte LEN UART khac so byte data thuc te
  kWarnReservedCrc = 1 << 3,     // I2C: word du phong sai CRC (khong anh huong PM)
  kWarnPm25Only = 1 << 4,        // chi nhan duoc PM2.5 khi da yeu cau ca 3 thong so
};

struct Measurement {
  uint16_t pm1_0 = 0;  // ug/m3
  uint16_t pm2_5 = 0;  // ug/m3
  uint16_t pm10 = 0;   // ug/m3
  bool has_pm1_0 = false;
  bool has_pm2_5 = false;
  bool has_pm10 = false;
  uint8_t warnings = kWarnNone;
};

// CRC8 theo datasheet: init 0xFF, poly 0x31 (x8 + x5 + x4 + 1)
uint8_t crc8(const uint8_t* data, size_t len);

// Checksum UART: byte thap cua tong cac byte
uint8_t sumLow8(const uint8_t* data, size_t len);

// Tao khung lenh UART (5 byte). Tra ve so byte da ghi, 0 neu cap < 5.
size_t buildUartCommand(uint8_t cmd, uint8_t* out, size_t cap);

struct UartParseResult {
  Status status = Status::NoResponse;
  size_t frameStart = 0;  // vi tri byte FE cua khung hop le
  size_t frameLen = 0;    // do dai khung hop le
};

// Tim va giai ma khung phan hoi trong buf.
//   requestedCmd : lenh da gui (kCmdReadPm25 / kCmdReadAll)
//   final        : true khi da het thoi gian cho (cho phep dung byte LEN lam phuong an du phong)
// `out` chi bi ghi khi status == Ok.
UartParseResult parseUartResponse(const uint8_t* buf, size_t len, uint8_t requestedCmd, bool final,
                                  Measurement& out);

// Giai ma 30 byte du lieu I2C. `out` chi bi ghi khi tra ve Ok.
Status decodeI2cData(const uint8_t* buf, size_t len, Measurement& out);

// Kiem tra tinh hop ly, tra ve co canh bao (kWarnOutOfRange / kWarnOrder).
uint8_t checkPlausibility(const Measurement& m);

}  // namespace apm10
