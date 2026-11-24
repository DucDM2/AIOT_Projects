#include "GatewayWifi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {
const uint32_t kClockReadValidMs = 10UL * 60UL * 1000UL;
const uint32_t kWifiMaxBackoffMs = 60UL * 1000UL;

// Goi y nguyen nhan theo ma ly do rot WiFi (ESP8266 WiFiDisconnectReason, trung ma voi ESP-IDF)
const char* wifiReasonHint(int r) {
    switch (r) {
        case 0: return "chua ro";
        case 2: case 15: case 204: return "co the SAI MAT KHAU";
        case 202: return "SAI MAT KHAU / bi tu choi xac thuc";
        case 201: return "KHONG THAY MANG (sai ten, ngoai vung song, hoac mang chi co 5 GHz)";
        case 200: return "mat song (beacon timeout) - dat gateway gan router hon";
        case 203: return "router tu choi lien ket (day thiet bi?)";
        case 8: return "tu ngat";
        default: return "xem WiFiDisconnectReason";
    }
}

// Chep chuoi vao JSON an toan (bo dau " \ va ky tu dieu khien)
void jsonSafe(const char* in, char* out, size_t cap) {
    size_t o = 0;
    for (; in && *in && o + 1 < cap; in++) {
        char c = *in;
        out[o++] = (c == '"' || c == '\\' || (unsigned char)c < 0x20) ? '_' : c;
    }
    out[o] = 0;
}
}  // namespace

const char* GatewayWifi::stateName(State s) {
    switch (s) {
        case State::WifiConnect: return "WIFI_CONNECT";
        case State::WifiWait: return "WIFI_WAIT";
        case State::MqttConnect: return "MQTT_CONNECT";
        case State::Running: return "RUNNING";
        case State::Backoff: return "BACKOFF";
    }
    return "?";
}

GatewayWifi::GatewayWifi(WifiHal& hal, const GatewayConfig& cfg) : hal_(hal), cfg_(cfg), mqtt_(hal) {
    clientId_[0] = statusTopic_[0] = cmdTopic_[0] = respTopic_[0] = 0;
}

void GatewayWifi::begin() {
    uint32_t now = hal_.millis();
    bootMs_ = now;
    lastServiceMs_ = now;
    char gw[33];
    Uplink::toLower(cfg_.gatewayId, gw, sizeof(gw));
    snprintf(statusTopic_, sizeof(statusTopic_), "kholanh/gw/%s/status", gw);
    snprintf(cmdTopic_, sizeof(cmdTopic_), "kholanh/gw/%s/cmd", gw);
    snprintf(respTopic_, sizeof(respTopic_), "kholanh/gw/%s/resp", gw);
    snprintf(clientId_, sizeof(clientId_), "kholanh-gw-%s-esp8266-%08lx", gw, (unsigned long)hal_.random32());
    sim_.begin(cfg_.devices, cfg_.deviceCount, cfg_.sensorPeriodMs, now, hal_.random32());
    go(State::WifiConnect);
}

void GatewayWifi::setWifi(int idx, const char* ssid, const char* pass) {
    if (idx < 0 || idx > 1) return;
    cfg_.wifi[idx].ssid = ssid;
    cfg_.wifi[idx].pass = pass;
}

// ================= Tien ich trang thai =================

void GatewayWifi::go(State s) {
    if (s != state_) hal_.logf("[GW] %s -> %s", stateName(state_), stateName(s));
    state_ = s;
    stateSince_ = hal_.millis();
}

void GatewayWifi::retryLater(State s, uint32_t delayMs) {
    if (delayMs == 0) {
        go(s);
        return;
    }
    hal_.logf("[GW] thu lai %s sau %lu s", stateName(s), (unsigned long)(delayMs / 1000));
    next_ = s;
    backoffMs_ = delayMs;
    go(State::Backoff);
}

uint32_t GatewayWifi::backoffFor(int failures, uint32_t cap) const {
    if (failures < 1) failures = 1;
    if (failures > 12) failures = 12;
    uint32_t d = 5000UL << (failures - 1);
    return d > cap ? cap : d;
}

int GatewayWifi::wifiCount() const {
    int n = 0;
    for (int i = 0; i < 2; i++)
        if (cfg_.wifi[i].ssid && cfg_.wifi[i].ssid[0]) n++;
    return n;
}

void GatewayWifi::resetWifi(const char* why) {
    hal_.logf("[GW] tat/bat lai WiFi: %s", why);
    stats_.wifiResets++;
    mqtt_.close(false);
    mqtt_.takeLost();
    hal_.wifiStop();
}

// ================= Vong lap chinh =================

void GatewayWifi::loop() {
    generateSensors();
    updateClock();

    if (wifiResetRequested_) {
        wifiResetRequested_ = false;
        resetWifi("theo yeu cau");
        wifiFails_ = 0;
        go(State::WifiConnect);
    }

    switch (state_) {
        case State::WifiConnect: stepWifiConnect(); break;
        case State::WifiWait: stepWifiWait(); break;
        case State::MqttConnect: stepMqttConnect(); break;
        case State::Running: stepRunning(); break;
        case State::Backoff:
            if (elapsedMs(hal_.millis(), stateSince_) >= backoffMs_) go(next_);
            break;
    }
    generateSensors();
    checkNoService();
}

void GatewayWifi::generateSensors() {
    Reading r;
    for (int i = 0; i < 8 && sim_.poll(hal_.millis(), clock_, r); i++) enqueue(r);
}

bool GatewayWifi::enqueue(const Reading& r) {
    stats_.queued++;
    if (!queue_.push(r)) {
        stats_.dropped++;
        if (stats_.dropped == 1 || stats_.dropped % 50 == 0)
            hal_.logf("[GW] hang doi day (%d), da bo %lu ban tin cu nhat", kQueueCap, (unsigned long)stats_.dropped);
        return false;
    }
    return true;
}

bool GatewayWifi::enqueueRaw(const char* json) {
    char eui[sizeof(RawUplink::devEui)];
    size_t n = strlen(json);
    if (n >= sizeof(RawUplink::json)) return false;
    if (!Uplink::parseRaw(json, eui, sizeof(eui))) return false;
    bool dropped;
    RawUplink& u = raw_.emplace(dropped);  // ghi thang vao hang doi (RawUplink ~670 byte, stack ESP8266 chi 4 KB)
    memcpy(u.devEui, eui, sizeof(eui));
    memcpy(u.json, json, n + 1);
    stats_.rawQueued++;
    if (dropped) stats_.rawDropped++;
    return true;
}

void GatewayWifi::updateClock() {
    uint32_t now = hal_.millis();
    uint32_t every = clock_.valid() ? kClockReadValidMs : 1000;
    if (elapsedMs(now, lastClockRead_) < every) return;
    lastClockRead_ = now;
    int64_t e;
    if (!hal_.epochMs(e) || !clock_.setFromEpoch(e, now)) return;
    if (stats_.clockSyncs++ == 0) {
        char iso[32];
        TimeKeeper::formatIso(e, iso, sizeof(iso));
        hal_.logf("[GW] dong bo gio SNTP: %s (UTC)", iso);
    }
}

void GatewayWifi::checkNoService() {
    if (!cfg_.noServiceRestartMs) return;
    uint32_t now = hal_.millis();
    if (elapsedMs(now, lastServiceMs_) < cfg_.noServiceRestartMs) return;
    lastServiceMs_ = now;  // xet lai sau them mot chu ky
    unsigned long mins = (unsigned long)(cfg_.noServiceRestartMs / 60000);
    if (!hal_.wifiConnected()) {
        // Router tat / ngoai vung song thi khoi dong lai cung vo ich, lai mat het hang doi trong RAM.
        // Chi khoi dong lai khi VAN THAY mang ma chip khong vao duoc (nghi ngan xep WiFi bi ket).
        bool seen = false;
        for (int i = 0; i < 2 && !seen; i++)
            if (cfg_.wifi[i].ssid && cfg_.wifi[i].ssid[0] && hal_.wifiSeen(cfg_.wifi[i].ssid)) seen = true;
        if (!seen) {
            hal_.logf("[GW] %lu phut khong gui duoc, khong thay mang WiFi nao -> giu nguyen, cho router (%d ban tin)",
                      mins, queueSize());
            return;
        }
    }
    hal_.logf("[GW] %lu phut khong gui duoc ban tin nao -> khoi dong lai ESP8266 (mat %d ban tin chua gui)", mins,
              queueSize());
    hal_.systemRestart();
}

// ================= Cac buoc =================

void GatewayWifi::stepWifiConnect() {
    if (hal_.wifiConnected()) {
        wifiUpAt_ = hal_.millis();
        go(State::MqttConnect);
        return;
    }
    int n = wifiCount();
    if (n == 0) {
        hal_.logf("[GW] chua cau hinh WiFi. Go:  wifi set TenWifi|MatKhau");
        retryLater(State::WifiConnect, 30000);
        return;
    }
    // Moi mang thu 2 lan roi doi sang mang du phong
    int pick = (wifiFails_ / 2) % n;
    wifiIdx_ = 0;
    for (int i = 0, k = 0; i < 2; i++) {
        if (!cfg_.wifi[i].ssid || !cfg_.wifi[i].ssid[0]) continue;
        if (k++ == pick) {
            wifiIdx_ = i;
            break;
        }
    }
    const WifiNetwork& w = cfg_.wifi[wifiIdx_];
    hal_.logf("[GW] ket noi WiFi \"%s\"...", w.ssid);
    hal_.wifiBegin(w.ssid, w.pass ? w.pass : "");
    go(State::WifiWait);
}

void GatewayWifi::stepWifiWait() {
    uint32_t now = hal_.millis();
    if (hal_.wifiConnected()) {
        char ip[20];
        hal_.wifiIp(ip, sizeof(ip));
        hal_.logf("[GW] WiFi OK \"%s\" IP=%s RSSI=%d dBm", cfg_.wifi[wifiIdx_].ssid, ip, hal_.wifiRssi());
        stats_.wifiConnects++;
        wifiFails_ = 0;
        wifiUpAt_ = now;
        go(State::MqttConnect);
        return;
    }
    if (elapsedMs(now, stateSince_) < cfg_.wifiTimeoutMs) return;
    wifiFails_++;
    stats_.wifiFails++;
    int r = hal_.wifiLastReason();
    hal_.logf("[GW] WiFi \"%s\" chua vao duoc sau %lu s (ly do %d: %s), lan %d", cfg_.wifi[wifiIdx_].ssid,
              (unsigned long)(cfg_.wifiTimeoutMs / 1000), r, wifiReasonHint(r), wifiFails_);
    hal_.wifiStop();
    retryLater(State::WifiConnect, backoffFor(wifiFails_, kWifiMaxBackoffMs));
}

void GatewayWifi::stepMqttConnect() {
    uint32_t now = hal_.millis();
    if (!hal_.wifiConnected()) {
        stats_.wifiLost++;
        hal_.logf("[GW] mat WiFi truoc khi ket noi MQTT");
        go(State::WifiConnect);
        return;
    }
    // Cho SNTP mot chut de ban tin mang dung thoi diem do (khong co gio thi app dung gio may chu)
    if (!clock_.valid() && elapsedMs(now, wifiUpAt_) < cfg_.timeWaitMs) return;

    char will[96];
    snprintf(will, sizeof(will), "{\"gw\":\"%s\",\"online\":false}", cfg_.gatewayId);
    MqttOptions o = {};
    o.host = cfg_.mqttHost;
    o.port = cfg_.mqttPort;
    o.tls = cfg_.mqttTls;
    o.clientId = clientId_;
    o.user = cfg_.mqttUser;
    o.pass = cfg_.mqttPass;
    o.keepAliveS = cfg_.keepAliveS;
    o.willTopic = statusTopic_;
    o.willMsg = will;
    o.timeoutMs = cfg_.mqttTimeoutMs;

    hal_.logf("[GW] ket noi MQTT %s:%u (%s)", o.host, o.port, o.tls ? "TLS" : "TCP");
    if (mqtt_.connect(o) && mqtt_.subscribe(cmdTopic_, 1, cfg_.ackTimeoutMs)) {
        stats_.mqttConnects++;
        mqttFails_ = 0;
        authRefused_ = false;
        mqtt_.takeLost();
        hal_.logf("[GW] MQTT da ket noi. Hang doi: %d ban tin", queueSize());
        statusDue_ = true;
        go(State::Running);
        return;
    }
    stats_.mqttConnFails++;
    int e = mqtt_.lastError();
    mqtt_.close(false);
    mqtt_.takeLost();
    if (!hal_.wifiConnected()) {
        stats_.wifiLost++;
        hal_.logf("[GW] mat WiFi khi ket noi MQTT");
        go(State::WifiConnect);
        return;
    }
    if (e == MiniMqtt::kBadUserPass || e == MiniMqtt::kNotAuthorized || e == MiniMqtt::kRefusedClientId) {
        // Broker tu choi -> mang van tot, reset WiFi/chip khong giup gi. Thu lai thua, giu du lieu.
        authRefused_ = true;
        hal_.logf("[GW] %s (ma %d): kiem tra MQTT_USER/MQTT_PASS tren EMQX", MiniMqtt::errorName(e), e);
        lastServiceMs_ = now;
        retryLater(State::MqttConnect, cfg_.maxBackoffMs);
        return;
    }
    mqttFails_++;
    hal_.logf("[GW] MQTT loi: %s (lan %d)", MiniMqtt::errorName(e), mqttFails_);
    if (mqttFails_ % 4 == 0) {
        // WiFi bao co IP nhung khong toi duoc broker nhieu lan -> lam moi han ket noi WiFi
        resetWifi("WiFi co IP nhung khong toi duoc broker");
        retryLater(State::WifiConnect, 2000);
        return;
    }
    retryLater(State::MqttConnect, backoffFor(mqttFails_, cfg_.maxBackoffMs));
}

void GatewayWifi::onPublishFail(const char* what) {
    stats_.pubFail++;
    stats_.mqttLost++;
    hal_.logf("[GW] gui %s loi: %s -> ket noi lai MQTT", what, MiniMqtt::errorName(mqtt_.lastError()));
    mqtt_.close(false);
    mqtt_.takeLost();
    retryLater(State::MqttConnect, 1000);
}

void GatewayWifi::stepRunning() {
    uint32_t now = hal_.millis();
    if (!hal_.wifiConnected()) {
        stats_.wifiLost++;
        hal_.logf("[GW] mat WiFi (ly do %d: %s). Hang doi: %d", hal_.wifiLastReason(),
                  wifiReasonHint(hal_.wifiLastReason()), queueSize());
        mqtt_.close(false);
        mqtt_.takeLost();
        go(State::WifiConnect);
        return;
    }
    mqtt_.loop();
    if (!mqtt_.connected()) {
        mqtt_.takeLost();
        stats_.mqttLost++;
        hal_.logf("[GW] mat ket noi MQTT (%s)", MiniMqtt::errorName(mqtt_.lastError()));
        retryLater(State::MqttConnect, 2000);
        return;
    }
    if (mqtt_.hasMessage()) {
        handleCommand(mqtt_.message());
        mqtt_.releaseMessage();
        if (state_ != State::Running) return;
    }

    if (!drainQueue()) return;

    if (statusDue_ || elapsedMs(now, lastStatus_) >= cfg_.statusIntervalMs) {
        statusDue_ = false;
        lastStatus_ = now;
        publishStatus();
    }
}

bool GatewayWifi::drainQueue() {
    for (int i = 0; i < cfg_.publishPerLoop; i++) {
        size_t len;
        bool fromRaw = false;
        if (raw_.size()) {
            RawUplink* u = raw_.front();
            if (!Uplink::topic(u->devEui, topicBuf_, sizeof(topicBuf_))) {
                raw_.pop();
                continue;
            }
            len = strlen(u->json);
            memcpy(buf_, u->json, len + 1);
            fromRaw = true;
        } else if (queue_.size()) {
            Reading* r = queue_.front();
            const SimDevice& dev = cfg_.devices[r->dev];
            len = Uplink::buildJson(*r, dev, cfg_.gatewayId, clock_, buf_, sizeof(buf_));
            if (!len || !Uplink::topic(dev.id, topicBuf_, sizeof(topicBuf_))) {
                queue_.pop();  // khong dung duoc -> bo, tranh ket hang doi
                continue;
            }
        } else {
            return true;
        }
        if (!mqtt_.publish(topicBuf_, (const uint8_t*)buf_, len, 1, false, cfg_.ackTimeoutMs)) {
            if (mqtt_.lastError() == MiniMqtt::kErrTooLarge) {
                stats_.tooLarge++;
                if (fromRaw) raw_.pop();
                else queue_.pop();
                continue;
            }
            onPublishFail("ban tin");
            return false;
        }
        // Chi xoa khi broker da xac nhan (PUBACK)
        if (fromRaw) {
            raw_.pop();
            hal_.logf("[UP] OK (PC) %s", topicBuf_);
        } else {
            queue_.pop();
        }
        stats_.pubOk++;
        lastServiceMs_ = hal_.millis();
    }
    return true;
}

bool GatewayWifi::publishStatus() {
    uint32_t now = hal_.millis();
    char ip[20], ssid[40];
    hal_.wifiIp(ip, sizeof(ip));
    jsonSafe(cfg_.wifi[wifiIdx_].ssid, ssid, sizeof(ssid));
    int n = snprintf(buf_, sizeof(buf_),
                     "{\"gw\":\"%s\",\"net\":\"wifi\",\"hw\":\"esp8266\",\"online\":true,\"up\":%lu,\"rssi\":%d,\"ssid\":\"%s\","
                     "\"ip\":\"%s\",\"mac\":\"%s\",\"q\":%d,\"drop\":%lu,\"pub_ok\":%lu,\"pub_fail\":%lu,"
                     "\"mqtt_lost\":%lu,\"wifi_lost\":%lu,\"wifi_rst\":%lu,\"heap\":%lu,\"min_heap\":%lu,\"rst\":\"%s\"}",
                     cfg_.gatewayId, (unsigned long)(elapsedMs(now, bootMs_) / 1000), hal_.wifiRssi(), ssid, ip,
                     hal_.macAddress(), queueSize(), (unsigned long)stats_.dropped, (unsigned long)stats_.pubOk,
                     (unsigned long)stats_.pubFail, (unsigned long)stats_.mqttLost, (unsigned long)stats_.wifiLost,
                     (unsigned long)stats_.wifiResets, (unsigned long)hal_.freeHeap(),
                     (unsigned long)hal_.minFreeHeap(), hal_.resetReason());
    if (n <= 0 || (size_t)n >= sizeof(buf_)) return true;
    if (!mqtt_.publish(statusTopic_, (const uint8_t*)buf_, (size_t)n, 1, false, cfg_.ackTimeoutMs)) {
        onPublishFail("status");
        return false;
    }
    stats_.pubOk++;
    lastServiceMs_ = now;
    hal_.logf("[GW] status: RSSI=%d q=%d ok=%lu loi=%lu", hal_.wifiRssi(), queueSize(), (unsigned long)stats_.pubOk,
              (unsigned long)stats_.pubFail);
    return true;
}

void GatewayWifi::handleCommand(const MqttInbound& m) {
    stats_.rxCmds++;
    hal_.logf("[CMD] %s: %.*s%s", m.topic, (int)m.len, m.payload, m.truncated ? "..." : "");
    char reply[160];
    const char* p = m.payload;
    if (strcmp(p, "ping") == 0) {
        snprintf(reply, sizeof(reply), "{\"gw\":\"%s\",\"pong\":true,\"q\":%d,\"rssi\":%d}", cfg_.gatewayId,
                 queueSize(), hal_.wifiRssi());
    } else if (strcmp(p, "status") == 0) {
        statusDue_ = true;
        return;
    } else if (strcmp(p, "reset_wifi") == 0) {
        wifiResetRequested_ = true;
        return;
    } else if (strcmp(p, "sim_on") == 0 || strcmp(p, "sim_off") == 0) {
        sim_.setEnabled(p[5] == 'n');
        snprintf(reply, sizeof(reply), "{\"gw\":\"%s\",\"sim\":%s}", cfg_.gatewayId, sim_.enabled() ? "true" : "false");
    } else {
        snprintf(reply, sizeof(reply), "{\"gw\":\"%s\",\"error\":\"lenh khong ho tro\"}", cfg_.gatewayId);
    }
    if (mqtt_.publish(respTopic_, (const uint8_t*)reply, strlen(reply), 1, false, cfg_.ackTimeoutMs)) {
        stats_.pubOk++;
        lastServiceMs_ = hal_.millis();
    } else {
        onPublishFail("tra loi lenh");
    }
}

void GatewayWifi::printStatus() {
    char iso[32] = "chua co";
    char ip[20];
    if (clock_.valid()) TimeKeeper::formatIso(clock_.epochAt(hal_.millis()), iso, sizeof(iso));
    hal_.wifiIp(ip, sizeof(ip));
    hal_.logf("==== TRANG THAI GATEWAY %s (WiFi) ====", cfg_.gatewayId);
    hal_.logf("trang thai : %s | MQTT %s | gio UTC %s", stateName(state_), mqtt_.connected() ? "ket noi" : "mat", iso);
    hal_.logf("WiFi       : %s \"%s\" IP=%s RSSI=%d dBm MAC=%s | ly do rot gan nhat %d (%s)",
              hal_.wifiConnected() ? "CO" : "MAT", cfg_.wifi[wifiIdx_].ssid ? cfg_.wifi[wifiIdx_].ssid : "", ip,
              hal_.wifiRssi(), hal_.macAddress(), hal_.wifiLastReason(), wifiReasonHint(hal_.wifiLastReason()));
    hal_.logf("hang doi   : %d/%d (+%d PC) | da tao %lu | bo %lu | gia lap %s", queue_.size(), kQueueCap, raw_.size(),
              (unsigned long)stats_.queued, (unsigned long)stats_.dropped, sim_.enabled() ? "BAT" : "TAT");
    hal_.logf("MQTT       : gui OK %lu, loi %lu, ket noi %lu, ket noi loi %lu, rot %lu, ping %lu%s",
              (unsigned long)stats_.pubOk, (unsigned long)stats_.pubFail, (unsigned long)stats_.mqttConnects,
              (unsigned long)stats_.mqttConnFails, (unsigned long)stats_.mqttLost, (unsigned long)mqtt_.pings(),
              authRefused_ ? " | BROKER TU CHOI user/pass" : "");
    hal_.logf("WiFi stats : vao %lu, that bai %lu, rot %lu, reset %lu", (unsigned long)stats_.wifiConnects,
              (unsigned long)stats_.wifiFails, (unsigned long)stats_.wifiLost, (unsigned long)stats_.wifiResets);
    hal_.logf("RAM        : free %lu, min %lu | reset: %s | uptime %lu s", (unsigned long)hal_.freeHeap(),
              (unsigned long)hal_.minFreeHeap(), hal_.resetReason(),
              (unsigned long)(elapsedMs(hal_.millis(), bootMs_) / 1000));
}
