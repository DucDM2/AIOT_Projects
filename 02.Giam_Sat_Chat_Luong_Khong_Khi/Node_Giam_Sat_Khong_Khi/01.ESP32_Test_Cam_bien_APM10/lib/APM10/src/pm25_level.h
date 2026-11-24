// Phan muc chat luong khong khi theo nong do PM2.5 tuc thoi.
// Nguong lay theo bang VN_AQI (QD 1459/QD-TCMT, 2019) cho PM2.5: 25 / 50 / 80 / 150 / 250 ug/m3.
// Chi mang tinh tham khao: AQI chuan tinh tren trung binh theo gio/ngay, khong phai gia tri tuc thoi.
#pragma once

#include <stdint.h>

namespace apm10 {

enum class Pm25Level : uint8_t { Good, Moderate, Poor, Bad, VeryBad, Hazardous };

inline Pm25Level classifyPm25(uint16_t pm25) {
  if (pm25 <= 25) return Pm25Level::Good;
  if (pm25 <= 50) return Pm25Level::Moderate;
  if (pm25 <= 80) return Pm25Level::Poor;
  if (pm25 <= 150) return Pm25Level::Bad;
  if (pm25 <= 250) return Pm25Level::VeryBad;
  return Pm25Level::Hazardous;
}

// Nhan khong dau de hien thi dung tren moi Serial Monitor
inline const char* pm25LevelName(Pm25Level level) {
  switch (level) {
    case Pm25Level::Good: return "Tot";
    case Pm25Level::Moderate: return "Trung binh";
    case Pm25Level::Poor: return "Kem";
    case Pm25Level::Bad: return "Xau";
    case Pm25Level::VeryBad: return "Rat xau";
    case Pm25Level::Hazardous: return "Nguy hai";
  }
  return "?";
}

}  // namespace apm10
