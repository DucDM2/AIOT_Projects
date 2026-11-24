// Unit test phan giao thuc AGS10: CRC8, giai ma khung, lenh hieu chuan / doi dia chi, quy doi va phan muc TVOC.
// Chay tren may tinh: pio test -e native -f test_protocol
// Chay tren ESP32  : pio test -e esp32 -f test_protocol
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include "ags10_protocol.h"
#include "tvoc_level.h"

using namespace ags10;

void setUp() {}
void tearDown() {}

// ---------- Ham tien ich ----------

static void makeFrame(uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3, uint8_t* out) {
  out[0] = d0;
  out[1] = d1;
  out[2] = d2;
  out[3] = d3;
  out[4] = crc8(out, 4);
}

static void makeTvocFrame(uint8_t status, uint32_t ppb, uint8_t* out) {
  makeFrame(status, (uint8_t)(ppb >> 16), (uint8_t)(ppb >> 8), (uint8_t)ppb, out);
}

static TvocReading sentinel() {
  TvocReading r;
  r.tvocPpb = 0xDEADBEEF;
  r.status = 0xA5;
  r.ready = true;
  r.warnings = 0x80;
  return r;
}

static void assertUntouched(const TvocReading& r) {
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, r.tvocPpb);
  TEST_ASSERT_EQUAL_HEX8(0xA5, r.status);
  TEST_ASSERT_EQUAL_HEX8(0x80, r.warnings);
}

// ---------- CRC8 ----------

void test_crc8_reference_vector() {
  // Vector chuan CRC-8 poly 0x31 init 0xFF (dung chung Sensirion/Aosong)
  const uint8_t d[2] = {0xBE, 0xEF};
  TEST_ASSERT_EQUAL_HEX8(0x92, crc8(d, 2));
}

void test_crc8_matches_datasheet_command_table() {
  // Bang 2 datasheet (muc 3.8): CRC cua cac lenh hieu chuan diem 0
  const uint8_t reset[4] = {0x00, 0x0C, 0xFF, 0xFF};
  const uint8_t current[4] = {0x00, 0x0C, 0x00, 0x00};
  const uint8_t custom[4] = {0x00, 0x0C, 0x1C, 0xBC};
  TEST_ASSERT_EQUAL_HEX8(0x81, crc8(reset, 4));
  TEST_ASSERT_EQUAL_HEX8(0xAC, crc8(current, 4));
  TEST_ASSERT_EQUAL_HEX8(0xB4, crc8(custom, 4));
}

void test_crc8_empty_is_init() { TEST_ASSERT_EQUAL_HEX8(0xFF, crc8(nullptr, 0)); }

void test_constants_match_datasheet() {
  TEST_ASSERT_EQUAL_HEX8(0x1A, kDefaultAddress);
  TEST_ASSERT_EQUAL_HEX8(0x34, (uint8_t)(kDefaultAddress << 1));        // byte ghi
  TEST_ASSERT_EQUAL_HEX8(0x35, (uint8_t)((kDefaultAddress << 1) | 1));  // byte doc
  TEST_ASSERT_EQUAL_UINT32(15000, kMaxI2cFreqHz);
  TEST_ASSERT_EQUAL_HEX8(0x00, kRegTvoc);
  TEST_ASSERT_EQUAL_HEX8(0x01, kRegCalibration);
  TEST_ASSERT_EQUAL_HEX8(0x11, kRegVersion);
  TEST_ASSERT_EQUAL_HEX8(0x20, kRegResistance);
  TEST_ASSERT_EQUAL_HEX8(0x21, kRegAddress);
  TEST_ASSERT_EQUAL_UINT32(30, kCommandGapMs);
  TEST_ASSERT_EQUAL_UINT32(1500, kAcquisitionGapMs);
  TEST_ASSERT_EQUAL_UINT32(120000, kPreheatMs);
  TEST_ASSERT_TRUE(kSamplingPeriodMs >= kAcquisitionGapMs);
}

// ---------- Lenh ghi ----------

void test_zero_point_commands_match_datasheet() {
  uint8_t out[5];
  const uint8_t reset[5] = {0x00, 0x0C, 0xFF, 0xFF, 0x81};
  const uint8_t current[5] = {0x00, 0x0C, 0x00, 0x00, 0xAC};
  const uint8_t custom[5] = {0x00, 0x0C, 0x1C, 0xBC, 0xB4};
  TEST_ASSERT_EQUAL(5, buildZeroPointData(kZeroPointFactoryReset, out, sizeof(out)));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(reset, out, 5);
  TEST_ASSERT_EQUAL(5, buildZeroPointData(kZeroPointCurrentResistance, out, sizeof(out)));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(current, out, 5);
  TEST_ASSERT_EQUAL(5, buildZeroPointData(0x1CBC, out, sizeof(out)));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(custom, out, 5);
}

void test_zero_point_rejects_small_buffer() {
  uint8_t out[4];
  TEST_ASSERT_EQUAL(0, buildZeroPointData(0, out, sizeof(out)));
  TEST_ASSERT_EQUAL(0, buildZeroPointData(0, nullptr, 5));
}

void test_address_command() {
  uint8_t out[5];
  TEST_ASSERT_EQUAL(5, buildAddressData(0x1B, out, sizeof(out)));
  TEST_ASSERT_EQUAL_HEX8(0x1B, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0xE4, out[1]);
  TEST_ASSERT_EQUAL_HEX8(0x1B, out[2]);
  TEST_ASSERT_EQUAL_HEX8(0xE4, out[3]);
  TEST_ASSERT_EQUAL_HEX8(crc8(out, 4), out[4]);
  TEST_ASSERT_EQUAL(5, buildAddressData(0x08, out, sizeof(out)));
  TEST_ASSERT_EQUAL(5, buildAddressData(0x77, out, sizeof(out)));
}

void test_address_command_rejects_reserved_addresses() {
  uint8_t out[5] = {0};
  const uint8_t bad[] = {0x00, 0x01, 0x07, 0x78, 0x7F, 0x80, 0xFF};
  for (size_t i = 0; i < sizeof(bad); ++i) TEST_ASSERT_EQUAL(0, buildAddressData(bad[i], out, sizeof(out)));
  TEST_ASSERT_EQUAL(0, buildAddressData(0x1B, out, 4));
  TEST_ASSERT_EQUAL(0, buildAddressData(0x1B, nullptr, 5));
}

// ---------- Giai ma TVOC ----------

void test_decode_tvoc_ready() {
  uint8_t f[5];
  makeTvocFrame(0x00, 0x000123, f);
  TvocReading r;
  TEST_ASSERT_EQUAL_STRING("OK", statusToString(decodeTvoc(f, 5, r)));
  TEST_ASSERT_TRUE(r.ready);
  TEST_ASSERT_EQUAL_UINT32(0x123, r.tvocPpb);
  TEST_ASSERT_EQUAL_HEX8(0x00, r.status);
  TEST_ASSERT_EQUAL_HEX8(kWarnNone, r.warnings);
}

void test_decode_tvoc_big_endian_24bit() {
  uint8_t f[5];
  makeTvocFrame(0x00, 0x01869F, f);  // 99999
  TEST_ASSERT_EQUAL_HEX8(0x01, f[1]);
  TEST_ASSERT_EQUAL_HEX8(0x86, f[2]);
  TEST_ASSERT_EQUAL_HEX8(0x9F, f[3]);
  TvocReading r;
  TEST_ASSERT_EQUAL(Status::Ok, decodeTvoc(f, 5, r));
  TEST_ASSERT_EQUAL_UINT32(99999, r.tvocPpb);
  TEST_ASSERT_EQUAL_HEX8(kWarnNone, r.warnings);
}

void test_decode_tvoc_out_of_range_warning() {
  uint8_t f[5];
  makeTvocFrame(0x00, 100000, f);
  TvocReading r;
  TEST_ASSERT_EQUAL(Status::Ok, decodeTvoc(f, 5, r));
  TEST_ASSERT_EQUAL_UINT32(100000, r.tvocPpb);
  TEST_ASSERT_EQUAL_HEX8(kWarnOutOfRange, r.warnings);
  makeTvocFrame(0x00, 0xFFFFFF, f);
  TEST_ASSERT_EQUAL(Status::Ok, decodeTvoc(f, 5, r));
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFF, r.tvocPpb);
}

void test_decode_tvoc_not_ready_still_reports_value() {
  uint8_t f[5];
  makeTvocFrame(0x01, 5000, f);
  TvocReading r = sentinel();
  TEST_ASSERT_EQUAL_STRING("NOT_READY", statusToString(decodeTvoc(f, 5, r)));
  TEST_ASSERT_FALSE(r.ready);
  TEST_ASSERT_EQUAL_UINT32(5000, r.tvocPpb);
  TEST_ASSERT_EQUAL_HEX8(0x01, r.status);
}

void test_decode_tvoc_reserved_high_bits_ignored() {
  // Bit 7:4 cua STATUS khong duoc dinh nghia -> khong anh huong ket qua
  uint8_t f[5];
  makeTvocFrame(0xF0, 42, f);
  TvocReading r;
  TEST_ASSERT_EQUAL(Status::Ok, decodeTvoc(f, 5, r));
  TEST_ASSERT_EQUAL_UINT32(42, r.tvocPpb);
  TEST_ASSERT_EQUAL_HEX8(0xF0, r.status);
}

void test_decode_tvoc_unsupported_unit() {
  for (uint8_t ch = 1; ch < 8; ++ch) {
    uint8_t f[5];
    makeTvocFrame((uint8_t)(ch << 1), 10, f);
    TvocReading r = sentinel();
    TEST_ASSERT_EQUAL(Status::UnsupportedUnit, decodeTvoc(f, 5, r));
    assertUntouched(r);
  }
}

void test_decode_tvoc_every_single_bit_flip_rejected() {
  uint8_t f[5];
  makeTvocFrame(0x00, 0x00ABCD, f);
  for (int b = 0; b < 5; ++b) {
    for (int bit = 0; bit < 8; ++bit) {
      uint8_t c[5];
      memcpy(c, f, 5);
      c[b] ^= (uint8_t)(1 << bit);
      TvocReading r = sentinel();
      TEST_ASSERT_EQUAL(Status::CrcError, decodeTvoc(c, 5, r));
      assertUntouched(r);
    }
  }
}

void test_decode_tvoc_short_and_null() {
  uint8_t f[5];
  makeTvocFrame(0x00, 1, f);
  TvocReading r = sentinel();
  for (size_t len = 0; len < 5; ++len) TEST_ASSERT_EQUAL(Status::ShortRead, decodeTvoc(f, len, r));
  TEST_ASSERT_EQUAL(Status::InvalidArgument, decodeTvoc(nullptr, 5, r));
  assertUntouched(r);
}

void test_decode_bus_floating_or_shorted() {
  // Bus tha noi doc ra toan 0xFF, ngan mach xuong GND doc ra 0x00: khong bao gio duoc coi la OK
  uint8_t f[5];
  TvocReading r = sentinel();
  memset(f, 0xFF, 5);
  TEST_ASSERT_NOT_EQUAL(Status::Ok, decodeTvoc(f, 5, r));
  TEST_ASSERT_NOT_EQUAL(Status::NotReady, decodeTvoc(f, 5, r));
  memset(f, 0x00, 5);
  TEST_ASSERT_EQUAL(Status::CrcError, decodeTvoc(f, 5, r));
  assertUntouched(r);
}

void test_decode_fuzz_random_frames() {
  // 50000 khung ngau nhien: chi chap nhan khi CRC dung, khong bao gio ghi `out` khi loi
  srand(4242);
  for (int i = 0; i < 50000; ++i) {
    uint8_t f[5];
    for (int k = 0; k < 5; ++k) f[k] = (uint8_t)rand();
    if (i % 4 == 0) f[4] = crc8(f, 4);
    TvocReading r = sentinel();
    const Status st = decodeTvoc(f, 5, r);
    if (st == Status::Ok || st == Status::NotReady) {
      TEST_ASSERT_EQUAL_HEX8(crc8(f, 4), f[4]);
      TEST_ASSERT_EQUAL_HEX8(0, f[0] & kStatusUnitMask);
      TEST_ASSERT_EQUAL_UINT32(((uint32_t)f[1] << 16) | ((uint32_t)f[2] << 8) | f[3], r.tvocPpb);
      TEST_ASSERT_EQUAL(st == Status::Ok, r.ready);
    } else {
      assertUntouched(r);
    }
  }
}

// ---------- Dien tro / phien ban ----------

void test_decode_resistance() {
  uint8_t f[5];
  makeFrame(0x00, 0x00, 0x1C, 0xBC, f);
  uint32_t r = 0;
  TEST_ASSERT_EQUAL(Status::Ok, decodeResistance(f, 5, r));
  TEST_ASSERT_EQUAL_UINT32(0x1CBC, r);  // 7356 x 0.1 kOhm = 735.6 kOhm
  makeFrame(0x12, 0x34, 0x56, 0x78, f);
  TEST_ASSERT_EQUAL(Status::Ok, decodeResistance(f, 5, r));
  TEST_ASSERT_EQUAL_HEX32(0x12345678, r);
  f[4] ^= 0x10;
  r = 7;
  TEST_ASSERT_EQUAL(Status::CrcError, decodeResistance(f, 5, r));
  TEST_ASSERT_EQUAL_UINT32(7, r);
  TEST_ASSERT_EQUAL(Status::ShortRead, decodeResistance(f, 4, r));
}

void test_decode_version() {
  uint8_t f[5];
  makeFrame(0x00, 0x00, 0x00, 0x0B, f);
  uint8_t v = 0;
  TEST_ASSERT_EQUAL(Status::Ok, decodeVersion(f, 5, v));
  TEST_ASSERT_EQUAL_HEX8(0x0B, v);
  f[3] ^= 0x01;
  v = 0x55;
  TEST_ASSERT_EQUAL(Status::CrcError, decodeVersion(f, 5, v));
  TEST_ASSERT_EQUAL_HEX8(0x55, v);
}

// ---------- Quy doi va phan muc ----------

void test_ppb_to_ugm3_ethanol() {
  TEST_ASSERT_EQUAL_UINT32(0, tvocPpbToUgm3(0));
  TEST_ASSERT_EQUAL_UINT32(2, tvocPpbToUgm3(1));          // 1.88 -> 2
  TEST_ASSERT_EQUAL_UINT32(1884, tvocPpbToUgm3(1000));    // 1884.25
  TEST_ASSERT_EQUAL_UINT32(188423, tvocPpbToUgm3(99999)); // 188423.47
  TEST_ASSERT_TRUE(tvocPpbToUgm3(0xFFFFFFFFUL) > 0);     // khong tran so
}

void test_tvoc_level_boundaries() {
  TEST_ASSERT_EQUAL(TvocLevel::Good, classifyTvocUgm3(0));
  TEST_ASSERT_EQUAL(TvocLevel::Good, classifyTvocUgm3(300));
  TEST_ASSERT_EQUAL(TvocLevel::Acceptable, classifyTvocUgm3(301));
  TEST_ASSERT_EQUAL(TvocLevel::Acceptable, classifyTvocUgm3(1000));
  TEST_ASSERT_EQUAL(TvocLevel::Poor, classifyTvocUgm3(1001));
  TEST_ASSERT_EQUAL(TvocLevel::Poor, classifyTvocUgm3(3000));
  TEST_ASSERT_EQUAL(TvocLevel::Bad, classifyTvocUgm3(3001));
  TEST_ASSERT_EQUAL(TvocLevel::Bad, classifyTvocUgm3(10000));
  TEST_ASSERT_EQUAL(TvocLevel::VeryBad, classifyTvocUgm3(10001));
  TEST_ASSERT_EQUAL(TvocLevel::VeryBad, classifyTvocUgm3(25000));
  TEST_ASSERT_EQUAL(TvocLevel::Hazardous, classifyTvocUgm3(25001));
  // Theo ppb (ethanol): 159 ppb ~ 299.6 ug/m3, 160 ppb ~ 301.5 ug/m3
  TEST_ASSERT_EQUAL(TvocLevel::Good, classifyTvocPpb(159));
  TEST_ASSERT_EQUAL(TvocLevel::Acceptable, classifyTvocPpb(160));
  TEST_ASSERT_EQUAL(TvocLevel::Hazardous, classifyTvocPpb(99999));
  TEST_ASSERT_EQUAL_STRING("Tot", tvocLevelName(TvocLevel::Good));
  TEST_ASSERT_EQUAL_STRING("Nguy hai", tvocLevelName(TvocLevel::Hazardous));
}

void test_status_strings_all_defined() {
  for (int i = 0; i < (int)Status::Count_; ++i) {
    TEST_ASSERT_NOT_EQUAL(0, strcmp("UNKNOWN", statusToString((Status)i)));
  }
}

static int runAll() {
  UNITY_BEGIN();
  RUN_TEST(test_crc8_reference_vector);
  RUN_TEST(test_crc8_matches_datasheet_command_table);
  RUN_TEST(test_crc8_empty_is_init);
  RUN_TEST(test_constants_match_datasheet);
  RUN_TEST(test_zero_point_commands_match_datasheet);
  RUN_TEST(test_zero_point_rejects_small_buffer);
  RUN_TEST(test_address_command);
  RUN_TEST(test_address_command_rejects_reserved_addresses);
  RUN_TEST(test_decode_tvoc_ready);
  RUN_TEST(test_decode_tvoc_big_endian_24bit);
  RUN_TEST(test_decode_tvoc_out_of_range_warning);
  RUN_TEST(test_decode_tvoc_not_ready_still_reports_value);
  RUN_TEST(test_decode_tvoc_reserved_high_bits_ignored);
  RUN_TEST(test_decode_tvoc_unsupported_unit);
  RUN_TEST(test_decode_tvoc_every_single_bit_flip_rejected);
  RUN_TEST(test_decode_tvoc_short_and_null);
  RUN_TEST(test_decode_bus_floating_or_shorted);
  RUN_TEST(test_decode_fuzz_random_frames);
  RUN_TEST(test_decode_resistance);
  RUN_TEST(test_decode_version);
  RUN_TEST(test_ppb_to_ugm3_ethanol);
  RUN_TEST(test_tvoc_level_boundaries);
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
