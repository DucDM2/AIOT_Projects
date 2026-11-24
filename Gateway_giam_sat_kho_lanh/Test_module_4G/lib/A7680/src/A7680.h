// Driver muc cao cho SIMCom A7680C (dong A76xx, LTE Cat-1).
// Dung bo lenh AT goc cua module (MQTT/HTTP/SSL chay trong module), khong can TinyGSM.
#pragma once
#include "AtClient.h"

struct ModemInfo {
    char model[32];
    char revision[48];
    char imei[24];
    char iccid[28];
    char imsi[20];
    char oper[40];
    char ip[48];
    char sysinfo[128];
    char clock[32];   // "yy/MM/dd,hh:mm:ss+zz" tu AT+CCLK
    int csq;          // 0..31, 99 = khong biet
    int rssiDbm;      // -113..-51, 0 = khong biet
    int regStat;      // 1 = mang nha, 5 = roaming
};

enum class SimState : uint8_t { Ready, Missing, Locked, NotReady };

struct MqttMessage {
    char topic[128];
    char payload[256];
    size_t len;
    bool truncated;
};

struct MqttSettings {
    const char* host;
    uint16_t port;
    bool tls;
    const char* clientId;
    const char* user;      // nullptr/"" = khong dung
    const char* pass;
    uint16_t keepAliveS;
    const char* willTopic; // nullptr = khong dung LWT
    const char* willMsg;
    const char* caCertName;  // ten file CA trong modem (nullptr = khong xac thuc server)
};

class A7680 : public AtUrcListener {
public:
    explicit A7680(ModemHal& hal);

    AtClient& at() { return at_; }
    ModemInfo& info() { return info_; }

    // ---- Nguon / khoi dong ----
    bool isResponsive(uint32_t waitMs);  // gui "AT" lap lai toi khi OK
    bool powerOn();                      // dam bao modem bat & tra loi AT
    bool hardReset();                    // cap nguon lai (switch / RESET / PWRKEY)
    bool softReset();                    // AT+CRESET
    bool configure();                    // ATE0, CMEE, doc IMEI/model...

    // ---- SIM / mang ----
    SimState waitSim(uint32_t timeoutMs);
    bool updateSignal();
    int updateRegistration();  // tra ve stat tot nhat (CEREG/CGREG/CREG)
    bool isRegistered() const { return info_.regStat == 1 || info_.regStat == 5; }
    void updateOperator();
    void updateSysInfo();
    bool updateClock();
    bool ensureApn(const char* apn, bool& changed);
    bool attachPdp(uint32_t timeoutMs);
    bool detachPdp();
    bool radioReset();  // CFUN=0 -> CFUN=1

    // ---- Chung chi TLS ----
    bool ensureCaCert(const char* name, const char* pem);

    // ---- HTTP (thu nghiem) ----
    int httpGet(const char* url, int& bodyLen);  // tra ve ma HTTP, <0 neu loi AT

    // ---- MQTT (client 0) ----
    void mqttCleanup();
    bool mqttConnect(const MqttSettings& s);
    bool mqttSubscribe(const char* topic, int qos);
    bool mqttPublish(const char* topic, const uint8_t* payload, size_t len, int qos, bool retain);
    void mqttDisconnect();
    bool mqttConnected() const { return mqttConnected_; }
    // Ma loi CMQTTCONNECT gan nhat (27..31 = broker tu choi: sai user/pass, clientId...)
    int lastMqttError() const { return lastMqttError_; }

    // ---- Su kien URC (doc va xoa) ----
    bool takeRebooted();
    bool takePowerDown();
    bool takeMqttLost();
    bool takeSimLost();
    bool takePdpLost();
    bool hasMessage() const { return msgReady_; }
    const MqttMessage& message() const { return msg_; }
    void releaseMessage() { msgReady_ = false; }
    uint32_t droppedMessages() const { return msgDropped_; }

    // AtUrcListener
    size_t onLine(const char* line) override;
    void onRaw(const uint8_t* data, size_t stored, size_t total) override;

    static bool safeQuoted(const char* s);  // chuoi dua vao "..." cua lenh AT

private:
    void pulsePwrKey(uint32_t ms);
    bool waitBoot(uint32_t timeoutMs);
    void clearSessionState();
    static int parseIntField(const char* s, int index, int def);

    ModemHal& hal_;
    AtClient at_;
    ModemInfo info_;
    bool mqttConnected_ = false;
    int lastMqttError_ = 0;

    bool rebooted_ = false;
    bool powerDown_ = false;
    bool mqttLost_ = false;
    bool simLost_ = false;
    bool pdpLost_ = false;

    // Nhan ban tin MQTT
    enum RawTarget : uint8_t { RawNone, RawTopic, RawPayload };
    RawTarget rawTarget_ = RawNone;
    bool rxActive_ = false;
    MqttMessage rx_;
    MqttMessage msg_;
    bool msgReady_ = false;
    uint32_t msgDropped_ = 0;

    const char* certQueryName_ = nullptr;
    bool certFound_ = false;
};
