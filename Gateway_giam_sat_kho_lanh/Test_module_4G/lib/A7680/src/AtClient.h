// Bo phan tich lenh AT khong dung cap phat dong (khong String, khong malloc)
// - Moi lenh co timeout, moi vong cho deu goi hal.idle() (an watchdog)
// - Xu ly URC xen giua phan hoi, echo, dau nhac '>', doc du lieu nhi phan theo do dai
// - Dong qua dai bi cat, khong tran bo dem
#pragma once
#include "ModemHal.h"

enum class AtStatus : uint8_t { Ok = 0, Error, CmeError, Timeout, BadArg };
const char* atStatusName(AtStatus s);

class AtUrcListener {
public:
    virtual ~AtUrcListener() {}
    // Goi cho MOI dong khong phai ket qua cuoi (OK/ERROR). Tra ve >0 neu
    // muon nhan tiep chinh xac N byte du lieu tho (vd payload MQTT).
    virtual size_t onLine(const char* line) = 0;
    virtual void onRaw(const uint8_t* data, size_t stored, size_t total) = 0;
};

class AtClient {
public:
    static const size_t kLineMax = 256;
    static const size_t kCmdMax = 384;
    static const size_t kRawMax = 512;
    static const size_t kRawLimit = 10240;        // payload MQTT toi da cua A76xx
    static const uint32_t kRawIdleAbortMs = 2000;  // dang doc tho ma im lang -> huy

    explicit AtClient(ModemHal& hal);

    void setListener(AtUrcListener* l) { listener_ = l; }
    void setTrace(bool on) { trace_ = on; }
    bool trace() const { return trace_; }

    // Xu ly du lieu dang cho (URC) khi khong co lenh nao dang chay
    void poll();
    // Xoa trang thai bo phan tich (sau khi reset/cap nguon lai modem)
    void resetParser();

    // Gui lenh, cho OK/ERROR
    AtStatus cmd(uint32_t timeoutMs, const char* fmt, ...)
        __attribute__((format(printf, 3, 4)));
    // Gui lenh, lay dong dau tien bat dau bang prefix (da bo prefix) vao out
    AtStatus query(const char* prefix, char* out, size_t cap, uint32_t timeoutMs, const char* fmt, ...)
        __attribute__((format(printf, 6, 7)));
    // Gui lenh, cho OK VA mot dong URC bat dau bang urcPrefix (thu tu bat ky)
    AtStatus cmdUrc(const char* urcPrefix, char* out, size_t cap, uint32_t timeoutMs, const char* fmt, ...)
        __attribute__((format(printf, 6, 7)));
    // Gui lenh, cho dau nhac '>', gui data, cho OK (+ URC neu urcPrefix != nullptr)
    AtStatus cmdData(const uint8_t* data, size_t len, const char* urcPrefix, char* out, size_t cap,
                     uint32_t timeoutMs, const char* fmt, ...)
        __attribute__((format(printf, 8, 9)));
    // Chi cho mot dong bat dau bang prefix (khong gui gi)
    AtStatus waitLine(const char* prefix, char* out, size_t cap, uint32_t timeoutMs);
    // Gui lenh va in moi dong nhan duoc ra log (dung cho console debug)
    AtStatus passthrough(const char* command, uint32_t timeoutMs);

    const char* lastError() const { return lastError_; }
    uint8_t consecutiveTimeouts() const { return consecTimeouts_; }
    void clearTimeouts() { consecTimeouts_ = 0; }

    struct Stats {
        uint32_t commands, timeouts, errors, lineOverflows, rawAborts, rawRejected, strayFinals;
    };
    const Stats& stats() const { return stats_; }

private:
    struct Req {
        const char* capPrefix = nullptr;
        char* capOut = nullptr;
        size_t capCap = 0;
        bool capGot = false;
        const char* urcPrefix = nullptr;
        char* urcOut = nullptr;
        size_t urcCap = 0;
        bool urcGot = false;
        bool lineOnly = false;  // waitLine: khong can OK
        bool wantPrompt = false;
        bool promptSeen = false;
        bool echoAll = false;   // passthrough
        bool final = false;
        AtStatus status = AtStatus::Timeout;
    };

    AtStatus exec(Req& rq, const uint8_t* data, size_t len, uint32_t timeoutMs, const char* fmt, va_list ap);
    AtStatus execf(Req& rq, uint32_t timeoutMs, const char* fmt, ...) __attribute__((format(printf, 4, 5)));
    AtStatus run(Req& rq, const uint8_t* data, size_t len, uint32_t timeoutMs);
    bool sendCommand(const char* fmt, va_list ap);
    void pump(Req* rq);
    void feed(uint8_t c);
    void handleLine();
    bool done(const Req& rq) const;
    void checkRawTimeout();
    static bool startsWith(const char* s, const char* p);
    static void copyAfter(char* out, size_t cap, const char* s, size_t skip);

    ModemHal& hal_;
    AtUrcListener* listener_ = nullptr;
    Req* req_ = nullptr;
    bool trace_ = false;

    char line_[kLineMax];
    size_t lineLen_ = 0;
    bool lineOverflow_ = false;

    uint8_t raw_[kRawMax];
    size_t rawStored_ = 0;
    size_t rawTotal_ = 0;
    size_t rawRemaining_ = 0;
    uint32_t rawLastMs_ = 0;

    char cmdBuf_[kCmdMax];
    char lastError_[64];
    uint8_t consecTimeouts_ = 0;
    Stats stats_ = {};
};
