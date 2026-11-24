// Quy doi TVOC ppb -> ug/m3 va phan muc chat luong khong khi.
// AGS10 hieu chuan bang khi chuan ethanol (datasheet) nen quy doi theo ethanol:
//   ug/m3 = ppb x M / Vm = ppb x 46.07 / 24.45   (25 do C, 1 atm)
// Nguong phan muc theo khuyen nghi TVOC trong nha cua UBA (Duc): 0.3 / 1 / 3 / 10 / 25 mg/m3.
// Chi mang tinh tham khao: cam bien ban dan do tong khi VOC tuong duong ethanol, khong phai TVOC phong thi nghiem.
#pragma once

#include <stdint.h>

namespace ags10 {

inline uint32_t tvocPpbToUgm3(uint32_t ppb) {
  if (ppb > 99999999UL) ppb = 99999999UL;  // tranh tran so: 99999999 x 4607 < 2^64
  return static_cast<uint32_t>((static_cast<uint64_t>(ppb) * 4607 + 1222) / 2445);  // lam tron
}

enum class TvocLevel : uint8_t { Good, Acceptable, Poor, Bad, VeryBad, Hazardous };

inline TvocLevel classifyTvocUgm3(uint32_t ugm3) {
  if (ugm3 <= 300) return TvocLevel::Good;
  if (ugm3 <= 1000) return TvocLevel::Acceptable;
  if (ugm3 <= 3000) return TvocLevel::Poor;
  if (ugm3 <= 10000) return TvocLevel::Bad;
  if (ugm3 <= 25000) return TvocLevel::VeryBad;
  return TvocLevel::Hazardous;
}

inline TvocLevel classifyTvocPpb(uint32_t ppb) { return classifyTvocUgm3(tvocPpbToUgm3(ppb)); }

// Nhan khong dau de hien thi dung tren moi Serial Monitor
inline const char* tvocLevelName(TvocLevel level) {
  switch (level) {
    case TvocLevel::Good: return "Tot";
    case TvocLevel::Acceptable: return "Chap nhan";
    case TvocLevel::Poor: return "Kem-thong gio";
    case TvocLevel::Bad: return "Xau";
    case TvocLevel::VeryBad: return "Rat xau";
    case TvocLevel::Hazardous: return "Nguy hai";
  }
  return "?";
}

}  // namespace ags10
