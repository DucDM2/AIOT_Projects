// Phan muc chat luong khong khi trong nha theo nong do CO2 (ppm).
// Nguong tham khao thong dung: khong khi ngoai troi ~420 ppm; ASHRAE / nhieu tieu chuan thong gio
// lay ~1000 ppm lam moc can tang thong gio. Chi mang tinh tham khao, khong phai tieu chuan suc khoe.
#pragma once

#include <stdint.h>

namespace acd10 {

enum class Co2Level : uint8_t { Good, Moderate, Poor, Bad, VeryBad };

inline Co2Level classifyCo2(uint32_t ppm) {
  if (ppm < 800) return Co2Level::Good;
  if (ppm < 1000) return Co2Level::Moderate;
  if (ppm < 1500) return Co2Level::Poor;
  if (ppm < 2500) return Co2Level::Bad;
  return Co2Level::VeryBad;
}

// Nhan khong dau de hien thi dung tren moi Serial Monitor
inline const char* co2LevelName(Co2Level level) {
  switch (level) {
    case Co2Level::Good: return "Tot";
    case Co2Level::Moderate: return "Trung binh";
    case Co2Level::Poor: return "Kem - nen thong gio";
    case Co2Level::Bad: return "Xau - mo cua ngay";
    case Co2Level::VeryBad: return "Rat xau";
  }
  return "?";
}

}  // namespace acd10
