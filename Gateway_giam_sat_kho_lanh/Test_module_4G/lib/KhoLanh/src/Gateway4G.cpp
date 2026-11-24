#include "Gateway4G.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {
const uint32_t kClockResyncMs = 6UL * 3600UL * 1000UL;
const uint32_t kRegPollMs = 3000;
}  // namespace

const char* Gateway4G::stateName(State s) {
    switch (s) {
        case State::PowerOn: return "POWER_ON";
        case State::HardReset: return "HARD_RESET";
        case State::Init: return "INIT";
        case State::WaitSim: return "WAIT_SIM";
        case State::WaitNetwork: return "WAIT_NETWORK";
        case State::AttachPdp: return "ATTACH_PDP";
        case State::MqttConnect: return "MQTT_CONNECT";
        case State::Running: return "RUNNING";
        case State::Backoff: return "BACKOFF";
    }
    return "?";
}

Gateway4G::Gateway4G(ModemHal& hal, const GatewayConfig& cfg) : hal_(hal), cfg_(cfg), modem_(hal) {
    clientId_[0] = statusTopic_[0] = cmdTopic_[0] = respTopic_[0] = 0;
}

void Gateway4G::begin() {
    uint32_t now = hal_.millis();
    bootMs_ = now;
    lastServiceMs_ = now;
    char gw[33];
    Uplink::toLower(cfg_.gatewayId, gw, sizeof(gw));
    snprintf(statusTopic_, sizeof(statusTopic_), "kholanh/gw/%s/status", gw);
    snprintf(cmdTopic_, sizeof(cmdTopic_), "kholanh/gw/%s/cmd", gw);
    snprintf(respTopic_, sizeof(respTopic_), "kholanh/gw/%s/resp", gw);
    snprintf(clientId_, sizeof(clientId_), "kholanh-gw-%s-%08lx", gw, (unsigned long)hal_.random32());
    sim_.begin(cfg_.devices, cfg_.deviceCount, cfg_.sensorPeriodMs, now, hal_.random32());
    go(State::PowerOn);
}

// ================= Tien ich trang thai =================

void Gateway4G::go(State s) {
    if (s != state_) hal_.logf("[GW] %s -> %s", stateName(state_), stateName(s));
    state_ = s;
    stateSince_ = hal_.millis();
}

void Gateway4G::retryLater(State s, uint32_t delayMs) {
    if (delayMs == 0) {
        go(s);
        return;
    }
    hal_.logf("[GW] thu lai %s sau %lu s", stateName(s), (unsigned long)(delayMs / 1000));
    next_ = s;
    backoffMs_ = delayMs;
    go(State::Backoff);
}

uint32_t Gateway4G::backoffFor(int failures) const {
    if (failures < 1) failures = 1;
    if (failures > 12) failures = 12;
    uint32_t d = 5000UL << (failures - 1);
    return d > cfg_.maxBackoffMs ? cfg_.maxBackoffMs : d;
}

void Gateway4G::requestHardReset() {
    hardResetStreak_++;
    // Lan dau reset ngay; sau do gian cach 30 s, 60 s, 120 s ... toi da 15 phut
    uint32_t d = 0;
    if (hardResetStreak_ > 1) {
        int sh = hardResetStreak_ - 2;
        if (sh > 5) sh = 5;
        d = 30000UL << sh;
        if (d > 900000UL) d = 900000UL;
    }
    retryLater(State::HardReset, d);
}

void Gateway4G::requestModemReset() { resetRequested_ = true; }

// ================= Vong lap chinh =================

void Gateway4G::loop() {
    modem_.at().poll();
    generateSensors();
    handleUrcFlags();

    switch (state_) {
        case State::PowerOn: stepPowerOn(); break;
        case State::HardReset: stepHardReset(); break;
        case State::Init: stepInit(); break;
        case State::WaitSim: stepWaitSim(); break;
        case State::WaitNetwork: stepWaitNetwork(); break;
        case State::AttachPdp: stepAttachPdp(); break;
        case State::MqttConnect: stepMqttConnect(); break;
        case State::Running: stepRunning(); break;
        case State::Backoff:
            if (elapsedMs(hal_.millis(), stateSince_) >= backoffMs_) go(next_);
            break;
    }
    generateSensors();
    checkNoService();
}

void Gateway4G::generateSensors() {
    Reading r;
    for (int i = 0; i < 8 && sim_.poll(hal_.millis(), clock_, r); i++) enqueue(r);
}

bool Gateway4G::enqueue(const Reading& r) {
    stats_.queued++;
    if (!queue_.push(r)) {
        stats_.dropped++;
        if (stats_.dropped == 1 || stats_.dropped % 50 == 0)
            hal_.logf("[GW] hang doi day (%d), da bo %lu ban tin cu nhat", kQueueCap, (unsigned long)stats_.dropped);
        return false;
    }
    return true;
}

bool Gateway4G::enqueueRaw(const char* json) {
    RawUplink u;
    size_t n = strlen(json);
    if (n >= sizeof(u.json)) return false;
    if (!Uplink::parseRaw(json, u.devEui, sizeof(u.devEui))) return false;
    memcpy(u.json, json, n + 1);
    stats_.rawQueued++;
    if (!raw_.push(u)) stats_.rawDropped++;
    return true;
}

void Gateway4G::handleUrcFlags() {
    if (resetRequested_) {
        resetRequested_ = false;
        hal_.logf("[GW] yeu cau reset modem tu nguoi dung");
        hardResetStreak_ = 0;
        go(State::HardReset);
        return;
    }
    if (state_ == State::PowerOn || state_ == State::HardReset) {
        modem_.takeRebooted();
        modem_.takePowerDown();
        return;
    }
    if (modem_.takePowerDown()) {
        hal_.logf("[GW] modem bao POWER DOWN");
        go(State::PowerOn);
        return;
    }
    if (modem_.takeRebooted()) {
        stats_.modemReboots++;
        hal_.logf("[GW] modem tu khoi dong lai -> cau hinh lai");
        go(State::Init);
        return;
    }
    // Modem khong tra loi lien tiep -> treo
    if (modem_.at().consecutiveTimeouts() >= 3) {
        hal_.logf("[GW] modem khong tra loi AT %u lan lien tiep", modem_.at().consecutiveTimeouts());
        modem_.at().clearTimeouts();
        requestHardReset();
        return;
    }
    if (modem_.takeSimLost() && state_ != State::Init && state_ != State::WaitSim) {
        hal_.logf("[GW] mat SIM");
        go(State::WaitSim);
        return;
    }
    if (modem_.takePdpLost()) healthDue_ = true;
    if (state_ == State::Running && modem_.takeMqttLost()) {
        stats_.mqttLost++;
        hal_.logf("[GW] mat ket noi MQTT");
        retryLater(State::MqttConnect, 2000);
    }
}

void Gateway4G::checkNoService() {
    if (!cfg_.noServiceRestartMs) return;
    uint32_t now = hal_.millis();
    if (elapsedMs(now, lastServiceMs_) < cfg_.noServiceRestartMs) return;
    hal_.logf("[GW] %lu phut khong gui duoc ban tin nao -> khoi dong lai ESP32",
              (unsigned long)(cfg_.noServiceRestartMs / 60000));
    lastServiceMs_ = now;
    hal_.systemRestart();
}

// ================= Cac buoc =================

void Gateway4G::stepPowerOn() {
    if (modem_.powerOn()) {
        go(State::Init);
    } else {
        hal_.logf("[GW] modem khong len nguon - kiem tra day nguon/PWRKEY/UART");
        requestHardReset();
    }
}

void Gateway4G::stepHardReset() {
    stats_.hardResets++;
    lastHardResetMs_ = hal_.millis() | 1;
    hal_.logf("[GW] cap nguon lai modem (lan %d lien tiep)", hardResetStreak_);
    if (modem_.hardReset()) {
        go(State::Init);
    } else {
        requestHardReset();
    }
}

void Gateway4G::stepInit() {
    if (!modem_.configure()) {
        requestHardReset();
        return;
    }
    modem_.takeRebooted();
    go(State::WaitSim);
}

void Gateway4G::stepWaitSim() {
    SimState s = modem_.waitSim(20000);
    if (s != SimState::Ready) {
        simFails_++;
        hal_.logf("[GW] SIM chua san sang (%s), lan %d",
                  s == SimState::Missing ? "khong co SIM" : s == SimState::Locked ? "SIM bi khoa PIN" : "chua san sang",
                  simFails_);
        requestHardReset();
        return;
    }
    simFails_ = 0;
    hal_.logf("[GW] SIM OK iccid=%s", modem_.info().iccid);
    bool changed = false;
    if (!modem_.ensureApn(cfg_.apn, changed)) hal_.logf("[GW] dat APN loi");
    if (changed) {
        hal_.logf("[GW] da doi APN -> reset song");
        modem_.radioReset();
    }
    if (cfg_.mqttTls && cfg_.caCertName && cfg_.caCertPem && cfg_.caCertPem[0]) {
        if (!modem_.ensureCaCert(cfg_.caCertName, cfg_.caCertPem)) hal_.logf("[GW] nap chung chi CA loi");
    }
    regStart_ = hal_.millis();
    lastRegPoll_ = regStart_ - kRegPollMs;
    radioResetTried_ = false;
    go(State::WaitNetwork);
}

void Gateway4G::stepWaitNetwork() {
    uint32_t now = hal_.millis();
    if (elapsedMs(now, lastRegPoll_) < kRegPollMs) return;
    lastRegPoll_ = now;
    int stat = modem_.updateRegistration();
    modem_.updateSignal();
    if (modem_.isRegistered()) {
        modem_.updateOperator();
        modem_.updateSysInfo();
        hal_.logf("[GW] da dang ky mang: %s, CSQ=%d (%d dBm) %s", modem_.info().oper, modem_.info().csq,
                  modem_.info().rssiDbm, stat == 5 ? "(roaming)" : "");
        pdpFails_ = 0;
        go(State::AttachPdp);
        return;
    }
    if (elapsedMs(now, regStart_) < cfg_.netRegTimeoutMs) return;
    hal_.logf("[GW] qua %lu s chua dang ky duoc mang (stat=%d, CSQ=%d)", (unsigned long)(cfg_.netRegTimeoutMs / 1000),
              stat, modem_.info().csq);
    // Khong co song (CSQ=99) va vua cap nguon chua lau -> chi reset song, khong cap nguon lien tuc
    bool noSignal = modem_.info().csq == 99;
    bool recentHardReset = lastHardResetMs_ && elapsedMs(now, lastHardResetMs_) < 30UL * 60UL * 1000UL;
    if (!radioResetTried_ || (noSignal && recentHardReset)) {
        radioResetTried_ = true;
        stats_.radioResets++;
        modem_.radioReset();
        regStart_ = hal_.millis();
    } else {
        requestHardReset();
    }
}

void Gateway4G::stepAttachPdp() {
    if (modem_.attachPdp(150000)) {
        hal_.logf("[GW] PDP OK, IP=%s", modem_.info().ip);
        pdpFails_ = 0;
        syncClock();
        go(State::MqttConnect);
        return;
    }
    pdpFails_++;
    hal_.logf("[GW] kich hoat PDP that bai (lan %d): %s", pdpFails_, modem_.at().lastError());
    if (pdpFails_ < 3) {
        retryLater(State::AttachPdp, 10000);
        return;
    }
    pdpFails_ = 0;
    stats_.radioResets++;
    modem_.radioReset();
    regStart_ = hal_.millis();
    radioResetTried_ = true;
    go(State::WaitNetwork);
}

void Gateway4G::stepMqttConnect() {
    MqttSettings s;
    s.host = cfg_.mqttHost;
    s.port = cfg_.mqttPort;
    s.tls = cfg_.mqttTls;
    s.clientId = clientId_;
    s.user = cfg_.mqttUser;
    s.pass = cfg_.mqttPass;
    s.keepAliveS = cfg_.keepAliveS;
    s.willTopic = statusTopic_;
    snprintf(buf_, sizeof(buf_), "{\"gw\":\"%s\",\"online\":false}", cfg_.gatewayId);
    s.willMsg = buf_;
    s.caCertName = (cfg_.caCertPem && cfg_.caCertPem[0]) ? cfg_.caCertName : nullptr;

    hal_.logf("[GW] ket noi MQTT %s:%u (%s)", s.host, s.port, s.tls ? "TLS" : "TCP");
    if (modem_.mqttConnect(s) && modem_.mqttSubscribe(cmdTopic_, 1)) {
        stats_.mqttConnects++;
        mqttFails_ = 0;
        pubFailStreak_ = 0;
        hardResetStreak_ = 0;
        netLost_ = false;
        modem_.takeMqttLost();
        hal_.logf("[GW] MQTT da ket noi. Hang doi: %d ban tin", queueSize());
        statusDue_ = true;
        lastHealth_ = hal_.millis();
        go(State::Running);
        return;
    }
    stats_.mqttConnFails++;
    mqttFails_++;
    modem_.updateRegistration();
    if (!modem_.isRegistered()) {
        hal_.logf("[GW] mat dang ky mang -> cho mang");
        regStart_ = hal_.millis();
        radioResetTried_ = false;
        go(State::WaitNetwork);
        return;
    }
    int code = modem_.lastMqttError();
    if (code >= 27 && code <= 31) {
        // Broker tra loi tu choi -> mang/modem van tot, reset khong giup gi. Thu lai thua.
        hal_.logf("[GW] broker TU CHOI ket noi (ma %d): kiem tra user/pass/clientId tren EMQX", code);
        lastServiceMs_ = hal_.millis();  // modem + mang van chay -> khoi dong lai ESP32 khong giup gi (va mat du lieu dem)
        retryLater(State::MqttConnect, cfg_.maxBackoffMs);
        return;
    }
    // Leo thang: PDP o lan 3, 6; cap nguon modem o lan 9, 21, 33... (~1 gio/lan khi backoff 5 phut)
    if (mqttFails_ % 12 == 9) {
        requestHardReset();
        return;
    }
    if (mqttFails_ % 3 == 0) {
        hal_.logf("[GW] MQTT loi %d lan -> kich hoat lai PDP", mqttFails_);
        stats_.pdpResets++;
        modem_.mqttCleanup();
        modem_.detachPdp();
        retryLater(State::AttachPdp, 3000);
        return;
    }
    retryLater(State::MqttConnect, backoffFor(mqttFails_));
}

void Gateway4G::stepRunning() {
    uint32_t now = hal_.millis();

    if (modem_.hasMessage()) {
        MqttMessage m = modem_.message();
        modem_.releaseMessage();
        handleCommand(m);
        if (state_ != State::Running) return;
    }

    if (healthDue_ || elapsedMs(now, lastHealth_) >= cfg_.healthIntervalMs) {
        healthDue_ = false;
        lastHealth_ = now;
        if (!healthCheck()) return;
    }

    if (!clock_.valid() || elapsedMs(now, lastClockSync_) >= kClockResyncMs) {
        // Khong co gio thi thu lai moi 10 phut, co roi thi 6 gio/lan
        if (clock_.valid() || elapsedMs(now, lastClockSync_) >= 600000UL) syncClock();
    }

    if (!drainQueue()) return;

    if (statusDue_ || elapsedMs(now, lastStatus_) >= cfg_.statusIntervalMs) {
        statusDue_ = false;
        lastStatus_ = now;
        if (!publishStatus()) {
            if (++pubFailStreak_ >= 2) retryLater(State::MqttConnect, 1000);
            return;
        }
    }

    if (httpRequested_) {
        httpRequested_ = false;
        runHttpTest();
    }
}

bool Gateway4G::healthCheck() {
    if (!modem_.updateSignal()) return true;  // AT timeout -> handleUrcFlags se xu ly khi du 3 lan
    modem_.updateRegistration();
    uint32_t now = hal_.millis();
    if (modem_.isRegistered()) {
        if (netLost_) hal_.logf("[GW] mang da tro lai");
        netLost_ = false;
        return true;
    }
    if (!netLost_) {
        netLost_ = true;
        netLostSince_ = now;
        hal_.logf("[GW] mat dang ky mang (stat=%d) - theo doi", modem_.info().regStat);
        return true;
    }
    if (elapsedMs(now, netLostSince_) >= cfg_.netLostRecoverMs) {
        hal_.logf("[GW] mat mang qua %lu s -> cho dang ky lai", (unsigned long)(cfg_.netLostRecoverMs / 1000));
        modem_.mqttCleanup();
        regStart_ = now;
        radioResetTried_ = false;
        netLost_ = false;
        go(State::WaitNetwork);
        return false;
    }
    return true;
}

void Gateway4G::syncClock() {
    lastClockSync_ = hal_.millis();
    if (!modem_.updateClock() && cfg_.ntpServer && cfg_.ntpServer[0] && A7680::safeQuoted(cfg_.ntpServer)) {
        char b[16];
        modem_.at().cmd(3000, "AT+CNTP=\"%s\",28", cfg_.ntpServer);  // 28 = UTC+7 (don vi 15 phut)
        modem_.at().cmdUrc("+CNTP:", b, sizeof(b), 30000, "AT+CNTP");
        modem_.updateClock();
    }
    if (modem_.info().clock[0] && clock_.setFromCclk(modem_.info().clock, hal_.millis())) {
        stats_.clockSyncs++;
        char iso[32];
        TimeKeeper::formatIso(clock_.epochAt(hal_.millis()), iso, sizeof(iso));
        hal_.logf("[GW] dong bo gio: %s (UTC)", iso);
    } else {
        hal_.logf("[GW] chua lay duoc gio mang - ban tin se dung gio may chu");
    }
}

bool Gateway4G::drainQueue() {
    for (int i = 0; i < cfg_.publishPerLoop; i++) {
        const char* topic;
        size_t len;
        bool fromRaw = false;
        if (raw_.size()) {
            RawUplink* u = raw_.front();
            if (!Uplink::topic(u->devEui, topicBuf_, sizeof(topicBuf_))) {
                raw_.pop();
                continue;
            }
            topic = topicBuf_;
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
            topic = topicBuf_;
        } else {
            return true;
        }
        if (!modem_.mqttPublish(topic, (const uint8_t*)buf_, len, 1, false)) {
            stats_.pubFail++;
            if (++pubFailStreak_ >= 2) {
                hal_.logf("[GW] gui loi %d lan lien tiep -> ket noi lai MQTT", pubFailStreak_);
                retryLater(State::MqttConnect, 1000);
            }
            return false;
        }
        if (fromRaw) {
            raw_.pop();
            hal_.logf("[UP] OK (PC) %s", topic);
        } else {
            queue_.pop();
        }
        stats_.pubOk++;
        pubFailStreak_ = 0;
        lastServiceMs_ = hal_.millis();
    }
    return true;
}

bool Gateway4G::publishStatus() {
    modem_.updateSignal();
    uint32_t now = hal_.millis();
    int n = snprintf(buf_, sizeof(buf_),
                     "{\"gw\":\"%s\",\"online\":true,\"up\":%lu,\"csq\":%d,\"dbm\":%d,\"op\":\"%s\",\"ip\":\"%s\","
                     "\"q\":%d,\"drop\":%lu,\"pub_ok\":%lu,\"pub_fail\":%lu,\"mqtt_lost\":%lu,\"hard_rst\":%lu,"
                     "\"reboots\":%lu,\"heap\":%lu,\"min_heap\":%lu,\"rst\":\"%s\",\"imei\":\"%s\"}",
                     cfg_.gatewayId, (unsigned long)(elapsedMs(now, bootMs_) / 1000), modem_.info().csq,
                     modem_.info().rssiDbm, modem_.info().oper, modem_.info().ip, queueSize(),
                     (unsigned long)stats_.dropped, (unsigned long)stats_.pubOk, (unsigned long)stats_.pubFail,
                     (unsigned long)stats_.mqttLost, (unsigned long)stats_.hardResets,
                     (unsigned long)stats_.modemReboots, (unsigned long)hal_.freeHeap(),
                     (unsigned long)hal_.minFreeHeap(), hal_.resetReason(), modem_.info().imei);
    if (n <= 0 || (size_t)n >= sizeof(buf_)) return true;
    if (!modem_.mqttPublish(statusTopic_, (const uint8_t*)buf_, (size_t)n, 1, false)) {
        stats_.pubFail++;
        return false;
    }
    stats_.pubOk++;
    lastServiceMs_ = now;
    hal_.logf("[GW] status: CSQ=%d q=%d ok=%lu loi=%lu", modem_.info().csq, queueSize(), (unsigned long)stats_.pubOk,
              (unsigned long)stats_.pubFail);
    return true;
}

void Gateway4G::handleCommand(const MqttMessage& m) {
    stats_.rxCmds++;
    hal_.logf("[CMD] %s: %.*s%s", m.topic, (int)m.len, m.payload, m.truncated ? "..." : "");
    char reply[160];
    const char* p = m.payload;
    if (strcmp(p, "ping") == 0) {
        snprintf(reply, sizeof(reply), "{\"gw\":\"%s\",\"pong\":true,\"q\":%d}", cfg_.gatewayId, queueSize());
    } else if (strcmp(p, "status") == 0) {
        statusDue_ = true;
        return;
    } else if (strcmp(p, "http") == 0) {
        httpRequested_ = true;
        return;
    } else if (strcmp(p, "reset_modem") == 0) {
        resetRequested_ = true;
        return;
    } else if (strcmp(p, "sim_on") == 0 || strcmp(p, "sim_off") == 0) {
        sim_.setEnabled(p[5] == 'n');
        snprintf(reply, sizeof(reply), "{\"gw\":\"%s\",\"sim\":%s}", cfg_.gatewayId, sim_.enabled() ? "true" : "false");
    } else {
        snprintf(reply, sizeof(reply), "{\"gw\":\"%s\",\"error\":\"lenh khong ho tro\"}", cfg_.gatewayId);
    }
    if (modem_.mqttPublish(respTopic_, (const uint8_t*)reply, strlen(reply), 1, false)) {
        stats_.pubOk++;
        lastServiceMs_ = hal_.millis();
    } else {
        stats_.pubFail++;
    }
}

void Gateway4G::runHttpTest() {
    if (!cfg_.httpTestUrl || !cfg_.httpTestUrl[0]) {
        hal_.logf("[HTTP] chua cau hinh URL thu");
        return;
    }
    int len = 0;
    int code = modem_.httpGet(cfg_.httpTestUrl, len);
    if (code >= 200 && code < 400) stats_.httpOk++;
    else stats_.httpFail++;
    hal_.logf("[HTTP] GET %s -> %d, %d byte", cfg_.httpTestUrl, code, len);
}

void Gateway4G::printStatus() {
    const ModemInfo& i = modem_.info();
    char iso[32] = "chua co";
    if (clock_.valid()) TimeKeeper::formatIso(clock_.epochAt(hal_.millis()), iso, sizeof(iso));
    hal_.logf("==== TRANG THAI GATEWAY %s ====", cfg_.gatewayId);
    hal_.logf("trang thai : %s | MQTT %s | gio UTC %s", stateName(state_), modem_.mqttConnected() ? "ket noi" : "mat",
              iso);
    hal_.logf("modem      : %s fw=%s imei=%s", i.model, i.revision, i.imei);
    hal_.logf("SIM/mang   : iccid=%s op=%s reg=%d CSQ=%d (%d dBm) ip=%s", i.iccid, i.oper, i.regStat, i.csq, i.rssiDbm,
              i.ip);
    hal_.logf("he thong   : %s", i.sysinfo);
    hal_.logf("hang doi   : %d/%d (+%d PC) | da tao %lu | bo %lu | gia lap %s", queue_.size(), kQueueCap, raw_.size(),
              (unsigned long)stats_.queued, (unsigned long)stats_.dropped, sim_.enabled() ? "BAT" : "TAT");
    hal_.logf("MQTT       : gui OK %lu, loi %lu, ket noi %lu, ket noi loi %lu, rot %lu", (unsigned long)stats_.pubOk,
              (unsigned long)stats_.pubFail, (unsigned long)stats_.mqttConnects, (unsigned long)stats_.mqttConnFails,
              (unsigned long)stats_.mqttLost);
    hal_.logf("phuc hoi   : PDP %lu, song %lu, cap nguon %lu, modem tu reboot %lu", (unsigned long)stats_.pdpResets,
              (unsigned long)stats_.radioResets, (unsigned long)stats_.hardResets, (unsigned long)stats_.modemReboots);
    const AtClient::Stats& a = modem_.at().stats();
    hal_.logf("AT         : lenh %lu, timeout %lu, loi %lu, dong dai %lu", (unsigned long)a.commands,
              (unsigned long)a.timeouts, (unsigned long)a.errors, (unsigned long)a.lineOverflows);
    hal_.logf("RAM        : free %lu, min %lu | reset: %s | uptime %lu s", (unsigned long)hal_.freeHeap(),
              (unsigned long)hal_.minFreeHeap(), hal_.resetReason(),
              (unsigned long)(elapsedMs(hal_.millis(), bootMs_) / 1000));
}
