// Chuong trinh test cam bien bui ASAIR APM10 tren ESP32.
// Doc PM1.0 / PM2.5 / PM10 moi giay, in ket qua + thong ke + goi y xu ly loi ra Serial Monitor (115200).
//
// Phim lenh tren Serial Monitor:
//   h : tro giup        r : bat/tat in byte tho (hex)     s : in thong ke ngay
//   c : xoa thong ke    p : tam dung / tiep tuc doc
//   2 : (UART) doc thu lenh rieng PM2.5 (0x00)
//   x : (I2C) gui lenh dung do        g : (I2C) gui lenh bat dau do
#include <Arduino.h>
#include <Wire.h>

#include "apm10_arduino.h"
#include "apm10_driver.h"
#include "config.h"
#include "pm25_level.h"

using namespace apm10;

static ArduinoClock gClock;

#if defined(APM10_MODE_UART)
static ArduinoUartPort gPort(Serial2);
static Apm10Uart gSensor(gPort, gClock);
static const char* kModeName = "UART (1200 baud, 8N1)";
#else
static ArduinoI2cBus gBus(Wire);
static Apm10I2c gSensor(gBus, gClock);
static const char* kModeName = "I2C (dia chi 0x08)";
#endif

// ---------------- Thong ke ----------------
struct Stat {
  uint32_t n = 0;
  uint16_t minV = 0xFFFF;
  uint16_t maxV = 0;
  uint64_t sum = 0;
  void add(uint16_t v) {
    ++n;
    sum += v;
    if (v < minV) minV = v;
    if (v > maxV) maxV = v;
  }
};

struct Stats {
  uint32_t reads = 0;
  uint32_t byStatus[static_cast<size_t>(Status::Count_)] = {0};
  uint32_t warnOrder = 0, warnRange = 0, warnLen = 0, warnReserved = 0, warnPm25Only = 0;
  Stat pm1, pm25, pm10;
};

static Stats gStats;
static bool gShowRaw = SHOW_RAW_DEFAULT;
static bool gPaused = false;
static uint32_t gConsecutiveFails = 0;
static uint32_t gLastReadMs = 0;
static uint32_t gBootMs = 0;

static void printHex(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) Serial.printf("%02X ", data[i]);
}

static void printHelp() {
  Serial.println(F("Phim lenh: h=tro giup | r=bat/tat byte tho | s=thong ke | c=xoa thong ke | p=tam dung"));
#if defined(APM10_MODE_UART)
  Serial.println(F("           2=doc thu lenh rieng PM2.5 (0x00)"));
#else
  Serial.println(F("           x=dung do (stop) | g=bat dau do (start)"));
#endif
}

static void printStatLine(const char* name, const Stat& s) {
  if (s.n == 0) {
    Serial.printf("  %-6s: chua co du lieu\n", name);
    return;
  }
  Serial.printf("  %-6s: min=%4u  max=%4u  tb=%7.1f ug/m3  (%lu mau)\n", name, s.minV, s.maxV,
                static_cast<double>(s.sum) / s.n, static_cast<unsigned long>(s.n));
}

static void printStats() {
  const uint32_t ok = gStats.byStatus[static_cast<size_t>(Status::Ok)];
  Serial.println(F("------------------- THONG KE -------------------"));
  Serial.printf("  Tong so lan doc: %lu | Thanh cong: %lu (%.1f%%)\n", static_cast<unsigned long>(gStats.reads),
                static_cast<unsigned long>(ok), gStats.reads ? 100.0 * ok / gStats.reads : 0.0);
  for (size_t i = 1; i < static_cast<size_t>(Status::Count_); ++i) {
    if (gStats.byStatus[i]) {
      Serial.printf("  Loi %-16s: %lu\n", statusToString(static_cast<Status>(i)),
                    static_cast<unsigned long>(gStats.byStatus[i]));
    }
  }
  if (gStats.warnOrder) Serial.printf("  Canh bao PM1<=PM2.5<=PM10 sai: %lu\n", (unsigned long)gStats.warnOrder);
  if (gStats.warnRange) Serial.printf("  Canh bao vuot 1000 ug/m3     : %lu\n", (unsigned long)gStats.warnRange);
  if (gStats.warnLen) Serial.printf("  Byte LEN khac datasheet      : %lu (binh thuong, xem README)\n", (unsigned long)gStats.warnLen);
  if (gStats.warnReserved) Serial.printf("  CRC word du phong sai (I2C)  : %lu\n", (unsigned long)gStats.warnReserved);
  if (gStats.warnPm25Only) Serial.printf("  Chi nhan PM2.5 (thieu PM1/10): %lu\n", (unsigned long)gStats.warnPm25Only);
  printStatLine("PM1.0", gStats.pm1);
  printStatLine("PM2.5", gStats.pm25);
  printStatLine("PM10", gStats.pm10);
  Serial.println(F("------------------------------------------------"));
}

static void printFailHints(Status st) {
  Serial.println(F("!!! Loi lien tiep - kiem tra:"));
  Serial.println(F("    1. Nguon 5V cho APM10 (dong ~100mA), GND chung voi ESP32. Quat cam bien co quay khong?"));
#if defined(APM10_MODE_UART)
  Serial.println(F("    2. Chan SET (pin3) phai de ho hoac muc cao -> che do UART."));
  Serial.printf("    3. Day cheo: APM10 TX(pin5)->GPIO%d, APM10 RX(pin4)<-GPIO%d. Thu dao 2 day.\n", APM10_UART_RX_PIN,
                APM10_UART_TX_PIN);
  if (st == Status::BadChecksum || st == Status::BadFrame)
    Serial.println(F("    4. Co byte nhung sai khung -> nhieu/sai baud. Bam 'r' xem byte tho, day ngan, GND tot."));
  else
    Serial.println(F("    4. Khong nhan byte nao -> sai day RX/TX, sai che do SET, hoac cam bien chua cap nguon."));
#else
  Serial.println(F("    2. Chan SET (pin3) phai noi GND -> che do I2C (rut nguon cam bien sau khi doi SET)."));
  Serial.printf("    3. SDA(pin4)<->GPIO%d, SCL(pin5)<->GPIO%d, co dien tro keo len 4.7k ve 3.3V.\n", APM10_I2C_SDA_PIN,
                APM10_I2C_SCL_PIN);
  if (st == Status::CrcError)
    Serial.println(F("    4. CRC sai -> nhieu tren bus: giam do dai day, kiem tra dien tro keo len."));
  else
    Serial.println(F("    4. NACK/khong du lieu -> cam bien chua o che do do (bam 'g') hoac sai day."));
#endif
}

static void recordAndPrint(Status st, const Measurement& m, uint32_t extraMs) {
  ++gStats.reads;
  ++gStats.byStatus[static_cast<size_t>(st)];
  const uint32_t t = (millis() - gBootMs) / 1000;

  if (st != Status::Ok) {
    ++gConsecutiveFails;
    Serial.printf("[%5lus] LOI: %s", static_cast<unsigned long>(t), statusToString(st));
#if defined(APM10_MODE_UART)
    Serial.printf(" (cho %lu ms)", static_cast<unsigned long>(extraMs));
#else
    (void)extraMs;
#endif
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
  if (m.has_pm1_0) gStats.pm1.add(m.pm1_0);
  if (m.has_pm2_5) gStats.pm25.add(m.pm2_5);
  if (m.has_pm10) gStats.pm10.add(m.pm10);
  if (m.warnings & kWarnOrder) ++gStats.warnOrder;
  if (m.warnings & kWarnOutOfRange) ++gStats.warnRange;
  if (m.warnings & kWarnLengthMismatch) ++gStats.warnLen;
  if (m.warnings & kWarnReservedCrc) ++gStats.warnReserved;
  if (m.warnings & kWarnPm25Only) ++gStats.warnPm25Only;

  Serial.printf("[%5lus] ", static_cast<unsigned long>(t));
  if (m.has_pm1_0) Serial.printf("PM1.0=%4u  ", m.pm1_0); else Serial.print(F("PM1.0=  --  "));
  Serial.printf("PM2.5=%4u  ", m.pm2_5);
  if (m.has_pm10) Serial.printf("PM10=%4u  ", m.pm10); else Serial.print(F("PM10=  --  "));
  Serial.printf("ug/m3 | %-10s", pm25LevelName(classifyPm25(m.pm2_5)));
  if (millis() - gBootMs < WARMUP_MS) Serial.print(F(" | dang khoi dong"));
  if (m.warnings & kWarnOrder) Serial.print(F(" | CANH BAO: PM1<=PM2.5<=PM10 sai"));
  if (m.warnings & kWarnOutOfRange) Serial.print(F(" | CANH BAO: vuot 1000"));
  if (m.warnings & kWarnPm25Only) Serial.print(F(" | chi co PM2.5"));
  if (m.warnings & kWarnReservedCrc) Serial.print(F(" | CRC du phong sai"));
  if (gShowRaw) {
    Serial.print(F(" | raw: "));
    printHex(gSensor.lastRaw(), gSensor.lastRawLen());
  }
  Serial.println();
}

static void doRead() {
  Measurement m;
#if defined(APM10_MODE_UART)
  const Status st = gSensor.readAll(m);
  recordAndPrint(st, m, gSensor.lastElapsedMs());
#else
  const Status st = gSensor.read(m);
  recordAndPrint(st, m, 0);
#endif
  if (gStats.reads % STATS_EVERY_N_READS == 0) printStats();
}

static void handleKey(int c) {
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
#if defined(APM10_MODE_UART)
    case '2': {
      Measurement m;
      const Status st = gSensor.readPm25(m);
      Serial.printf("Lenh 0x00 (gui: ");
      printHex(gSensor.lastCommand(), kUartCommandLen);
      Serial.printf(") -> %s", statusToString(st));
      if (st == Status::Ok) Serial.printf(", PM2.5=%u ug/m3", m.pm2_5);
      Serial.print(F(" | nhan: "));
      printHex(gSensor.lastRaw(), gSensor.lastRawLen());
      Serial.println();
      break;
    }
#else
    case 'x': case 'X':
      Serial.printf("Stop measurement -> %s\n", statusToString(gSensor.stopMeasurement()));
      break;
    case 'g': case 'G':
      Serial.printf("Start measurement -> %s\n", statusToString(gSensor.startMeasurement()));
      break;
#endif
    default: break;
  }
}

#if defined(APM10_MODE_I2C)
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
  Serial.println(F(" TEST CAM BIEN BUI ASAIR APM10 - PM1.0/PM2.5/PM10"));
  Serial.println(F("================================================"));
  Serial.printf("Che do giao tiep: %s\n", kModeName);

#if defined(APM10_MODE_UART)
  Serial2.begin(kUartBaud, SERIAL_8N1, APM10_UART_RX_PIN, APM10_UART_TX_PIN);
  Serial.printf("UART2: RX=GPIO%d (noi APM10 TX pin5), TX=GPIO%d (noi APM10 RX pin4)\n", APM10_UART_RX_PIN,
                APM10_UART_TX_PIN);
  uint8_t cmd[kUartCommandLen];
  buildUartCommand(kCmdReadAll, cmd, sizeof(cmd));
  Serial.print(F("Lenh doc 3 thong so: "));
  printHex(cmd, sizeof(cmd));
  Serial.println();
#else
  Wire.begin(APM10_I2C_SDA_PIN, APM10_I2C_SCL_PIN, APM10_I2C_FREQ_HZ);
  Wire.setTimeOut(50);
  Serial.printf("I2C: SDA=GPIO%d, SCL=GPIO%d, %lu Hz\n", APM10_I2C_SDA_PIN, APM10_I2C_SCL_PIN,
                static_cast<unsigned long>(APM10_I2C_FREQ_HZ));
  scanI2c();
  Serial.printf("APM10 tai 0x%02X: %s\n", kI2cAddress, gSensor.probe() ? "CO phan hoi (ACK)" : "KHONG phan hoi");
  Status st = Status::I2cNack;
  for (int i = 0; i < 3 && st != Status::Ok; ++i) {
    st = gSensor.startMeasurement();
    if (st != Status::Ok) delay(200);
  }
  Serial.printf("Gui lenh bat dau do: %s\n", statusToString(st));
  delay(1000);  // du lieu dau tien san sang sau ~1 s
#endif
  Serial.printf("Doc moi %d ms. %d giay dau la thoi gian khoi dong quat/laser.\n", READ_INTERVAL_MS,
                WARMUP_MS / 1000);
  printHelp();
  Serial.println(F("------------------------------------------------"));
  gLastReadMs = millis() - READ_INTERVAL_MS;
}

void loop() {
  while (Serial.available() > 0) handleKey(Serial.read());
  if (gPaused) return;
  const uint32_t now = millis();
  if (now - gLastReadMs >= READ_INTERVAL_MS) {
    gLastReadMs = now;
    doRead();
  }
}
