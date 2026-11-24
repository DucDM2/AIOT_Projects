// Gateway kho lanh qua WiFi: may trang thai giu ket noi 24/7 + hang doi gui ban tin.
//
//  Cam bien gia lap / ban tin tu PC --> hang doi RAM --> WiFi --MQTT(TLS, QoS1)--> EMQX
//                                                        topic kholanh/<devEUI>/up
//
// Phuc hoi loi theo bac thang (tu nhe den nang):
//   ket noi lai MQTT (backoff) -> ket noi lai WiFi -> tat/bat han song WiFi (doi mang du phong neu co)
//   -> khoi dong lai chip (chi khi mat dich vu qua lau VA khoi dong lai co the giup duoc).
#pragma once
#include "KhoLanhData.h"
#include "MiniMqtt.h"

#ifndef GW_QUEUE_CAP
#define GW_QUEUE_CAP 300  // ~5 gio du lieu cua 5 cam bien (5 phut/lan), 20 byte/ban tin = 6 KB
#endif
#ifndef GW_RAW_CAP
#define GW_RAW_CAP 6  // ban tin JSON tu PC (~650 byte/ban)
#endif

struct WifiNetwork {
    const char* ssid;
    const char* pass;
};

struct GatewayConfig {
    WifiNetwork wifi[2];  // mang chinh + du phong (ssid "" = khong dung)
    const char* mqttHost;
    uint16_t mqttPort;
    bool mqttTls;
    const char* mqttUser;
    const char* mqttPass;
    uint16_t keepAliveS;
    const char* gatewayId;  // ma gateway tren app, vd "UG65-0FA31C"

    const SimDevice* devices;
    int deviceCount;
    uint32_t sensorPeriodMs;  // chu ky gui cua moi cam bien gia lap

    uint32_t statusIntervalMs;    // gui trang thai gateway
    uint32_t wifiTimeoutMs;       // cho WiFi len (moi lan thu)
    uint32_t timeWaitMs;          // cho SNTP truoc khi gui (de ban tin co dung gio do)
    uint32_t mqttTimeoutMs;       // ket noi TLS + CONNACK
    uint32_t ackTimeoutMs;        // cho PUBACK
    uint32_t noServiceRestartMs;  // khong gui duoc gi bao lau thi xet khoi dong lai ESP32 (0 = tat)
    uint32_t maxBackoffMs;
    int publishPerLoop;  // so ban tin gui toi da moi vong loop
};

struct GatewayStats {
    uint32_t pubOk, pubFail, queued, dropped, rawQueued, rawDropped, tooLarge;
    uint32_t mqttConnects, mqttConnFails, mqttLost;
    uint32_t wifiConnects, wifiFails, wifiLost, wifiResets;
    uint32_t rxCmds, clockSyncs;
};

class GatewayWifi {
public:
    enum class State : uint8_t { WifiConnect, WifiWait, MqttConnect, Running, Backoff };
    // ESP8266 it RAM (~80 KB, TLS can ~20-28 KB) -> hang doi nho hon ban ESP32 (600/8). Doi qua build_flags.
    static const int kQueueCap = GW_QUEUE_CAP;
    static const int kRawCap = GW_RAW_CAP;

    GatewayWifi(WifiHal& hal, const GatewayConfig& cfg);
    void begin();
    void loop();

    // Nguon du lieu
    bool enqueue(const Reading& r);
    bool enqueueRaw(const char* json);  // ban tin JSON tu PC

    // Dieu khien tu console / lenh MQTT
    void requestWifiReset() { wifiResetRequested_ = true; }
    void requestStatus() { statusDue_ = true; }
    void setWifi(int idx, const char* ssid, const char* pass);  // con tro phai song mai (bo dem tinh)
    void printStatus();

    State state() const { return state_; }
    static const char* stateName(State s);
    const GatewayStats& stats() const { return stats_; }
    MiniMqtt& mqtt() { return mqtt_; }
    SensorSim& sim() { return sim_; }
    const TimeKeeper& clock() const { return clock_; }
    int queueSize() const { return queue_.size() + raw_.size(); }
    const char* currentSsid() const { return cfg_.wifi[wifiIdx_].ssid; }

private:
    void go(State s);
    void retryLater(State s, uint32_t delayMs);
    uint32_t backoffFor(int failures, uint32_t cap) const;
    int wifiCount() const;
    void resetWifi(const char* why);
    void generateSensors();
    void updateClock();
    void checkNoService();

    void stepWifiConnect();
    void stepWifiWait();
    void stepMqttConnect();
    void stepRunning();

    bool drainQueue();
    bool publishStatus();
    void handleCommand(const MqttInbound& m);
    void onPublishFail(const char* what);

    WifiHal& hal_;
    GatewayConfig cfg_;
    MiniMqtt mqtt_;
    TimeKeeper clock_;
    SensorSim sim_;
    Ring<Reading, kQueueCap> queue_;
    Ring<RawUplink, kRawCap> raw_;
    GatewayStats stats_ = {};

    State state_ = State::WifiConnect;
    State next_ = State::WifiConnect;
    uint32_t stateSince_ = 0;
    uint32_t backoffMs_ = 0;

    int wifiIdx_ = 0;
    int wifiFails_ = 0;
    int mqttFails_ = 0;
    uint32_t wifiUpAt_ = 0;
    uint32_t lastStatus_ = 0;
    uint32_t lastClockRead_ = 0;
    uint32_t lastServiceMs_ = 0;
    uint32_t bootMs_ = 0;
    bool statusDue_ = false;
    bool wifiResetRequested_ = false;
    bool authRefused_ = false;

    char clientId_[64];
    char statusTopic_[80];
    char cmdTopic_[80];
    char respTopic_[80];
    char buf_[768];
    char topicBuf_[96];
};
