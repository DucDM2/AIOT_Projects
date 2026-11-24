#include "apm10_protocol.h"

namespace apm10 {

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

size_t buildUartCommand(uint8_t cmd, uint8_t* out, size_t cap) {
  if (out == nullptr || cap < kUartCommandLen) return 0;
  out[0] = kUartHeader;
  out[1] = kUartFixedCode;
  out[2] = 0x00;  // LEN: lenh doc khong co data
  out[3] = cmd;
  out[4] = sumLow8(&out[1], 3);
  return kUartCommandLen;
}

static uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

uint8_t checkPlausibility(const Measurement& m) {
  uint8_t w = kWarnNone;
  if ((m.has_pm1_0 && m.pm1_0 > kMaxConcentration) || (m.has_pm2_5 && m.pm2_5 > kMaxConcentration) ||
      (m.has_pm10 && m.pm10 > kMaxConcentration)) {
    w |= kWarnOutOfRange;
  }
  if ((m.has_pm1_0 && m.has_pm2_5 && m.pm1_0 > m.pm2_5) || (m.has_pm2_5 && m.has_pm10 && m.pm2_5 > m.pm10)) {
    w |= kWarnOrder;
  }
  return w;
}

// Giai ma phan data cua khung UART (n = 2 hoac 6 byte).
static void decodeUartData(const uint8_t* data, size_t n, uint8_t requestedCmd, uint8_t lenByte,
                           Measurement& out) {
  Measurement m;
  if (n == 6) {
    m.pm1_0 = be16(&data[0]);
    m.pm2_5 = be16(&data[2]);
    m.pm10 = be16(&data[4]);
    m.has_pm1_0 = m.has_pm2_5 = m.has_pm10 = true;
  } else {
    m.pm2_5 = be16(&data[0]);
    m.has_pm2_5 = true;
    if (requestedCmd == kCmdReadAll) m.warnings |= kWarnPm25Only;
  }
  if (lenByte != n) m.warnings |= kWarnLengthMismatch;
  m.warnings |= checkPlausibility(m);
  out = m;
}

UartParseResult parseUartResponse(const uint8_t* buf, size_t len, uint8_t requestedCmd, bool final,
                                  Measurement& out) {
  UartParseResult r;
  if (buf == nullptr || len == 0) {
    r.status = Status::NoResponse;
    return r;
  }
  if (requestedCmd != kCmdReadPm25 && requestedCmd != kCmdReadAll) {
    r.status = Status::InvalidArgument;
    return r;
  }

  const size_t expected = (requestedCmd == kCmdReadAll) ? 6 : 2;
  bool needMore = false;
  bool badChecksum = false;

  for (size_t i = 0; i < len; ++i) {
    if (buf[i] != kUartHeader) continue;
    if (i + 1 >= len) {  // FE o cuoi buffer, co the la dau khung
      needMore = true;
      continue;
    }
    if (buf[i + 1] != kUartFixedCode) continue;
    if (i + 4 > len) {  // chua du FE A5 LEN CMD
      needMore = true;
      continue;
    }
    const uint8_t lenByte = buf[i + 2];

    // Ung vien so byte data: uu tien theo lenh da gui; chi dung byte LEN khi da het thoi gian cho,
    // tranh nhan nham khung ngan khi khung dai chua ve het (checksum trung ngau nhien 1/256).
    size_t candidates[2] = {expected, 0};
    size_t nCandidates = 1;
    if (final && lenByte != expected && (lenByte == 2 || lenByte == 6)) candidates[nCandidates++] = lenByte;

    for (size_t c = 0; c < nCandidates; ++c) {
      const size_t n = candidates[c];
      const size_t total = 4 + n + 1;
      if (i + total > len) {
        needMore = true;
        continue;
      }
      const uint8_t cs = sumLow8(&buf[i + 1], 3 + n);
      if (cs != buf[i + total - 1]) {
        // Chi tinh loi checksum cho ung vien chinh; ung vien du phong chi la phong doan
        if (c == 0) badChecksum = true;
        continue;
      }
      decodeUartData(&buf[i + 4], n, requestedCmd, lenByte, out);
      r.status = Status::Ok;
      r.frameStart = i;
      r.frameLen = total;
      return r;
    }
  }

  if (needMore && !final) {
    r.status = Status::Incomplete;
  } else if (badChecksum) {
    r.status = Status::BadChecksum;
  } else if (needMore) {
    r.status = Status::Incomplete;
  } else {
    r.status = Status::BadFrame;  // co byte nhung khong co dau khung FE A5
  }
  return r;
}

Status decodeI2cData(const uint8_t* buf, size_t len, Measurement& out) {
  if (buf == nullptr) return Status::InvalidArgument;
  if (len < kI2cDataLen) return Status::ShortRead;

  Measurement m;
  uint16_t words[kI2cWordCount];
  for (size_t w = 0; w < kI2cWordCount; ++w) {
    const uint8_t* p = &buf[w * 3];
    const bool crcOk = crc8(p, 2) == p[2];
    words[w] = be16(p);
    const bool isPmWord = (w == 0 || w == 1 || w == 3);
    if (!crcOk) {
      if (isPmWord) return Status::CrcError;
      m.warnings |= kWarnReservedCrc;
    }
  }
  m.pm1_0 = words[0];
  m.pm2_5 = words[1];
  m.pm10 = words[3];
  m.has_pm1_0 = m.has_pm2_5 = m.has_pm10 = true;
  m.warnings |= checkPlausibility(m);
  out = m;
  return Status::Ok;
}

}  // namespace apm10
