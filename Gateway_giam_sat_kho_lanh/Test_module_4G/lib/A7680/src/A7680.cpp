#include "A7680.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// Thoi gian theo tai lieu phan cung A76xx: bat >= 50 ms, tat >= 2.5 s
#ifndef A7680_PWRKEY_ON_MS
#define A7680_PWRKEY_ON_MS 1000
#endif
#ifndef A7680_PWRKEY_OFF_MS
#define A7680_PWRKEY_OFF_MS 3000
#endif
#ifndef A7680_BOOT_TIMEOUT_MS
#define A7680_BOOT_TIMEOUT_MS 30000
#endif

namespace {
bool startsWith(const char* s, const char* p) { return strncmp(s, p, strlen(p)) == 0; }

// Bo dau ngoac kep o dau/cuoi (tai cho)
void stripQuotes(char* s) {
    size_t n = strlen(s);
    if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
        memmove(s, s + 1, n - 2);
        s[n - 2] = 0;
    }
}

void copyStr(char* dst, size_t cap, const char* src) {
    if (!cap) return;
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

bool allDigits(const char* s) {
    if (!*s) return false;
    for (; *s; s++)
        if (!isdigit((unsigned char)*s)) return false;
    return true;
}

bool validIp(const char* ip) {
    if (!ip[0] || strcmp(ip, "0.0.0.0") == 0) return false;
    for (const char* p = ip; *p; p++)
        if (isdigit((unsigned char)*p)) return true;
    return false;
}
}  // namespace

A7680::A7680(ModemHal& hal) : hal_(hal), at_(hal) {
    memset(&info_, 0, sizeof(info_));
    info_.csq = 99;
    memset(&rx_, 0, sizeof(rx_));
    memset(&msg_, 0, sizeof(msg_));
    at_.setListener(this);
}

bool A7680::safeQuoted(const char* s) {
    if (!s) return false;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c < 0x20 || c == 0x7f) return false;
    }
    return true;
}

int A7680::parseIntField(const char* s, int index, int def) {
    int i = 0;
    while (*s) {
        while (*s == ' ' || *s == '"') s++;
        if (i == index) {
            if (*s == '-' || isdigit((unsigned char)*s)) return atoi(s);
            return def;
        }
        const char* c = strchr(s, ',');
        if (!c) return def;
        s = c + 1;
        i++;
    }
    return def;
}

void A7680::clearSessionState() {
    mqttConnected_ = false;
    rxActive_ = false;
    rawTarget_ = RawNone;
}

// ================= Nguon =================

void A7680::pulsePwrKey(uint32_t ms) {
    hal_.setPwrKey(true);
    uint32_t t0 = hal_.millis();
    while (elapsedMs(hal_.millis(), t0) < ms) hal_.idle(ms > 50 ? 50 : ms);
    hal_.setPwrKey(false);
}

bool A7680::isResponsive(uint32_t waitMs) {
    uint32_t t0 = hal_.millis();
    do {
        if (at_.cmd(700, "AT") == AtStatus::Ok) {
            at_.clearTimeouts();
            return true;
        }
        hal_.idle(300);
    } while (elapsedMs(hal_.millis(), t0) < waitMs);
    return false;
}

bool A7680::waitBoot(uint32_t timeoutMs) {
    bool ok = isResponsive(timeoutMs);
    if (ok) {
        // Cho modem in het URC khoi dong (*ATREADY, +CPIN, SMS DONE...) roi xoa co
        hal_.idle(500);
        at_.poll();
    }
    rebooted_ = false;
    powerDown_ = false;
    return ok;
}

bool A7680::powerOn() {
    if (isResponsive(2500)) {
        hal_.logf("[MODEM] dang bat san, tra loi AT");
        return true;
    }
    for (int attempt = 1; attempt <= 2; attempt++) {
        hal_.logf("[MODEM] nhan PWRKEY de bat (lan %d)", attempt);
        pulsePwrKey(A7680_PWRKEY_ON_MS);
        at_.resetParser();
        clearSessionState();
        if (waitBoot(A7680_BOOT_TIMEOUT_MS)) return true;
    }
    return false;
}

bool A7680::softReset() {
    if (at_.cmd(5000, "AT+CRESET") != AtStatus::Ok) return false;
    clearSessionState();
    hal_.idle(3000);
    at_.resetParser();
    return waitBoot(A7680_BOOT_TIMEOUT_MS);
}

bool A7680::hardReset() {
    clearSessionState();
    if (hal_.hasPowerSwitch()) {
        hal_.logf("[MODEM] cat nguon VBAT 5s");
        hal_.setPowerSwitch(false);
        hal_.idle(5000);
        hal_.setPowerSwitch(true);
        hal_.idle(800);
        at_.resetParser();
        if (isResponsive(1500)) return waitBoot(1000);  // board tu bat khi co nguon
        pulsePwrKey(A7680_PWRKEY_ON_MS);
        if (waitBoot(A7680_BOOT_TIMEOUT_MS)) return true;
    }
    if (hal_.hasResetPin()) {
        hal_.logf("[MODEM] keo chan RESET 2.5s");
        hal_.setResetPin(true);
        hal_.idle(2500);
        hal_.setResetPin(false);
        at_.resetParser();
        if (waitBoot(A7680_BOOT_TIMEOUT_MS)) return true;
    }
    // Chi co PWRKEY: khong biet modem dang bat (treo) hay tat.
    // Nhan dai -> tat (neu dang bat). Neu dang tat thi nhan dai cung lam no bat.
    hal_.logf("[MODEM] PWRKEY dai %d ms (tat)", A7680_PWRKEY_OFF_MS);
    pulsePwrKey(A7680_PWRKEY_OFF_MS);
    hal_.idle(8000);
    at_.resetParser();
    if (isResponsive(1500)) return waitBoot(1000);
    hal_.logf("[MODEM] PWRKEY ngan (bat)");
    pulsePwrKey(A7680_PWRKEY_ON_MS);
    at_.resetParser();
    return waitBoot(A7680_BOOT_TIMEOUT_MS);
}

bool A7680::configure() {
    if (!isResponsive(20000)) return false;
    at_.cmd(1000, "ATE0");
    if (at_.cmd(1000, "ATE0") != AtStatus::Ok) return false;
    at_.cmd(1000, "AT+CMEE=2");
    at_.cmd(1000, "AT+CTZU=1");  // dong bo gio theo mang (NITZ)

    char buf[64];
    if (at_.query("", buf, sizeof(buf), 2000, "AT+CGMM") == AtStatus::Ok) copyStr(info_.model, sizeof(info_.model), buf);
    if (at_.query("+CGMR:", buf, sizeof(buf), 2000, "AT+CGMR") == AtStatus::Ok)
        copyStr(info_.revision, sizeof(info_.revision), buf);
    for (int i = 0; i < 3 && !allDigits(info_.imei); i++) {
        if (at_.query("", buf, sizeof(buf), 2000, "AT+CGSN") == AtStatus::Ok && allDigits(buf))
            copyStr(info_.imei, sizeof(info_.imei), buf);
    }
    hal_.logf("[MODEM] model=%s fw=%s imei=%s", info_.model, info_.revision, info_.imei);
    return true;
}

// ================= SIM / mang =================

SimState A7680::waitSim(uint32_t timeoutMs) {
    char buf[48];
    SimState st = SimState::NotReady;
    uint32_t t0 = hal_.millis();
    for (;;) {
        AtStatus r = at_.query("+CPIN:", buf, sizeof(buf), 5000, "AT+CPIN?");
        if (r == AtStatus::Ok) {
            if (strstr(buf, "READY")) {
                st = SimState::Ready;
                break;
            }
            if (strstr(buf, "PIN") || strstr(buf, "PUK")) return SimState::Locked;
            st = SimState::NotReady;
        } else if (r == AtStatus::CmeError) {
            const char* e = at_.lastError();
            st = (strstr(e, "not inserted") || strstr(e, "SIM failure") || strstr(e, ": 10") ||
                  strstr(e, ": 13"))
                     ? SimState::Missing
                     : SimState::NotReady;
        }
        if (elapsedMs(hal_.millis(), t0) >= timeoutMs) return st;
        hal_.idle(1000);
    }
    simLost_ = false;
    if (at_.query("+ICCID:", buf, sizeof(buf), 2000, "AT+CICCID") == AtStatus::Ok)
        copyStr(info_.iccid, sizeof(info_.iccid), buf);
    if (at_.query("", buf, sizeof(buf), 2000, "AT+CIMI") == AtStatus::Ok && allDigits(buf))
        copyStr(info_.imsi, sizeof(info_.imsi), buf);
    return SimState::Ready;
}

bool A7680::updateSignal() {
    char buf[32];
    if (at_.query("+CSQ:", buf, sizeof(buf), 2000, "AT+CSQ") != AtStatus::Ok) return false;
    int rssi = parseIntField(buf, 0, 99);
    info_.csq = rssi;
    info_.rssiDbm = (rssi >= 0 && rssi <= 31) ? (-113 + 2 * rssi) : 0;
    return true;
}

int A7680::updateRegistration() {
    static const char* const kCmds[3] = {"AT+CEREG?", "AT+CGREG?", "AT+CREG?"};
    static const char* const kPfx[3] = {"+CEREG:", "+CGREG:", "+CREG:"};
    char buf[64];
    int best = 0;
    for (int i = 0; i < 3; i++) {
        if (at_.query(kPfx[i], buf, sizeof(buf), 2000, "%s", kCmds[i]) != AtStatus::Ok) continue;
        int stat = parseIntField(buf, 1, 0);
        if (stat == 1 || stat == 5) {
            best = stat;
            break;
        }
        if (stat > best) best = stat;
    }
    info_.regStat = best;
    return best;
}

void A7680::updateOperator() {
    char buf[64];
    if (at_.query("+COPS:", buf, sizeof(buf), 3000, "AT+COPS?") != AtStatus::Ok) return;
    const char* q1 = strchr(buf, '"');
    const char* q2 = q1 ? strchr(q1 + 1, '"') : nullptr;
    if (q1 && q2) {
        size_t n = (size_t)(q2 - q1 - 1);
        if (n >= sizeof(info_.oper)) n = sizeof(info_.oper) - 1;
        memcpy(info_.oper, q1 + 1, n);
        info_.oper[n] = 0;
    } else {
        info_.oper[0] = 0;
    }
}

void A7680::updateSysInfo() {
    char buf[128];
    if (at_.query("+CPSI:", buf, sizeof(buf), 3000, "AT+CPSI?") == AtStatus::Ok)
        copyStr(info_.sysinfo, sizeof(info_.sysinfo), buf);
}

bool A7680::updateClock() {
    char buf[40];
    if (at_.query("+CCLK:", buf, sizeof(buf), 2000, "AT+CCLK?") != AtStatus::Ok) return false;
    stripQuotes(buf);
    // "yy/MM/dd,hh:mm:ss+zz" - gio mac dinh khi chua dong bo la 70/80
    int yy = atoi(buf);
    if (strlen(buf) < 17 || yy < 24 || yy >= 70) {
        info_.clock[0] = 0;
        return false;
    }
    copyStr(info_.clock, sizeof(info_.clock), buf);
    return true;
}

bool A7680::ensureApn(const char* apn, bool& changed) {
    changed = false;
    if (!apn || !apn[0]) return true;  // de mang tu cap APN mac dinh
    if (!safeQuoted(apn)) return false;
    char buf[96];
    char want[80];
    snprintf(want, sizeof(want), "\"%s\"", apn);
    if (at_.query("+CGDCONT: 1,", buf, sizeof(buf), 3000, "AT+CGDCONT?") == AtStatus::Ok && strstr(buf, want))
        return true;
    if (at_.cmd(5000, "AT+CGDCONT=1,\"IP\",\"%s\"", apn) != AtStatus::Ok) return false;
    changed = true;
    return true;
}

bool A7680::attachPdp(uint32_t timeoutMs) {
    char buf[64];
    if (at_.query("+CGATT:", buf, sizeof(buf), 3000, "AT+CGATT?") == AtStatus::Ok && atoi(buf) != 1) {
        if (at_.cmd(timeoutMs < 75000 ? timeoutMs : 75000, "AT+CGATT=1") != AtStatus::Ok) return false;
    }
    for (int attempt = 0; attempt < 2; attempt++) {
        if (at_.query("+CGPADDR: 1,", buf, sizeof(buf), 3000, "AT+CGPADDR=1") == AtStatus::Ok) {
            stripQuotes(buf);
            if (validIp(buf)) {
                copyStr(info_.ip, sizeof(info_.ip), buf);
                pdpLost_ = false;
                return true;
            }
        }
        if (attempt == 0 && at_.cmd(timeoutMs < 150000 ? timeoutMs : 150000, "AT+CGACT=1,1") != AtStatus::Ok)
            return false;
    }
    info_.ip[0] = 0;
    return false;
}

bool A7680::detachPdp() {
    mqttConnected_ = false;
    info_.ip[0] = 0;
    return at_.cmd(40000, "AT+CGACT=0,1") == AtStatus::Ok;
}

bool A7680::radioReset() {
    clearSessionState();
    info_.ip[0] = 0;
    info_.regStat = 0;
    AtStatus a = at_.cmd(15000, "AT+CFUN=0");
    hal_.idle(2000);
    AtStatus b = at_.cmd(15000, "AT+CFUN=1");
    return a == AtStatus::Ok && b == AtStatus::Ok;
}

// ================= Chung chi =================

bool A7680::ensureCaCert(const char* name, const char* pem) {
    if (!name || !pem || !pem[0] || !safeQuoted(name)) return false;
    certQueryName_ = name;
    certFound_ = false;
    at_.cmd(5000, "AT+CCERTLIST");
    certQueryName_ = nullptr;
    if (certFound_) return true;
    size_t len = strlen(pem);
    hal_.logf("[MODEM] nap chung chi CA \"%s\" (%u byte)", name, (unsigned)len);
    return at_.cmdData((const uint8_t*)pem, len, nullptr, nullptr, 0, 10000, "AT+CCERTDOWN=\"%s\",%u", name,
                       (unsigned)len) == AtStatus::Ok;
}

// ================= HTTP =================

int A7680::httpGet(const char* url, int& bodyLen) {
    bodyLen = 0;
    if (!safeQuoted(url)) return -1;
    at_.cmd(3000, "AT+HTTPTERM");
    if (at_.cmd(5000, "AT+HTTPINIT") != AtStatus::Ok) return -2;
    int status = -3;
    char buf[48];
    if (at_.cmd(3000, "AT+HTTPPARA=\"URL\",\"%s\"", url) == AtStatus::Ok &&
        at_.cmdUrc("+HTTPACTION:", buf, sizeof(buf), 90000, "AT+HTTPACTION=0") == AtStatus::Ok) {
        status = parseIntField(buf, 1, -4);
        bodyLen = parseIntField(buf, 2, 0);
    }
    at_.cmd(3000, "AT+HTTPTERM");
    return status;
}

// ================= MQTT =================

void A7680::mqttCleanup() {
    char buf[24];
    at_.cmdUrc("+CMQTTDISC:", buf, sizeof(buf), 12000, "AT+CMQTTDISC=0,10");
    at_.cmd(3000, "AT+CMQTTREL=0");
    at_.cmdUrc("+CMQTTSTOP:", buf, sizeof(buf), 12000, "AT+CMQTTSTOP");
    mqttConnected_ = false;
    rxActive_ = false;
}

bool A7680::mqttConnect(const MqttSettings& s) {
    const char* user = (s.user && s.user[0]) ? s.user : nullptr;
    if (!safeQuoted(s.host) || !safeQuoted(s.clientId) || (user && (!safeQuoted(user) || !safeQuoted(s.pass)))) {
        hal_.logf("[MQTT] cau hinh chua ky tu khong hop le (dau \" hoac ky tu dieu khien)");
        return false;
    }
    lastMqttError_ = 0;
    mqttCleanup();
    mqttLost_ = false;

    char buf[32];
    AtStatus st = at_.cmdUrc("+CMQTTSTART:", buf, sizeof(buf), 12000, "AT+CMQTTSTART");
    if (st == AtStatus::Ok && atoi(buf) != 0) {
        hal_.logf("[MQTT] CMQTTSTART loi %s", buf);
        return false;
    }
    // ERROR o day thuong la "da start roi" -> van thu ACCQ
    if (at_.cmd(5000, "AT+CMQTTACCQ=0,\"%s\",%d", s.clientId, s.tls ? 1 : 0) != AtStatus::Ok) {
        hal_.logf("[MQTT] ACCQ loi: %s", at_.lastError());
        return false;
    }
    if (s.tls) {
        at_.cmd(2000, "AT+CSSLCFG=\"sslversion\",0,4");
        bool verify = s.caCertName && s.caCertName[0] && safeQuoted(s.caCertName);
        at_.cmd(2000, "AT+CSSLCFG=\"authmode\",0,%d", verify ? 1 : 0);
        if (verify) at_.cmd(2000, "AT+CSSLCFG=\"cacert\",0,\"%s\"", s.caCertName);
        at_.cmd(2000, "AT+CSSLCFG=\"enableSNI\",0,1");  // EMQX Cloud can SNI
        if (at_.cmd(3000, "AT+CMQTTSSLCFG=0,0") != AtStatus::Ok) {
            hal_.logf("[MQTT] CMQTTSSLCFG loi: %s", at_.lastError());
            return false;
        }
    }
    if (s.willTopic && s.willTopic[0] && s.willMsg) {
        size_t tl = strlen(s.willTopic), ml = strlen(s.willMsg);
        if (at_.cmdData((const uint8_t*)s.willTopic, tl, nullptr, nullptr, 0, 5000, "AT+CMQTTWILLTOPIC=0,%u",
                        (unsigned)tl) != AtStatus::Ok ||
            at_.cmdData((const uint8_t*)s.willMsg, ml, nullptr, nullptr, 0, 5000, "AT+CMQTTWILLMSG=0,%u,1",
                        (unsigned)ml) != AtStatus::Ok) {
            hal_.logf("[MQTT] dat LWT loi: %s", at_.lastError());
            return false;
        }
    }

    // Khong in mat khau ra log khi bat trace
    bool tr = at_.trace();
    if (tr) {
        at_.setTrace(false);
        hal_.logf(">> AT+CMQTTCONNECT=0,\"tcp://%s:%u\",%u,1%s", s.host, s.port, s.keepAliveS,
                  user ? ",\"***\",\"***\"" : "");
    }
    if (user) {
        st = at_.cmdUrc("+CMQTTCONNECT:", buf, sizeof(buf), 60000, "AT+CMQTTCONNECT=0,\"tcp://%s:%u\",%u,1,\"%s\",\"%s\"",
                        s.host, s.port, s.keepAliveS, user, s.pass);
    } else {
        st = at_.cmdUrc("+CMQTTCONNECT:", buf, sizeof(buf), 60000, "AT+CMQTTCONNECT=0,\"tcp://%s:%u\",%u,1",
                        s.host, s.port, s.keepAliveS);
    }
    at_.setTrace(tr);
    int err = parseIntField(buf, 1, -1);
    lastMqttError_ = err;
    if (st != AtStatus::Ok || err != 0) {
        hal_.logf("[MQTT] ket noi that bai: %s, ma=%d (%s)", atStatusName(st), err, buf[0] ? buf : at_.lastError());
        return false;
    }
    mqttConnected_ = true;
    mqttLost_ = false;
    return true;
}

bool A7680::mqttSubscribe(const char* topic, int qos) {
    size_t tl = strlen(topic);
    if (!tl || tl > 1024) return false;
    char buf[24];
    AtStatus st = at_.cmdData((const uint8_t*)topic, tl, "+CMQTTSUB:", buf, sizeof(buf), 20000, "AT+CMQTTSUB=0,%u,%d",
                              (unsigned)tl, qos);
    return st == AtStatus::Ok && parseIntField(buf, 1, -1) == 0;
}

bool A7680::mqttPublish(const char* topic, const uint8_t* payload, size_t len, int qos, bool retain) {
    size_t tl = strlen(topic);
    if (!mqttConnected_ || !tl || tl > 1024 || !len || len > 10240) return false;
    if (at_.cmdData((const uint8_t*)topic, tl, nullptr, nullptr, 0, 5000, "AT+CMQTTTOPIC=0,%u", (unsigned)tl) !=
        AtStatus::Ok)
        return false;
    if (at_.cmdData(payload, len, nullptr, nullptr, 0, 5000, "AT+CMQTTPAYLOAD=0,%u", (unsigned)len) != AtStatus::Ok)
        return false;
    char buf[24];
    AtStatus st = at_.cmdUrc("+CMQTTPUB:", buf, sizeof(buf), 25000, "AT+CMQTTPUB=0,%d,20%s", qos, retain ? ",1" : "");
    int err = parseIntField(buf, 1, -1);
    if (st == AtStatus::Ok && err == 0) return true;
    hal_.logf("[MQTT] publish loi: %s ma=%d", atStatusName(st), err);
    return false;
}

void A7680::mqttDisconnect() { mqttCleanup(); }

// ================= URC =================

bool A7680::takeRebooted() { bool v = rebooted_; rebooted_ = false; return v; }
bool A7680::takePowerDown() { bool v = powerDown_; powerDown_ = false; return v; }
bool A7680::takeMqttLost() { bool v = mqttLost_; mqttLost_ = false; return v; }
bool A7680::takeSimLost() { bool v = simLost_; simLost_ = false; return v; }
bool A7680::takePdpLost() { bool v = pdpLost_; pdpLost_ = false; return v; }

size_t A7680::onLine(const char* s) {
    if (strcmp(s, "RDY") == 0 || startsWith(s, "*ATREADY")) {
        rebooted_ = true;
        clearSessionState();
        return 0;
    }
    if (strstr(s, "POWER DOWN")) {
        powerDown_ = true;
        clearSessionState();
        return 0;
    }
    if (startsWith(s, "+CPIN: NOT") || startsWith(s, "+CPIN: SIM REMOVED") || startsWith(s, "+SIMCARD: NOT")) {
        simLost_ = true;
        return 0;
    }
    if (startsWith(s, "+CMQTTCONNLOST:") || startsWith(s, "+CMQTTNONET")) {
        mqttLost_ = true;
        mqttConnected_ = false;
        return 0;
    }
    if (startsWith(s, "+CGEV:") && (strstr(s, "DEACT") || strstr(s, "DETACH"))) {
        pdpLost_ = true;
        return 0;
    }
    if (certQueryName_ && startsWith(s, "+CCERTLIST:") && strstr(s, certQueryName_)) {
        certFound_ = true;
        return 0;
    }
    if (startsWith(s, "+CMQTTRXSTART:")) {
        rxActive_ = true;
        memset(&rx_, 0, sizeof(rx_));
        return 0;
    }
    if (rxActive_ && startsWith(s, "+CMQTTRXTOPIC:")) {
        int n = parseIntField(s + 14, 1, 0);
        rawTarget_ = RawTopic;
        return n > 0 ? (size_t)n : 0;
    }
    if (rxActive_ && startsWith(s, "+CMQTTRXPAYLOAD:")) {
        int n = parseIntField(s + 16, 1, 0);
        rawTarget_ = RawPayload;
        return n > 0 ? (size_t)n : 0;
    }
    if (startsWith(s, "+CMQTTRXEND:")) {
        if (rxActive_) {
            if (!msgReady_) {
                msg_ = rx_;
                msgReady_ = true;
            } else {
                msgDropped_++;
            }
        }
        rxActive_ = false;
        rawTarget_ = RawNone;
        return 0;
    }
    return 0;
}

void A7680::onRaw(const uint8_t* data, size_t stored, size_t total) {
    if (!rxActive_) return;
    if (rawTarget_ == RawTopic) {
        size_t cur = strlen(rx_.topic);
        size_t room = sizeof(rx_.topic) - 1 - cur;
        size_t n = stored < room ? stored : room;
        memcpy(rx_.topic + cur, data, n);
        rx_.topic[cur + n] = 0;
        if (n < total) rx_.truncated = true;
    } else if (rawTarget_ == RawPayload) {
        size_t room = sizeof(rx_.payload) - 1 - rx_.len;
        size_t n = stored < room ? stored : room;
        memcpy(rx_.payload + rx_.len, data, n);
        rx_.len += n;
        rx_.payload[rx_.len] = 0;
        if (n < total) rx_.truncated = true;
    }
    rawTarget_ = RawNone;
}
