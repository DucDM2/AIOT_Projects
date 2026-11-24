// ESP32 + A7680C (4G) : gateway kho lanh gia lap -> EMQX Cloud -> web app giam sat kho lanh
//
// Console USB (115200, ket thuc dong bang Enter):
//   help | status | at <lenh AT> | trace on|off | http | reset_modem | reboot
//   sim on|off            bat/tat cam bien gia lap tren ESP32
//   ep A01=12 | ep A01=off  ep nhiet do cam bien (thu canh bao vuot nguong)
//   UP {json}             gui 1 ban tin JSON (dinh dang gateway Milesight) tu PC qua 4G
#include <Arduino.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

#include "Gateway4G.h"
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

// ---------------- HAL cho ESP32 ----------------
class Esp32Hal : public ModemHal {
public:
    int available() override { return Serial2.available(); }
    int read() override { return Serial2.read(); }
    size_t write(const uint8_t* d, size_t n) override { return Serial2.write(d, n); }
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

    void setPwrKey(bool asserted) override {
#if MODEM_PWRKEY_VIA_TRANSISTOR
        digitalWrite(PIN_MODEM_PWRKEY, asserted ? HIGH : LOW);
#else
        if (asserted) {
            digitalWrite(PIN_MODEM_PWRKEY, LOW);
            pinMode(PIN_MODEM_PWRKEY, OUTPUT);
        } else {
            pinMode(PIN_MODEM_PWRKEY, INPUT);  // tha noi, module tu keo len
        }
#endif
    }
    bool hasResetPin() override { return PIN_MODEM_RESET >= 0; }
    void setResetPin(bool asserted) override {
#if PIN_MODEM_RESET >= 0
        if (asserted) {
            digitalWrite(PIN_MODEM_RESET, LOW);
            pinMode(PIN_MODEM_RESET, OUTPUT);
        } else {
            pinMode(PIN_MODEM_RESET, INPUT);
        }
#endif
    }
    bool hasPowerSwitch() override { return PIN_MODEM_POWER >= 0; }
    void setPowerSwitch(bool on) override {
#if PIN_MODEM_POWER >= 0
        digitalWrite(PIN_MODEM_POWER, on ? MODEM_POWER_ON_LEVEL : !MODEM_POWER_ON_LEVEL);
#endif
    }

    void log(const char* line) override {
        uint32_t t = ::millis();
        Serial.printf("[%6lu.%03lu] %s\n", (unsigned long)(t / 1000), (unsigned long)(t % 1000), line);
    }
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
};

static Esp32Hal hal;

static GatewayConfig makeConfig() {
    GatewayConfig c = {};
    c.apn = CFG_APN;
    c.mqttHost = SECRET_MQTT_HOST;
    c.mqttPort = SECRET_MQTT_PORT;
    c.mqttTls = SECRET_MQTT_TLS;
    c.mqttUser = SECRET_MQTT_USER;
    c.mqttPass = SECRET_MQTT_PASS;
    c.caCertName = "kholanh_ca.pem";
    c.caCertPem = SECRET_MQTT_CA_PEM;
    c.keepAliveS = CFG_MQTT_KEEPALIVE_S;
    c.gatewayId = CFG_GATEWAY_ID;
    c.ntpServer = CFG_NTP_SERVER;
    c.httpTestUrl = CFG_HTTP_TEST_URL;
    c.devices = kDevices;
    c.deviceCount = sizeof(kDevices) / sizeof(kDevices[0]);
    c.sensorPeriodMs = CFG_SENSOR_PERIOD_MS;
    c.statusIntervalMs = CFG_STATUS_INTERVAL_MS;
    c.healthIntervalMs = CFG_HEALTH_INTERVAL_MS;
    c.netRegTimeoutMs = CFG_NET_REG_TIMEOUT_MS;
    c.netLostRecoverMs = CFG_NET_LOST_RECOVER_MS;
    c.noServiceRestartMs = CFG_NO_SERVICE_RESTART_MS;
    c.maxBackoffMs = CFG_MAX_BACKOFF_MS;
    c.publishPerLoop = 5;
    return c;
}

// Doi tuong lon (hang doi ~17 KB) dat o vung nho tinh, khong nam tren stack
static Gateway4G gateway(hal, makeConfig());

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
        Serial.println("Lenh: status | at <AT...> | trace on|off | http | reset_modem | reboot | sim on|off |"
                       " ep A01=12 | ep A01=off | UP {json}");
    } else if (strcmp(s, "status") == 0) {
        gateway.printStatus();
    } else if (strncmp(s, "at ", 3) == 0 || strncmp(s, "AT", 2) == 0) {
        const char* c = (s[0] == 'a') ? s + 3 : s;
        AtStatus st = gateway.modem().at().passthrough(c, 15000);
        Serial.printf("-> %s\n", atStatusName(st));
    } else if (strcmp(s, "trace on") == 0 || strcmp(s, "trace off") == 0) {
        gateway.modem().at().setTrace(s[7] == 'n');
    } else if (strcmp(s, "http") == 0) {
        gateway.requestHttpTest();
    } else if (strcmp(s, "reset_modem") == 0) {
        gateway.requestModemReset();
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
    Serial.println("=== Gateway kho lanh 4G (ESP32 + A7680C) ===");

#if MODEM_PWRKEY_VIA_TRANSISTOR
    pinMode(PIN_MODEM_PWRKEY, OUTPUT);
    digitalWrite(PIN_MODEM_PWRKEY, LOW);
#else
    pinMode(PIN_MODEM_PWRKEY, INPUT);
#endif
#if PIN_MODEM_RESET >= 0
    pinMode(PIN_MODEM_RESET, INPUT);
#endif
#if PIN_MODEM_POWER >= 0
    pinMode(PIN_MODEM_POWER, OUTPUT);
    digitalWrite(PIN_MODEM_POWER, MODEM_POWER_ON_LEVEL);
#endif

    Serial2.setRxBufferSize(4096);
    Serial2.begin(MODEM_BAUD, SERIAL_8N1, PIN_MODEM_RX, PIN_MODEM_TX);

    esp_task_wdt_init(WDT_TIMEOUT_S, true);
    esp_task_wdt_add(NULL);

    hal.logf("reset: %s | heap %lu | gateway %s | broker %s:%d", hal.resetReason(), (unsigned long)hal.freeHeap(),
             CFG_GATEWAY_ID, SECRET_MQTT_HOST, SECRET_MQTT_PORT);
    gateway.begin();
}

void loop() {
    gateway.loop();
    pollConsole();
    hal.idle(5);
}
