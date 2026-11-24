#include "AtClient.h"

#include <string.h>

namespace {
// Gioi han so byte xu ly lien tuc trong 1 lan bom du lieu: neu modem phun rac
// lien tuc, van quay lai kiem tra timeout + an watchdog.
const int kMaxBytesPerPump = 512;
}  // namespace

const char* atStatusName(AtStatus s) {
    switch (s) {
        case AtStatus::Ok: return "OK";
        case AtStatus::Error: return "ERROR";
        case AtStatus::CmeError: return "CME_ERROR";
        case AtStatus::Timeout: return "TIMEOUT";
        case AtStatus::BadArg: return "BAD_ARG";
    }
    return "?";
}

AtClient::AtClient(ModemHal& hal) : hal_(hal) {
    line_[0] = 0;
    cmdBuf_[0] = 0;
    lastError_[0] = 0;
}

bool AtClient::startsWith(const char* s, const char* p) { return strncmp(s, p, strlen(p)) == 0; }

void AtClient::copyAfter(char* out, size_t cap, const char* s, size_t skip) {
    if (!out || cap == 0) return;
    s += skip;
    while (*s == ' ') s++;
    size_t n = strlen(s);
    if (n >= cap) n = cap - 1;
    memcpy(out, s, n);
    out[n] = 0;
}

void AtClient::resetParser() {
    lineLen_ = 0;
    lineOverflow_ = false;
    rawRemaining_ = 0;
    rawStored_ = 0;
    rawTotal_ = 0;
    consecTimeouts_ = 0;
}

void AtClient::checkRawTimeout() {
    if (rawRemaining_ > 0 && elapsedMs(hal_.millis(), rawLastMs_) > kRawIdleAbortMs) {
        hal_.logf("[AT] huy doc du lieu tho: thieu %u byte", (unsigned)rawRemaining_);
        stats_.rawAborts++;
        rawRemaining_ = 0;
        rawStored_ = 0;
    }
}

void AtClient::feed(uint8_t c) {
    if (rawRemaining_ > 0) {
        if (rawStored_ < kRawMax) raw_[rawStored_++] = c;
        rawRemaining_--;
        rawLastMs_ = hal_.millis();
        if (rawRemaining_ == 0 && listener_) listener_->onRaw(raw_, rawStored_, rawTotal_);
        return;
    }
    if (c == '\n') {
        handleLine();
        return;
    }
    if (c == '\r') return;
    if (c == '>' && lineLen_ == 0 && req_ && req_->wantPrompt && !req_->promptSeen) {
        req_->promptSeen = true;
        return;
    }
    if (c == 0) c = '?';
    if (lineLen_ < kLineMax - 1) {
        line_[lineLen_++] = (char)c;
    } else {
        lineOverflow_ = true;
    }
}

void AtClient::handleLine() {
    line_[lineLen_] = 0;
    bool overflow = lineOverflow_;
    lineLen_ = 0;
    lineOverflow_ = false;

    char* s = line_;
    while (*s == ' ' || *s == '\t') s++;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
    if (n == 0) return;

    if (overflow) {
        stats_.lineOverflows++;
        hal_.logf("[AT] dong qua dai (>%u) da bi cat", (unsigned)(kLineMax - 1));
    }
    if (trace_ || (req_ && req_->echoAll)) hal_.logf("<< %s", s);

    // Echo lenh (khi modem vua khoi dong lai, ATE mac dinh = 1)
    if (s[0] == 'A' && s[1] == 'T') return;

    Req* rq = (req_ && !req_->lineOnly && !req_->final) ? req_ : nullptr;
    if (strcmp(s, "OK") == 0) {
        if (rq) {
            rq->final = true;
            rq->status = AtStatus::Ok;
        } else {
            stats_.strayFinals++;
        }
        return;
    }
    bool isErr = strcmp(s, "ERROR") == 0;
    bool isCme = startsWith(s, "+CME ERROR") || startsWith(s, "+CMS ERROR");
    if (isErr || isCme) {
        copyAfter(lastError_, sizeof(lastError_), s, 0);
        if (rq) {
            rq->final = true;
            rq->status = isErr ? AtStatus::Error : AtStatus::CmeError;
        } else {
            stats_.strayFinals++;
        }
        return;
    }

    if (req_) {
        if (req_->capPrefix && !req_->capGot && startsWith(s, req_->capPrefix)) {
            copyAfter(req_->capOut, req_->capCap, s, strlen(req_->capPrefix));
            req_->capGot = true;
        } else if (req_->urcPrefix && !req_->urcGot && startsWith(s, req_->urcPrefix)) {
            copyAfter(req_->urcOut, req_->urcCap, s, strlen(req_->urcPrefix));
            req_->urcGot = true;
        }
    }

    // Moi dong deu chuyen cho listener (theo doi URC: mat ket noi, modem reboot...)
    if (listener_) {
        size_t want = listener_->onLine(s);
        if (want > 0) {
            if (want > kRawLimit) {
                stats_.rawRejected++;
                hal_.logf("[AT] tu choi doc %u byte (vuot gioi han)", (unsigned)want);
            } else {
                rawRemaining_ = want;
                rawTotal_ = want;
                rawStored_ = 0;
                rawLastMs_ = hal_.millis();
            }
        }
    }
}

bool AtClient::done(const Req& rq) const {
    if (rq.lineOnly) return rq.urcGot;
    if (!rq.final) return false;
    if (rq.status != AtStatus::Ok) return true;
    if (rq.urcPrefix && !rq.urcGot) return false;
    return true;
}

void AtClient::pump(Req* rq) {
    for (int i = 0; i < kMaxBytesPerPump; i++) {
        if (hal_.available() <= 0) break;
        int c = hal_.read();
        if (c < 0) break;
        feed((uint8_t)c);
        if (rq && (done(*rq) || (rq->wantPrompt && rq->promptSeen))) break;
    }
    checkRawTimeout();
}

void AtClient::poll() {
    if (req_) return;  // khong tai nhap
    for (int i = 0; i < 8 && hal_.available() > 0; i++) pump(nullptr);
}

bool AtClient::sendCommand(const char* fmt, va_list ap) {
    int n = vsnprintf(cmdBuf_, kCmdMax - 1, fmt, ap);
    if (n < 0 || (size_t)n >= kCmdMax - 2) {
        hal_.logf("[AT] lenh qua dai, bo qua");
        return false;
    }
    if (trace_ || (req_ && req_->echoAll)) hal_.logf(">> %s", cmdBuf_);
    cmdBuf_[n++] = '\r';
    cmdBuf_[n] = 0;
    hal_.write((const uint8_t*)cmdBuf_, (size_t)n);
    return true;
}

AtStatus AtClient::run(Req& rq, const uint8_t* data, size_t len, uint32_t timeoutMs) {
    bool dataSent = (data == nullptr);
    uint32_t start = hal_.millis();
    for (;;) {
        pump(&rq);
        if (!dataSent && rq.promptSeen && !rq.final) {
            if (len) hal_.write(data, len);
            dataSent = true;
            continue;
        }
        if (done(rq)) break;
        if (elapsedMs(hal_.millis(), start) >= timeoutMs) {
            rq.status = AtStatus::Timeout;
            break;
        }
        hal_.idle(2);
    }
    if (rq.status == AtStatus::Ok && !dataSent) rq.status = AtStatus::Error;  // OK ma khong co '>'
    return rq.status;
}

AtStatus AtClient::exec(Req& rq, const uint8_t* data, size_t len, uint32_t timeoutMs, const char* fmt,
                        va_list ap) {
    if (req_) return AtStatus::BadArg;  // khong cho goi long nhau (tu listener)
    poll();                             // xa cac URC/phan hoi cu truoc khi gui lenh moi
    if (rawRemaining_ > 0) {            // dang doc du lieu tho do -> doi xong (toi da 2s)
        uint32_t t0 = hal_.millis();
        while (rawRemaining_ > 0 && elapsedMs(hal_.millis(), t0) < kRawIdleAbortMs + 100) {
            hal_.idle(2);
            poll();
        }
    }
    req_ = &rq;
    stats_.commands++;
    if (!sendCommand(fmt, ap)) {
        req_ = nullptr;
        return AtStatus::BadArg;
    }
    AtStatus st = run(rq, data, len, timeoutMs);
    req_ = nullptr;

    if (st == AtStatus::Timeout) {
        stats_.timeouts++;
        // Chi tinh "modem khong tra loi" khi chua nhan duoc ca OK/ERROR
        if (!rq.final && consecTimeouts_ < 255) consecTimeouts_++;
        if (!rq.final) copyAfter(lastError_, sizeof(lastError_), "timeout", 0);
    } else {
        consecTimeouts_ = 0;
        if (st != AtStatus::Ok) stats_.errors++;
    }
    return st;
}

AtStatus AtClient::cmd(uint32_t timeoutMs, const char* fmt, ...) {
    Req rq;
    va_list ap;
    va_start(ap, fmt);
    AtStatus st = exec(rq, nullptr, 0, timeoutMs, fmt, ap);
    va_end(ap);
    return st;
}

AtStatus AtClient::query(const char* prefix, char* out, size_t cap, uint32_t timeoutMs, const char* fmt, ...) {
    if (out && cap) out[0] = 0;
    Req rq;
    rq.capPrefix = prefix;
    rq.capOut = out;
    rq.capCap = cap;
    va_list ap;
    va_start(ap, fmt);
    AtStatus st = exec(rq, nullptr, 0, timeoutMs, fmt, ap);
    va_end(ap);
    if (st == AtStatus::Ok && !rq.capGot) st = AtStatus::Error;  // OK nhung thieu du lieu
    return st;
}

AtStatus AtClient::cmdUrc(const char* urcPrefix, char* out, size_t cap, uint32_t timeoutMs, const char* fmt,
                          ...) {
    if (out && cap) out[0] = 0;
    Req rq;
    rq.urcPrefix = urcPrefix;
    rq.urcOut = out;
    rq.urcCap = cap;
    va_list ap;
    va_start(ap, fmt);
    AtStatus st = exec(rq, nullptr, 0, timeoutMs, fmt, ap);
    va_end(ap);
    return st;
}

AtStatus AtClient::cmdData(const uint8_t* data, size_t len, const char* urcPrefix, char* out, size_t cap,
                           uint32_t timeoutMs, const char* fmt, ...) {
    if (out && cap) out[0] = 0;
    Req rq;
    rq.wantPrompt = true;
    rq.urcPrefix = urcPrefix;
    rq.urcOut = out;
    rq.urcCap = cap;
    va_list ap;
    va_start(ap, fmt);
    AtStatus st = exec(rq, data ? data : (const uint8_t*)"", len, timeoutMs, fmt, ap);
    va_end(ap);
    return st;
}

AtStatus AtClient::waitLine(const char* prefix, char* out, size_t cap, uint32_t timeoutMs) {
    if (req_) return AtStatus::BadArg;
    if (out && cap) out[0] = 0;
    Req rq;
    rq.lineOnly = true;
    rq.urcPrefix = prefix;
    rq.urcOut = out;
    rq.urcCap = cap;
    req_ = &rq;
    AtStatus st = run(rq, nullptr, 0, timeoutMs);
    req_ = nullptr;
    return rq.urcGot ? AtStatus::Ok : st;
}

AtStatus AtClient::passthrough(const char* command, uint32_t timeoutMs) {
    Req rq;
    rq.echoAll = true;
    return execf(rq, timeoutMs, "%s", command);
}

AtStatus AtClient::execf(Req& rq, uint32_t timeoutMs, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    AtStatus st = exec(rq, nullptr, 0, timeoutMs, fmt, ap);
    va_end(ap);
    return st;
}
