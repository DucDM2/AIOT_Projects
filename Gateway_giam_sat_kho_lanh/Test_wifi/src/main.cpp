// ESP32 (WiFi) : gateway kho lanh gia lap -> EMQX Cloud -> web app giam sat kho lanh
//
// Console USB (115200, ket thuc dong bang Enter):
//   help | status | reboot | reset_wifi
//   wifi                  xem cau hinh WiFi
//   wifi scan             quet cac mang xung quanh
//   wifi set Ten|MatKhau  doi WiFi (luu flash, khong can nap lai)   |  wifi xoa  (ve lai wifi.env)
//   sim on|off            bat/tat cam bien gia lap tren ESP32
//   ep A01=12 | ep A01=off  ep nhiet do cam bien (thu canh bao vuot nguong)
//   UP {json}             gui 1 ban tin JSON (dinh dang gateway Milesight) tu PC qua WiFi
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
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

static void logLine(const char* line) {
    uint32_t t = ::millis();
    Serial.printf("[%6lu.%03lu] %s\n", (unsigned long)(t / 1000), (unsigned long)(t % 1000), line);
}

// ---------------- Ket noi TCP/TLS ----------------
class EspSocket : public NetSocket {
public:
    void setCa(const char* pem) {
        if (pem && pem[0]) tls_.setCACert(pem);
        else tls_.setInsecure();  // van ma hoa, nhung khong xac thuc server
    }
    bool open(const char* host, uint16_t port, bool tls, uint32_t timeoutMs) override {
        close();
        if (tls) {
            tls_.setHandshakeTimeout((timeoutMs + 999) / 1000);  // mac dinh 120 s > watchdog
            if (tls_.connect(host, port, (int32_t)timeoutMs) == 1) {
                cur_ = &tls_;
                return true;
            }
            char err[96];
            int e = tls_.lastError(err, sizeof(err));
            char b[160];
            snprintf(b, sizeof(b), "[NET] TLS toi %s:%u loi %d: %s", host, port, e, err);
            logLine(b);
            tls_.stop();
            return false;
        }
        if (tcp_.connect(host, port, (int32_t)timeoutMs)) {
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
        if (cur_) cur_->stop();
        cur_ = nullptr;
    }

private:
    WiFiClientSecure tls_;
    WiFiClient tcp_;
    Client* cur_ = nullptr;
};

// ---------------- HAL cho ESP32 ----------------
class Esp32Hal : public WifiHal {
public:
    uint32_t millis() override { return ::millis(); }
    void idle(uint32_t ms) override {
        esp_task_wdt_reset();
        if (ms == 0) {
            yield();
            return;
        }
        while (ms) {
            uint32_t s = ms > 500 ? 500 : ms;
            delay(s);
            ms -= s;
            esp_task_wdt_reset();
        }
    }
    void log(const char* line) override { logLine(line); }

    void wifiBegin(const char* ssid, const char* pass) override {
        if (WiFi.getMode() != WIFI_STA) WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);  // tat tiet kiem dien: giam tre & rot ket noi
        WiFi.setAutoReconnect(true);
        lastWifiReason = 0;
        WiFi.begin(ssid, pass);
    }
    bool wifiConnected() override { return WiFi.isConnected(); }
    void wifiStop() override {
        WiFi.disconnect(true, false);
        WiFi.mode(WIFI_OFF);
        idle(300);
    }
    bool wifiSeen(const char* ssid) override {
        if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
        int n = WiFi.scanNetworks(false, true);
        bool found = false;
        for (int i = 0; i < n && !found; i++) found = strcmp(WiFi.SSID(i).c_str(), ssid) == 0;
        WiFi.scanDelete();
        esp_task_wdt_reset();
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
    uint32_t freeHeap() override { return ESP.getFreeHeap(); }
    uint32_t minFreeHeap() override { return ESP.getMinFreeHeap(); }
    uint32_t random32() override { return esp_random(); }
    const char* resetReason() override {
        switch (esp_reset_reason()) {
            case ESP_RST_POWERON: return "poweron";
            case ESP_RST_SW: return "software";
            case ESP_RST_PANIC: return "panic";
            case ESP_RST_INT_WDT: return "int_wdt";
            case ESP_RST_TASK_WDT: return "task_wdt";
            case ESP_RST_WDT: return "wdt";
            case ESP_RST_BROWNOUT: return "brownout";
            case ESP_RST_DEEPSLEEP: return "deepsleep";
            case ESP_RST_EXT: return "external";
            default: return "other";
        }
    }
    const char* macAddress() override { return macStr; }

    EspSocket sock;
};

static Esp32Hal hal;

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

// Doi tuong lon (hang doi ~17 KB) dat o vung nho tinh, khong nam tren stack
static GatewayWifi gateway(hal, makeConfig());

// ---------------- WiFi luu trong flash (NVS) ----------------
static void copyStr(char* dst, size_t cap, const char* src) {
    snprintf(dst, cap, "%s", src ? src : "");
}

static void loadWifi() {
    copyStr(wifiSsid[0], sizeof(wifiSsid[0]), SECRET_WIFI_SSID);
    copyStr(wifiPass[0], sizeof(wifiPass[0]), SECRET_WIFI_PASS);
    copyStr(wifiSsid[1], sizeof(wifiSsid[1]), SECRET_WIFI_SSID2);
    copyStr(wifiPass[1], sizeof(wifiPass[1]), SECRET_WIFI_PASS2);
    Preferences p;
    if (p.begin("kholanh", true)) {
        if (p.isKey("ssid")) {
            p.getString("ssid", wifiSsid[0], sizeof(wifiSsid[0]));
            p.getString("pass", wifiPass[0], sizeof(wifiPass[0]));
            Serial.printf("WiFi lay tu flash (lenh 'wifi set'): \"%s\"\n", wifiSsid[0]);
        }
        p.end();
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
    esp_task_wdt_reset();
    if (n <= 0) Serial.println("Khong thay mang nao (ESP32 chi bat WiFi 2.4 GHz)");
    for (int i = 0; i < n; i++)
        Serial.printf("  %2d. %-32s %4d dBm  kenh %2d  %s\n", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i),
                      WiFi.channel(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "mo" : "co mat khau");
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
        Preferences p;
        if (p.begin("kholanh", false)) {
            p.putString("ssid", ten);
            p.putString("pass", bar + 1);
            p.end();
        }
        copyStr(wifiSsid[0], sizeof(wifiSsid[0]), ten);
        copyStr(wifiPass[0], sizeof(wifiPass[0]), bar + 1);
        Serial.printf("Da luu WiFi \"%s\" -> ket noi lai\n", wifiSsid[0]);
        gateway.requestWifiReset();
    } else if (strcmp(s, "wifi xoa") == 0) {
        Preferences p;
        if (p.begin("kholanh", false)) {
            p.clear();
            p.end();
        }
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

// ---------------- Arduino ----------------
void setup() {
    Serial.setRxBufferSize(4096);
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("=== Gateway kho lanh WiFi (ESP32) ===");

    esp_task_wdt_init(WDT_TIMEOUT_S, true);
    esp_task_wdt_add(NULL);

    loadWifi();
    WiFi.persistent(false);  // khong ghi flash moi lan begin (ta tu quan ly trong NVS)
    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) { lastWifiReason = info.wifi_sta_disconnected.reason; },
                 ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(CFG_HOSTNAME);
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    configTime(0, 0, CFG_NTP_SERVER, CFG_NTP_SERVER2);  // gio UTC, SNTP tu chay nen khi co mang
    hal.sock.setCa(SECRET_MQTT_CA_PEM);

    hal.logf("reset: %s | heap %lu | gateway %s | broker %s:%d | MAC %s", hal.resetReason(),
             (unsigned long)hal.freeHeap(), CFG_GATEWAY_ID, SECRET_MQTT_HOST, SECRET_MQTT_PORT, macStr);
    gateway.begin();
}

void loop() {
    gateway.loop();
    pollConsole();
    hal.idle(5);
}
