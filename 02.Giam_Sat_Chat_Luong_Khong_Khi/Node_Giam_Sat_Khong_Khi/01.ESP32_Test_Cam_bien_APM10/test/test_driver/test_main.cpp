// Unit test driver APM10 voi cam bien gia lap (UART 1200 baud + I2C).
// Chay tren may tinh: pio test -e native -f test_driver
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include "apm10_driver.h"
#include "fake_apm10.h"

using namespace apm10;
using fake::FakeClock;
using fake::FakeI2cSensor;
using fake::FakeUartSensor;

void setUp() {}
void tearDown() {}

// ================= UART =================

void test_uart_read_all_sends_datasheet_command() {
  FakeClock clk;
  FakeUartSensor s(clk);
  Apm10Uart drv(s, clk);
  Measurement m;
  TEST_ASSERT_EQUAL_STRING("OK", statusToString(drv.readAll(m)));
  const uint8_t expected[] = {0xFE, 0xA5, 0x00, 0x01, 0xA6};
  TEST_ASSERT_EQUAL(5, s.lastCmdLen);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, s.lastCmd, 5);
  TEST_ASSERT_EQUAL_UINT16(10, m.pm1_0);
  TEST_ASSERT_EQUAL_UINT16(20, m.pm2_5);
  TEST_ASSERT_EQUAL_UINT16(30, m.pm10);
  TEST_ASSERT_TRUE(m.has_pm1_0 && m.has_pm2_5 && m.has_pm10);
}

void test_uart_read_all_returns_fast_without_waiting_timeout() {
  // 5 byte lenh + 10 ms + 11 byte phan hoi ~ 154 ms; driver phai tra ngay khi du khung, khong cho 500 ms
  FakeClock clk;
  FakeUartSensor s(clk);
  Apm10Uart drv(s, clk);
  Measurement m;
  TEST_ASSERT_EQUAL(Status::Ok, drv.readAll(m));
  TEST_ASSERT_UINT32_WITHIN(5, 5 * 9 + 10 + 11 * 9, drv.lastElapsedMs());
  TEST_ASSERT_EQUAL(11, drv.lastRawLen());
}

void test_uart_len_byte_variants() {
  const uint8_t lens[] = {0x02, 0x06};
  const uint8_t cmds[] = {0x00, 0x01};
  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < 2; ++j) {
      FakeClock clk;
      FakeUartSensor s(clk);
      s.lenStyleAll = lens[i];
      s.respCmdByte = cmds[j];
      s.pm1 = 101;
      s.pm25 = 202;
      s.pm10 = 303;
      Apm10Uart drv(s, clk);
      Measurement m;
      TEST_ASSERT_EQUAL(Status::Ok, drv.readAll(m));
      TEST_ASSERT_EQUAL_UINT16(101, m.pm1_0);
      TEST_ASSERT_EQUAL_UINT16(202, m.pm2_5);
      TEST_ASSERT_EQUAL_UINT16(303, m.pm10);
      TEST_ASSERT_EQUAL(lens[i] == 0x06, (m.warnings & kWarnLengthMismatch) == 0);
    }
  }
}

void test_uart_read_pm25_command() {
  FakeClock clk;
  FakeUartSensor s(clk);
  s.pm25 = 77;
  Apm10Uart drv(s, clk);
  Measurement m;
  TEST_ASSERT_EQUAL(Status::Ok, drv.readPm25(m));
  const uint8_t expected[] = {0xFE, 0xA5, 0x00, 0x00, 0xA5};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, s.lastCmd, 5);
  TEST_ASSERT_EQUAL_UINT16(77, m.pm2_5);
  TEST_ASSERT_FALSE(m.has_pm1_0);
  TEST_ASSERT_FALSE(m.has_pm10);
}

void test_uart_silent_sensor_times_out() {
  FakeClock clk;
  FakeUartSensor s(clk);
  s.silent = true;
  Apm10Uart drv(s, clk, 500);
  Measurement m;
  TEST_ASSERT_EQUAL_STRING("NO_RESPONSE", statusToString(drv.readAll(m)));
  TEST_ASSERT_UINT32_WITHIN(2, 500, drv.lastElapsedMs());
  TEST_ASSERT_EQUAL(0, drv.lastRawLen());
}

void test_uart_slow_sensor_within_timeout() {
  FakeClock clk;
  FakeUartSensor s(clk);
  s.responseDelayMs = 300;
  Apm10Uart drv(s, clk, 500);
  Measurement m;
  TEST_ASSERT_EQUAL(Status::Ok, drv.readAll(m));
}

void test_uart_too_slow_sensor_is_incomplete() {
  FakeClock clk;
  FakeUartSensor s(clk);
  s.responseDelayMs = 400;  // khung den cham, chi kip vai byte truoc 500 ms
  Apm10Uart drv(s, clk, 500);
  Measurement m;
  const Status st = drv.readAll(m);
  TEST_ASSERT_TRUE(st == Status::Incomplete || st == Status::NoResponse);
}

void test_uart_stale_bytes_are_flushed() {
  FakeClock clk;
  FakeUartSensor s(clk);
  // Khung cu hop le (gia tri 999) con nam trong buffer nhan -> phai bi bo
  uint8_t old[11] = {0xFE, 0xA5, 0x06, 0x00, 0x03, 0xE7, 0x03, 0xE7, 0x03, 0xE7, 0};
  old[10] = sumLow8(&old[1], 9);
  s.injectStale(old, sizeof(old));
  s.pm1 = 1;
  s.pm25 = 2;
  s.pm10 = 3;
  Apm10Uart drv(s, clk);
  Measurement m;
  TEST_ASSERT_EQUAL(Status::Ok, drv.readAll(m));
  TEST_ASSERT_EQUAL_UINT16(2, m.pm2_5);
}

void test_uart_garbage_before_frame() {
  FakeClock clk;
  FakeUartSensor s(clk);
  const uint8_t g[] = {0x00, 0xFE, 0x12, 0xFE, 0xA5, 0x06, 0x99, 0xFF};
  memcpy(s.garbage, g, sizeof(g));
  s.garbageLen = sizeof(g);
  Apm10Uart drv(s, clk);
  Measurement m;
  TEST_ASSERT_EQUAL(Status::Ok, drv.readAll(m));
  TEST_ASSERT_EQUAL_UINT16(10, m.pm1_0);
  TEST_ASSERT_EQUAL_UINT16(20, m.pm2_5);
  TEST_ASSERT_EQUAL_UINT16(30, m.pm10);
}

void test_uart_long_noise_burst_then_frame() {
  // 100 byte nhieu (vuot buffer 64 byte cua driver) roi moi toi khung that
  FakeClock clk;
  FakeUartSensor s(clk);
  srand(7);
  for (size_t i = 0; i < 100; ++i) s.garbage[i] = (uint8_t)(rand() & 0x7F);  // khong chua 0xFE
  s.garbageLen = 100;
  Apm10Uart drv(s, clk, 2000);
  Measurement m;
  TEST_ASSERT_EQUAL(Status::Ok, drv.readAll(m));
  TEST_ASSERT_EQUAL_UINT16(20, m.pm2_5);
}

void test_uart_corrupted_byte_never_accepted() {
  for (int idx = 0; idx < 11; ++idx) {
    FakeClock clk;
    FakeUartSensor s(clk);
    s.corruptIndex = idx;
    Apm10Uart drv(s, clk);
    Measurement m;
    const Status st = drv.readAll(m);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(Status::Ok, st, "Khung hong van duoc chap nhan");
  }
}

void test_uart_truncated_frame() {
  FakeClock clk;
  FakeUartSensor s(clk);
  s.truncateTo = 8;
  Apm10Uart drv(s, clk);
  Measurement m;
  TEST_ASSERT_EQUAL_STRING("INCOMPLETE", statusToString(drv.readAll(m)));
  TEST_ASSERT_EQUAL(8, drv.lastRawLen());
}

void test_uart_sensor_only_supports_pm25() {
  FakeClock clk;
  FakeUartSensor s(clk);
  s.onlyPm25ForAll = true;
  s.pm25 = 55;
  Apm10Uart drv(s, clk);
  Measurement m;
  TEST_ASSERT_EQUAL(Status::Ok, drv.readAll(m));
  TEST_ASSERT_EQUAL_UINT16(55, m.pm2_5);
  TEST_ASSERT_TRUE(m.warnings & kWarnPm25Only);
}

void test_uart_soak_with_random_faults() {
  // 3000 lan doc lien tiep voi gia tri ngau nhien + loi ngau nhien.
  // Yeu cau: MOI ket qua OK deu dung tuyet doi voi gia tri cam bien da gui.
  FakeClock clk;
  FakeUartSensor s(clk);
  Apm10Uart drv(s, clk);
  srand(2024);
  int ok = 0, expectedOk = 0;
  for (int i = 0; i < 3000; ++i) {
    s.pm1 = (uint16_t)(rand() % 600);
    s.pm25 = (uint16_t)(s.pm1 + rand() % 300);
    s.pm10 = (uint16_t)(s.pm25 + rand() % 200);
    s.lenStyleAll = (rand() & 1) ? 0x02 : 0x06;
    s.silent = false;
    s.corruptIndex = -1;
    s.truncateTo = -1;
    s.garbageLen = 0;
    const int fault = rand() % 100;
    bool shouldSucceed = true;
    if (fault < 10) {
      s.corruptIndex = 1 + rand() % 10;
      s.corruptMask = (uint8_t)(1 + rand() % 255);
      shouldSucceed = false;
    } else if (fault < 15) {
      s.silent = true;
      shouldSucceed = false;
    } else if (fault < 20) {
      s.truncateTo = rand() % 11;
      shouldSucceed = false;
    } else if (fault < 30) {
      s.garbageLen = (size_t)(1 + rand() % 20);
      for (size_t k = 0; k < s.garbageLen; ++k) s.garbage[k] = (uint8_t)rand();
    }
    if (shouldSucceed) ++expectedOk;
    Measurement m;
    const Status st = drv.readAll(m);
    if (st == Status::Ok) {
      ++ok;
      TEST_ASSERT_EQUAL_UINT16(s.pm1, m.pm1_0);
      TEST_ASSERT_EQUAL_UINT16(s.pm25, m.pm2_5);
      TEST_ASSERT_EQUAL_UINT16(s.pm10, m.pm10);
      TEST_ASSERT_FALSE(m.warnings & kWarnOrder);
    }
    clk.delayMs(1000);  // chu ky doc 1 s
  }
  // Rac ngau nhien co the vo tinh lam mat 1 so khung, nhung ty le thanh cong phai rat sat ky vong
  TEST_ASSERT_TRUE(ok <= expectedOk);
  TEST_ASSERT_TRUE(ok >= expectedOk * 98 / 100);
}

// ================= I2C =================

void test_i2c_probe() {
  FakeClock clk;
  FakeI2cSensor s;
  Apm10I2c drv(s, clk);
  TEST_ASSERT_TRUE(drv.probe());
  s.present = false;
  TEST_ASSERT_FALSE(drv.probe());
}

void test_i2c_start_read_stop_sequence() {
  FakeClock clk;
  FakeI2cSensor s;
  Apm10I2c drv(s, clk);
  Measurement m;
  // Chua start -> cam bien khong tra du lieu
  TEST_ASSERT_EQUAL_STRING("NO_RESPONSE", statusToString(drv.read(m)));

  TEST_ASSERT_EQUAL(Status::Ok, drv.startMeasurement());
  const uint8_t start[] = {0x00, 0x10, 0x05, 0x00, 0xF6};
  TEST_ASSERT_EQUAL(5, s.lastWriteLen);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(start, s.lastWrite, 5);

  clk.totalDelay = 0;
  TEST_ASSERT_EQUAL(Status::Ok, drv.read(m));
  const uint8_t rd[] = {0x03, 0x00};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(rd, s.lastWrite, 2);
  TEST_ASSERT_EQUAL_UINT16(5, m.pm1_0);
  TEST_ASSERT_EQUAL_UINT16(9, m.pm2_5);
  TEST_ASSERT_EQUAL_UINT16(14, m.pm10);
  TEST_ASSERT_EQUAL(30, drv.lastRawLen());
  TEST_ASSERT_EQUAL_UINT32(5, clk.totalDelay);  // co delay giua lenh doc va doc du lieu

  TEST_ASSERT_EQUAL(Status::Ok, drv.stopMeasurement());
  const uint8_t stop[] = {0x01, 0x04};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(stop, s.lastWrite, 2);
  TEST_ASSERT_EQUAL(Status::NoResponse, drv.read(m));
}

void test_i2c_absent_sensor_nack() {
  FakeClock clk;
  FakeI2cSensor s;
  s.present = false;
  Apm10I2c drv(s, clk);
  Measurement m;
  TEST_ASSERT_EQUAL_STRING("I2C_NACK", statusToString(drv.startMeasurement()));
  TEST_ASSERT_EQUAL(Status::I2cNack, drv.stopMeasurement());
  TEST_ASSERT_EQUAL(Status::I2cNack, drv.read(m));
}

void test_i2c_wrong_address_nack() {
  FakeClock clk;
  FakeI2cSensor s;
  Apm10I2c drv(s, clk, 0x09);
  TEST_ASSERT_EQUAL(Status::I2cNack, drv.startMeasurement());
}

void test_i2c_corrupted_byte() {
  for (int idx = 0; idx < 30; ++idx) {
    FakeClock clk;
    FakeI2cSensor s;
    s.corruptIndex = idx;
    Apm10I2c drv(s, clk);
    TEST_ASSERT_EQUAL(Status::Ok, drv.startMeasurement());
    Measurement m;
    const Status st = drv.read(m);
    const int word = idx / 3;
    if (word == 0 || word == 1 || word == 3) {
      TEST_ASSERT_EQUAL(Status::CrcError, st);
    } else {
      TEST_ASSERT_EQUAL(Status::Ok, st);  // word du phong hong: chi canh bao
      TEST_ASSERT_TRUE(m.warnings & kWarnReservedCrc);
      TEST_ASSERT_EQUAL_UINT16(9, m.pm2_5);
    }
  }
}

void test_i2c_short_read() {
  FakeClock clk;
  FakeI2cSensor s;
  s.shortRead = 12;
  Apm10I2c drv(s, clk);
  drv.startMeasurement();
  Measurement m;
  TEST_ASSERT_EQUAL_STRING("SHORT_READ", statusToString(drv.read(m)));
}

void test_i2c_soak() {
  FakeClock clk;
  FakeI2cSensor s;
  Apm10I2c drv(s, clk);
  TEST_ASSERT_EQUAL(Status::Ok, drv.startMeasurement());
  srand(99);
  for (int i = 0; i < 3000; ++i) {
    s.pm1 = (uint16_t)(rand() % 600);
    s.pm25 = (uint16_t)(s.pm1 + rand() % 300);
    s.pm10 = (uint16_t)(s.pm25 + rand() % 100);
    s.corruptIndex = (rand() % 10 == 0) ? rand() % 30 : -1;
    Measurement m;
    const Status st = drv.read(m);
    if (st == Status::Ok) {
      TEST_ASSERT_EQUAL_UINT16(s.pm1, m.pm1_0);
      TEST_ASSERT_EQUAL_UINT16(s.pm25, m.pm2_5);
      TEST_ASSERT_EQUAL_UINT16(s.pm10, m.pm10);
    } else {
      TEST_ASSERT_EQUAL(Status::CrcError, st);
      TEST_ASSERT_TRUE(s.corruptIndex >= 0);
    }
  }
}

static int runAll() {
  UNITY_BEGIN();
  RUN_TEST(test_uart_read_all_sends_datasheet_command);
  RUN_TEST(test_uart_read_all_returns_fast_without_waiting_timeout);
  RUN_TEST(test_uart_len_byte_variants);
  RUN_TEST(test_uart_read_pm25_command);
  RUN_TEST(test_uart_silent_sensor_times_out);
  RUN_TEST(test_uart_slow_sensor_within_timeout);
  RUN_TEST(test_uart_too_slow_sensor_is_incomplete);
  RUN_TEST(test_uart_stale_bytes_are_flushed);
  RUN_TEST(test_uart_garbage_before_frame);
  RUN_TEST(test_uart_long_noise_burst_then_frame);
  RUN_TEST(test_uart_corrupted_byte_never_accepted);
  RUN_TEST(test_uart_truncated_frame);
  RUN_TEST(test_uart_sensor_only_supports_pm25);
  RUN_TEST(test_uart_soak_with_random_faults);
  RUN_TEST(test_i2c_probe);
  RUN_TEST(test_i2c_start_read_stop_sequence);
  RUN_TEST(test_i2c_absent_sensor_nack);
  RUN_TEST(test_i2c_wrong_address_nack);
  RUN_TEST(test_i2c_corrupted_byte);
  RUN_TEST(test_i2c_short_read);
  RUN_TEST(test_i2c_soak);
  return UNITY_END();
}

#ifdef ARDUINO
#include <Arduino.h>
void setup() {
  delay(2000);
  runAll();
}
void loop() {}
#else
int main() { return runAll(); }
#endif
