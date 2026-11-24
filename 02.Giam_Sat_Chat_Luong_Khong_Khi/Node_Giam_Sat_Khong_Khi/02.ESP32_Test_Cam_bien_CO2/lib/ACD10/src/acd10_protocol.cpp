#include "acd10_protocol.h"

#include <string.h>

namespace acd10 {

const char* statusToString(Status s) {
  switch (s) {
    case Status::Ok: return "OK";
    case Status::NoResponse: return "NO_RESPONSE";
    case Status::Incomplete: return "INCOMPLETE";
    case Status::BadChecksum: return "BAD_CHECKSUM";
    case Status::BadFrame: return "BAD_FRAME";
    case Status::CrcError: return "CRC_ERROR";
    case Status::ShortRead: return "SHORT_READ";
    case Status::I2cNack: return "I2C_NACK";
    case Status::WriteFailed: return "WRITE_FAILED";
    case Status::VerifyFailed: return "VERIFY_FAILED";
    case Status::InvalidArgument: return "INVALID_ARGUMENT";
    case Status::Count_: break;
  }
  return "UNKNOWN";
}

uint8_t crc8(const uint8_t* data, size_t len) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 8; bit > 0; --bit) {
      crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x31) : static_cast<uint8_t>(crc << 1);
    }
  }
  return crc;
}

uint8_t sumLow8(const uint8_t* data, size_t len) {
  uint8_t sum = 0;
  for (size_t i = 0; i < len; ++i) sum = static_cast<uint8_t>(sum + data[i]);
  return sum;
}

static uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

uint8_t checkPlausibility(const Measurement& m) {
  return (m.co2_ppm < kMinPpm || m.co2_ppm > kMaxPpm) ? kWarnOutOfRange : kWarnNone;
}

// ---------------- UART ----------------

size_t buildUartFrame(uint8_t cmd, const uint8_t* data, size_t n, uint8_t* out, size_t cap) {
  if (out == nullptr || n > kUartMaxData || (n > 0 && data == nullptr) || cap < kUartOverhead + n) return 0;
  out[0] = kUartHeader;
  out[1] = kUartFixedCode;
  out[2] = static_cast<uint8_t>(n);
  out[3] = cmd;
  if (n > 0) memcpy(&out[4], data, n);
  out[4 + n] = sumLow8(&out[1], 3 + n);
  return kUartOverhead + n;
}

size_t buildUartManualCal(uint16_t ppm, uint8_t* out, size_t cap) {
  if (ppm < kMinPpm || ppm > kMaxPpm) return 0;
  const uint8_t d[2] = {static_cast<uint8_t>(ppm >> 8), static_cast<uint8_t>(ppm)};
  return buildUartFrame(kUartCmdManualCal, d, 2, out, cap);
}

size_t buildUartSetCalMode(uint8_t mode, uint8_t* out, size_t cap) {
  if (mode != kCalModeManual && mode != kCalModeAuto) return 0;
  const uint8_t d[2] = {0x00, mode};
  return buildUartFrame(kUartCmdSetCalMode, d, 2, out, cap);
}

static void fillFrame(const uint8_t* p, UartFrame& out) {
  UartFrame f;
  f.len = p[2];
  f.cmd = p[3];
  memcpy(f.data, &p[4], f.len);
  out = f;
}

UartParseResult parseUartResponse(const uint8_t* buf, size_t len, uint8_t expectedCmd, uint8_t expectedLen,
                                  UartFrame& out) {
  UartParseResult r;
  if (buf == nullptr || len == 0) {
    r.status = Status::NoResponse;
    return r;
  }

  bool needMore = false;
  bool badChecksum = false;
  bool haveFallback = false;
  size_t fbStart = 0, fbLen = 0;

  for (size_t i = 0; i < len; ++i) {
    if (buf[i] != kUartHeader) continue;
    if (i + 1 >= len) {  // FE o cuoi buffer, co the la dau khung
      needMore = true;
      continue;
    }
    if (buf[i + 1] != kUartFixedCode) continue;
    if (i + 4 > len) {  // chua du FE A6 LEN CMD
      needMore = true;
      continue;
    }
    const size_t n = buf[i + 2];
    if (n > kUartMaxData) continue;  // LEN vo ly -> header gia
    const size_t total = kUartOverhead + n;
    if (i + total > len) {
      needMore = true;
      continue;
    }
    if (sumLow8(&buf[i + 1], 3 + n) != buf[i + total - 1]) {
      badChecksum = true;
      continue;
    }
    if (buf[i + 3] == expectedCmd) {
      fillFrame(&buf[i], out);
      r.status = Status::Ok;
      r.frameStart = i;
      r.frameLen = total;
      return r;
    }
    if (!haveFallback && expectedLen != kUartAnyLen && n == expectedLen) {
      haveFallback = true;
      fbStart = i;
      fbLen = total;
    }
  }

  // Khung dung checksum + dung do dai nhung CMD khac: chi nhan khi khong con khung nao dang cho byte,
  // de khung dung CMD (neu dang ve) duoc uu tien.
  if (haveFallback && !needMore) {
    fillFrame(&buf[fbStart], out);
    r.status = Status::Ok;
    r.frameStart = fbStart;
    r.frameLen = fbLen;
    r.cmdMismatch = true;
    return r;
  }

  if (needMore) {
    r.status = Status::Incomplete;
  } else if (badChecksum) {
    r.status = Status::BadChecksum;
  } else {
    r.status = Status::BadFrame;  // co byte nhung khong co khung FE A6 hop le
  }
  return r;
}

Status decodeUartCo2(const UartFrame& f, Measurement& out) {
  if (f.len < 2) return Status::BadFrame;
  Measurement m;
  m.co2_ppm = be16(&f.data[0]);
  if (f.len != kUartLenReadCo2) m.warnings |= kWarnLengthMismatch;
  if (f.cmd != kUartCmdReadCo2) m.warnings |= kWarnCmdMismatch;
  m.warnings |= checkPlausibility(m);
  out = m;
  return Status::Ok;
}

// ---------------- I2C ----------------

static size_t buildI2cSet(const uint8_t cmd[2], uint16_t value, uint8_t* out, size_t cap) {
  if (out == nullptr || cap < kI2cSetCmdLen) return 0;
  out[0] = cmd[0];
  out[1] = cmd[1];
  out[2] = static_cast<uint8_t>(value >> 8);
  out[3] = static_cast<uint8_t>(value);
  out[4] = crc8(&out[2], 2);
  return kI2cSetCmdLen;
}

size_t buildI2cSetCalMode(uint8_t mode, uint8_t* out, size_t cap) {
  if (mode != kCalModeManual && mode != kCalModeAuto) return 0;
  return buildI2cSet(kI2cCmdCalMode, mode, out, cap);
}

size_t buildI2cManualCal(uint16_t ppm, uint8_t* out, size_t cap) {
  if (ppm < kMinPpm || ppm > kMaxPpm) return 0;
  return buildI2cSet(kI2cCmdManualCal, ppm, out, cap);
}

size_t buildI2cFactoryReset(uint8_t* out, size_t cap) {
  if (out == nullptr || cap < kI2cFactoryCmdLen) return 0;
  out[0] = kI2cCmdFactoryReset[0];
  out[1] = kI2cCmdFactoryReset[1];
  out[2] = 0x00;
  return kI2cFactoryCmdLen;
}

Status decodeI2cCo2(const uint8_t* buf, size_t len, Measurement& out) {
  if (buf == nullptr) return Status::InvalidArgument;
  if (len < kI2cCo2DataLen) return Status::ShortRead;
  for (size_t w = 0; w < 3; ++w) {
    if (crc8(&buf[w * kI2cWordLen], 2) != buf[w * kI2cWordLen + 2]) return Status::CrcError;
  }
  Measurement m;
  m.co2_ppm = (static_cast<uint32_t>(be16(&buf[0])) << 16) | be16(&buf[3]);
  m.temp_raw = be16(&buf[6]);
  m.has_temp = true;
  m.warnings |= checkPlausibility(m);
  out = m;
  return Status::Ok;
}

Status decodeI2cWord(const uint8_t* buf, size_t len, uint16_t& value) {
  if (buf == nullptr) return Status::InvalidArgument;
  if (len < kI2cWordLen) return Status::ShortRead;
  if (crc8(buf, 2) != buf[2]) return Status::CrcError;
  value = be16(buf);
  return Status::Ok;
}

size_t copyAscii(const uint8_t* src, size_t n, char* dst, size_t cap) {
  if (dst == nullptr || cap == 0) return 0;
  size_t j = 0;
  for (size_t i = 0; src != nullptr && i < n && j + 1 < cap; ++i) {
    const uint8_t c = src[i];
    if (c == 0x00 || c == ' ') {
      dst[j++] = ' ';
    } else {
      dst[j++] = (c >= 0x21 && c <= 0x7E) ? static_cast<char>(c) : '.';
    }
  }
  while (j > 0 && dst[j - 1] == ' ') --j;
  dst[j] = '\0';
  return j;
}

}  // namespace acd10
