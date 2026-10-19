#include "link_probe.h"

namespace ubx {
namespace {

constexpr uint8_t kNmeaMaxLength = 82;  // the standard's limit, "$" to the checksum

int hexValue(uint8_t c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

void LinkProbe::push(const uint8_t* data, size_t n) {
  for (size_t i = 0; i < n; ++i) nmeaByte(data[i]);
  while (n && !ubx_) {
    size_t taken = scanner_.push(data, n);
    data += taken;
    n -= taken;
    Frame frame;
    while (scanner_.next(frame)) ubx_ = true;
  }
}

void LinkProbe::nmeaByte(uint8_t c) {
  if (c == '$') {  // a new sentence, whatever came before
    state_ = Nmea::Body;
    length_ = sum_ = 0;
    return;
  }
  switch (state_) {
    case Nmea::Idle:
      return;
    case Nmea::Body:
      if (c == '*') {
        state_ = length_ >= 6 ? Nmea::Hex1 : Nmea::Idle;  // "GxXXX," at least
        return;
      }
      // Printable ASCII only, from a GNSS talker ("GP", "GN", "GA"...), within the length limit.
      if (c < 0x20 || c > 0x7E || ++length_ > kNmeaMaxLength || (length_ == 1 && c != 'G')) {
        state_ = Nmea::Idle;
        return;
      }
      sum_ ^= c;
      return;
    case Nmea::Hex1: {
      int v = hexValue(c);
      state_ = v < 0 ? Nmea::Idle : Nmea::Hex2;
      given_ = v << 4;
      return;
    }
    case Nmea::Hex2: {
      int v = hexValue(c);
      if (v >= 0 && (given_ | v) == sum_) nmea_ = true;
      state_ = Nmea::Idle;
      return;
    }
  }
}

}  // namespace ubx
