// Unit test phan giao thuc APM10: CRC8, khung UART, giai ma I2C, phan muc PM2.5.
// Chay tren may tinh: pio test -e native -f test_protocol
// Chay tren ESP32  : pio test -e esp32_uart -f test_protocol
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include "apm10_protocol.h"
#include "pm25_level.h"

using namespace apm10;

void setUp() {}
void tearDown() {}

// ---------- Ham tien ich ----------

// Tao khung phan hoi UART: FE A5 lenByte cmdByte data[n] CS
static size_t makeUartFrame(uint8_t lenByte, uint8_t cmdByte, const uint8_t* data, size_t n, uint8_t* out) {
  out[0] = 0xFE;
  out[1] = 0xA5;
  out[2] = lenByte;
  out[3] = cmdByte;
  memcpy(&out[4], data, n);
  out[4 + n] = sumLow8(&out[1], 3 + n);
  return 5 + n;
}

static size_t makeAllFrame(uint16_t pm1, uint16_t pm25, uint16_t pm10, uint8_t lenByte, uint8_t* out) {
  const uint8_t d[6] = {(uint8_t)(pm1 >> 8),  (uint8_t)pm1,  (uint8_t)(pm25 >> 8),
                        (uint8_t)pm25,        (uint8_t)(pm10 >> 8), (uint8_t)pm10};
  return makeUartFrame(lenByte, 0x00, d, 6, out);
}

// Tao 30 byte du lieu I2C: 10 word, moi word kem CRC
static void makeI2cData(uint16_t pm1, uint16_t pm25, uint16_t pm10, uint8_t* out) {
  const uint16_t words[10] = {pm1, pm25, 0, pm10, 0, 0, 0, 0, 0, 0};
  for (int w = 0; w < 10; ++w) {
    out[w * 3] = (uint8_t)(words[w] >> 8);
    out[w * 3 + 1] = (uint8_t)words[w];
    out[w * 3 + 2] = crc8(&out[w * 3], 2);
  }
}

static Measurement sentinel() {
  Measurement m;
  m.pm1_0 = 0xDEAD;
  m.pm2_5 = 0xBEEF;
  m.pm10 = 0xCAFE;
  m.warnings = 0x80;
  return m;
}

static void assertUntouched(const Measurement& m) {
  TEST_ASSERT_EQUAL_HEX16(0xDEAD, m.pm1_0);
  TEST_ASSERT_EQUAL_HEX16(0xBEEF, m.pm2_5);
  TEST_ASSERT_EQUAL_HEX16(0xCAFE, m.pm10);
  TEST_ASSERT_EQUAL_HEX8(0x80, m.warnings);
}

// ---------- CRC8 ----------

void test_crc8_reference_vector() {
  // Vector chuan cua CRC-8 poly 0x31 init 0xFF (dung chung cho cac cam bien Sensirion/Aosong)
  const uint8_t d[2] = {0xBE, 0xEF};
  TEST_ASSERT_EQUAL_HEX8(0x92, crc8(d, 2));
}

void test_crc8_matches_datasheet_start_command() {
  // Datasheet: Start = 10 | 00 10 05 00 F6 -> F6 la CRC cua (05 00)
  const uint8_t d[2] = {0x05, 0x00};
  TEST_ASSERT_EQUAL_HEX8(0xF6, crc8(d, 2));
  TEST_ASSERT_EQUAL_HEX8(kI2cCmdStart[4], crc8(&kI2cCmdStart[2], 2));
}

void test_crc8_empty_is_init() { TEST_ASSERT_EQUAL_HEX8(0xFF, crc8(nullptr, 0)); }

void test_i2c_command_bytes_match_datasheet() {
  const uint8_t start[] = {0x00, 0x10, 0x05, 0x00, 0xF6};
  const uint8_t stop[] = {0x01, 0x04};
  const uint8_t rd[] = {0x03, 0x00};
  TEST_ASSERT_EQUAL(sizeof(start), sizeof(kI2cCmdStart));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(start, kI2cCmdStart, sizeof(start));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(stop, kI2cCmdStop, sizeof(stop));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(rd, kI2cCmdRead, sizeof(rd));
  TEST_ASSERT_EQUAL_HEX8(0x08, kI2cAddress);
  // Datasheet ghi byte ghi/doc la 0x10 / 0x11 = dia chi 7-bit 0x08 dich trai + bit R/W
  TEST_ASSERT_EQUAL_HEX8(0x10, (uint8_t)(kI2cAddress << 1));
  TEST_ASSERT_EQUAL_HEX8(0x11, (uint8_t)((kI2cAddress << 1) | 1));
}

// ---------- Lenh UART ----------

void test_build_uart_command_read_all() {
  uint8_t out[8];
  const uint8_t expected[] = {0xFE, 0xA5, 0x00, 0x01, 0xA6};  // Bang 9 datasheet
  TEST_ASSERT_EQUAL(5, buildUartCommand(kCmdReadAll, out, sizeof(out)));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 5);
}

void test_build_uart_command_read_pm25() {
  uint8_t out[5];
  const uint8_t expected[] = {0xFE, 0xA5, 0x00, 0x00, 0xA5};  // Bang 8 datasheet
  TEST_ASSERT_EQUAL(5, buildUartCommand(kCmdReadPm25, out, sizeof(out)));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 5);
}

void test_build_uart_command_rejects_small_buffer() {
  uint8_t out[4];
  TEST_ASSERT_EQUAL(0, buildUartCommand(kCmdReadAll, out, sizeof(out)));
  TEST_ASSERT_EQUAL(0, buildUartCommand(kCmdReadAll, nullptr, 5));
}

void test_uart_baud_is_1200() { TEST_ASSERT_EQUAL_UINT32(1200, kUartBaud); }

// ---------- Phan tich phan hoi UART ----------

void test_parse_all_with_correct_len_byte() {
  uint8_t f[16];
  const size_t n = makeAllFrame(12, 25, 40, 0x06, f);
  Measurement m;
  const UartParseResult r = parseUartResponse(f, n, kCmdReadAll, false, m);
  TEST_ASSERT_EQUAL_STRING("OK", statusToString(r.status));
  TEST_ASSERT_EQUAL(0, r.frameStart);
  TEST_ASSERT_EQUAL(11, r.frameLen);
  TEST_ASSERT_TRUE(m.has_pm1_0 && m.has_pm2_5 && m.has_pm10);
  TEST_ASSERT_EQUAL_UINT16(12, m.pm1_0);
  TEST_ASSERT_EQUAL_UINT16(25, m.pm2_5);
  TEST_ASSERT_EQUAL_UINT16(40, m.pm10);
  TEST_ASSERT_EQUAL_HEX8(kWarnNone, m.warnings);
}

void test_parse_all_with_datasheet_len_02() {
  // Datasheet ghi nguyen van: FE A5 02 00 DF11 DF12 DF21 DF22 DF31 DF32 CS
  uint8_t f[16];
  const size_t n = makeAllFrame(300, 512, 999, 0x02, f);
  Measurement m;
  const UartParseResult r = parseUartResponse(f, n, kCmdReadAll, false, m);
  TEST_ASSERT_EQUAL_STRING("OK", statusToString(r.status));
  TEST_ASSERT_EQUAL_UINT16(300, m.pm1_0);
  TEST_ASSERT_EQUAL_UINT16(512, m.pm2_5);
  TEST_ASSERT_EQUAL_UINT16(999, m.pm10);
  TEST_ASSERT_EQUAL_HEX8(kWarnLengthMismatch, m.warnings);
}

void test_parse_known_hex_frame() {
  // Khung viet tay: PM1.0=0x0010(16) PM2.5=0x0019(25) PM10=0x0023(35)
  // CS = A5+02+00+00+10+00+19+00+23 = 0xF3 (byte thap)
  const uint8_t f[] = {0xFE, 0xA5, 0x02, 0x00, 0x00, 0x10, 0x00, 0x19, 0x00, 0x23, 0xF3};
  Measurement m;
  TEST_ASSERT_EQUAL_STRING("OK", statusToString(parseUartResponse(f, sizeof(f), kCmdReadAll, false, m).status));
  TEST_ASSERT_EQUAL_UINT16(16, m.pm1_0);
  TEST_ASSERT_EQUAL_UINT16(25, m.pm2_5);
  TEST_ASSERT_EQUAL_UINT16(35, m.pm10);
}

void test_parse_pm25_only_command() {
  const uint8_t d[2] = {0x00, 0x2A};
  uint8_t f[8];
  const size_t n = makeUartFrame(0x02, 0x00, d, 2, f);
  Measurement m;
  const UartParseResult r = parseUartResponse(f, n, kCmdReadPm25, false, m);
  TEST_ASSERT_EQUAL_STRING("OK", statusToString(r.status));
  TEST_ASSERT_TRUE(m.has_pm2_5);
  TEST_ASSERT_FALSE(m.has_pm1_0);
  TEST_ASSERT_FALSE(m.has_pm10);
  TEST_ASSERT_EQUAL_UINT16(42, m.pm2_5);
  TEST_ASSERT_EQUAL_HEX8(kWarnNone, m.warnings);
}

void test_parse_with_leading_garbage() {
  uint8_t buf[32] = {0x00, 0x13, 0xFE, 0x37, 0xA5, 0xFE, 0xFE};
  const size_t g = 7;
  const size_t n = makeAllFrame(5, 7, 9, 0x06, buf + g);
  Measurement m;
  const UartParseResult r = parseUartResponse(buf, g + n, kCmdReadAll, false, m);
  TEST_ASSERT_EQUAL_STRING("OK", statusToString(r.status));
  TEST_ASSERT_EQUAL(g, r.frameStart);
  TEST_ASSERT_EQUAL_UINT16(5, m.pm1_0);
  TEST_ASSERT_EQUAL_UINT16(7, m.pm2_5);
  TEST_ASSERT_EQUAL_UINT16(9, m.pm10);
}

void test_parse_fake_header_before_real_frame() {
  // "FE A5" gia (khung loi checksum) dung truoc khung that
  uint8_t buf[40];
  size_t p = makeAllFrame(1, 2, 3, 0x06, buf);
  buf[p - 1] ^= 0x55;  // lam hong checksum khung dau
  p += makeAllFrame(10, 20, 30, 0x06, buf + p);
  Measurement m;
  const UartParseResult r = parseUartResponse(buf, p, kCmdReadAll, false, m);
  TEST_ASSERT_EQUAL_STRING("OK", statusToString(r.status));
  TEST_ASSERT_EQUAL(11, r.frameStart);
  TEST_ASSERT_EQUAL_UINT16(10, m.pm1_0);
  TEST_ASSERT_EQUAL_UINT16(20, m.pm2_5);
  TEST_ASSERT_EQUAL_UINT16(30, m.pm10);
}

void test_parse_values_containing_header_bytes() {
  // Gia tri 0xFEA5 nam trong data khong duoc lam roi parser
  uint8_t f[16];
  const size_t n = makeAllFrame(0xFEA5, 0xFEA5, 0xFEA5, 0x06, f);
  Measurement m;
  const UartParseResult r = parseUartResponse(f, n, kCmdReadAll, false, m);
  TEST_ASSERT_EQUAL_STRING("OK", statusToString(r.status));
  TEST_ASSERT_EQUAL(0, r.frameStart);
  TEST_ASSERT_EQUAL_HEX16(0xFEA5, m.pm2_5);
  TEST_ASSERT_TRUE(m.warnings & kWarnOutOfRange);
}

void test_parse_bad_checksum() {
  uint8_t f[16];
  const size_t n = makeAllFrame(12, 25, 40, 0x06, f);
  f[n - 1] ^= 0x01;
  Measurement m = sentinel();
  TEST_ASSERT_EQUAL_STRING("BAD_CHECKSUM", statusToString(parseUartResponse(f, n, kCmdReadAll, false, m).status));
  TEST_ASSERT_EQUAL_STRING("BAD_CHECKSUM", statusToString(parseUartResponse(f, n, kCmdReadAll, true, m).status));
  assertUntouched(m);
}

void test_parse_every_single_byte_corruption_is_rejected() {
  // Doi bat ky 1 byte nao trong khung (tru byte FE) deu phai bi phat hien
  uint8_t f[16];
  const size_t n = makeAllFrame(123, 234, 345, 0x06, f);
  for (size_t i = 1; i < n; ++i) {
    for (int delta = 1; delta < 256; delta += 7) {
      uint8_t c[16];
      memcpy(c, f, n);
      c[i] = (uint8_t)(c[i] + delta);
      Measurement m = sentinel();
      const UartParseResult r = parseUartResponse(c, n, kCmdReadAll, true, m);
      if (r.status == Status::Ok) {
        // Chi chap nhan neu ket qua van dung (vi du byte LEN bi doi ma checksum van khop: khong the)
        TEST_FAIL_MESSAGE("Khung bi hong 1 byte nhung van duoc chap nhan");
      }
      assertUntouched(m);
    }
  }
}

void test_parse_truncated_frame() {
  const uint8_t lens[] = {0x02, 0x06};
  for (int li = 0; li < 2; ++li) {
    uint8_t f[16];
    const size_t n = makeAllFrame(12, 25, 40, lens[li], f);
    for (size_t len = 1; len < n; ++len) {
      for (int fin = 0; fin < 2; ++fin) {
        Measurement m = sentinel();
        TEST_ASSERT_EQUAL_STRING("INCOMPLETE",
                                 statusToString(parseUartResponse(f, len, kCmdReadAll, fin == 1, m).status));
        assertUntouched(m);
      }
    }
  }
}

void test_parse_byte_by_byte_stream() {
  uint8_t f[16];
  const size_t n = makeAllFrame(77, 88, 99, 0x02, f);
  Measurement m = sentinel();
  for (size_t len = 1; len < n; ++len) {
    TEST_ASSERT_NOT_EQUAL(Status::Ok, parseUartResponse(f, len, kCmdReadAll, false, m).status);
  }
  assertUntouched(m);
  TEST_ASSERT_EQUAL(Status::Ok, parseUartResponse(f, n, kCmdReadAll, false, m).status);
  TEST_ASSERT_EQUAL_UINT16(88, m.pm2_5);
}

void test_parse_no_false_accept_of_short_frame_while_streaming() {
  // Khung 6 byte data, LEN=02. Chon du lieu sao cho cach hieu "2 byte data" CUNG khop checksum
  // tai byte thu 7 (trung ngau nhien). Khi chua het thoi gian cho, parser KHONG duoc chap nhan som.
  uint8_t d[6] = {0x00, 0x14, 0x00, 0x1E, 0x00, 0x28};
  d[2] = (uint8_t)(0xA5 + 0x02 + 0x00 + d[0] + d[1]);  // byte thu 7 == checksum gia
  uint8_t f[16];
  const size_t n = makeUartFrame(0x02, 0x00, d, 6, f);
  Measurement m = sentinel();
  TEST_ASSERT_EQUAL(Status::Incomplete, parseUartResponse(f, 7, kCmdReadAll, false, m).status);
  assertUntouched(m);
  TEST_ASSERT_EQUAL(Status::Ok, parseUartResponse(f, n, kCmdReadAll, false, m).status);
  TEST_ASSERT_TRUE(m.has_pm1_0 && m.has_pm10);
  TEST_ASSERT_EQUAL_UINT16(0x0014, m.pm1_0);
  TEST_ASSERT_EQUAL_UINT16((uint16_t)((d[2] << 8) | d[3]), m.pm2_5);
  TEST_ASSERT_EQUAL_UINT16(0x0028, m.pm10);
}

void test_parse_sensor_returns_only_pm25_for_read_all() {
  // Neu firmware cam bien chi tra 2 byte cho lenh 0x01: cho het timeout roi moi chap nhan
  const uint8_t d[2] = {0x00, 0x33};
  uint8_t f[8];
  const size_t n = makeUartFrame(0x02, 0x01, d, 2, f);
  Measurement m = sentinel();
  TEST_ASSERT_EQUAL(Status::Incomplete, parseUartResponse(f, n, kCmdReadAll, false, m).status);
  assertUntouched(m);
  TEST_ASSERT_EQUAL(Status::Ok, parseUartResponse(f, n, kCmdReadAll, true, m).status);
  TEST_ASSERT_EQUAL_UINT16(0x33, m.pm2_5);
  TEST_ASSERT_FALSE(m.has_pm1_0);
  TEST_ASSERT_FALSE(m.has_pm10);
  TEST_ASSERT_TRUE(m.warnings & kWarnPm25Only);
}

void test_parse_two_frames_returns_first() {
  uint8_t buf[32];
  size_t p = makeAllFrame(1, 2, 3, 0x06, buf);
  p += makeAllFrame(4, 5, 6, 0x06, buf + p);
  Measurement m;
  const UartParseResult r = parseUartResponse(buf, p, kCmdReadAll, false, m);
  TEST_ASSERT_EQUAL(Status::Ok, r.status);
  TEST_ASSERT_EQUAL(0, r.frameStart);
  TEST_ASSERT_EQUAL_UINT16(2, m.pm2_5);
}

void test_parse_empty_and_garbage() {
  Measurement m = sentinel();
  TEST_ASSERT_EQUAL(Status::NoResponse, parseUartResponse(nullptr, 0, kCmdReadAll, true, m).status);
  const uint8_t g[] = {0x00, 0x11, 0x22, 0x33, 0xA5, 0x44};
  TEST_ASSERT_EQUAL(Status::BadFrame, parseUartResponse(g, sizeof(g), kCmdReadAll, true, m).status);
  const uint8_t ff[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  TEST_ASSERT_EQUAL(Status::BadFrame, parseUartResponse(ff, sizeof(ff), kCmdReadAll, true, m).status);
  assertUntouched(m);
}

void test_parse_invalid_command() {
  uint8_t f[16];
  const size_t n = makeAllFrame(1, 2, 3, 0x06, f);
  Measurement m = sentinel();
  TEST_ASSERT_EQUAL(Status::InvalidArgument, parseUartResponse(f, n, 0x07, true, m).status);
  assertUntouched(m);
}

void test_parse_zero_values() {
  uint8_t f[16];
  const size_t n = makeAllFrame(0, 0, 0, 0x06, f);
  Measurement m;
  TEST_ASSERT_EQUAL(Status::Ok, parseUartResponse(f, n, kCmdReadAll, false, m).status);
  TEST_ASSERT_EQUAL_UINT16(0, m.pm1_0);
  TEST_ASSERT_EQUAL_UINT16(0, m.pm2_5);
  TEST_ASSERT_EQUAL_UINT16(0, m.pm10);
  TEST_ASSERT_EQUAL_HEX8(kWarnNone, m.warnings);
}

void test_parse_fuzz_random_buffers() {
  // 20000 buffer ngau nhien: khong duoc crash, khung duoc chap nhan phai dung checksum va nam trong buffer
  srand(12345);
  uint8_t buf[48];
  for (int iter = 0; iter < 20000; ++iter) {
    const size_t len = (size_t)(rand() % (int)sizeof(buf));
    for (size_t i = 0; i < len; ++i) {
      const int r = rand() % 10;
      buf[i] = r == 0 ? 0xFE : (r == 1 ? 0xA5 : (r == 2 ? 0x06 : (uint8_t)rand()));
    }
    for (int fin = 0; fin < 2; ++fin) {
      Measurement m = sentinel();
      const uint8_t cmd = (iter & 1) ? kCmdReadAll : kCmdReadPm25;
      const UartParseResult r = parseUartResponse(buf, len, cmd, fin == 1, m);
      if (r.status == Status::Ok) {
        TEST_ASSERT_TRUE(r.frameStart + r.frameLen <= len);
        TEST_ASSERT_EQUAL_HEX8(0xFE, buf[r.frameStart]);
        TEST_ASSERT_EQUAL_HEX8(0xA5, buf[r.frameStart + 1]);
        TEST_ASSERT_EQUAL_HEX8(sumLow8(&buf[r.frameStart + 1], r.frameLen - 2), buf[r.frameStart + r.frameLen - 1]);
        TEST_ASSERT_TRUE(r.frameLen == 7 || r.frameLen == 11);
      } else {
        assertUntouched(m);
      }
    }
  }
}

// ---------- Kiem tra hop ly ----------

void test_plausibility_range_boundary() {
  Measurement m;
  m.has_pm1_0 = m.has_pm2_5 = m.has_pm10 = true;
  m.pm1_0 = 1000;
  m.pm2_5 = 1000;
  m.pm10 = 1000;
  TEST_ASSERT_EQUAL_HEX8(kWarnNone, checkPlausibility(m));
  m.pm10 = 1001;
  TEST_ASSERT_EQUAL_HEX8(kWarnOutOfRange, checkPlausibility(m));
}

void test_plausibility_order() {
  Measurement m;
  m.has_pm1_0 = m.has_pm2_5 = m.has_pm10 = true;
  m.pm1_0 = 10;
  m.pm2_5 = 10;
  m.pm10 = 10;
  TEST_ASSERT_EQUAL_HEX8(kWarnNone, checkPlausibility(m));
  m.pm1_0 = 11;
  TEST_ASSERT_EQUAL_HEX8(kWarnOrder, checkPlausibility(m));
  m.pm1_0 = 5;
  m.pm2_5 = 20;
  TEST_ASSERT_EQUAL_HEX8(kWarnOrder, checkPlausibility(m));
  m.has_pm10 = false;  // khong co PM10 thi khong so sanh
  TEST_ASSERT_EQUAL_HEX8(kWarnNone, checkPlausibility(m));
}

// ---------- I2C ----------

void test_i2c_decode_valid() {
  uint8_t d[30];
  makeI2cData(8, 15, 27, d);
  Measurement m;
  TEST_ASSERT_EQUAL(Status::Ok, decodeI2cData(d, 30, m));
  TEST_ASSERT_TRUE(m.has_pm1_0 && m.has_pm2_5 && m.has_pm10);
  TEST_ASSERT_EQUAL_UINT16(8, m.pm1_0);
  TEST_ASSERT_EQUAL_UINT16(15, m.pm2_5);
  TEST_ASSERT_EQUAL_UINT16(27, m.pm10);
  TEST_ASSERT_EQUAL_HEX8(kWarnNone, m.warnings);
}

void test_i2c_decode_big_endian() {
  uint8_t d[30];
  makeI2cData(0x0102, 0x0304, 0x0506, d);
  TEST_ASSERT_EQUAL_HEX8(0x01, d[0]);
  TEST_ASSERT_EQUAL_HEX8(0x02, d[1]);
  Measurement m;
  TEST_ASSERT_EQUAL(Status::Ok, decodeI2cData(d, 30, m));
  TEST_ASSERT_EQUAL_HEX16(0x0102, m.pm1_0);
  TEST_ASSERT_EQUAL_HEX16(0x0304, m.pm2_5);
  TEST_ASSERT_EQUAL_HEX16(0x0506, m.pm10);
}

void test_i2c_every_single_bit_flip_in_pm_words_is_rejected() {
  uint8_t d[30];
  makeI2cData(111, 222, 333, d);
  const int pmWords[3] = {0, 1, 3};
  for (int k = 0; k < 3; ++k) {
    for (int b = 0; b < 3; ++b) {
      for (int bit = 0; bit < 8; ++bit) {
        uint8_t c[30];
        memcpy(c, d, 30);
        c[pmWords[k] * 3 + b] ^= (uint8_t)(1 << bit);
        Measurement m = sentinel();
        TEST_ASSERT_EQUAL(Status::CrcError, decodeI2cData(c, 30, m));
        assertUntouched(m);
      }
    }
  }
}

void test_i2c_reserved_word_crc_error_is_only_warning() {
  uint8_t d[30];
  makeI2cData(1, 2, 3, d);
  d[2 * 3 + 2] ^= 0xFF;  // word 2 (du phong / PM4)
  d[9 * 3 + 2] ^= 0xFF;  // word 9 (du phong)
  Measurement m;
  TEST_ASSERT_EQUAL(Status::Ok, decodeI2cData(d, 30, m));
  TEST_ASSERT_EQUAL_UINT16(2, m.pm2_5);
  TEST_ASSERT_EQUAL_HEX8(kWarnReservedCrc, m.warnings);
}

void test_i2c_short_read_and_null() {
  uint8_t d[30];
  makeI2cData(1, 2, 3, d);
  Measurement m = sentinel();
  for (size_t len = 0; len < 30; ++len) TEST_ASSERT_EQUAL(Status::ShortRead, decodeI2cData(d, len, m));
  TEST_ASSERT_EQUAL(Status::InvalidArgument, decodeI2cData(nullptr, 30, m));
  assertUntouched(m);
}

void test_i2c_bus_floating_all_ff_or_00() {
  // Bus khong co thiet bi thuong doc ra toan 0xFF; ngan mach xuong GND doc ra 0x00
  uint8_t d[30];
  Measurement m = sentinel();
  memset(d, 0xFF, sizeof(d));
  TEST_ASSERT_EQUAL(Status::CrcError, decodeI2cData(d, 30, m));
  memset(d, 0x00, sizeof(d));
  TEST_ASSERT_EQUAL(Status::CrcError, decodeI2cData(d, 30, m));
  assertUntouched(m);
}

void test_i2c_order_warning() {
  uint8_t d[30];
  makeI2cData(50, 20, 30, d);
  Measurement m;
  TEST_ASSERT_EQUAL(Status::Ok, decodeI2cData(d, 30, m));
  TEST_ASSERT_EQUAL_HEX8(kWarnOrder, m.warnings);
}

// ---------- Phan muc PM2.5 ----------

void test_pm25_level_boundaries() {
  TEST_ASSERT_EQUAL(Pm25Level::Good, classifyPm25(0));
  TEST_ASSERT_EQUAL(Pm25Level::Good, classifyPm25(25));
  TEST_ASSERT_EQUAL(Pm25Level::Moderate, classifyPm25(26));
  TEST_ASSERT_EQUAL(Pm25Level::Moderate, classifyPm25(50));
  TEST_ASSERT_EQUAL(Pm25Level::Poor, classifyPm25(51));
  TEST_ASSERT_EQUAL(Pm25Level::Poor, classifyPm25(80));
  TEST_ASSERT_EQUAL(Pm25Level::Bad, classifyPm25(81));
  TEST_ASSERT_EQUAL(Pm25Level::Bad, classifyPm25(150));
  TEST_ASSERT_EQUAL(Pm25Level::VeryBad, classifyPm25(151));
  TEST_ASSERT_EQUAL(Pm25Level::VeryBad, classifyPm25(250));
  TEST_ASSERT_EQUAL(Pm25Level::Hazardous, classifyPm25(251));
  TEST_ASSERT_EQUAL(Pm25Level::Hazardous, classifyPm25(65535));
  TEST_ASSERT_EQUAL_STRING("Tot", pm25LevelName(Pm25Level::Good));
  TEST_ASSERT_EQUAL_STRING("Nguy hai", pm25LevelName(Pm25Level::Hazardous));
}

void test_status_strings_all_defined() {
  for (int i = 0; i < (int)Status::Count_; ++i) {
    TEST_ASSERT_NOT_EQUAL(0, strcmp("UNKNOWN", statusToString((Status)i)));
  }
}

static int runAll() {
  UNITY_BEGIN();
  RUN_TEST(test_crc8_reference_vector);
  RUN_TEST(test_crc8_matches_datasheet_start_command);
  RUN_TEST(test_crc8_empty_is_init);
  RUN_TEST(test_i2c_command_bytes_match_datasheet);
  RUN_TEST(test_build_uart_command_read_all);
  RUN_TEST(test_build_uart_command_read_pm25);
  RUN_TEST(test_build_uart_command_rejects_small_buffer);
  RUN_TEST(test_uart_baud_is_1200);
  RUN_TEST(test_parse_all_with_correct_len_byte);
  RUN_TEST(test_parse_all_with_datasheet_len_02);
  RUN_TEST(test_parse_known_hex_frame);
  RUN_TEST(test_parse_pm25_only_command);
  RUN_TEST(test_parse_with_leading_garbage);
  RUN_TEST(test_parse_fake_header_before_real_frame);
  RUN_TEST(test_parse_values_containing_header_bytes);
  RUN_TEST(test_parse_bad_checksum);
  RUN_TEST(test_parse_every_single_byte_corruption_is_rejected);
  RUN_TEST(test_parse_truncated_frame);
  RUN_TEST(test_parse_byte_by_byte_stream);
  RUN_TEST(test_parse_no_false_accept_of_short_frame_while_streaming);
  RUN_TEST(test_parse_sensor_returns_only_pm25_for_read_all);
  RUN_TEST(test_parse_two_frames_returns_first);
  RUN_TEST(test_parse_empty_and_garbage);
  RUN_TEST(test_parse_invalid_command);
  RUN_TEST(test_parse_zero_values);
  RUN_TEST(test_parse_fuzz_random_buffers);
  RUN_TEST(test_plausibility_range_boundary);
  RUN_TEST(test_plausibility_order);
  RUN_TEST(test_i2c_decode_valid);
  RUN_TEST(test_i2c_decode_big_endian);
  RUN_TEST(test_i2c_every_single_bit_flip_in_pm_words_is_rejected);
  RUN_TEST(test_i2c_reserved_word_crc_error_is_only_warning);
  RUN_TEST(test_i2c_short_read_and_null);
  RUN_TEST(test_i2c_bus_floating_all_ff_or_00);
  RUN_TEST(test_i2c_order_warning);
  RUN_TEST(test_pm25_level_boundaries);
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
