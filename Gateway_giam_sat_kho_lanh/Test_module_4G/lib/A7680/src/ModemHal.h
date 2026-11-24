// Lop truu tuong phan cung (HAL) cho driver A7680C.
// Driver KHONG phu thuoc Arduino -> cung mot ma nguon chay tren ESP32 va
// tren PC (mo phong modem gia lap trong test/sim).
#pragma once
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

class ModemHal {
public:
    virtual ~ModemHal() {}

    // ---- UART toi modem ----
    virtual int available() = 0;
    virtual int read() = 0;  // -1 neu khong co du lieu
    virtual size_t write(const uint8_t* data, size_t len) = 0;

    // ---- Thoi gian ----
    virtual uint32_t millis() = 0;
    // Nghi ~ms mili giay. BAT BUOC cho an watchdog ben trong.
    // Moi vong cho trong driver deu goi ham nay -> khong bao gio treo cung.
    virtual void idle(uint32_t ms) = 0;

    // ---- Chan dieu khien modem ----
    // asserted=true: "nhan" PWRKEY (chan PWRKEY cua module bi keo xuong muc thap)
    virtual void setPwrKey(bool asserted) = 0;
    virtual bool hasResetPin() { return false; }
    virtual void setResetPin(bool asserted) { (void)asserted; }
    // Cong tac nguon (MOSFET cat VBAT) - cach phuc hoi manh nhat khi modem treo cung
    virtual bool hasPowerSwitch() { return false; }
    virtual void setPowerSwitch(bool on) { (void)on; }

    // ---- He thong ----
    virtual void log(const char* line) = 0;
    virtual void systemRestart() = 0;
    virtual uint32_t freeHeap() { return 0; }
    virtual uint32_t minFreeHeap() { return 0; }
    virtual const char* resetReason() { return "?"; }
    virtual uint32_t random32() { return 0x12345678u; }

    void logf(const char* fmt, ...)
#if defined(__GNUC__)
        __attribute__((format(printf, 2, 3)))
#endif
    {
        char buf[300];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        log(buf);
    }
};

// So sanh thoi gian an toan khi millis() tran so (sau ~49.7 ngay)
inline uint32_t elapsedMs(uint32_t now, uint32_t since) { return (uint32_t)(now - since); }
