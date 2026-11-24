// Chuong trinh test cam bien TVOC ASAIR AGS10 (module MakerEdu MKE-S17) tren ESP32.
// Doc TVOC moi READ_INTERVAL_MS, in ket qua + thong ke + goi y xu ly loi ra Serial Monitor (115200).
//
// Phim lenh tren Serial Monitor:
//   h : tro giup        r : bat/tat in byte tho (hex)     s : in thong ke ngay
//   c : xoa thong ke    p : tam dung / tiep tuc doc
//   v : doc phien ban firmware        o : doc dien tro cam bien
//   z : hieu chuan diem 0 bang dien tro hien tai (can xac nhan 'y')
//   f : tra diem 0 ve gia tri nha san xuat     (can xac nhan 'y')
#include <Arduino.h>
#include <Wire.h>

#include "ags10_arduino.h"
#include "ags10_driver.h"
#include "config.h"
#include "tvoc_level.h"

using namespace ags10;

static ArduinoClock gClock;
static ArduinoI2cBus gBus(Wire);
static Ags10 gSensor(gBus, gClock);

// ---------------- Thong ke ----------------
struct Stats {
  uint32_t reads = 0;
  uint32_t byStatus[static_cast<size_t>(Status::Count_)] = {0};
  uint32_t warnRange = 0;
  uint32_t n = 0;
  uint32_t minPpb = 0xFFFFFFFFUL;
  uint32_t maxPpb = 0;
  uint64_t sumPpb = 0;
  void add(uint32_t v) {
    ++n;
    sumPpb += v;
    if (v < minPpb) minPpb = v;
    if (v > maxPpb) maxPpb = v;
  }
};

enum class Pending : uint8_t { None, ZeroCurrent, ZeroFactory };

static Stats gStats;
static bool gShowRaw = SHOW_RAW_DEFAULT;
static bool gPaused = false;
static bool gEverReady = false;
static bool gStuckHintShown = false;
static uint32_t gConsecutiveFails = 0;
static uint32_t gLastReadMs = 0;
static uint32_t gBootMs = 0;
static Pending gPending = Pending::None;
static uint32_t gPendingSinceMs = 0;

static unsigned long ul(uint32_t v) { return static_cast<unsigned long>(v); }

static void printHex(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) Serial.printf("%02X ", data[i]);
}

static void printHelp() {
  Serial.println(F("Phim lenh: h=tro giup | r=bat/tat byte tho | s=thong ke | c=xoa thong ke | p=tam dung"));
  Serial.println(F("           v=phien ban | o=dien tro | z=hieu chuan diem 0 | f=diem 0 nha san xuat"));
}

static void printStats() {
  const uint32_t ok = gStats.byStatus[static_cast<size_t>(Status::Ok)];
  Serial.println(F("------------------- THONG KE -------------------"));
  Serial.printf("  Tong so lan doc: %lu | Thanh cong: %lu (%.1f%%)\n", ul(gStats.reads), ul(ok),
                gStats.reads ? 100.0 * ok / gStats.reads : 0.0);
  for (size_t i = 1; i < static_cast<size_t>(Status::Count_); ++i) {
    if (gStats.byStatus[i]) {
      Serial.printf("  %-4s %-16s: %lu\n", i == static_cast<size_t>(Status::NotReady) ? "" : "Loi",
                    statusToString(static_cast<Status>(i)), ul(gStats.byStatus[i]));
    }
  }
  if (gStats.warnRange) Serial.printf("  Canh bao vuot 99999 ppb    : %lu\n", ul(gStats.warnRange));
  if (gStats.n == 0) {
    Serial.println(F("  TVOC: chua co du lieu hop le"));
  } else {
    Serial.printf("  TVOC: min=%lu  max=%lu  tb=%.1f ppb  (%lu mau)\n", ul(gStats.minPpb), ul(gStats.maxPpb),
                  static_cast<double>(gStats.sumPpb) / gStats.n, ul(gStats.n));
  }
  Serial.println(F("------------------------------------------------"));
}

static void printFailHints(Status st) {
  Serial.println(F("!!! Loi lien tiep - kiem tra:"));
  Serial.println(F("    1. Nguon 5V cho module MKE-S17 (~30 mA), GND chung voi ESP32."));
  Serial.printf("    2. SDA<->GPIO%d, SCL<->GPIO%d. Dao thu 2 day neu nghi noi nham.\n", AGS10_I2C_SDA_PIN,
                AGS10_I2C_SCL_PIN);
  Serial.printf("    3. Toc do I2C hien tai %lu Hz (AGS10 chi chap nhan <= 15000 Hz).\n", ul(Wire.getClock()));
  if (st == Status::CrcError || st == Status::ShortRead)
    Serial.println(F("    4. Co du lieu nhung sai CRC/thieu byte -> nhieu: day ngan hon, giam toc do I2C, kiem tra keo len."));
  else
    Serial.println(F("    4. Khong ACK/khong du lieu -> sai day, chua cap nguon, hoac sai dia chi (mac dinh 0x1A)."));
}

static void printVersion() {
  uint8_t v = 0;
  const Status st = gSensor.readVersion(v);
  if (st == Status::Ok)
    Serial.printf("Phien ban firmware AGS10: 0x%02X\n", v);
  else
    Serial.printf("Doc phien ban: %s\n", statusToString(st));
}

static void printResistance() {
  uint32_t r = 0;
  const Status st = gSensor.readResistance(r);
  if (st == Status::Ok)
    Serial.printf("Dien tro cam bien: %lu.%lu kOhm (raw=%lu x 0.1 kOhm)\n", ul(r / 10), ul(r % 10), ul(r));
  else
    Serial.printf("Doc dien tro: %s\n", statusToString(st));
  if (gSensor.lastWaitMs() > 0)
    Serial.printf("  (da cho %lu ms de dung khoang cach >= 1.5 s giua cac lan doc)\n", ul(gSensor.lastWaitMs()));
}

static void recordAndPrint(Status st, const TvocReading& r) {
  ++gStats.reads;
  ++gStats.byStatus[static_cast<size_t>(st)];
  const uint32_t uptime = millis() - gBootMs;
  const unsigned long t = ul(uptime / 1000);

  if (st == Status::NotReady) {
    gConsecutiveFails = 0;  // giao tiep van tot, chi la cam bien chua san sang
    if (uptime < kPreheatMs + PREHEAT_GRACE_MS) {
      const uint32_t left = uptime < kPreheatMs ? (kPreheatMs - uptime) / 1000 : 0;
      Serial.printf("[%5lus] Dang lam nong (RDY=1), con ~%lu s | gia tri tam %lu ppb (chua dung)\n", t, ul(left),
                    ul(r.tvocPpb));
    } else {
      Serial.printf("[%5lus] CHUA SAN SANG (RDY=1) du da qua thoi gian lam nong | STATUS=0x%02X\n", t, r.status);
      if (!gStuckHintShown) {
        gStuckHintShown = true;
        Serial.println(F("!!! RDY luon = 1: datasheet cho biet doc TVOC nhanh hon 1.5 s se gay loi nay."));
        Serial.println(F("    Kiem tra READ_INTERVAL_MS, nguon cap on dinh (khong cap ngat quang), roi rut nguon cam bien."));
      }
    }
    return;
  }

  if (st != Status::Ok) {
    ++gConsecutiveFails;
    Serial.printf("[%5lus] LOI: %s", t, statusToString(st));
    if (gSensor.lastRawLen() > 0) {
      Serial.printf(" | %u byte: ", static_cast<unsigned>(gSensor.lastRawLen()));
      printHex(gSensor.lastRaw(), gSensor.lastRawLen());
    }
    Serial.println();
    if (gConsecutiveFails == FAIL_HINT_AFTER) printFailHints(st);
    return;
  }

  if (gConsecutiveFails >= FAIL_HINT_AFTER) Serial.println(F(">>> Da doc lai duoc du lieu."));
  gConsecutiveFails = 0;
  if (!gEverReady) {
    gEverReady = true;
    Serial.printf(">>> Cam bien san sang sau %lu s.\n", t);
  }
  gStats.add(r.tvocPpb);
  if (r.warnings & kWarnOutOfRange) ++gStats.warnRange;

  Serial.printf("[%5lus] TVOC=%5lu ppb  ~%6lu ug/m3 | %-13s", t, ul(r.tvocPpb), ul(tvocPpbToUgm3(r.tvocPpb)),
                tvocLevelName(classifyTvocPpb(r.tvocPpb)));
  if (r.warnings & kWarnOutOfRange) Serial.print(F(" | CANH BAO: vuot dai do 99999 ppb"));
  if (gShowRaw) {
    Serial.print(F(" | raw: "));
    printHex(gSensor.lastRaw(), gSensor.lastRawLen());
  }
  Serial.println();

  if (gStats.reads % STATS_EVERY_N_READS == 0) printStats();
}

static void doRead() {
  TvocReading r;
  const Status st = gSensor.readTvoc(r);
  recordAndPrint(st, r);
}

static void askConfirm(Pending action) {
  gPending = action;
  gPendingSinceMs = millis();
  if (action == Pending::ZeroCurrent) {
    Serial.println(F("Hieu chuan diem 0 bang dien tro hien tai. CHI lam khi cam bien da o trong khong khi sach >= 15 phut."));
  } else {
    Serial.println(F("Tra diem 0 ve gia tri hieu chuan cua nha san xuat."));
  }
  Serial.printf("Nhan 'y' trong %d s de xac nhan, phim khac de huy.\n", CONFIRM_TIMEOUT_MS / 1000);
}

static void runPending() {
  const Status st =
      gPending == Pending::ZeroCurrent ? gSensor.zeroPointCalibrateCurrent() : gSensor.zeroPointFactoryReset();
  Serial.printf("%s -> %s", gPending == Pending::ZeroCurrent ? "Hieu chuan diem 0" : "Diem 0 nha san xuat",
                statusToString(st));
  Serial.println(st == Status::Ok ? F(" (gia tri mat khi mat nguon)") : F(""));
  gPending = Pending::None;
}

static void handleKey(int c) {
  if (c == '\r' || c == '\n') return;  // Serial Monitor gui kem ky tu xuong dong
  if (gPending != Pending::None) {
    if (c == 'y' || c == 'Y') {
      runPending();
    } else {
      gPending = Pending::None;
      Serial.println(F("Da huy."));
    }
    return;
  }
  switch (c) {
    case 'h': case 'H': printHelp(); break;
    case 'r': case 'R':
      gShowRaw = !gShowRaw;
      Serial.printf("In byte tho: %s\n", gShowRaw ? "BAT" : "TAT");
      break;
    case 's': case 'S': printStats(); break;
    case 'c': case 'C':
      gStats = Stats();
      Serial.println(F("Da xoa thong ke."));
      break;
    case 'p': case 'P':
      gPaused = !gPaused;
      Serial.println(gPaused ? F("Tam dung doc.") : F("Tiep tuc doc."));
      break;
    case 'v': case 'V': printVersion(); break;
    case 'o': case 'O': printResistance(); break;
    case 'z': case 'Z': askConfirm(Pending::ZeroCurrent); break;
    case 'f': case 'F': askConfirm(Pending::ZeroFactory); break;
    default: break;
  }
}

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
  delay(kCommandGapMs);  // lan quet vua cham toi AGS10, cho dung 30 ms truoc lenh tiep theo
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  gBootMs = millis();
  Serial.println();
  Serial.println(F("================================================"));
  Serial.println(F(" TEST CAM BIEN TVOC ASAIR AGS10 (MKE-S17) - I2C"));
  Serial.println(F("================================================"));

  Wire.begin(AGS10_I2C_SDA_PIN, AGS10_I2C_SCL_PIN, AGS10_I2C_FREQ_HZ);
  Wire.setTimeOut(AGS10_I2C_TIMEOUT_MS);
  Serial.printf("I2C: SDA=GPIO%d, SCL=GPIO%d, %lu Hz (yeu cau <= %lu Hz)\n", AGS10_I2C_SDA_PIN, AGS10_I2C_SCL_PIN,
                ul(Wire.getClock()), ul(kMaxI2cFreqHz));
  if (Wire.getClock() > kMaxI2cFreqHz) Serial.println(F("!!! Toc do I2C vuot 15 kHz, AGS10 se khong tra loi."));

  if (SCAN_I2C_ON_BOOT) scanI2c();
  Serial.printf("AGS10 tai 0x%02X: %s\n", gSensor.address(), gSensor.probe() ? "CO phan hoi (ACK)" : "KHONG phan hoi");
  printVersion();
  printResistance();

  Serial.printf("Doc moi %d ms. Cam bien can lam nong %lu s sau khi cap nguon (bit RDY).\n", READ_INTERVAL_MS,
                ul(kPreheatMs / 1000));
  Serial.println(F("Luu y: cam bien moi/lau khong dung can cap nguon lien tuc 12-96 gio de gia tri on dinh."));
  printHelp();
  Serial.println(F("------------------------------------------------"));
  gLastReadMs = millis();
}

void loop() {
  while (Serial.available() > 0) handleKey(Serial.read());
  if (gPending != Pending::None && millis() - gPendingSinceMs >= CONFIRM_TIMEOUT_MS) {
    gPending = Pending::None;
    Serial.println(F("Het thoi gian xac nhan, da huy."));
  }
  if (gPaused) return;
  const uint32_t now = millis();
  if (now - gLastReadMs >= READ_INTERVAL_MS) {
    gLastReadMs = now;
    doRead();
  }
}
