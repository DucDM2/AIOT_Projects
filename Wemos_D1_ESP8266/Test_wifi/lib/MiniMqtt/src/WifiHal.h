// Lop truu tuong phan cung cho gateway WiFi: ESP32 that (src/main.cpp) hoac the gioi gia lap tren PC
// (test/sim). Nho vay may trang thai gateway + client MQTT chay y het tren ca hai.
#pragma once
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// So sanh thoi gian bang phep tru khong dau -> dung ca khi millis() tran so (49,7 ngay)
inline uint32_t elapsedMs(uint32_t now, uint32_t since) { return (uint32_t)(now - since); }

// Ket noi TCP/TLS toi broker. Moi ham KHONG duoc treo qua timeout.
class NetSocket {
public:
    virtual ~NetSocket() {}
    virtual bool open(const char* host, uint16_t port, bool tls, uint32_t timeoutMs) = 0;
    virtual bool isOpen() = 0;
    virtual int available() = 0;
    virtual int read(uint8_t* buf, size_t n) = 0;
    virtual size_t write(const uint8_t* buf, size_t n) = 0;
    virtual void close() = 0;
};

class WifiHal {
public:
    virtual ~WifiHal() {}
    virtual uint32_t millis() = 0;
    virtual void idle(uint32_t ms) = 0;  // cho + an watchdog
    virtual void log(const char* line) = 0;
    void logf(const char* fmt, ...)
#if defined(__GNUC__)
        __attribute__((format(printf, 2, 3)))
#endif
    {
        char b[256];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(b, sizeof(b), fmt, ap);
        va_end(ap);
        log(b);
    }

    // ---- WiFi (che do tram STA) ----
    virtual void wifiBegin(const char* ssid, const char* pass) = 0;  // bat dau ket noi, khong cho
    virtual bool wifiConnected() = 0;                                // da co IP
    virtual void wifiStop() = 0;                                     // tat han song WiFi (reset nang)
    virtual bool wifiSeen(const char* ssid) = 0;                     // quet: co thay mang nay khong (cham ~3 s)
    virtual int wifiRssi() = 0;
    virtual void wifiIp(char* out, size_t cap) = 0;
    virtual int wifiLastReason() = 0;  // ma ly do rot WiFi gan nhat (0 = khong ro)

    // ---- Gio UTC tu SNTP. false neu chua dong bo ----
    virtual bool epochMs(int64_t& out) = 0;

    // ---- Ket noi toi broker ----
    virtual NetSocket& socket() = 0;

    // ---- He thong ----
    virtual void systemRestart() = 0;
    virtual uint32_t freeHeap() = 0;
    virtual uint32_t minFreeHeap() = 0;
    virtual uint32_t random32() = 0;
    virtual const char* resetReason() = 0;
    virtual const char* macAddress() = 0;
};
