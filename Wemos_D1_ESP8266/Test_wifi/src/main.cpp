// Wemos D1 (ESP8266, WiFi) : gateway kho lanh gia lap -> EMQX Cloud -> web app giam sat kho lanh
//
// Console USB (115200, ket thuc dong bang Enter):
//   help | status | reboot | reset_wifi
//   wifi                  xem cau hinh WiFi
//   wifi scan             quet cac mang xung quanh
//   wifi set Ten|MatKhau  doi WiFi (luu flash, khong can nap lai)   |  wifi xoa  (ve lai wifi.env)
//   sim on|off            bat/tat cam bien gia lap tren chip
//   ep A01=12 | ep A01=off  ep nhiet do cam bien (thu canh bao vuot nguong)
//   UP {json}             gui 1 ban tin JSON (dinh dang gateway Milesight) tu PC qua WiFi
//
// Den LED tren board (GPIO2): sang lien = da ket noi EMQX, nhay = dang ket noi WiFi/MQTT.
#include <Arduino.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecureBearSSL.h>
#include <sys/time.h>
#include <time.h>

#include "GatewayWifi.h"
#include "config.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "Chua co include/secrets.h -> dung secrets.example.h (chay: node tools/tao-secrets.mjs)"
#include "secrets.example.h"
#endif

// Danh muc cam bien gia lap: PHAI trung ma thiet bi tren app (bang thiet_bi), lay tu
// App-giam-sat-kho-lanh/cau-noi/lib/mo-phong.mjs (cac cam bien thuoc gateway UG65-0FA31C;
// 24E124-B02 bo qua vi mo phong "mat ket noi").
static const SimDevice kDevices[] = {
    {"24E124-A01", SENSOR_TEMP_HUM, 4.2f, 72.0f, 0.6f, 87},
    {"24E124-A02", SENSOR_TEMP_HUM, 5.1f, 68.0f, 0.6f, 64},
    {"24E124-A03", SENSOR_DOOR, 0, 0, 0, 92},
    {"24E124-B01", SENSOR_TEMP_HUM, 7.4f, 61.0f, 1.8f, 45},
    {"24E124-B03", SENSOR_DOOR, 0, 0, 0, 78},
};

// Ten/mat khau WiFi dang dung (bo dem tinh, gateway giu con tro)
static char wifiSsid[2][33];
static char wifiPass[2][65];
static char macStr[18];
static volatile int lastWifiReason = 0;
static WiFiEventHandler onWifiDisconnected;  // phai giu song, neu khong su kien bi huy
static uint32_t minHeap = UINT32_MAX;

static void logLine(const char* line) {
    uint32_t t = ::millis();
    Serial.printf("[%6lu.%03lu] %s\n", (unsigned long)(t / 1000), (unsigned long)(t % 1000), line);
}

static uint32_t trackHeap() {
    uint32_t h = ESP.getFreeHeap();
    if (h < minHeap) minHeap = h;
    return h;
}

// ---------------- Ket noi TCP/TLS ----------------
class EspSocket : public NetSocket {
public:
    enum Mfln : uint8_t { kMflnUnknown, kMflnYes, kMflnNo };

    void setCa(const char* pem) {
        if (pem && pem[0]) {
            static BearSSL::X509List ca(pem);  // tao 1 lan, song mai
            tls_.setTrustAnchors(&ca);
            hasCa_ = true;
        } else {
            tls_.setInsecure();  // van ma hoa, nhung khong xac thuc server
        }
    }
    bool open(const char* host, uint16_t port, bool tls, uint32_t timeoutMs) override {
        close();
        if (tls) {
            // Broker ho tro MFLN -> bo dem nhan 1 KB thay vi 16 KB (heap ESP8266 chi con ~20 KB khi da noi WiFi).
            // EMQX Cloud co ho tro (da kiem bang openssl). Do den khi thay "co" (lan do co the loi do mang chap chon).
            bool small = mfln_ == kMflnYes;
            if (!small) {
                small = BearSSL::WiFiClientSecure::probeMaxFragmentLength(host, port, CFG_TLS_MFLN_SIZE);
                if (small || mfln_ == kMflnUnknown)
                    logLine(small ? "[NET] broker ho tro MFLN -> bo dem TLS 1 KB"
                                  : "[NET] do MFLN that bai/khong ho tro -> thu bo dem TLS 16 KB");
                mfln_ = small ? kMflnYes : kMflnNo;
            }
            uint32_t block = ESP.getMaxFreeBlockSize();
            if (!small && block < CFG_TLS_MIN_HEAP) {
                char b[120];
                snprintf(b, sizeof(b), "[NET] CANH BAO: heap lien tuc lon nhat %lu < %u byte, TLS co the that bai",
                         (unsigned long)block, (unsigned)CFG_TLS_MIN_HEAP);
                logLine(b);
            }
            tls_.setBufferSizes(small ? CFG_TLS_MFLN_SIZE : 16384, 512);
            // Timeout TLS do core quy dinh: DNS 10 s, TCP + bat tay moi buoc 15 s (setTimeout() cua lop boc
            // WiFiClientSecure KHONG truyen xuong ngu canh BearSSL). Trong luc cho van nhuong CPU -> khong dinh watchdog.
            if (hasCa_) {
                time_t now = time(nullptr);
                if (now > 1704067200) tls_.setX509Time(now);
            }
            if (tls_.connect(host, port)) {
                tls_.setNoDelay(true);
                cur_ = &tls_;
                return true;
            }
            char err[96] = "";
            int e = tls_.getLastSSLError(err, sizeof(err));
            char b[200];
            snprintf(b, sizeof(b), "[NET] TLS toi %s:%u loi %d: %s (heap %lu, block %lu)", host, port, e, err,
                     (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMaxFreeBlockSize());
            logLine(b);
            tls_.stop();
            return false;
        }
        tcp_.setTimeout(timeoutMs);
        if (tcp_.connect(host, port)) {
            tcp_.setNoDelay(true);
            cur_ = &tcp_;
            return true;
        }
        tcp_.stop();
        return false;
    }
    bool isOpen() override { return cur_ && cur_->connected(); }
    int available() override { return cur_ ? cur_->available() : 0; }
    int read(uint8_t* b, size_t n) override { return cur_ ? cur_->read(b, n) : -1; }
    size_t write(const uint8_t* b, size_t n) override { return cur_ ? cur_->write(b, n) : 0; }
    void close() override {
        if (cur_) cur_->stop();  // TLS: stop() giai phong bo dem BearSSL
        cur_ = nullptr;
    }
    const char* mflnName() const { return mfln_ == kMflnYes ? "co (1 KB)" : mfln_ == kMflnNo ? "khong (16 KB)" : "?"; }

private:
    BearSSL::WiFiClientSecure tls_;
    WiFiClient tcp_;
    Client* cur_ = nullptr;
    bool hasCa_ = false;
    Mfln mfln_ = kMflnUnknown;
};

// ---------------- HAL cho ESP8266 ----------------
class Esp8266Hal : public WifiHal {
public:
    uint32_t millis() override { return ::millis(); }
    void idle(uint32_t ms) override {
        trackHeap();
        if (ms == 0) yield();
        else delay(ms);  // delay() nhuong CPU cho WiFi va an watchdog
    }
    void log(const char* line) override { logLine(line); }

    void wifiBegin(const char* ssid, const char* pass) override {
        if (WiFi.getMode() != WIFI_STA) WiFi.mode(WIFI_STA);
        WiFi.setSleepMode(WIFI_NONE_SLEEP);  // tat tiet kiem dien: giam tre & rot ket noi
        WiFi.setAutoReconnect(true);
        WiFi.hostname(CFG_HOSTNAME);
        lastWifiReason = 0;
        WiFi.begin(ssid, pass);
    }
    bool wifiConnected() override { return WiFi.isConnected(); }
    void wifiStop() override {
        WiFi.disconnect(true);  // persistent(false) -> khong xoa/ghi flash
        WiFi.mode(WIFI_OFF);
        idle(300);
    }
    bool wifiSeen(const char* ssid) override {
        if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
        int n = WiFi.scanNetworks(false, true);  // dong bo ~2-3 s, ben trong van nhuong CPU
        bool found = false;
        for (int i = 0; i < n && !found; i++) found = strcmp(WiFi.SSID(i).c_str(), ssid) == 0;
        WiFi.scanDelete();
        return found;
    }
    int wifiRssi() override { return WiFi.isConnected() ? WiFi.RSSI() : 0; }
    void wifiIp(char* out, size_t cap) override {
        IPAddress ip = WiFi.localIP();
        snprintf(out, cap, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    }
    int wifiLastReason() override { return lastWifiReason; }

    bool epochMs(int64_t& out) override {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        if (tv.tv_sec < 1704067200) return false;  // chua dong bo (truoc 2024)
        out = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
        return true;
    }

    NetSocket& socket() override { return sock; }

    void systemRestart() override {
        Serial.flush();
        delay(200);
        ESP.restart();
    }
    uint32_t freeHeap() override { return trackHeap(); }
    uint32_t minFreeHeap() override {
        trackHeap();
        return minHeap;
    }
    uint32_t random32() override { return ESP.random(); }
    const char* resetReason() override {
        switch (ESP.getResetInfoPtr()->reason) {
            case REASON_DEFAULT_RST: return "poweron";
            case REASON_WDT_RST: return "hw_wdt";
            case REASON_EXCEPTION_RST: return "exception";
            case REASON_SOFT_WDT_RST: return "soft_wdt";
            case REASON_SOFT_RESTART: return "software";
            case REASON_DEEP_SLEEP_AWAKE: return "deepsleep";
            case REASON_EXT_SYS_RST: return "external";
            default: return "other";
        }
    }
    const char* macAddress() override { return macStr; }

    EspSocket sock;
};

static Esp8266Hal hal;

static GatewayConfig makeConfig() {
    GatewayConfig c = {};
    c.wifi[0] = {wifiSsid[0], wifiPass[0]};
    c.wifi[1] = {wifiSsid[1], wifiPass[1]};
    c.mqttHost = SECRET_MQTT_HOST;
    c.mqttPort = SECRET_MQTT_PORT;
    c.mqttTls = SECRET_MQTT_TLS;
    c.mqttUser = SECRET_MQTT_USER;
    c.mqttPass = SECRET_MQTT_PASS;
    c.keepAliveS = CFG_MQTT_KEEPALIVE_S;
    c.gatewayId = CFG_GATEWAY_ID;
    c.devices = kDevices;
    c.deviceCount = sizeof(kDevices) / sizeof(kDevices[0]);
    c.sensorPeriodMs = CFG_SENSOR_PERIOD_MS;
    c.statusIntervalMs = CFG_STATUS_INTERVAL_MS;
    c.wifiTimeoutMs = CFG_WIFI_TIMEOUT_MS;
    c.timeWaitMs = CFG_TIME_WAIT_MS;
    c.mqttTimeoutMs = CFG_MQTT_TIMEOUT_MS;
    c.ackTimeoutMs = CFG_ACK_TIMEOUT_MS;
    c.noServiceRestartMs = CFG_NO_SERVICE_RESTART_MS;
    c.maxBackoffMs = CFG_MAX_BACKOFF_MS;
    c.publishPerLoop = 5;
    return c;
}

// Doi tuong lon (hang doi ~9 KB) dat o vung nho tinh, khong nam tren stack (stack ESP8266 chi 4 KB)
static GatewayWifi gateway(hal, makeConfig());

// ---------------- WiFi luu trong flash (EEPROM gia lap) ----------------
struct WifiStore {
    uint32_t magic;
    char ssid[33];
    char pass[65];
    uint32_t sum;
};
static const uint32_t kStoreMagic = 0x4B4C5731;  // "KLW1"

static uint32_t storeSum(const WifiStore& s) {
    uint32_t h = 2166136261u;  // FNV-1a
    const uint8_t* p = (const uint8_t*)&s;
    for (size_t i = 0; i < offsetof(WifiStore, sum); i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

static void copyStr(char* dst, size_t cap, const char* src) {
    snprintf(dst, cap, "%s", src ? src : "");
}

static void saveWifi(const char* ssid, const char* pass) {
    WifiStore s;
    memset(&s, 0, sizeof(s));
    if (ssid) {
        s.magic = kStoreMagic;
        copyStr(s.ssid, sizeof(s.ssid), ssid);
        copyStr(s.pass, sizeof(s.pass), pass);
    }
    s.sum = storeSum(s);
    EEPROM.put(0, s);
    if (!EEPROM.commit()) Serial.println("LOI: khong ghi duoc flash");
}

static void loadWifi() {
    copyStr(wifiSsid[0], sizeof(wifiSsid[0]), SECRET_WIFI_SSID);
    copyStr(wifiPass[0], sizeof(wifiPass[0]), SECRET_WIFI_PASS);
    copyStr(wifiSsid[1], sizeof(wifiSsid[1]), SECRET_WIFI_SSID2);
    copyStr(wifiPass[1], sizeof(wifiPass[1]), SECRET_WIFI_PASS2);
    WifiStore s;
    EEPROM.get(0, s);
    if (s.magic == kStoreMagic && s.sum == storeSum(s) && s.ssid[0] && memchr(s.ssid, 0, sizeof(s.ssid)) &&
        memchr(s.pass, 0, sizeof(s.pass))) {
        copyStr(wifiSsid[0], sizeof(wifiSsid[0]), s.ssid);
        copyStr(wifiPass[0], sizeof(wifiPass[0]), s.pass);
        Serial.printf("WiFi lay tu flash (lenh 'wifi set'): \"%s\"\n", wifiSsid[0]);
    }
}

static void printWifi() {
    for (int i = 0; i < 2; i++) {
        if (!wifiSsid[i][0]) continue;
        Serial.printf("  %s: \"%s\"  mat khau %u ky tu\n", i ? "du phong" : "chinh   ", wifiSsid[i],
                      (unsigned)strlen(wifiPass[i]));
    }
    char ip[20];
    hal.wifiIp(ip, sizeof(ip));
    Serial.printf("  trang thai: %s, IP %s, RSSI %d dBm, MAC %s\n", WiFi.isConnected() ? "DA KET NOI" : "chua ket noi",
                  ip, hal.wifiRssi(), macStr);
}

static void scanWifi() {
    if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
    Serial.println("Dang quet WiFi...");
    int n = WiFi.scanNetworks(false, true);
    if (n <= 0) Serial.println("Khong thay mang nao (ESP8266 chi bat WiFi 2.4 GHz)");
    for (int i = 0; i < n; i++)
        Serial.printf("  %2d. %-32s %4d dBm  kenh %2d  %s\n", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i),
                      WiFi.channel(i), WiFi.encryptionType(i) == ENC_TYPE_NONE ? "mo" : "co mat khau");
    WiFi.scanDelete();
}

// ---------------- Console USB ----------------
static char conLine[720];
static size_t conLen = 0;
static bool conOverflow = false;

static void handleConsole(char* s) {
    while (*s == ' ') s++;
    if (!*s) return;
    if (strncmp(s, "UP ", 3) == 0 || strncmp(s, "up ", 3) == 0) {
        bool ok = gateway.enqueueRaw(s + 3);
        Serial.println(ok ? "OK UP" : "ERR UP (JSON hong hoac thieu devEUI)");
        return;
    }
    if (strcmp(s, "help") == 0) {
        Serial.println("Lenh: status | wifi | wifi scan | wifi set Ten|MatKhau | wifi xoa | reset_wifi | reboot |"
                       " sim on|off | ep A01=12 | ep A01=off | UP {json}");
    } else if (strcmp(s, "status") == 0) {
        gateway.printStatus();
        hal.logf("ESP8266    : heap lien tuc lon nhat %lu, phan manh %u%% | TLS MFLN %s | CPU %u MHz",
                 (unsigned long)ESP.getMaxFreeBlockSize(), (unsigned)ESP.getHeapFragmentation(), hal.sock.mflnName(),
                 (unsigned)ESP.getCpuFreqMHz());
    } else if (strcmp(s, "wifi") == 0) {
        printWifi();
    } else if (strcmp(s, "wifi scan") == 0) {
        scanWifi();
    } else if (strncmp(s, "wifi set ", 9) == 0) {
        char* ten = s + 9;
        char* bar = strchr(ten, '|');
        if (!bar || bar == ten || (size_t)(bar - ten) >= sizeof(wifiSsid[0]) || strlen(bar + 1) >= sizeof(wifiPass[0])) {
            Serial.println("Vi du: wifi set Kho Lanh 2.4G|matkhau123   (ten <= 32, mat khau <= 64 ky tu)");
            return;
        }
        *bar = 0;
        saveWifi(ten, bar + 1);
        copyStr(wifiSsid[0], sizeof(wifiSsid[0]), ten);
        copyStr(wifiPass[0], sizeof(wifiPass[0]), bar + 1);
        Serial.printf("Da luu WiFi \"%s\" -> ket noi lai\n", wifiSsid[0]);
        gateway.requestWifiReset();
    } else if (strcmp(s, "wifi xoa") == 0) {
        saveWifi(nullptr, nullptr);
        copyStr(wifiSsid[0], sizeof(wifiSsid[0]), SECRET_WIFI_SSID);
        copyStr(wifiPass[0], sizeof(wifiPass[0]), SECRET_WIFI_PASS);
        Serial.printf("Da xoa WiFi trong flash, dung lai \"%s\" (wifi.env)\n", wifiSsid[0]);
        gateway.requestWifiReset();
    } else if (strcmp(s, "reset_wifi") == 0) {
        gateway.requestWifiReset();
    } else if (strcmp(s, "reboot") == 0) {
        hal.systemRestart();
    } else if (strcmp(s, "sim on") == 0 || strcmp(s, "sim off") == 0) {
        gateway.sim().setEnabled(s[5] == 'n');
        Serial.printf("Gia lap: %s\n", gateway.sim().enabled() ? "BAT" : "TAT");
    } else if (strncmp(s, "ep ", 3) == 0) {
        char* eq = strchr(s + 3, '=');
        if (!eq) {
            Serial.println("Vi du: ep A01=12  hoac  ep A01=off");
            return;
        }
        *eq = 0;
        int16_t v = INT16_MIN;
        if (strcmp(eq + 1, "off") != 0) v = (int16_t)lroundf(atof(eq + 1) * 10.0f);
        Serial.println(gateway.sim().force(s + 3, v) ? "OK" : "Khong tim thay cam bien nhiet/am do");
    } else {
        Serial.println("Lenh khong hop le. Go: help");
    }
}

static void pollConsole() {
    for (int i = 0; i < 1024 && Serial.available() > 0; i++) {
        int c = Serial.read();
        if (c < 0) break;
        if (c == '\r') continue;
        if (c == '\n') {
            conLine[conLen] = 0;
            if (conOverflow) Serial.println("ERR UP (dong qua dai)");
            else handleConsole(conLine);
            conLen = 0;
            conOverflow = false;
            continue;
        }
        if (conLen < sizeof(conLine) - 1) conLine[conLen++] = (char)c;
        else conOverflow = true;
    }
}

// ---------------- Den LED bao trang thai ----------------
static void updateLed() {
    static uint32_t last = 0;
    static bool on = false;
    uint32_t now = ::millis();
    bool running = gateway.state() == GatewayWifi::State::Running;
    if (running) {
        if (!on) digitalWrite(LED_BUILTIN, LOW);  // LED tich cuc muc thap
        on = true;
        return;
    }
    if (now - last < 500) return;
    last = now;
    on = !on;
    digitalWrite(LED_BUILTIN, on ? LOW : HIGH);
}

// ---------------- Arduino ----------------
void setup() {
    Serial.setRxBufferSize(1024);  // du 1 dong "UP {json}" (<= 720 byte)
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("=== Gateway kho lanh WiFi (Wemos D1 / ESP8266) ===");
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);

    EEPROM.begin(sizeof(WifiStore));
    loadWifi();
    WiFi.persistent(false);  // khong ghi flash moi lan begin (ta tu quan ly trong EEPROM)
    WiFi.setAutoConnect(false);
    onWifiDisconnected = WiFi.onStationModeDisconnected(
        [](const WiFiEventStationModeDisconnected& e) { lastWifiReason = (int)e.reason; });
    WiFi.mode(WIFI_STA);
    WiFi.hostname(CFG_HOSTNAME);
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    configTime(0, 0, CFG_NTP_SERVER, CFG_NTP_SERVER2);  // gio UTC, SNTP tu chay nen khi co mang
    hal.sock.setCa(SECRET_MQTT_CA_PEM);

    hal.logf("reset: %s | heap %lu (block %lu) | gateway %s | broker %s:%d | MAC %s", hal.resetReason(),
             (unsigned long)hal.freeHeap(), (unsigned long)ESP.getMaxFreeBlockSize(), CFG_GATEWAY_ID, SECRET_MQTT_HOST,
             SECRET_MQTT_PORT, macStr);
    gateway.begin();
}

void loop() {
    gateway.loop();
    pollConsole();
    updateLed();
    hal.idle(5);
}
