#include "MiniMqtt.h"

#include <string.h>

namespace {
enum : uint8_t {
    CONNECT = 1,
    CONNACK = 2,
    PUBLISH = 3,
    PUBACK = 4,
    SUBSCRIBE = 8,
    SUBACK = 9,
    PINGREQ = 12,
    PINGRESP = 13,
    DISCONNECT = 14,
};
const size_t kHdrRoom = 5;  // 1 byte loai + toi da 4 byte do dai, noi dung bat dau tu tx_[5]
}  // namespace

const char* MiniMqtt::errorName(int e) {
    switch (e) {
        case kOk: return "OK";
        case kRefusedProtocol: return "broker tu choi: sai phien ban giao thuc";
        case kRefusedClientId: return "broker tu choi: clientId";
        case kServerUnavailable: return "broker tam ngung dich vu";
        case kBadUserPass: return "broker tu choi: SAI user/mat khau";
        case kNotAuthorized: return "broker tu choi: khong co quyen";
        case kErrSocket: return "khong ket noi duoc / socket dong";
        case kErrTimeout: return "broker khong tra loi (timeout)";
        case kErrProtocol: return "goi tin sai";
        case kErrTooLarge: return "ban tin qua lon";
        case kErrNotConnected: return "chua ket noi";
        case kErrWrite: return "gui du lieu loi";
    }
    return "?";
}

uint16_t MiniMqtt::nextId() {
    if (++packetId_ == 0) packetId_ = 1;
    return packetId_;
}

bool MiniMqtt::putStr(size_t& n, const char* s) {
    size_t l = s ? strlen(s) : 0;
    if (l > 0xffff || n + 2 + l > kTxCap) return false;
    tx_[n++] = (uint8_t)(l >> 8);
    tx_[n++] = (uint8_t)(l & 0xff);
    if (l) memcpy(tx_ + n, s, l);
    n += l;
    return true;
}

// tx_[kHdrRoom .. n) la noi dung; gan header phia truoc roi gui ca goi mot lan (hieu qua voi TLS)
static size_t frame(uint8_t* tx, uint8_t hdr, size_t bodyEnd, size_t& start) {
    uint8_t lb[4];
    size_t body = bodyEnd - kHdrRoom;
    size_t k = 0;
    size_t len = body;
    do {
        uint8_t b = len % 128;
        len /= 128;
        if (len) b |= 0x80;
        lb[k++] = b;
    } while (len && k < 4);
    start = kHdrRoom - 1 - k;
    tx[start] = hdr;
    memcpy(tx + start + 1, lb, k);
    return 1 + k + body;
}

void MiniMqtt::fail(int err) {
    lastError_ = err;
    if (connected_) lost_ = true;
    connected_ = false;
    pingOut_ = false;
    rxState_ = 0;
    hal_.socket().close();
}

void MiniMqtt::close(bool graceful) {
    if (connected_ && graceful) {
        uint8_t d[2] = {(uint8_t)(DISCONNECT << 4), 0};
        hal_.socket().write(d, 2);
    }
    connected_ = false;
    pingOut_ = false;
    rxState_ = 0;
    hal_.socket().close();
}

int MiniMqtt::pump() {
    NetSocket& s = hal_.socket();
    for (int guard = 0; guard < 64; guard++) {
        int av = s.available();
        if (av <= 0) return 0;
        if (rxState_ == 0) {
            uint8_t b;
            if (s.read(&b, 1) != 1) return 0;
            rxHdr_ = b;
            rxRemain_ = 0;
            rxMul_ = 1;
            rxGot_ = 0;
            rxLen_ = 0;
            rxTrunc_ = false;
            rxState_ = 1;
            continue;
        }
        if (rxState_ == 1) {
            uint8_t b;
            if (s.read(&b, 1) != 1) return 0;
            rxRemain_ += (uint32_t)(b & 0x7f) * rxMul_;
            if (rxMul_ > 128UL * 128UL * 128UL) {
                fail(kErrProtocol);
                return -1;
            }
            rxMul_ *= 128;
            if (b & 0x80) continue;
            if (rxRemain_ == 0) {
                rxState_ = 0;
                lastRx_ = hal_.millis();
                return rxHdr_ >> 4;
            }
            rxState_ = 2;
            continue;
        }
        uint8_t scratch[64];
        uint8_t* dst;
        size_t room;
        if (rxLen_ < kRxCap) {
            dst = rx_ + rxLen_;
            room = kRxCap - rxLen_;
        } else {
            dst = scratch;  // goi qua lon: bo phan duoi
            room = sizeof(scratch);
            rxTrunc_ = true;
        }
        size_t want = rxRemain_ - rxGot_;
        if (want > room) want = room;
        if (want > (size_t)av) want = (size_t)av;
        int got = s.read(dst, want);
        if (got <= 0) return 0;
        if (dst != scratch) rxLen_ += (size_t)got;
        rxGot_ += (uint32_t)got;
        if (rxGot_ >= rxRemain_) {
            rxState_ = 0;
            lastRx_ = hal_.millis();
            return rxHdr_ >> 4;
        }
    }
    return 0;
}

void MiniMqtt::dispatch(uint8_t type) {
    pingOut_ = false;  // co goi tin bat ky = broker con song
    if (type != PUBLISH) return;
    uint8_t qos = (rxHdr_ >> 1) & 3;
    if (rxLen_ < 2) return;
    size_t tl = (size_t)((rx_[0] << 8) | rx_[1]);
    size_t pos = 2 + tl;
    uint16_t id = 0;
    if (qos) {
        if (pos + 2 > rxLen_) return;
        id = (uint16_t)((rx_[pos] << 8) | rx_[pos + 1]);
        pos += 2;
    }
    if (pos > rxLen_) return;
    if (qos == 1) {
        uint8_t a[4] = {(uint8_t)(PUBACK << 4), 2, (uint8_t)(id >> 8), (uint8_t)(id & 0xff)};
        if (hal_.socket().write(a, 4) != 4) {
            fail(kErrWrite);
            return;
        }
        lastTx_ = hal_.millis();
    }
    if (hasMsg_) {
        droppedMsgs_++;
        return;
    }
    size_t tcopy = tl < sizeof(msg_.topic) - 1 ? tl : sizeof(msg_.topic) - 1;
    memcpy(msg_.topic, rx_ + 2, tcopy);
    msg_.topic[tcopy] = 0;
    size_t pl = rxLen_ - pos;
    msg_.truncated = rxTrunc_ || pl > sizeof(msg_.payload) - 1;
    if (pl > sizeof(msg_.payload) - 1) pl = sizeof(msg_.payload) - 1;
    memcpy(msg_.payload, rx_ + pos, pl);
    msg_.payload[pl] = 0;
    msg_.len = pl;
    hasMsg_ = true;
}

bool MiniMqtt::waitFor(uint8_t type, uint16_t id, uint32_t timeoutMs) {
    uint32_t start = hal_.millis();
    for (;;) {
        int t = pump();
        if (t < 0) return false;
        if (t) {
            if (t == type && (id == 0 || rxId() == id)) return true;
            dispatch((uint8_t)t);
            if (!hal_.socket().isOpen()) {
                fail(lastError_ ? lastError_ : kErrSocket);
                return false;
            }
            continue;
        }
        if (!hal_.socket().isOpen()) {
            fail(kErrSocket);
            return false;
        }
        if (elapsedMs(hal_.millis(), start) >= timeoutMs) {
            fail(kErrTimeout);
            return false;
        }
        hal_.idle(2);
    }
}

bool MiniMqtt::connect(const MqttOptions& o) {
    close(false);
    lost_ = false;
    lastError_ = kOk;
    keepAliveS_ = o.keepAliveS ? o.keepAliveS : 60;
    if (!hal_.socket().open(o.host, o.port, o.tls, o.timeoutMs)) {
        lastError_ = kErrSocket;
        hal_.socket().close();
        return false;
    }
    bool hasUser = o.user && o.user[0];
    bool hasPass = hasUser && o.pass && o.pass[0];
    bool hasWill = o.willTopic && o.willTopic[0];
    size_t n = kHdrRoom;
    putStr(n, "MQTT");
    tx_[n++] = 4;  // MQTT 3.1.1
    uint8_t flags = 0x02;  // clean session
    if (hasWill) flags |= 0x04;  // will QoS0, khong retain
    if (hasUser) flags |= 0x80;
    if (hasPass) flags |= 0x40;
    tx_[n++] = flags;
    tx_[n++] = (uint8_t)(keepAliveS_ >> 8);
    tx_[n++] = (uint8_t)(keepAliveS_ & 0xff);
    bool ok = putStr(n, o.clientId);
    if (hasWill) ok = ok && putStr(n, o.willTopic) && putStr(n, o.willMsg ? o.willMsg : "");
    if (hasUser) ok = ok && putStr(n, o.user);
    if (hasPass) ok = ok && putStr(n, o.pass);
    if (!ok) {
        fail(kErrTooLarge);
        return false;
    }
    size_t start;
    size_t len = frame(tx_, (uint8_t)(CONNECT << 4), n, start);
    if (hal_.socket().write(tx_ + start, len) != len) {
        fail(kErrWrite);
        return false;
    }
    lastTx_ = hal_.millis();
    if (!waitFor(CONNACK, 0, o.timeoutMs)) return false;
    if (rxLen_ < 2) {
        fail(kErrProtocol);
        return false;
    }
    if (rx_[1] != 0) {
        lastError_ = rx_[1];
        hal_.socket().close();
        return false;
    }
    connected_ = true;
    pingOut_ = false;
    lastTx_ = lastRx_ = hal_.millis();
    return true;
}

bool MiniMqtt::subscribe(const char* topic, uint8_t qos, uint32_t timeoutMs) {
    if (!connected_) {
        lastError_ = kErrNotConnected;
        return false;
    }
    uint16_t id = nextId();
    size_t n = kHdrRoom;
    tx_[n++] = (uint8_t)(id >> 8);
    tx_[n++] = (uint8_t)(id & 0xff);
    if (!putStr(n, topic) || n + 1 > kTxCap) {
        lastError_ = kErrTooLarge;
        return false;
    }
    tx_[n++] = qos > 1 ? 1 : qos;
    size_t start;
    size_t len = frame(tx_, (uint8_t)((SUBSCRIBE << 4) | 0x02), n, start);
    if (hal_.socket().write(tx_ + start, len) != len) {
        fail(kErrWrite);
        return false;
    }
    lastTx_ = hal_.millis();
    if (!waitFor(SUBACK, id, timeoutMs)) return false;
    if (rxLen_ < 3 || rx_[2] == 0x80) {
        lastError_ = kErrProtocol;
        return false;
    }
    return true;
}

bool MiniMqtt::publish(const char* topic, const uint8_t* payload, size_t plen, uint8_t qos, bool retain,
                       uint32_t timeoutMs) {
    if (!connected_) {
        lastError_ = kErrNotConnected;
        return false;
    }
    if (qos > 1) qos = 1;
    size_t n = kHdrRoom;
    if (!putStr(n, topic)) {
        lastError_ = kErrTooLarge;
        return false;
    }
    uint16_t id = 0;
    if (qos) {
        id = nextId();
        tx_[n++] = (uint8_t)(id >> 8);
        tx_[n++] = (uint8_t)(id & 0xff);
    }
    if (n + plen > kTxCap) {
        lastError_ = kErrTooLarge;
        return false;
    }
    memcpy(tx_ + n, payload, plen);
    n += plen;
    size_t start;
    size_t len = frame(tx_, (uint8_t)((PUBLISH << 4) | (qos << 1) | (retain ? 1 : 0)), n, start);
    if (hal_.socket().write(tx_ + start, len) != len) {
        fail(kErrWrite);
        return false;
    }
    lastTx_ = hal_.millis();
    if (!qos) return true;
    return waitFor(PUBACK, id, timeoutMs);
}

void MiniMqtt::loop() {
    if (!connected_) return;
    for (int i = 0; i < 8; i++) {
        int t = pump();
        if (t < 0 || !connected_) return;
        if (!t) break;
        dispatch((uint8_t)t);
        if (!connected_) return;
    }
    if (!hal_.socket().isOpen()) {
        fail(kErrSocket);
        return;
    }
    uint32_t now = hal_.millis();
    uint32_t half = (uint32_t)keepAliveS_ * 500UL;
    uint32_t waitResp = half > 10000 ? half : 10000;
    if (pingOut_) {
        if (elapsedMs(now, pingAt_) >= waitResp) fail(kErrTimeout);  // ket noi chet im
    } else if (elapsedMs(now, lastTx_) >= half) {
        uint8_t p[2] = {(uint8_t)(PINGREQ << 4), 0};
        if (hal_.socket().write(p, 2) != 2) {
            fail(kErrWrite);
            return;
        }
        lastTx_ = pingAt_ = now;
        pingOut_ = true;
        pings_++;
    }
}
