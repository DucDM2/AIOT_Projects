// Unit test phan giao thuc ACD10: CRC8, byte lenh, khung UART, giai ma I2C, phan muc CO2.
// Chay tren may tinh: pio test -e native -f test_protocol
// Chay tren ESP32  : pio test -e esp32_i2c -f test_protocol
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include "acd10_protocol.h"
#include "co2_level.h"

using namespace acd10;

void setUp() {}
void tearDown() {}

// ---------- Ham tien ich ----------

// Tao khung phan hoi UART: FE A6 lenByte cmdByte data[n] CS
static size_t makeUartFrame(uint8_t lenByte, uint8_t cmdByte, const uint8_t* data, size_t n, uint8_t* out) {
  out[0] = 0xFE;
  out[1] = 0xA6;
  out[2] = lenByte;
  out[3] = cmdByte;
  if (n) memcpy(&out[4], data, n);
  out[4 + n] = sumLow8(&out[1], 3 + n);
  return 5 + n;
}

static size_t makeCo2Frame(uint16_t ppm, uint8_t* out, uint8_t cmdByte = 0x01) {
  const uint8_t d[4] = {(uint8_t)(ppm >> 8), (uint8_t)ppm, 0x00, 0x00};
  return makeUartFrame(4, cmdByte, d, 4, out);
}

// Tao 9 byte du lieu I2C doc CO2
static void makeI2cCo2(uint32_t ppm, uint16_t temp, uint8_t* out) {
  const uint16_t words[3] = {(uint16_t)(ppm >> 16), (uint16_t)ppm, temp};
  for (int w = 0; w < 3; ++w) {
    out[w * 3] = (uint8_t)(words[w] >> 8);
    out[w * 3 + 1] = (uint8_t)words[w];
    out[w * 3 + 2] = crc8(&out[w * 3], 2);
  }
}

static Measurement sentinel() {
  Measurement m;
  m.co2_ppm = 0xDEADBEEF;
  m.temp_raw = 0xCAFE;
  m.has_temp = true;
  m.warnings = 0x80;
  return m;
}

static void assertUntouched(const Measurement& m) {
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, m.co2_ppm);
  TEST_ASSERT_EQUAL_HEX16(0xCAFE, m.temp_raw);
  TEST_ASSERT_EQUAL_HEX8(0x80, m.warnings);
}

static UartFrame sentinelFrame() {
  UartFrame f;
  f.cmd = 0x77;
  f.len = 0x77;
  memset(f.data, 0x77, sizeof(f.data));
  return f;
}

// ---------- CRC8 (vector lay tu datasheet 04/2022) ----------

void test_crc8_datasheet_example_0000() {
  const uint8_t d[2] = {0x00, 0x00};
  TEST_ASSERT_EQUAL_HEX8(0x81, crc8(d, 2));  // bang 3: CRC(0x0000) = 0x81
}

void test_crc8_datasheet_manual_cal_450ppm() {
  const uint8_t d[2] = {0x01, 0xC2};  // bang 10: 52 04 01 C2 50
  TEST_ASSERT_EQUAL_HEX8(0x50, crc8(d, 2));
}

void test_crc8_datasheet_factory_result() {
  const uint8_t d[2] = {0x00, 0x01};  // bang 15: 00 01 B0
  TEST_ASSERT_EQUAL_HEX8(0xB0, crc8(d, 2));
}

void test_crc8_reference_vector_and_empty() {
  const uint8_t d[2] = {0xBE, 0xEF};  // vector chuan CRC-8 poly 0x31 init 0xFF
  TEST_ASSERT_EQUAL_HEX8(0x92, crc8(d, 2));
  TEST_ASSERT_EQUAL_HEX8(0xFF, crc8(nullptr, 0));
}

// ---------- Byte lenh I2C ----------

void test_i2c_address_and_read_commands() {
  TEST_ASSERT_EQUAL_HEX8(0x2A, kI2cAddress);
  TEST_ASSERT_EQUAL_HEX8(0x54, kI2cAddress << 1);  // datasheet ghi dia chi ghi 0x54
  const uint8_t co2[] = {0x03, 0x00}, ver[] = {0xD1, 0x00}, sn[] = {0xD2, 0x01};
  const uint8_t mode[] = {0x53, 0x06}, cal[] = {0x52, 0x04}, fac[] = {0x52, 0x02};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(co2, kI2cCmdReadCo2, 2);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(ver, kI2cCmdVersion, 2);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(sn, kI2cCmdSerial, 2);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(mode, kI2cCmdCalMode, 2);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(cal, kI2cCmdManualCal, 2);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(fac, kI2cCmdFactoryReset, 2);
  TEST_ASSERT_EQUAL(9, kI2cCo2DataLen);
}

void test_i2c_build_set_cal_mode_matches_datasheet() {
  uint8_t out[8];
  const uint8_t manual[] = {0x53, 0x06, 0x00, 0x00, 0x81};  // bang 7
  TEST_ASSERT_EQUAL(5, buildI2cSetCalMode(kCalModeManual, out, sizeof(out)));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(manual, out, 5);
  const uint8_t autoMode[] = {0x53, 0x06, 0x00, 0x01, 0xB0};
  TEST_ASSERT_EQUAL(5, buildI2cSetCalMode(kCalModeAuto, out, sizeof(out)));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(autoMode, out, 5);
  TEST_ASSERT_EQUAL(0, buildI2cSetCalMode(2, out, sizeof(out)));
  TEST_ASSERT_EQUAL(0, buildI2cSetCalMode(kCalModeAuto, out, 4));
  TEST_ASSERT_EQUAL(0, buildI2cSetCalMode(kCalModeAuto, nullptr, 8));
}

void test_i2c_build_manual_cal_matches_datasheet() {
  uint8_t out[8];
  const uint8_t ds[] = {0x52, 0x04, 0x01, 0xC2, 0x50};  // bang 10: 450 ppm
  TEST_ASSERT_EQUAL(5, buildI2cManualCal(450, out, sizeof(out)));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(ds, out, 5);
  TEST_ASSERT_EQUAL(5, buildI2cManualCal(400, out, sizeof(out)));
  TEST_ASSERT_EQUAL(5, buildI2cManualCal(5000, out, sizeof(out)));
  TEST_ASSERT_EQUAL(0, buildI2cManualCal(399, out, sizeof(out)));
  TEST_ASSERT_EQUAL(0, buildI2cManualCal(5001, out, sizeof(out)));
  TEST_ASSERT_EQUAL(0, buildI2cManualCal(0, out, sizeof(out)));
}

void test_i2c_build_factory_reset() {
  uint8_t out[4];
  const uint8_t ds[] = {0x52, 0x02, 0x00};  // bang 13
  TEST_ASSERT_EQUAL(3, buildI2cFactoryReset(out, sizeof(out)));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(ds, out, 3);
  TEST_ASSERT_EQUAL(0, buildI2cFactoryReset(out, 2));
}

// ---------- Byte lenh UART ----------

void test_uart_settings() {
  TEST_ASSERT_EQUAL_UINT32(1200, kUartBaud);
  TEST_ASSERT_EQUAL_HEX8(0xFE, kUartHeader);
  TEST_ASSERT_EQUAL_HEX8(0xA6, kUartFixedCode);
}

void test_uart_build_commands_match_datasheet() {
  uint8_t out[kUartMaxFrame];
  const uint8_t readCo2[] = {0xFE, 0xA6, 0x00, 0x01, 0xA7};
  const uint8_t factory[] = {0xFE, 0xA6, 0x00, 0x05, 0xAB};
  const uint8_t version[] = {0xFE, 0xA6, 0x00, 0x1E, 0xC4};
  const uint8_t serial[] = {0xFE, 0xA6, 0x00, 0x1F, 0xC5};
  TEST_ASSERT_EQUAL(5, buildUartFrame(kUartCmdReadCo2, nullptr, 0, out, sizeof(out)));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(readCo2, out, 5);
  TEST_ASSERT_EQUAL(5, buildUartFrame(kUartCmdFactoryReset, nullptr, 0, out, sizeof(out)));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(factory, out, 5);
  TEST_ASSERT_EQUAL(5, buildUartFrame(kUartCmdVersion, nullptr, 0, out, sizeof(out)));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(version, out, 5);
  TEST_ASSERT_EQUAL(5, buildUartFrame(kUartCmdSerial, nullptr, 0, out, sizeof(out)));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(serial, out, 5);
}

void test_uart_build_manual_cal_and_mode() {
  uint8_t out[kUartMaxFrame];
  // FE A6 02 03 D1 D2 CS, 450 ppm = 01 C2, CS = A6+02+03+01+C2 = 0x16E -> 6E
  const uint8_t cal[] = {0xFE, 0xA6, 0x02, 0x03, 0x01, 0xC2, 0x6E};
  TEST_ASSERT_EQUAL(7, buildUartManualCal(450, out, sizeof(out)));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(cal, out, 7);
  // FE A6 02 04 00 D1 CS
  const uint8_t modeManual[] = {0xFE, 0xA6, 0x02, 0x04, 0x00, 0x00, 0xAC};
  const uint8_t modeAuto[] = {0xFE, 0xA6, 0x02, 0x04, 0x00, 0x01, 0xAD};
  TEST_ASSERT_EQUAL(7, buildUartSetCalMode(kCalModeManual, out, sizeof(out)));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(modeManual, out, 7);
  TEST_ASSERT_EQUAL(7, buildUartSetCalMode(kCalModeAuto, out, sizeof(out)));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(modeAuto, out, 7);
  TEST_ASSERT_EQUAL(0, buildUartSetCalMode(2, out, sizeof(out)));
  TEST_ASSERT_EQUAL(0, buildUartManualCal(399, out, sizeof(out)));
  TEST_ASSERT_EQUAL(0, buildUartManualCal(5001, out, sizeof(out)));
}

void test_uart_build_rejects_bad_args() {
  uint8_t out[kUartMaxFrame];
  const uint8_t d[2] = {1, 2};
  TEST_ASSERT_EQUAL(0, buildUartFrame(0x01, nullptr, 0, out, 4));
  TEST_ASSERT_EQUAL(0, buildUartFrame(0x01, nullptr, 0, nullptr, 8));
  TEST_ASSERT_EQUAL(0, buildUartFrame(0x01, nullptr, 2, out, sizeof(out)));
  TEST_ASSERT_EQUAL(0, buildUartFrame(0x01, d, 2, out, 6));
  TEST_ASSERT_EQUAL(0, buildUartFrame(0x01, d, kUartMaxData + 1, out, 64));
}

// ---------- Parser UART ----------

void test_parse_known_hex_frame() {
  // 600 ppm = 02 58; CS = A6+04+01+02+58+00+00 = 0x105 -> 05
  const uint8_t buf[] = {0xFE, 0xA6, 0x04, 0x01, 0x02, 0x58, 0x00, 0x00, 0x05};
  UartFrame f = sentinelFrame();
  const UartParseResult r = parseUartResponse(buf, sizeof(buf), kUartCmdReadCo2, kUartLenReadCo2, f);
  TEST_ASSERT_EQUAL(Status::Ok, r.status);
  TEST_ASSERT_EQUAL(0, r.frameStart);
  TEST_ASSERT_EQUAL(9, r.frameLen);
  TEST_ASSERT_FALSE(r.cmdMismatch);
  Measurement m = sentinel();
  TEST_ASSERT_EQUAL(Status::Ok, decodeUartCo2(f, m));
  TEST_ASSERT_EQUAL_UINT32(600, m.co2_ppm);
  TEST_ASSERT_FALSE(m.has_temp);
  TEST_ASSERT_EQUAL_HEX8(kWarnNone, m.warnings);
}

void test_parse_ack_frames_from_datasheet() {
  const uint8_t cal[] = {0xFE, 0xA6, 0x00, 0x03, 0xA9};
  const uint8_t mode[] = {0xFE, 0xA6, 0x00, 0x04, 0xAA};
  const uint8_t fac[] = {0xFE, 0xA6, 0x00, 0x05, 0xAB};
  UartFrame f;
  TEST_ASSERT_EQUAL(Status::Ok, parseUartResponse(cal, 5, kUartCmdManualCal, kUartAnyLen, f).status);
  TEST_ASSERT_EQUAL(0, f.len);
  TEST_ASSERT_EQUAL(Status::Ok, parseUartResponse(mode, 5, kUartCmdSetCalMode, kUartAnyLen, f).status);
  TEST_ASSERT_EQUAL(Status::Ok, parseUartResponse(fac, 5, kUartCmdFactoryReset, kUartAnyLen, f).status);
  // Ack cua lenh khac khong duoc tinh la ack cua lenh nay
  TEST_ASSERT_NOT_EQUAL(Status::Ok, parseUartResponse(cal, 5, kUartCmdFactoryReset, kUartAnyLen, f).status);
}

void test_parse_version_frame() {
  const uint8_t d[11] = {'A', 'C', 'D', '1', '0', '-', 'V', '1', '.', '2', 0x00};
  uint8_t buf[32];
  const size_t n = makeUartFrame(11, kUartCmdVersion, d, 11, buf);
  UartFrame f;
  TEST_ASSERT_EQUAL(Status::Ok, parseUartResponse(buf, n, kUartCmdVersion, kUartLenVersion, f).status);
  TEST_ASSERT_EQUAL(11, f.len);
  char s[11];
  copyAscii(f.data, 10, s, sizeof(s));
  TEST_ASSERT_EQUAL_STRING("ACD10-V1.2", s);
}

void test_parse_all_ppm_values_roundtrip() {
  uint8_t buf[16];
  for (uint32_t ppm = 0; ppm <= 0xFFFF; ppm += 7) {
    const size_t n = makeCo2Frame((uint16_t)ppm, buf);
    UartFrame f;
    Measurement m;
    TEST_ASSERT_EQUAL(Status::Ok, parseUartResponse(buf, n, kUartCmdReadCo2, kUartLenReadCo2, f).status);
    TEST_ASSERT_EQUAL(Status::Ok, decodeUartCo2(f, m));
    TEST_ASSERT_EQUAL_UINT32(ppm, m.co2_ppm);
  }
}

void test_parse_with_leading_garbage() {
  uint8_t buf[32] = {0x00, 0x13, 0xFE, 0x55, 0xA6, 0xFF, 0xFE};
  const size_t n = 7 + makeCo2Frame(812, &buf[7]);
  UartFrame f;
  const UartParseResult r = parseUartResponse(buf, n, kUartCmdReadCo2, kUartLenReadCo2, f);
  TEST_ASSERT_EQUAL(Status::Ok, r.status);
  TEST_ASSERT_EQUAL(7, r.frameStart);
  Measurement m;
  decodeUartCo2(f, m);
  TEST_ASSERT_EQUAL_UINT32(812, m.co2_ppm);
}

void test_parse_fake_header_with_huge_len() {
  // FE A6 FF ... : LEN = 255 vo ly, khong duoc lam parser cho mai
  uint8_t buf[32] = {0xFE, 0xA6, 0xFF, 0x01};
  const size_t n = 4 + makeCo2Frame(1000, &buf[4]);
  UartFrame f;
  const UartParseResult r = parseUartResponse(buf, n, kUartCmdReadCo2, kUartLenReadCo2, f);
  TEST_ASSERT_EQUAL(Status::Ok, r.status);
  TEST_ASSERT_EQUAL(4, r.frameStart);
}

void test_parse_data_containing_header_bytes() {
  // Byte du phong D3 D4 = FE A6 -> khong duoc nham thanh dau khung
  const uint8_t d[4] = {0x01, 0xFE, 0xFE, 0xA6};
  uint8_t buf[16];
  const size_t n = makeUartFrame(4, kUartCmdReadCo2, d, 4, buf);
  UartFrame f;
  Measurement m;
  TEST_ASSERT_EQUAL(Status::Ok, parseUartResponse(buf, n, kUartCmdReadCo2, kUartLenReadCo2, f).status);
  TEST_ASSERT_EQUAL(Status::Ok, decodeUartCo2(f, m));
  TEST_ASSERT_EQUAL_UINT32(0x01FE, m.co2_ppm);
}

void test_parse_bad_checksum() {
  uint8_t buf[16];
  const size_t n = makeCo2Frame(700, buf);
  buf[n - 1] ^= 0xFF;
  UartFrame f = sentinelFrame();
  TEST_ASSERT_EQUAL(Status::BadChecksum, parseUartResponse(buf, n, kUartCmdReadCo2, kUartLenReadCo2, f).status);
  TEST_ASSERT_EQUAL_HEX8(0x77, f.cmd);  // khong bi ghi
}

void test_parse_every_single_byte_corruption_is_rejected() {
  uint8_t good[16];
  const size_t n = makeCo2Frame(950, good);
  for (size_t i = 0; i < n; ++i) {
    for (int mask = 1; mask < 256; ++mask) {
      uint8_t buf[16];
      memcpy(buf, good, n);
      buf[i] ^= (uint8_t)mask;
      UartFrame f = sentinelFrame();
      const UartParseResult r = parseUartResponse(buf, n, kUartCmdReadCo2, kUartLenReadCo2, f);
      if (r.status == Status::Ok) {
        // Chi chap nhan duoc neu that su la khung dung checksum (vd LEN bi doi khop checksum) - khong the xay ra
        TEST_FAIL_MESSAGE("khung bi hong 1 byte van duoc chap nhan");
      }
    }
  }
}

void test_parse_truncated_prefixes_are_incomplete() {
  uint8_t buf[16];
  const size_t n = makeCo2Frame(1234, buf);
  for (size_t len = 1; len < n; ++len) {
    UartFrame f;
    const Status st = parseUartResponse(buf, len, kUartCmdReadCo2, kUartLenReadCo2, f).status;
    TEST_ASSERT_EQUAL_MESSAGE(Status::Incomplete, st, "tien to cua khung phai la INCOMPLETE");
  }
  UartFrame f;
  TEST_ASSERT_EQUAL(Status::Ok, parseUartResponse(buf, n, kUartCmdReadCo2, kUartLenReadCo2, f).status);
}

void test_parse_two_frames_returns_first_matching() {
  uint8_t buf[32];
  size_t n = makeCo2Frame(500, buf);
  n += makeCo2Frame(900, &buf[n]);
  UartFrame f;
  Measurement m;
  TEST_ASSERT_EQUAL(Status::Ok, parseUartResponse(buf, n, kUartCmdReadCo2, kUartLenReadCo2, f).status);
  decodeUartCo2(f, m);
  TEST_ASSERT_EQUAL_UINT32(500, m.co2_ppm);
}

void test_parse_skips_valid_frame_of_other_command() {
  // Phan hoi cu cua lenh doc phien ban (LEN 11) dung truoc khung CO2 -> phai lay khung CO2
  const uint8_t d[11] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 0};
  uint8_t buf[48];
  size_t n = makeUartFrame(11, kUartCmdVersion, d, 11, buf);
  n += makeCo2Frame(777, &buf[n]);
  UartFrame f;
  const UartParseResult r = parseUartResponse(buf, n, kUartCmdReadCo2, kUartLenReadCo2, f);
  TEST_ASSERT_EQUAL(Status::Ok, r.status);
  TEST_ASSERT_FALSE(r.cmdMismatch);
  TEST_ASSERT_EQUAL(kUartCmdReadCo2, f.cmd);
}

void test_parse_cmd_mismatch_fallback() {
  // Cam bien tra byte CMD = 00 thay vi 01 nhung dung LEN + checksum -> chap nhan, gan canh bao
  uint8_t buf[16];
  const size_t n = makeCo2Frame(640, buf, 0x00);
  UartFrame f;
  const UartParseResult r = parseUartResponse(buf, n, kUartCmdReadCo2, kUartLenReadCo2, f);
  TEST_ASSERT_EQUAL(Status::Ok, r.status);
  TEST_ASSERT_TRUE(r.cmdMismatch);
  Measurement m;
  TEST_ASSERT_EQUAL(Status::Ok, decodeUartCo2(f, m));
  TEST_ASSERT_EQUAL_UINT32(640, m.co2_ppm);
  TEST_ASSERT_TRUE(m.warnings & kWarnCmdMismatch);
}

void test_parse_fallback_waits_for_pending_frame() {
  // Khung CMD sai (du phong) roi den dau khung dung CMD chua ve het -> phai cho (INCOMPLETE)
  uint8_t full[32];
  size_t n = makeCo2Frame(640, full, 0x00);
  const size_t good = makeCo2Frame(650, &full[n]);
  UartFrame f;
  TEST_ASSERT_EQUAL(Status::Incomplete,
                    parseUartResponse(full, n + 4, kUartCmdReadCo2, kUartLenReadCo2, f).status);
  const UartParseResult r = parseUartResponse(full, n + good, kUartCmdReadCo2, kUartLenReadCo2, f);
  TEST_ASSERT_EQUAL(Status::Ok, r.status);
  TEST_ASSERT_FALSE(r.cmdMismatch);
  Measurement m;
  decodeUartCo2(f, m);
  TEST_ASSERT_EQUAL_UINT32(650, m.co2_ppm);
}

void test_parse_no_fallback_for_ack_commands() {
  const uint8_t otherAck[] = {0xFE, 0xA6, 0x00, 0x04, 0xAA};
  UartFrame f;
  TEST_ASSERT_EQUAL(Status::BadFrame, parseUartResponse(otherAck, 5, kUartCmdManualCal, kUartAnyLen, f).status);
}

void test_decode_uart_short_and_long_frames() {
  UartFrame f;
  f.cmd = kUartCmdReadCo2;
  f.len = 2;
  f.data[0] = 0x03;
  f.data[1] = 0x20;
  Measurement m;
  TEST_ASSERT_EQUAL(Status::Ok, decodeUartCo2(f, m));
  TEST_ASSERT_EQUAL_UINT32(800, m.co2_ppm);
  TEST_ASSERT_TRUE(m.warnings & kWarnLengthMismatch);
  f.len = 1;
  m = sentinel();
  TEST_ASSERT_EQUAL(Status::BadFrame, decodeUartCo2(f, m));
  assertUntouched(m);
  f.len = 0;
  TEST_ASSERT_EQUAL(Status::BadFrame, decodeUartCo2(f, m));
}

void test_parse_empty_and_garbage() {
  UartFrame f;
  TEST_ASSERT_EQUAL(Status::NoResponse, parseUartResponse(nullptr, 5, 1, 4, f).status);
  const uint8_t one[] = {0x00};
  TEST_ASSERT_EQUAL(Status::NoResponse, parseUartResponse(one, 0, 1, 4, f).status);
  const uint8_t junk[] = {0x00, 0x11, 0x22, 0xA6, 0xFE, 0x01};
  TEST_ASSERT_EQUAL(Status::BadFrame, parseUartResponse(junk, sizeof(junk), 1, 4, f).status);
  const uint8_t ff[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  TEST_ASSERT_EQUAL(Status::BadFrame, parseUartResponse(ff, sizeof(ff), 1, 4, f).status);
  const uint8_t tailFe[] = {0x00, 0xFE};
  TEST_ASSERT_EQUAL(Status::Incomplete, parseUartResponse(tailFe, sizeof(tailFe), 1, 4, f).status);
}

void test_parse_fuzz_random_buffers() {
  // 20000 buffer ngau nhien: khong crash, khung duoc chap nhan phai dung checksum va nam trong buffer
  srand(12345);
  uint8_t buf[48];
  for (int iter = 0; iter < 20000; ++iter) {
    const size_t len = (size_t)(rand() % (int)sizeof(buf));
    for (size_t i = 0; i < len; ++i) {
      const int r = rand() % 10;
      buf[i] = r == 0 ? 0xFE : (r == 1 ? 0xA6 : (r == 2 ? 0x04 : (r == 3 ? 0x01 : (uint8_t)rand())));
    }
    UartFrame f = sentinelFrame();
    const UartParseResult r = parseUartResponse(buf, len, kUartCmdReadCo2, kUartLenReadCo2, f);
    if (r.status == Status::Ok) {
      TEST_ASSERT_TRUE(r.frameStart + r.frameLen <= len);
      TEST_ASSERT_EQUAL_HEX8(0xFE, buf[r.frameStart]);
      TEST_ASSERT_EQUAL_HEX8(0xA6, buf[r.frameStart + 1]);
      TEST_ASSERT_EQUAL(r.frameLen, 5 + buf[r.frameStart + 2]);
      TEST_ASSERT_EQUAL_HEX8(sumLow8(&buf[r.frameStart + 1], r.frameLen - 2), buf[r.frameStart + r.frameLen - 1]);
      TEST_ASSERT_TRUE(f.cmd == kUartCmdReadCo2 || (r.cmdMismatch && f.len == kUartLenReadCo2));
    } else {
      TEST_ASSERT_EQUAL_HEX8(0x77, f.cmd);
    }
  }
}

// ---------- Giai ma I2C ----------

void test_i2c_decode_valid() {
  uint8_t buf[9];
  makeI2cCo2(612, 0x1234, buf);
  Measurement m = sentinel();
  TEST_ASSERT_EQUAL(Status::Ok, decodeI2cCo2(buf, 9, m));
  TEST_ASSERT_EQUAL_UINT32(612, m.co2_ppm);
  TEST_ASSERT_EQUAL_HEX16(0x1234, m.temp_raw);
  TEST_ASSERT_TRUE(m.has_temp);
  TEST_ASSERT_EQUAL_HEX8(kWarnNone, m.warnings);
}

void test_i2c_decode_known_bytes() {
  // 1000 ppm = 00 00 03 E8: CRC(00 00) = 81, CRC(03 E8) tinh tay roi kiem lai bang ham
  uint8_t buf[9] = {0x00, 0x00, 0x81, 0x03, 0xE8, 0x00, 0x00, 0x00, 0x81};
  buf[5] = crc8(&buf[3], 2);
  Measurement m;
  TEST_ASSERT_EQUAL(Status::Ok, decodeI2cCo2(buf, 9, m));
  TEST_ASSERT_EQUAL_UINT32(1000, m.co2_ppm);
  TEST_ASSERT_EQUAL_HEX16(0, m.temp_raw);
}

void test_i2c_decode_32bit_big_endian() {
  uint8_t buf[9];
  makeI2cCo2(0x00011170, 0, buf);  // 70000 ppm: kiem tra 2 byte cao duoc ghep dung
  Measurement m;
  TEST_ASSERT_EQUAL(Status::Ok, decodeI2cCo2(buf, 9, m));
  TEST_ASSERT_EQUAL_UINT32(70000, m.co2_ppm);
  TEST_ASSERT_TRUE(m.warnings & kWarnOutOfRange);
}

void test_i2c_every_single_bit_flip_is_rejected() {
  uint8_t good[9];
  makeI2cCo2(845, 0x0ABC, good);
  for (size_t i = 0; i < 9; ++i) {
    for (int bit = 0; bit < 8; ++bit) {
      uint8_t buf[9];
      memcpy(buf, good, 9);
      buf[i] ^= (uint8_t)(1 << bit);
      Measurement m = sentinel();
      TEST_ASSERT_EQUAL(Status::CrcError, decodeI2cCo2(buf, 9, m));
      assertUntouched(m);
    }
  }
}

void test_i2c_short_read_and_null() {
  uint8_t buf[9];
  makeI2cCo2(500, 0, buf);
  Measurement m = sentinel();
  for (size_t n = 0; n < 9; ++n) TEST_ASSERT_EQUAL(Status::ShortRead, decodeI2cCo2(buf, n, m));
  TEST_ASSERT_EQUAL(Status::InvalidArgument, decodeI2cCo2(nullptr, 9, m));
  assertUntouched(m);
}

void test_i2c_bus_floating_all_ff_or_00() {
  uint8_t ff[9], zz[9];
  memset(ff, 0xFF, 9);
  memset(zz, 0x00, 9);
  Measurement m = sentinel();
  TEST_ASSERT_EQUAL(Status::CrcError, decodeI2cCo2(ff, 9, m));
  TEST_ASSERT_EQUAL(Status::CrcError, decodeI2cCo2(zz, 9, m));
  assertUntouched(m);
}

void test_i2c_decode_word() {
  const uint8_t ok[] = {0x01, 0xC2, 0x50};
  uint16_t v = 0xBEEF;
  TEST_ASSERT_EQUAL(Status::Ok, decodeI2cWord(ok, 3, v));
  TEST_ASSERT_EQUAL_UINT16(450, v);
  const uint8_t bad[] = {0x01, 0xC2, 0x51};
  v = 0xBEEF;
  TEST_ASSERT_EQUAL(Status::CrcError, decodeI2cWord(bad, 3, v));
  TEST_ASSERT_EQUAL(Status::ShortRead, decodeI2cWord(ok, 2, v));
  TEST_ASSERT_EQUAL(Status::InvalidArgument, decodeI2cWord(nullptr, 3, v));
  TEST_ASSERT_EQUAL_HEX16(0xBEEF, v);
}

// ---------- Kiem tra hop ly, ASCII, phan muc ----------

void test_plausibility_range_boundary() {
  Measurement m;
  m.co2_ppm = 399;
  TEST_ASSERT_EQUAL_HEX8(kWarnOutOfRange, checkPlausibility(m));
  m.co2_ppm = 400;
  TEST_ASSERT_EQUAL_HEX8(kWarnNone, checkPlausibility(m));
  m.co2_ppm = 5000;
  TEST_ASSERT_EQUAL_HEX8(kWarnNone, checkPlausibility(m));
  m.co2_ppm = 5001;
  TEST_ASSERT_EQUAL_HEX8(kWarnOutOfRange, checkPlausibility(m));
  m.co2_ppm = 0;
  TEST_ASSERT_EQUAL_HEX8(kWarnOutOfRange, checkPlausibility(m));
}

void test_copy_ascii() {
  char s[11];
  const uint8_t a[10] = {'V', '1', '.', '0', ' ', ' ', 0, 0, 0, 0};
  TEST_ASSERT_EQUAL(4, copyAscii(a, 10, s, sizeof(s)));
  TEST_ASSERT_EQUAL_STRING("V1.0", s);
  const uint8_t b[10] = {'A', 0x01, 'B', 0xFF, 'C', '0', '1', '2', '3', '4'};
  copyAscii(b, 10, s, sizeof(s));
  TEST_ASSERT_EQUAL_STRING("A.B.C01234", s);
  uint8_t ff[10];
  memset(ff, 0xFF, 10);
  copyAscii(ff, 10, s, sizeof(s));
  TEST_ASSERT_EQUAL_STRING("..........", s);
  char small[4];
  TEST_ASSERT_EQUAL(3, copyAscii(b, 10, small, sizeof(small)));
  TEST_ASSERT_EQUAL_STRING("A.B", small);
  TEST_ASSERT_EQUAL(0, copyAscii(b, 10, small, 0));
  TEST_ASSERT_EQUAL(0, copyAscii(nullptr, 10, s, sizeof(s)));
  TEST_ASSERT_EQUAL_STRING("", s);
}

void test_co2_level_boundaries() {
  TEST_ASSERT_EQUAL(Co2Level::Good, classifyCo2(0));
  TEST_ASSERT_EQUAL(Co2Level::Good, classifyCo2(799));
  TEST_ASSERT_EQUAL(Co2Level::Moderate, classifyCo2(800));
  TEST_ASSERT_EQUAL(Co2Level::Moderate, classifyCo2(999));
  TEST_ASSERT_EQUAL(Co2Level::Poor, classifyCo2(1000));
  TEST_ASSERT_EQUAL(Co2Level::Poor, classifyCo2(1499));
  TEST_ASSERT_EQUAL(Co2Level::Bad, classifyCo2(1500));
  TEST_ASSERT_EQUAL(Co2Level::Bad, classifyCo2(2499));
  TEST_ASSERT_EQUAL(Co2Level::VeryBad, classifyCo2(2500));
  TEST_ASSERT_EQUAL(Co2Level::VeryBad, classifyCo2(70000));
  for (int i = 0; i <= (int)Co2Level::VeryBad; ++i) {
    TEST_ASSERT_NOT_EQUAL(0, strcmp("?", co2LevelName((Co2Level)i)));
  }
}

void test_status_strings_all_defined() {
  for (int i = 0; i < (int)Status::Count_; ++i) {
    TEST_ASSERT_NOT_EQUAL(0, strcmp("UNKNOWN", statusToString((Status)i)));
  }
}

static int runAll() {
  UNITY_BEGIN();
  RUN_TEST(test_crc8_datasheet_example_0000);
  RUN_TEST(test_crc8_datasheet_manual_cal_450ppm);
  RUN_TEST(test_crc8_datasheet_factory_result);
  RUN_TEST(test_crc8_reference_vector_and_empty);
  RUN_TEST(test_i2c_address_and_read_commands);
  RUN_TEST(test_i2c_build_set_cal_mode_matches_datasheet);
  RUN_TEST(test_i2c_build_manual_cal_matches_datasheet);
  RUN_TEST(test_i2c_build_factory_reset);
  RUN_TEST(test_uart_settings);
  RUN_TEST(test_uart_build_commands_match_datasheet);
  RUN_TEST(test_uart_build_manual_cal_and_mode);
  RUN_TEST(test_uart_build_rejects_bad_args);
  RUN_TEST(test_parse_known_hex_frame);
  RUN_TEST(test_parse_ack_frames_from_datasheet);
  RUN_TEST(test_parse_version_frame);
  RUN_TEST(test_parse_all_ppm_values_roundtrip);
  RUN_TEST(test_parse_with_leading_garbage);
  RUN_TEST(test_parse_fake_header_with_huge_len);
  RUN_TEST(test_parse_data_containing_header_bytes);
  RUN_TEST(test_parse_bad_checksum);
  RUN_TEST(test_parse_every_single_byte_corruption_is_rejected);
  RUN_TEST(test_parse_truncated_prefixes_are_incomplete);
  RUN_TEST(test_parse_two_frames_returns_first_matching);
  RUN_TEST(test_parse_skips_valid_frame_of_other_command);
  RUN_TEST(test_parse_cmd_mismatch_fallback);
  RUN_TEST(test_parse_fallback_waits_for_pending_frame);
  RUN_TEST(test_parse_no_fallback_for_ack_commands);
  RUN_TEST(test_decode_uart_short_and_long_frames);
  RUN_TEST(test_parse_empty_and_garbage);
  RUN_TEST(test_parse_fuzz_random_buffers);
  RUN_TEST(test_i2c_decode_valid);
  RUN_TEST(test_i2c_decode_known_bytes);
  RUN_TEST(test_i2c_decode_32bit_big_endian);
  RUN_TEST(test_i2c_every_single_bit_flip_is_rejected);
  RUN_TEST(test_i2c_short_read_and_null);
  RUN_TEST(test_i2c_bus_floating_all_ff_or_00);
  RUN_TEST(test_i2c_decode_word);
  RUN_TEST(test_plausibility_range_boundary);
  RUN_TEST(test_copy_ascii);
  RUN_TEST(test_co2_level_boundaries);
  RUN_TEST(test_status_strings_all_defined);
  return UNITY_END();
}

#ifdef ARDUINO
#include <Arduino.h>
void setup() {
  delay(2000);  // cho Serial on dinh truoc khi chay test tren mach
  runAll();
}
void loop() {}
#else
int main() { return runAll(); }
#endif
