#include "ags10_protocol.h"

namespace ags10 {

const char* statusToString(Status s) {
  switch (s) {
    case Status::Ok: return "OK";
    case Status::NotReady: return "NOT_READY";
    case Status::NoResponse: return "NO_RESPONSE";
    case Status::ShortRead: return "SHORT_READ";
    case Status::CrcError: return "CRC_ERROR";
    case Status::I2cNack: return "I2C_NACK";
    case Status::UnsupportedUnit: return "UNSUPPORTED_UNIT";
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

Status checkFrame(const uint8_t* buf, size_t len) {
  if (buf == nullptr) return Status::InvalidArgument;
  if (len < kFrameLen) return Status::ShortRead;
  return crc8(buf, 4) == buf[4] ? Status::Ok : Status::CrcError;
}

Status decodeTvoc(const uint8_t* buf, size_t len, TvocReading& out) {
  const Status st = checkFrame(buf, len);
  if (st != Status::Ok) return st;

  const uint8_t status = buf[0];
  if ((status & kStatusUnitMask) != kStatusUnitPpb) return Status::UnsupportedUnit;

  TvocReading r;
  r.status = status;
  r.ready = (status & kStatusRdyMask) == 0;
  r.tvocPpb = (static_cast<uint32_t>(buf[1]) << 16) | (static_cast<uint32_t>(buf[2]) << 8) | buf[3];
  if (r.tvocPpb > kMaxTvocPpb) r.warnings |= kWarnOutOfRange;
  out = r;
  return r.ready ? Status::Ok : Status::NotReady;
}

Status decodeResistance(const uint8_t* buf, size_t len, uint32_t& out) {
  const Status st = checkFrame(buf, len);
  if (st != Status::Ok) return st;
  out = (static_cast<uint32_t>(buf[0]) << 24) | (static_cast<uint32_t>(buf[1]) << 16) |
        (static_cast<uint32_t>(buf[2]) << 8) | buf[3];
  return Status::Ok;
}

Status decodeVersion(const uint8_t* buf, size_t len, uint8_t& out) {
  const Status st = checkFrame(buf, len);
  if (st != Status::Ok) return st;
  out = buf[3];  // Data1..Data3 du phong
  return Status::Ok;
}

size_t buildZeroPointData(uint16_t value, uint8_t* out, size_t cap) {
  if (out == nullptr || cap < kFrameLen) return 0;
  out[0] = 0x00;
  out[1] = 0x0C;
  out[2] = static_cast<uint8_t>(value >> 8);
  out[3] = static_cast<uint8_t>(value);
  out[4] = crc8(out, 4);
  return kFrameLen;
}

size_t buildAddressData(uint8_t newAddr, uint8_t* out, size_t cap) {
  if (out == nullptr || cap < kFrameLen) return 0;
  if (newAddr < 0x08 || newAddr > 0x77) return 0;  // dia chi danh rieng cua I2C
  const uint8_t inv = static_cast<uint8_t>(~newAddr);
  out[0] = newAddr;
  out[1] = inv;
  out[2] = newAddr;
  out[3] = inv;
  out[4] = crc8(out, 4);
  return kFrameLen;
}

}  // namespace ags10
