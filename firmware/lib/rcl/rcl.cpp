#include "rcl.h"

#include <string.h>

namespace rcl {
namespace {

// The table of the reflected polynomial 0xEDB88320, worked out by the compiler: in flash on the
// device, and nothing to build at run time from two tasks at once.
struct CrcTable {
  uint32_t v[256];
};

constexpr CrcTable makeCrcTable() {
  CrcTable t{};
  for (uint32_t i = 0; i < 256; ++i) {
    uint32_t c = i;
    for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    t.v[i] = c;
  }
  return t;
}

constexpr CrcTable kCrc = makeCrcTable();

// Little-endian, field after field: a cursor that knows whether there was room.
class Out {
 public:
  Out(uint8_t* p, size_t capacity) : p_(p), cap_(capacity) {}
  void u8(uint8_t v) { put(&v, 1); }
  void u16(uint16_t v) {
    uint8_t b[2] = {uint8_t(v), uint8_t(v >> 8)};
    put(b, 2);
  }
  void u32(uint32_t v) {
    uint8_t b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)};
    put(b, 4);
  }
  void u64(uint64_t v) {
    u32(uint32_t(v));
    u32(uint32_t(v >> 32));
  }
  void put(const void* data, size_t n) {
    if (at_ + n > cap_) {
      ok_ = false;
      return;
    }
    memcpy(p_ + at_, data, n);
    at_ += n;
  }
  size_t done() const { return ok_ ? at_ : 0; }

 private:
  uint8_t* p_;
  size_t cap_, at_ = 0;
  bool ok_ = true;
};

class In {
 public:
  In(const uint8_t* p, size_t n) : p_(p), n_(n) {}
  bool has(size_t n) const { return n_ - at_ >= n; }
  uint8_t u8() { return p_[at_++]; }
  uint16_t u16() {
    uint16_t v = p_[at_] | p_[at_ + 1] << 8;
    at_ += 2;
    return v;
  }
  uint32_t u32() {
    uint32_t v = p_[at_] | uint32_t(p_[at_ + 1]) << 8 | uint32_t(p_[at_ + 2]) << 16 |
                 uint32_t(p_[at_ + 3]) << 24;
    at_ += 4;
    return v;
  }
  uint64_t u64() {
    uint64_t lo = u32();
    return lo | uint64_t(u32()) << 32;
  }
  void get(void* out, size_t n) {
    memcpy(out, p_ + at_, n);
    at_ += n;
  }

 private:
  const uint8_t* p_;
  size_t n_, at_ = 0;
};

uint32_t seedOf(uint64_t nonce) {
  uint8_t b[8];
  Out o(b, sizeof b);
  o.u64(nonce);
  return crc32(b, sizeof b);
}

}  // namespace

uint32_t crc32(const uint8_t* data, size_t n, uint32_t crc) {
  crc = ~crc;
  for (size_t i = 0; i < n; ++i) crc = kCrc.v[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

size_t writeFileHeader(uint64_t nonce, uint8_t* out, size_t capacity) {
  if (capacity < kFileHeaderBytes) return 0;
  Out o(out, capacity);
  o.put(kMagic, sizeof kMagic);
  o.u16(kVersion);
  o.u16(kFileHeaderBytes);
  o.u64(nonce);
  o.u32(crc32(out, 20));
  return o.done();
}

Writer::Writer(uint64_t nonce) : seed_(seedOf(nonce)) {}

size_t Writer::record(Type type, uint32_t tMs, const uint8_t* payload, size_t length, uint8_t* out,
                      size_t capacity) const {
  if (length > kMaxPayload || capacity < kRecordOverhead + length) return 0;
  Out o(out, capacity);
  o.u8(kSync);
  o.u8(static_cast<uint8_t>(type));
  o.u16(static_cast<uint16_t>(length));
  o.u32(tMs);
  if (length) o.put(payload, length);
  o.u32(crc32(out + 1, kRecordHead - 1 + length, seed_));
  return o.done();
}

bool Reader::begin(const uint8_t* data, size_t size) {
  data_ = data;
  size_ = size;
  skipped_ = 0;
  if (size < kFileHeaderBytes || memcmp(data, kMagic, sizeof kMagic) != 0) return false;
  In in(data + 8, size - 8);
  version_ = in.u16();
  uint16_t headerSize = in.u16();
  nonce_ = in.u64();
  uint32_t crc = in.u32();
  if (crc != crc32(data, 20) || version_ != kVersion) return false;
  if (headerSize < kFileHeaderBytes || headerSize > size) return false;
  seed_ = seedOf(nonce_);
  pos_ = headerSize;
  return true;
}

bool Reader::next(Record& out) {
  while (pos_ < size_) {
    const uint8_t* p = data_ + pos_;
    size_t left = size_ - pos_;
    if (p[0] == kSync && left >= kRecordOverhead) {
      In in(p + 1, left - 1);
      uint8_t type = in.u8();
      uint16_t length = in.u16();
      uint32_t tMs = in.u32();
      if (left - kRecordOverhead >= length) {
        In tail(p + kRecordHead + length, 4);
        if (tail.u32() == crc32(p + 1, kRecordHead - 1 + length, seed_)) {
          out.type = type;
          out.tMs = tMs;
          out.payload = p + kRecordHead;
          out.length = length;
          out.offset = pos_;
          pos_ += kRecordOverhead + length;
          return true;
        }
      }
    }
    ++pos_;  // not a record: on to the next byte that could start one
    ++skipped_;
  }
  return false;
}

// --- Payloads ----------------------------------------------------------------------

size_t encode(const SessionHeader& in, uint8_t* out, size_t capacity) {
  Out o(out, capacity);
  o.u32(in.sessionId);
  o.put(in.device, 6);
  o.u16(in.post);
  o.put(in.firmware, 16);
  o.u16(in.epochMs);
  o.u16(in.rcPeriodUs);
  o.u16(in.imuRawPeriodUs);
  o.u8(in.accelRangeG);
  o.u8(0);
  o.u16(in.gyroRangeDps);
  o.u8(in.carProfile);
  o.u8(0);
  return o.done();
}

bool decode(const uint8_t* payload, size_t length, SessionHeader& out) {
  In in(payload, length);
  if (!in.has(kSessionHeaderBytes)) return false;
  out.sessionId = in.u32();
  in.get(out.device, 6);
  out.post = in.u16();
  in.get(out.firmware, 16);
  out.epochMs = in.u16();
  out.rcPeriodUs = in.u16();
  out.imuRawPeriodUs = in.u16();
  out.accelRangeG = in.u8();
  in.u8();
  out.gyroRangeDps = in.u16();
  out.carProfile = in.u8();
  return true;
}

size_t encode(const ImuAggregate& in, uint8_t* out, size_t capacity) {
  Out o(out, capacity);
  o.u16(in.count);
  for (const AxisSummary& a : in.axes) {
    o.u16(static_cast<uint16_t>(a.min));
    o.u16(static_cast<uint16_t>(a.max));
    o.u16(static_cast<uint16_t>(a.mean));
  }
  return o.done();
}

bool decode(const uint8_t* payload, size_t length, ImuAggregate& out) {
  In in(payload, length);
  if (!in.has(kImuAggregateBytes)) return false;
  out.count = in.u16();
  for (AxisSummary& a : out.axes) {
    a.min = static_cast<int16_t>(in.u16());
    a.max = static_cast<int16_t>(in.u16());
    a.mean = static_cast<int16_t>(in.u16());
  }
  return true;
}

size_t encode(const ImuRaw& in, uint8_t* out, size_t capacity) {
  if (in.count > kMaxSamples) return 0;
  Out o(out, capacity);
  o.u8(in.count);
  o.u8(0);
  for (uint8_t i = 0; i < in.count; ++i) {
    for (int16_t v : in.samples[i]) o.u16(static_cast<uint16_t>(v));
  }
  return o.done();
}

bool decode(const uint8_t* payload, size_t length, ImuRaw& out) {
  In in(payload, length);
  if (!in.has(2)) return false;
  out.count = in.u8();
  in.u8();
  if (out.count > kMaxSamples || !in.has(size_t(out.count) * 12)) return false;
  for (uint8_t i = 0; i < out.count; ++i) {
    for (int16_t& v : out.samples[i]) v = static_cast<int16_t>(in.u16());
  }
  return true;
}

size_t encode(const RcSamples& in, uint8_t* out, size_t capacity) {
  if (in.count > kMaxSamples) return 0;
  Out o(out, capacity);
  o.u8(in.count);
  o.u8(0);
  for (uint8_t i = 0; i < in.count; ++i) {
    for (uint16_t v : in.samples[i]) o.u16(v);
  }
  return o.done();
}

bool decode(const uint8_t* payload, size_t length, RcSamples& out) {
  In in(payload, length);
  if (!in.has(2)) return false;
  out.count = in.u8();
  in.u8();
  if (out.count > kMaxSamples || !in.has(size_t(out.count) * 6)) return false;
  for (uint8_t i = 0; i < out.count; ++i) {
    for (uint16_t& v : out.samples[i]) v = in.u16();
  }
  return true;
}

size_t encode(const Marker& in, uint8_t* out, size_t capacity) {
  Out o(out, capacity);
  o.u16(in.number);
  o.u8(in.source);
  o.u8(0);
  return o.done();
}

bool decode(const uint8_t* payload, size_t length, Marker& out) {
  In in(payload, length);
  if (!in.has(kMarkerBytes)) return false;
  out.number = in.u16();
  out.source = in.u8();
  return true;
}

size_t encode(const Lap& in, uint8_t* out, size_t capacity) {
  Out o(out, capacity);
  o.u16(in.lap);
  o.u8(in.flags);
  o.u8(in.line);
  o.u32(in.crossingMs);
  o.u32(in.crossingItowMs);
  o.u32(in.lapTimeMs);
  return o.done();
}

bool decode(const uint8_t* payload, size_t length, Lap& out) {
  In in(payload, length);
  if (!in.has(kLapBytes)) return false;
  out.lap = in.u16();
  out.flags = in.u8();
  out.line = in.u8();
  out.crossingMs = in.u32();
  out.crossingItowMs = in.u32();
  out.lapTimeMs = in.u32();
  return true;
}

size_t encode(const Footer& in, uint8_t* out, size_t capacity) {
  Out o(out, capacity);
  o.u8(in.close);
  o.u8(0);
  o.u8(0);
  o.u8(0);
  const uint32_t fields[] = {in.ubxFrames,     in.records,       in.uartOverruns, in.uartErrors,
                             in.ubxBadChecksums, in.ubxOversize, in.gnssQueueFull, in.ringOverflow,
                             in.rcRejects[0],  in.rcRejects[1], in.rcRejects[2], in.writes,
                             in.slowWrites,    in.writeMaxMs,   in.writeP99Ms,   in.ringHighWater};
  for (uint32_t v : fields) o.u32(v);
  return o.done();
}

bool decode(const uint8_t* payload, size_t length, Footer& out) {
  In in(payload, length);
  if (!in.has(kFooterBytes)) return false;
  out.close = in.u8();
  in.u8();
  in.u8();
  in.u8();
  uint32_t* fields[] = {&out.ubxFrames,       &out.records,      &out.uartOverruns,
                        &out.uartErrors,      &out.ubxBadChecksums, &out.ubxOversize,
                        &out.gnssQueueFull,   &out.ringOverflow, &out.rcRejects[0],
                        &out.rcRejects[1],    &out.rcRejects[2], &out.writes,
                        &out.slowWrites,      &out.writeMaxMs,   &out.writeP99Ms,
                        &out.ringHighWater};
  for (uint32_t* f : fields) *f = in.u32();
  return true;
}

}  // namespace rcl
