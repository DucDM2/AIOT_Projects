#include "KhoLanhData.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ================= TimeKeeper =================

int64_t TimeKeeper::daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

bool TimeKeeper::setFromCclk(const char* s, uint32_t nowMs) {
    if (!s) return false;
    if (*s == '"') s++;
    int yy, mo, dd, hh, mi, ss, tz = 0;
    char sign = '+';
    int n = sscanf(s, "%d/%d/%d,%d:%d:%d%c%d", &yy, &mo, &dd, &hh, &mi, &ss, &sign, &tz);
    if (n < 6) return false;
    if (yy < 24 || yy >= 70 || mo < 1 || mo > 12 || dd < 1 || dd > 31 || hh > 23 || mi > 59 || ss > 60) return false;
    if (n < 8 || (sign != '+' && sign != '-')) tz = 0;
    if (tz > 56) return false;  // mui gio toi da +14h = 56 quy
    int64_t days = daysFromCivil(2000 + yy, (unsigned)mo, (unsigned)dd);
    int64_t sec = days * 86400 + hh * 3600 + mi * 60 + ss;
    sec -= (sign == '-' ? -1 : 1) * (int64_t)tz * 900;  // gio dia phuong -> UTC
    baseEpochMs_ = sec * 1000;
    baseMs_ = nowMs;
    valid_ = true;
    return true;
}

void TimeKeeper::formatIso(int64_t epochMs, char* out, size_t cap) {
    int64_t sec = epochMs / 1000;
    int ms = (int)(epochMs % 1000);
    if (ms < 0) {
        ms += 1000;
        sec--;
    }
    int64_t days = sec / 86400;
    int64_t rem = sec % 86400;
    if (rem < 0) {
        rem += 86400;
        days--;
    }
    // civil_from_days
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned doe = (unsigned)(days - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = (int64_t)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    y += (m <= 2);
    snprintf(out, cap, "%04d-%02u-%02uT%02d:%02d:%02d.%03dZ", (int)y, m, d, (int)(rem / 3600), (int)(rem % 3600 / 60),
             (int)(rem % 60), ms);
}

// ================= SensorSim =================

uint32_t SensorSim::rnd() {
    // xorshift32
    uint32_t x = state_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state_ = x;
    return x;
}

void SensorSim::begin(const SimDevice* devs, int count, uint32_t periodMs, uint32_t nowMs, uint32_t seed) {
    devs_ = devs;
    count_ = count > kMaxDevices ? kMaxDevices : count;
    periodMs_ = periodMs ? periodMs : 300000;
    state_ = seed ? seed : 0x9e3779b9u;
    for (int i = 0; i < count_; i++) {
        next_[i] = nowMs + (uint32_t)i * 2000;  // gui ngay khi bat, cach nhau 2 s
        fcnt_[i] = rnd() % 50000;               // tranh trung fCnt sau khi khoi dong lai
        forced_[i] = INT16_MIN;
        door_[i] = devs_[i].type == SENSOR_DOOR ? 0 : -1;
        doorChangeAt_[i] = 0;
    }
    lastDoorCheck_ = nowMs;
}

int SensorSim::find(const char* s) const {
    if (!s || !*s) return -1;
    size_t ls = strlen(s);
    for (int i = 0; i < count_; i++) {
        const char* id = devs_[i].id;
        size_t li = strlen(id);
        if (strcasecmp(id, s) == 0) return i;
        if (li > ls && id[li - ls - 1] == '-' && strcasecmp(id + li - ls, s) == 0) return i;
    }
    return -1;
}

bool SensorSim::force(const char* s, int16_t temp10) {
    int i = find(s);
    if (i < 0 || devs_[i].type != SENSOR_TEMP_HUM) return false;
    forced_[i] = temp10;
    next_[i] = lastDoorCheck_;  // gui ngay o lan poll sau
    return true;
}

void SensorSim::make(int i, uint32_t nowMs, const TimeKeeper& tk, Reading& r) {
    const SimDevice& d = devs_[i];
    memset(&r, 0, sizeof(r));
    r.dev = (uint8_t)i;
    r.type = d.type;
    r.battery = d.battery;
    r.door = -1;
    r.stampMs = nowMs;
    r.fcnt = ++fcnt_[i];
    r.rssi = (int16_t)(-60 - (int)(rnd() % 46));
    r.snr10 = (int8_t)((int)(rnd() % 121) - 20);
    if (d.type == SENSOR_DOOR) {
        r.door = door_[i];
        return;
    }
    double hours = tk.valid() ? (double)(tk.epochAt(nowMs) / 1000) / 3600.0 + 7.0 : nowMs / 3600000.0;
    double gio = fmod(hours, 24.0);
    double chuKy = sin((gio - 9.0) / 24.0 * 2.0 * M_PI);  // cao nhat ~15h
    double t = d.tempBase + chuKy * d.amp + (frand() - 0.5) * 0.8;
    double h = d.humBase + chuKy * 3.0 + (frand() - 0.5) * 4.0;
    if (h < 0) h = 0;
    if (h > 100) h = 100;
    r.temp10 = forced_[i] != INT16_MIN ? forced_[i] : (int16_t)lround(t * 10.0);
    r.hum = (uint8_t)lround(h);
}

bool SensorSim::poll(uint32_t nowMs, const TimeKeeper& tk, Reading& out) {
    if (!enabled_ || !devs_) {
        lastDoorCheck_ = nowMs;
        return false;
    }
    // Mo/dong cua ngau nhien: trung binh ~45 phut mo 1 lan, mo 20 s - 4 phut (10% mo lau 6-12 phut)
    uint32_t secs = (nowMs - lastDoorCheck_) / 1000;
    if (secs > 0) {
        if (secs > 10) secs = 10;
        lastDoorCheck_ += secs * 1000;
        if ((int32_t)(nowMs - lastDoorCheck_) > 10000) lastDoorCheck_ = nowMs;
        for (int i = 0; i < count_; i++) {
            if (devs_[i].type != SENSOR_DOOR) continue;
            if (door_[i] == 1) {
                if ((int32_t)(nowMs - doorChangeAt_[i]) >= 0) {
                    door_[i] = 0;
                    next_[i] = nowMs;
                }
            } else {
                for (uint32_t k = 0; k < secs; k++) {
                    if (rnd() % 2700 == 0) {
                        uint32_t dur = (rnd() % 10 == 0) ? (360 + rnd() % 361) : (20 + rnd() % 221);
                        door_[i] = 1;
                        doorChangeAt_[i] = nowMs + dur * 1000;
                        next_[i] = nowMs;
                        break;
                    }
                }
            }
        }
    }
    for (int i = 0; i < count_; i++) {
        if ((int32_t)(nowMs - next_[i]) >= 0) {
            make(i, nowMs, tk, out);
            next_[i] = nowMs + periodMs_;
            return true;
        }
    }
    return false;
}

// ================= Uplink =================

namespace Uplink {

size_t encodeTlv(const Reading& r, uint8_t* out, size_t cap) {
    size_t n = 0;
    if (cap < 10) return 0;
    out[n++] = 0x01;
    out[n++] = 0x75;
    out[n++] = r.battery;
    if (r.type == SENSOR_DOOR) {
        out[n++] = 0x03;
        out[n++] = 0x00;
        out[n++] = r.door == 1 ? 1 : 0;
    } else {
        uint16_t t = (uint16_t)r.temp10;
        out[n++] = 0x03;
        out[n++] = 0x67;
        out[n++] = (uint8_t)(t & 0xff);
        out[n++] = (uint8_t)(t >> 8);
        out[n++] = 0x04;
        out[n++] = 0x68;
        out[n++] = (uint8_t)(r.hum * 2);
    }
    return n;
}

size_t base64(const uint8_t* in, size_t len, char* out, size_t cap) {
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t need = (len + 2) / 3 * 4;
    if (cap < need + 1) return 0;
    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < len) v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < len) v |= in[i + 2];
        out[o++] = tbl[(v >> 18) & 63];
        out[o++] = tbl[(v >> 12) & 63];
        out[o++] = i + 1 < len ? tbl[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < len ? tbl[v & 63] : '=';
    }
    out[o] = 0;
    return o;
}

void toLower(const char* in, char* out, size_t cap) {
    size_t i = 0;
    for (; in[i] && i + 1 < cap; i++) out[i] = (char)tolower((unsigned char)in[i]);
    out[i] = 0;
}

static bool validEui(const char* s) {
    size_t n = strlen(s);
    if (n == 0 || n > 32) return false;
    for (; *s; s++)
        if (!(isalnum((unsigned char)*s) || *s == '-' || *s == '_')) return false;
    return true;
}

bool topic(const char* devEui, char* out, size_t cap) {
    if (!validEui(devEui)) return false;
    char low[33];
    toLower(devEui, low, sizeof(low));
    int n = snprintf(out, cap, "kholanh/%s/up", low);
    return n > 0 && (size_t)n < cap;
}

static void fmt10(char* out, size_t cap, int v) {
    snprintf(out, cap, "%s%d.%d", v < 0 ? "-" : "", abs(v) / 10, abs(v) % 10);
}

size_t buildJson(const Reading& r, const SimDevice& dev, const char* gatewayId, const TimeKeeper& tk, char* out,
                 size_t cap) {
    uint8_t tlv[16];
    char b64[32];
    size_t tl = encodeTlv(r, tlv, sizeof(tlv));
    if (!tl || !base64(tlv, tl, b64, sizeof(b64))) return 0;

    char eui[33], gw[33], timeField[48] = "", obj[96], snr[12];
    toLower(dev.id, eui, sizeof(eui));
    toLower(gatewayId, gw, sizeof(gw));
    if (tk.valid()) {
        char iso[32];
        TimeKeeper::formatIso(tk.epochAt(r.stampMs), iso, sizeof(iso));
        snprintf(timeField, sizeof(timeField), "\"time\":\"%s\",", iso);
    }
    if (r.type == SENSOR_DOOR) {
        snprintf(obj, sizeof(obj), "{\"magnet_status\":\"%s\",\"battery\":%u}", r.door == 1 ? "open" : "close",
                 r.battery);
    } else {
        char t[12];
        fmt10(t, sizeof(t), r.temp10);
        snprintf(obj, sizeof(obj), "{\"temperature\":%s,\"humidity\":%u,\"battery\":%u}", t, r.hum, r.battery);
    }
    fmt10(snr, sizeof(snr), r.snr10);
    int n = snprintf(out, cap,
                     "{\"applicationID\":\"1\",\"applicationName\":\"kho-lanh\",\"deviceName\":\"%s\","
                     "\"devEUI\":\"%s\",\"fPort\":%d,\"fCnt\":%lu,%s\"data\":\"%s\",\"object\":%s,"
                     "\"rxInfo\":[{\"mac\":\"%s\",\"rssi\":%d,\"loRaSNR\":%s}],"
                     "\"txInfo\":{\"frequency\":923200000,\"dr\":2}}",
                     dev.id, eui, r.type == SENSOR_DOOR ? 85 : 5, (unsigned long)r.fcnt, timeField, b64, obj, gw,
                     r.rssi, snr);
    if (n <= 0 || (size_t)n >= cap) return 0;
    return (size_t)n;
}

bool parseRaw(const char* json, char* devEui, size_t cap) {
    while (*json == ' ') json++;
    size_t n = strlen(json);
    while (n && (json[n - 1] == ' ' || json[n - 1] == '\r' || json[n - 1] == '\n')) n--;
    if (n < 2 || json[0] != '{' || json[n - 1] != '}') return false;
    int depth = 0;
    bool inStr = false;
    for (size_t i = 0; i < n; i++) {
        char c = json[i];
        if ((unsigned char)c < 0x20) return false;
        if (inStr) {
            if (c == '\\') i++;
            else if (c == '"') inStr = false;
        } else if (c == '"') {
            inStr = true;
        } else if (c == '{' || c == '[') {
            depth++;
        } else if (c == '}' || c == ']') {
            if (--depth < 0) return false;
        }
    }
    if (depth != 0 || inStr) return false;
    const char* k = strstr(json, "\"devEUI\"");
    if (!k) return false;
    k = strchr(k + 8, ':');
    if (!k) return false;
    k++;
    while (*k == ' ') k++;
    if (*k != '"') return false;
    k++;
    const char* e = strchr(k, '"');
    if (!e || (size_t)(e - k) >= cap) return false;
    memcpy(devEui, k, (size_t)(e - k));
    devEui[e - k] = 0;
    return validEui(devEui);
}

}  // namespace Uplink
