// Gateway kho lanh qua 4G: may trang thai giu ket noi 24/7 + hang doi gui ban tin.
//
//  Cam bien gia lap / ban tin tu PC --> hang doi RAM --> A7680C --MQTT(TLS)--> EMQX
//                                                         topic kholanh/<devEUI>/up
//
// Phuc hoi loi theo bac thang (tu nhe den nang):
//   ket noi lai MQTT -> kich hoat lai PDP -> CFUN 0/1 (reset song) -> cap nguon lai modem
//   -> khoi dong lai ESP32 (chi khi mat dich vu qua lau, mac dinh 60 phut).
#pragma once
#include "A7680.h"
#include "KhoLanhData.h"

struct GatewayConfig {
    const char* apn;            // "" = de mang tu cap
    const char* mqttHost;
    uint16_t mqttPort;
    bool mqttTls;
    const char* mqttUser;
    const char* mqttPass;
    const char* caCertName;     // nullptr = khong xac thuc chung chi server
    const char* caCertPem;
    uint16_t keepAliveS;
    const char* gatewayId;      // ma gateway tren app, vd "UG65-0FA31C"
    const char* ntpServer;      // "" = chi dung gio mang (NITZ)
    const char* httpTestUrl;    // "" = khong thu HTTP

    const SimDevice* devices;
    int deviceCount;
    uint32_t sensorPeriodMs;    // chu ky gui cua moi cam bien gia lap

    uint32_t statusIntervalMs;  // gui trang thai gateway
    uint32_t healthIntervalMs;  // kiem tra song/sóng
    uint32_t netRegTimeoutMs;   // cho dang ky mang
    uint32_t netLostRecoverMs;  // mat mang bao lau thi reset song
    uint32_t noServiceRestartMs;  // khong gui duoc gi bao lau thi khoi dong lai ESP32 (0 = tat)
    uint32_t maxBackoffMs;
    int publishPerLoop;         // so ban tin gui toi da moi vong loop
};

struct GatewayStats {
    uint32_t pubOk, pubFail, queued, dropped, rawQueued, rawDropped;
    uint32_t mqttConnects, mqttConnFails, mqttLost;
    uint32_t pdpResets, radioResets, hardResets, modemReboots;
    uint32_t httpOk, httpFail, rxCmds, clockSyncs;
};

class Gateway4G {
public:
    enum class State : uint8_t { PowerOn, HardReset, Init, WaitSim, WaitNetwork, AttachPdp, MqttConnect, Running, Backoff };
    static const int kQueueCap = 600;  // ~10 gio du lieu cua 5 cam bien (5 phut/lan)
    static const int kRawCap = 8;

    Gateway4G(ModemHal& hal, const GatewayConfig& cfg);
    void begin();
    void loop();

    // Nguon du lieu
    bool enqueue(const Reading& r);
    bool enqueueRaw(const char* json);  // ban tin JSON tu PC

    // Dieu khien tu console
    void requestModemReset();
    void requestHttpTest() { httpRequested_ = true; }
    void requestStatus() { statusDue_ = true; }
    void printStatus();

    State state() const { return state_; }
    static const char* stateName(State s);
    const GatewayStats& stats() const { return stats_; }
    A7680& modem() { return modem_; }
    SensorSim& sim() { return sim_; }
    const TimeKeeper& clock() const { return clock_; }
    int queueSize() const { return queue_.size() + raw_.size(); }
    uint32_t lastServiceMs() const { return lastServiceMs_; }

private:
    void go(State s);
    void retryLater(State s, uint32_t delayMs);
    void requestHardReset();
    uint32_t backoffFor(int failures) const;
    void handleUrcFlags();
    void generateSensors();
    void checkNoService();

    void stepPowerOn();
    void stepHardReset();
    void stepInit();
    void stepWaitSim();
    void stepWaitNetwork();
    void stepAttachPdp();
    void stepMqttConnect();
    void stepRunning();

    bool healthCheck();
    void syncClock();
    bool drainQueue();
    bool publishStatus();
    void handleCommand(const MqttMessage& m);
    void runHttpTest();

    ModemHal& hal_;
    GatewayConfig cfg_;
    A7680 modem_;
    TimeKeeper clock_;
    SensorSim sim_;
    Ring<Reading, kQueueCap> queue_;
    Ring<RawUplink, kRawCap> raw_;
    GatewayStats stats_ = {};

    State state_ = State::PowerOn;
    State next_ = State::PowerOn;
    uint32_t stateSince_ = 0;
    uint32_t backoffMs_ = 0;

    int hardResetStreak_ = 0;
    int simFails_ = 0;
    int pdpFails_ = 0;
    int mqttFails_ = 0;
    int pubFailStreak_ = 0;
    bool radioResetTried_ = false;
    uint32_t regStart_ = 0;
    uint32_t lastRegPoll_ = 0;
    uint32_t lastHealth_ = 0;
    uint32_t lastStatus_ = 0;
    uint32_t lastClockSync_ = 0;
    uint32_t netLostSince_ = 0;
    bool netLost_ = false;
    uint32_t lastServiceMs_ = 0;
    uint32_t lastHardResetMs_ = 0;
    uint32_t bootMs_ = 0;
    bool statusDue_ = false;
    bool httpRequested_ = false;
    bool resetRequested_ = false;
    bool healthDue_ = false;

    char clientId_[64];
    char statusTopic_[80];
    char cmdTopic_[80];
    char respTopic_[80];
    char buf_[768];
    char topicBuf_[96];
};
