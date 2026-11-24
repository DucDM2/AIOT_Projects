// Du lieu cam bien gia lap + dung ban tin JSON giong gateway Milesight UG65/UG67
// (dung dinh dang ma web app "giam sat kho lanh" dang nhan qua EMQX: topic kholanh/<devEUI>/up).
#pragma once
#include <stddef.h>
#include <stdint.h>

// ---------------- Dong ho (gio UTC lay tu SNTP qua WiFi) ----------------
class TimeKeeper {
public:
    // s = "yy/MM/dd,hh:mm:ss+zz" (AT+CCLK, zz = mui gio tinh theo 15 phut)
    bool setFromCclk(const char* s, uint32_t nowMs);
    // Gio UTC (ms tu 1970) dung tai thoi diem millis() = nowMs (lay tu SNTP)
    bool setFromEpoch(int64_t epochMs, uint32_t nowMs);
    bool valid() const { return valid_; }
    // Gio UTC (ms tu 1970) tai thoi diem millis() = stampMs. Dung duoc ca cho moc truoc khi dong bo.
    int64_t epochAt(uint32_t stampMs) const { return baseEpochMs_ + (int32_t)(stampMs - baseMs_); }
    static void formatIso(int64_t epochMs, char* out, size_t cap);  // 2026-10-10T01:02:03.456Z
    static int64_t daysFromCivil(int y, unsigned m, unsigned d);

private:
    bool valid_ = false;
    int64_t baseEpochMs_ = 0;
    uint32_t baseMs_ = 0;
};

// ---------------- Thiet bi gia lap ----------------
enum SensorType : uint8_t { SENSOR_TEMP_HUM = 0, SENSOR_DOOR = 1 };

struct SimDevice {
    const char* id;       // devEUI dung nhu tren app, vd "24E124-A01"
    uint8_t type;         // SensorType
    float tempBase;       // °C
    float humBase;        // %
    float amp;            // bien do dao dong ngay
    uint8_t battery;      // %
};

struct Reading {
    uint8_t dev;       // chi so trong bang thiet bi
    uint8_t type;
    int16_t temp10;    // 0.1 °C
    uint8_t hum;       // %
    uint8_t battery;   // %
    int8_t door;       // -1 khong co, 0 dong, 1 mo
    int8_t snr10;      // 0.1 dB  (-128..127 -> -12.8..12.7)
    int16_t rssi;      // dBm
    uint32_t fcnt;
    uint32_t stampMs;  // millis() luc do
};

class SensorSim {
public:
    static const int kMaxDevices = 16;
    void begin(const SimDevice* devs, int count, uint32_t periodMs, uint32_t nowMs, uint32_t seed);
    // Goi thuong xuyen. Tra ve true + dien 'out' khi co mot ban tin moi (goi lai toi khi false).
    bool poll(uint32_t nowMs, const TimeKeeper& tk, Reading& out);
    void setEnabled(bool on) { enabled_ = on; }
    bool enabled() const { return enabled_; }
    // Ep nhiet do (thu canh bao). temp10 = INT16_MIN de bo ep.
    bool force(const char* idOrSuffix, int16_t temp10);
    int find(const char* idOrSuffix) const;
    int count() const { return count_; }
    const SimDevice& device(int i) const { return devs_[i]; }

private:
    uint32_t rnd();
    float frand() { return (rnd() >> 8) * (1.0f / 16777216.0f); }
    void make(int i, uint32_t nowMs, const TimeKeeper& tk, Reading& out);

    const SimDevice* devs_ = nullptr;
    int count_ = 0;
    uint32_t periodMs_ = 300000;
    uint32_t next_[kMaxDevices];
    uint32_t fcnt_[kMaxDevices];
    int16_t forced_[kMaxDevices];
    int8_t door_[kMaxDevices];
    uint32_t doorChangeAt_[kMaxDevices];
    uint32_t lastDoorCheck_ = 0;
    uint32_t state_ = 1;
    bool enabled_ = true;
};

// ---------------- Hang doi gui (luu tam khi mat mang) ----------------
template <typename T, int N>
class Ring {
public:
    bool push(const T& v) {  // day -> bo ban cu nhat
        bool dropped = false;
        if (count_ == N) {
            head_ = (head_ + 1) % N;
            count_--;
            dropped = true;
        }
        buf_[(head_ + count_) % N] = v;
        count_++;
        return !dropped;
    }
    T* front() { return count_ ? &buf_[head_] : nullptr; }
    void pop() {
        if (!count_) return;
        head_ = (head_ + 1) % N;
        count_--;
    }
    int size() const { return count_; }
    static int capacity() { return N; }
    void clear() { head_ = count_ = 0; }

private:
    T buf_[N];
    int head_ = 0;
    int count_ = 0;
};

// Ban tin JSON tho nhan tu PC qua cong USB (lenh "UP {...}")
struct RawUplink {
    char devEui[33];
    char json[640];
};

// ---------------- Dung ban tin ----------------
namespace Uplink {
// Ma hoa TLV Milesight (giong maHoaTLV trong cau-noi/lib/giai-ma-milesight.mjs)
size_t encodeTlv(const Reading& r, uint8_t* out, size_t cap);
size_t base64(const uint8_t* in, size_t len, char* out, size_t cap);
// Topic "kholanh/<devEUI chu thuong>/up"
bool topic(const char* devEui, char* out, size_t cap);
// JSON kieu gateway Milesight. Tra ve do dai, 0 neu thieu bo dem.
size_t buildJson(const Reading& r, const SimDevice& dev, const char* gatewayId, const TimeKeeper& tk, char* out,
                 size_t cap);
// Kiem tra JSON tho tu PC: ngoac can bang, lay devEUI. Tra ve false neu hong.
bool parseRaw(const char* json, char* devEui, size_t cap);
void toLower(const char* in, char* out, size_t cap);
}  // namespace Uplink
