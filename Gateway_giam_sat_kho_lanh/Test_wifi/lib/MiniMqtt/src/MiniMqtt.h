// Client MQTT 3.1.1 toi gian, khong cap phat dong, chay tren NetSocket (TCP hoac TLS).
//  - publish QoS1 CHO PUBACK (giong AT+CMQTTPUB cua A7680C): tra ve true = broker DA NHAN ban tin
//    -> gateway chi xoa ban tin khoi hang doi khi chac chan da toi broker (khong mat du lieu).
//  - subscribe QoS1, nhan ban tin xuong (lenh), tu tra PUBACK.
//  - keepalive: tu gui PINGREQ, rot ket noi neu broker im lang qua lau (phat hien ket noi "chet im").
//  - moi thao tac co timeout, trong luc cho goi hal.idle() (an watchdog).
#pragma once
#include "WifiHal.h"

struct MqttOptions {
    const char* host;
    uint16_t port;
    bool tls;
    const char* clientId;
    const char* user;       // nullptr/"" = khong dung
    const char* pass;
    uint16_t keepAliveS;
    const char* willTopic;  // nullptr = khong co LWT
    const char* willMsg;
    uint32_t timeoutMs;     // ket noi socket + cho CONNACK
};

struct MqttInbound {
    char topic[96];
    char payload[512];
    size_t len;
    bool truncated;
};

class MiniMqtt {
public:
    // Ma loi (lastError): >0 la ma tu choi trong CONNACK (1..5), <0 la loi phia client
    enum : int {
        kOk = 0,
        kRefusedProtocol = 1,
        kRefusedClientId = 2,
        kServerUnavailable = 3,
        kBadUserPass = 4,
        kNotAuthorized = 5,
        kErrSocket = -1,     // khong mo duoc ket noi / socket dong
        kErrTimeout = -2,    // broker khong tra loi kip
        kErrProtocol = -3,   // goi tin sai
        kErrTooLarge = -4,   // ban tin vuot bo dem
        kErrNotConnected = -5,
        kErrWrite = -6,
    };
    static const size_t kTxCap = 1100;
    static const size_t kRxCap = 700;

    MiniMqtt(WifiHal& hal) : hal_(hal) {}

    bool connect(const MqttOptions& o);
    bool subscribe(const char* topic, uint8_t qos, uint32_t timeoutMs);
    bool publish(const char* topic, const uint8_t* payload, size_t len, uint8_t qos, bool retain, uint32_t timeoutMs);
    void loop();  // goi thuong xuyen: doc goi tin den, keepalive
    void close(bool graceful);

    bool connected() const { return connected_; }
    bool takeLost() {  // true mot lan khi ket noi vua rot (do loi/keepalive), khong tinh close() chu dong
        bool v = lost_;
        lost_ = false;
        return v;
    }
    int lastError() const { return lastError_; }
    static const char* errorName(int e);

    bool hasMessage() const { return hasMsg_; }
    const MqttInbound& message() const { return msg_; }
    void releaseMessage() { hasMsg_ = false; }
    uint32_t droppedMessages() const { return droppedMsgs_; }
    uint32_t pings() const { return pings_; }

private:
    bool putStr(size_t& n, const char* s);
    int pump();  // doc het du lieu dang co; tra ve loai goi (1..14) khi du 1 goi, 0 neu chua, -1 neu goi sai (da dong ket noi)
    bool waitFor(uint8_t type, uint16_t id, uint32_t timeoutMs);
    void dispatch(uint8_t type);
    void fail(int err);
    uint16_t nextId();
    uint16_t rxId() const { return rxLen_ >= 2 ? (uint16_t)((rx_[0] << 8) | rx_[1]) : 0; }

    WifiHal& hal_;
    bool connected_ = false;
    bool lost_ = false;
    int lastError_ = kOk;
    uint16_t keepAliveS_ = 60;
    uint16_t packetId_ = 0;
    uint32_t lastTx_ = 0;
    uint32_t lastRx_ = 0;
    bool pingOut_ = false;
    uint32_t pingAt_ = 0;
    uint32_t pings_ = 0;

    // bo phan tich goi den (tung byte, khong chan)
    uint8_t rxState_ = 0;  // 0 cho header, 1 do dai, 2 noi dung
    uint8_t rxHdr_ = 0;
    uint32_t rxRemain_ = 0;
    uint32_t rxMul_ = 1;
    uint32_t rxGot_ = 0;
    size_t rxLen_ = 0;
    bool rxTrunc_ = false;
    uint8_t rx_[kRxCap];
    uint8_t tx_[kTxCap];

    MqttInbound msg_;
    bool hasMsg_ = false;
    uint32_t droppedMsgs_ = 0;
};
