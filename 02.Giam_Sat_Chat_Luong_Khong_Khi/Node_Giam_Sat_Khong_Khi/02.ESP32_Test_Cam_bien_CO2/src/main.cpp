// Chuong trinh test cam bien CO2 ASAIR ACD10 (NDIR, 400~5000 ppm) tren ESP32.
// Doc CO2 moi 2 giay, in ket qua + thong ke + goi y xu ly loi ra Serial Monitor (115200).
//
// Lenh tren Serial Monitor (go roi bam Enter):
//   h : tro giup          i : thong tin cam bien (phien ban, so serial, che do hieu chuan)
//   r : bat/tat byte tho  s : in thong ke   c : xoa thong ke   p : tam dung / tiep tuc doc
//   auto / manual  : chon che do hieu chuan tu dong (mac dinh xuat xuong) / thu cong
//   cal <ppm>      : hieu chuan 1 diem (vd: cal 420) - can xac nhan bang 'y'
//   factory        : khoi phuc hieu chuan goc        - can xac nhan bang 'y'
#include <Arduino.h>
#include <Wire.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "acd10_arduino.h"
#include "acd10_driver.h"
#include "co2_level.h"
#include "config.h"

using namespace acd10;

static ArduinoClock gClock;

#if defined(ACD10_MODE_UART)
static ArduinoUartPort gPort(Serial2);
static Acd10Uart gSensor(gPort, gClock);
static const char* kModeName = "UART (1200 baud, 8N1)";
#else
static ArduinoI2cBus gBus(Wire);
static Acd10I2c gSensor(gBus, gClock);
static const char* kModeName = "I2C (dia chi 0x2A)";
#endif

// ---------------- Thong ke ----------------
struct Stat {
  uint32_t n = 0;
  uint32_t minV = 0xFFFFFFFF;
  uint32_t maxV = 0;
  uint64_t sum = 0;
  void add(uint32_t v) {
    ++n;
    sum += v;
    if (v < minV) minV = v;
    if (v > maxV) maxV = v;
  }
};

struct Stats {
  uint32_t reads = 0;
  uint32_t byStatus[static_cast<size_t>(Status::Count_)] = {0};
  uint32_t warnRange = 0, warnLen = 0, warnCmd = 0;
  Stat co2;        // chi tinh cac lan doc sau khi lam nong xong
  Stat tempRaw;
};

enum class Pending : uint8_t { None, Calibrate, FactoryReset };

static Stats gStats;
static bool gShowRaw = SHOW_RAW_DEFAULT;
static bool gPaused = false;
static uint32_t gConsecutiveFails = 0;
static uint32_t gLastReadMs = 0;
static uint32_t gBootMs = 0;
static Pending gPending = Pending::None;
static uint16_t gPendingPpm = 0;
static uint32_t gPendingAtMs = 0;
static char gLine[32];
static size_t gLineLen = 0;

static void printHex(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) Serial.printf("%02X ", data[i]);
}

static uint32_t preheatLeftMs() {
  const uint32_t up = millis() - gBootMs;
  return up >= kPreheatMs ? 0 : kPreheatMs - up;
}

static void printHelp() {
  Serial.println(F("Lenh (go roi bam Enter):"));
  Serial.println(F("  h=tro giup | i=thong tin cam bien | r=bat/tat byte tho | s=thong ke | c=xoa thong ke | p=tam dung"));
  Serial.println(F("  auto / manual = che do hieu chuan tu dong / thu cong"));
  Serial.println(F("  cal <ppm>     = hieu chuan 1 diem, vd 'cal 420' (de cam bien ngoai troi >= 20 phut truoc)"));
  Serial.println(F("  factory       = khoi phuc hieu chuan goc"));
}

static void printStatLine(const char* name, const Stat& s, const char* unit) {
  if (s.n == 0) {
    Serial.printf("  %-8s: chua co du lieu\n", name);
    return;
  }
  Serial.printf("  %-8s: min=%5lu  max=%5lu  tb=%8.1f %s (%lu mau)\n", name, (unsigned long)s.minV,
                (unsigned long)s.maxV, static_cast<double>(s.sum) / s.n, unit, (unsigned long)s.n);
}

static void printStats() {
  const uint32_t ok = gStats.byStatus[static_cast<size_t>(Status::Ok)];
  Serial.println(F("------------------- THONG KE -------------------"));
  Serial.printf("  Tong so lan doc: %lu | Thanh cong: %lu (%.1f%%)\n", (unsigned long)gStats.reads,
                (unsigned long)ok, gStats.reads ? 100.0 * ok / gStats.reads : 0.0);
  for (size_t i = 1; i < static_cast<size_t>(Status::Count_); ++i) {
    if (gStats.byStatus[i]) {
      Serial.printf("  Loi %-16s: %lu\n", statusToString(static_cast<Status>(i)), (unsigned long)gStats.byStatus[i]);
    }
  }
  if (gStats.warnRange) Serial.printf("  Ngoai dai 400~5000 ppm     : %lu (sau lam nong)\n", (unsigned long)gStats.warnRange);
  if (gStats.warnLen) Serial.printf("  Byte LEN khac datasheet    : %lu\n", (unsigned long)gStats.warnLen);
  if (gStats.warnCmd) Serial.printf("  Byte CMD khac datasheet    : %lu\n", (unsigned long)gStats.warnCmd);
  printStatLine("CO2", gStats.co2, "ppm");
#if defined(ACD10_MODE_I2C)
  printStatLine("T_raw", gStats.tempRaw, "(tho)");
#endif
  Serial.println(F("------------------------------------------------"));
}

static void printFailHints(Status st) {
  Serial.println(F("!!! Loi lien tiep - kiem tra:"));
  Serial.println(F("    1. Nguon 5V cho ACD10 (pin4 VCC, dong dinh 150 mA), GND (pin3) chung voi ESP32."));
  Serial.println(F("    2. Day tin hieu cua ACD10 keo len 5V ben trong -> phai qua mach chuyen muc 5V<->3.3V."));
#if defined(ACD10_MODE_I2C)
  Serial.println(F("    3. Chan SET (pin5) phai de ho hoac muc cao -> che do I2C (rut nguon cam bien sau khi doi)."));
  Serial.printf("    4. ACD10 pin1 SDA <-> GPIO%d, pin2 SCL <-> GPIO%d. Thu dao 2 day neu quet I2C khong thay 0x2A.\n",
                ACD10_I2C_SDA_PIN, ACD10_I2C_SCL_PIN);
  if (st == Status::CrcError)
    Serial.println(F("    5. CRC sai -> nhieu tren bus: day ngan lai, kiem tra GND va dien tro keo len."));
  else if (st == Status::I2cNack)
    Serial.println(F("    5. NACK -> cam bien khong tra loi dia chi 0x2A: sai day, sai che do SET hoac chua co nguon."));
  else
    Serial.println(F("    5. Doc thieu/khong co du lieu -> thu tang thoi gian cho kDefaultReadDelayMs (acd10_driver.h)."));
#else
  Serial.println(F("    3. Chan SET (pin5) phai noi GND -> che do UART (rut nguon cam bien sau khi doi)."));
  Serial.printf("    4. Day cheo: ACD10 pin2 TX -> GPIO%d, ACD10 pin1 RX <- GPIO%d. Thu dao 2 day.\n", ACD10_UART_RX_PIN,
                ACD10_UART_TX_PIN);
  if (st == Status::BadChecksum || st == Status::BadFrame)
    Serial.println(F("    5. Co byte nhung sai khung -> nhieu/sai muc dien ap. Go 'r' xem byte tho."));
  else
    Serial.println(F("    5. Khong nhan byte nao -> sai day RX/TX, SET chua noi GND, hoac cam bien chua cap nguon."));
#endif
}

static void printLastRaw() {
  if (gSensor.lastRawLen() > 0) {
    Serial.printf(" | %u byte: ", static_cast<unsigned>(gSensor.lastRawLen()));
    printHex(gSensor.lastRaw(), gSensor.lastRawLen());
  }
}

static void recordAndPrint(Status st, const Measurement& m) {
  ++gStats.reads;
  ++gStats.byStatus[static_cast<size_t>(st)];
  const uint32_t t = (millis() - gBootMs) / 1000;
  const uint32_t preheat = preheatLeftMs();

  if (st != Status::Ok) {
    ++gConsecutiveFails;
    Serial.printf("[%5lus] LOI: %s", (unsigned long)t, statusToString(st));
#if defined(ACD10_MODE_UART)
    Serial.printf(" (cho %lu ms)", (unsigned long)gSensor.lastElapsedMs());
#else
    if (st == Status::I2cNack) Serial.printf(" (Wire loi %u)", gBus.lastError());
#endif
    printLastRaw();
    Serial.println();
    if (gConsecutiveFails == FAIL_HINT_AFTER) printFailHints(st);
    return;
  }

  if (gConsecutiveFails >= FAIL_HINT_AFTER) Serial.println(F(">>> Da doc lai duoc du lieu."));
  gConsecutiveFails = 0;
  if (preheat == 0) {
    gStats.co2.add(m.co2_ppm);
    if (m.warnings & kWarnOutOfRange) ++gStats.warnRange;
  }
  if (m.has_temp) gStats.tempRaw.add(m.temp_raw);
  if (m.warnings & kWarnLengthMismatch) ++gStats.warnLen;
  if (m.warnings & kWarnCmdMismatch) ++gStats.warnCmd;

  Serial.printf("[%5lus] CO2 = %5lu ppm | %-19s", (unsigned long)t, (unsigned long)m.co2_ppm,
                co2LevelName(classifyCo2(m.co2_ppm)));
  if (m.has_temp) Serial.printf(" | T_raw=%5u", m.temp_raw);
  if (preheat > 0) Serial.printf(" | dang lam nong, con %lus", (unsigned long)((preheat + 999) / 1000));
  if (m.warnings & kWarnOutOfRange) Serial.print(F(" | ngoai dai 400~5000"));
  if (m.warnings & kWarnLengthMismatch) Serial.print(F(" | LEN khac datasheet"));
  if (m.warnings & kWarnCmdMismatch) Serial.print(F(" | CMD khac datasheet"));
  if (gShowRaw) {
    Serial.print(F(" | raw: "));
    printHex(gSensor.lastRaw(), gSensor.lastRawLen());
  }
  Serial.println();

  if (gStats.reads % STATS_EVERY_N_READS == 0) printStats();
}

static void doRead() {
  Measurement m;
  const Status st = gSensor.readCo2(m);
  recordAndPrint(st, m);
}

static void printInfo() {
  char buf[kAsciiLen + 1];
  Status st = gSensor.readVersion(buf, sizeof(buf));
  Serial.printf("  Phien ban phan mem : %s", st == Status::Ok ? buf : statusToString(st));
  printLastRaw();
  Serial.println();
  st = gSensor.readSerial(buf, sizeof(buf));
  Serial.printf("  So serial          : %s", st == Status::Ok ? buf : statusToString(st));
  printLastRaw();
  Serial.println();
#if defined(ACD10_MODE_I2C)
  uint8_t mode = 0;
  st = gSensor.readCalibrationMode(mode);
  if (st == Status::Ok)
    Serial.printf("  Che do hieu chuan  : %s\n", mode == kCalModeAuto ? "TU DONG (mac dinh)" : "THU CONG");
  else
    Serial.printf("  Che do hieu chuan  : %s\n", statusToString(st));
#else
  Serial.println(F("  Che do hieu chuan  : UART khong co lenh doc lai (mac dinh xuat xuong: TU DONG)"));
#endif
}

static void setCalMode(uint8_t mode) {
  const Status st = gSensor.setCalibrationMode(mode);
  Serial.printf("Dat che do hieu chuan %s -> %s\n", mode == kCalModeAuto ? "TU DONG" : "THU CONG", statusToString(st));
}

static void runPending() {
  Status st = Status::InvalidArgument;
  if (gPending == Pending::Calibrate) {
    st = gSensor.manualCalibrate(gPendingPpm);
    Serial.printf("Hieu chuan 1 diem %u ppm -> %s\n", gPendingPpm, statusToString(st));
  } else if (gPending == Pending::FactoryReset) {
    st = gSensor.factoryReset();
    Serial.printf("Khoi phuc hieu chuan goc -> %s\n", statusToString(st));
  }
  gPending = Pending::None;
}

static void handleLine(char* line) {
  for (char* p = line; *p; ++p) *p = static_cast<char>(tolower(static_cast<unsigned char>(*p)));
  while (*line == ' ') ++line;
  size_t n = strlen(line);
  while (n > 0 && line[n - 1] == ' ') line[--n] = '\0';
  if (n == 0) return;

  // Lenh xac nhan
  if (gPending != Pending::None) {
    if (strcmp(line, "y") == 0 && millis() - gPendingAtMs <= CONFIRM_TIMEOUT_MS) {
      runPending();
      return;
    }
    Serial.println(F("Da huy lenh dang cho xac nhan."));
    gPending = Pending::None;
    if (strcmp(line, "y") == 0) return;
  }

  if (strcmp(line, "h") == 0 || strcmp(line, "?") == 0) {
    printHelp();
  } else if (strcmp(line, "i") == 0) {
    printInfo();
  } else if (strcmp(line, "r") == 0) {
    gShowRaw = !gShowRaw;
    Serial.printf("In byte tho: %s\n", gShowRaw ? "BAT" : "TAT");
  } else if (strcmp(line, "s") == 0) {
    printStats();
  } else if (strcmp(line, "c") == 0) {
    gStats = Stats();
    Serial.println(F("Da xoa thong ke."));
  } else if (strcmp(line, "p") == 0) {
    gPaused = !gPaused;
    Serial.println(gPaused ? F("Tam dung doc.") : F("Tiep tuc doc."));
  } else if (strcmp(line, "auto") == 0) {
    setCalMode(kCalModeAuto);
  } else if (strcmp(line, "manual") == 0) {
    setCalMode(kCalModeManual);
  } else if (strncmp(line, "cal", 3) == 0) {
    char* end = nullptr;
    const long ppm = strtol(line + 3, &end, 10);
    if (end == line + 3 || *end != '\0' || ppm < (long)kMinPpm || ppm > (long)kMaxPpm) {
      Serial.println(F("Cu phap: cal <ppm>, ppm trong khoang 400..5000. Vd: cal 420"));
      return;
    }
    gPending = Pending::Calibrate;
    gPendingPpm = static_cast<uint16_t>(ppm);
    gPendingAtMs = millis();
    Serial.printf("CANH BAO: hieu chuan sai se lam lech moi ket qua sau nay!\n"
                  "Cam bien phai o moi truong CO2 = %ld ppm on dinh >= 20 phut. Go 'y' trong %d s de xac nhan.\n",
                  ppm, CONFIRM_TIMEOUT_MS / 1000);
  } else if (strcmp(line, "factory") == 0) {
    gPending = Pending::FactoryReset;
    gPendingAtMs = millis();
    Serial.printf("Khoi phuc hieu chuan goc cua nha san xuat? Go 'y' trong %d s de xac nhan.\n",
                  CONFIRM_TIMEOUT_MS / 1000);
  } else {
    Serial.printf("Lenh khong hop le: '%s'. Go 'h' de xem tro giup.\n", line);
  }
}

static void pollSerial() {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (gLineLen > 0) {
        gLine[gLineLen] = '\0';
        gLineLen = 0;
        handleLine(gLine);
      }
    } else if (c >= 0x20 && c < 0x7F && gLineLen < sizeof(gLine) - 1) {
      gLine[gLineLen++] = static_cast<char>(c);
    }
  }
}

#if defined(ACD10_MODE_I2C)
static void scanI2c() {
  Serial.print(F("Quet bus I2C: "));
  int found = 0;
  for (uint8_t addr = 1; addr < 127; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("0x%02X ", addr);
      ++found;
    }
  }
  Serial.println(found ? "" : "khong tim thay thiet bi nao!");
}
#endif

void setup() {
  Serial.begin(115200);
  delay(1500);
  gBootMs = millis();
  Serial.println();
  Serial.println(F("================================================"));
  Serial.println(F(" TEST CAM BIEN CO2 ASAIR ACD10 (NDIR 400~5000ppm)"));
  Serial.println(F("================================================"));
  Serial.printf("Che do giao tiep: %s\n", kModeName);

#if defined(ACD10_MODE_UART)
  Serial2.begin(kUartBaud, SERIAL_8N1, ACD10_UART_RX_PIN, ACD10_UART_TX_PIN);
  Serial.printf("UART2: RX=GPIO%d (noi ACD10 pin2 TX), TX=GPIO%d (noi ACD10 pin1 RX)\n", ACD10_UART_RX_PIN,
                ACD10_UART_TX_PIN);
  uint8_t cmd[kUartOverhead];
  buildUartFrame(kUartCmdReadCo2, nullptr, 0, cmd, sizeof(cmd));
  Serial.print(F("Lenh doc CO2: "));
  printHex(cmd, sizeof(cmd));
  Serial.println();
#else
  Wire.begin(ACD10_I2C_SDA_PIN, ACD10_I2C_SCL_PIN, ACD10_I2C_FREQ_HZ);
  Wire.setTimeOut(100);
  Serial.printf("I2C: SDA=GPIO%d, SCL=GPIO%d, %lu Hz\n", ACD10_I2C_SDA_PIN, ACD10_I2C_SCL_PIN,
                (unsigned long)ACD10_I2C_FREQ_HZ);
  scanI2c();
  Serial.printf("ACD10 tai 0x%02X: %s\n", kI2cAddress, gSensor.probe() ? "CO phan hoi (ACK)" : "KHONG phan hoi");
#endif
  Serial.println(F("Thong tin cam bien:"));
  printInfo();
  Serial.printf("Doc moi %d ms. %lu giay dau la thoi gian lam nong (so doc chi de tham khao).\n", READ_INTERVAL_MS,
                (unsigned long)(kPreheatMs / 1000));
  printHelp();
  Serial.println(F("------------------------------------------------"));
  gLastReadMs = millis() - READ_INTERVAL_MS;
}

void loop() {
  pollSerial();
  if (gPending != Pending::None && millis() - gPendingAtMs > CONFIRM_TIMEOUT_MS) {
    Serial.println(F("Het thoi gian xac nhan - da huy lenh."));
    gPending = Pending::None;
  }
  if (gPaused) return;
  const uint32_t now = millis();
  if (now - gLastReadMs >= READ_INTERVAL_MS) {
    gLastReadMs = now;
    doRead();
  }
}
