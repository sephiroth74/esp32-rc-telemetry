// The .rcl session log — docs/protocol.md, PLAN-ESP32.md §7.2, ROADMAP.md step 5.2.
//
// One of the format's implementations, the other being packages/rct_core in Dart; both are held to
// the golden file in docs/fixtures/ (rcl-golden-v1). docs/protocol.md is the authority: offsets,
// widths and units are its, and the names here follow it.
//
// Writing builds whole records into a buffer the caller owns — the logger's ring, in phase 5 — with
// their CRC, seeded with the session's nonce. Reading walks a buffer holding a file: every whole
// record with a matching CRC, in order, and a count of the bytes that were none, which is how a
// file cut short by a power cut, or followed by an older session's bytes, still reads (§4.5).
// Payloads encode and decode one struct per record type; a payload longer than its version's
// layout decodes, a shorter one does not.
//
// Framework-free (§6.1): nothing here includes Arduino.h, so the host tests run it as the device
// does.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace rcl {

constexpr uint8_t kMagic[8] = {0x89, 'R', 'C', 'L', 0x0D, 0x0A, 0x1A, 0x0A};
constexpr uint16_t kVersion = 1;
constexpr size_t kFileHeaderBytes = 24;
constexpr uint8_t kSync = 0xA5;
constexpr size_t kRecordHead = 8;                   // sync, type, length, t_ms
constexpr size_t kRecordOverhead = kRecordHead + 4;  // and the CRC
constexpr size_t kMaxPayload = 65535;

enum class Type : uint8_t {
  SessionHeader = 0x01,
  Ubx = 0x02,
  ImuAggregate = 0x03,
  ImuRaw = 0x04,
  RcSamples = 0x05,
  Marker = 0x06,
  Lap = 0x07,
  Footer = 0x08,
};

// CRC-32/ISO-HDLC, zlib's, continued from `crc` (0 to start): crc32("123456789") is 0xCBF43926.
uint32_t crc32(const uint8_t* data, size_t n, uint32_t crc = 0);

// The 24-byte file header; its size, or 0 when it does not fit.
size_t writeFileHeader(uint64_t nonce, uint8_t* out, size_t capacity);

// The records of one session: the nonce's share of every CRC is worked out once.
class Writer {
 public:
  explicit Writer(uint64_t nonce);
  // A whole record into `out`: its size, kRecordOverhead + length, or 0 when it does not fit.
  size_t record(Type type, uint32_t tMs, const uint8_t* payload, size_t length, uint8_t* out,
                size_t capacity) const;

 private:
  uint32_t seed_;
};

struct Record {
  uint8_t type;  // a Type, or one this reader does not know: skip it
  uint32_t tMs;
  const uint8_t* payload;  // into the reader's buffer
  uint16_t length;
  size_t offset;  // of its sync byte in the file
};

class Reader {
 public:
  // False for anything but an .rcl of a version this reader knows.
  bool begin(const uint8_t* data, size_t size);
  // The next whole record with a matching CRC; false at the end of the data.
  bool next(Record& out);
  // A file read in pieces: `data` now holds it from byte `base` on, its header read by begin() on
  // the first piece. The records' offsets stay the file's.
  void resume(const uint8_t* data, size_t size, size_t base);
  size_t position() const { return base_ + pos_; }  // where the next record is looked for

  uint16_t version() const { return version_; }
  uint64_t nonce() const { return nonce_; }
  size_t skipped() const { return skipped_; }  // bytes in no record: a cut, a tail, a corruption

 private:
  const uint8_t* data_ = nullptr;
  size_t size_ = 0, pos_ = 0, skipped_ = 0, base_ = 0;
  uint16_t version_ = 0;
  uint64_t nonce_ = 0;
  uint32_t seed_ = 0;
};

// Where a file that was never closed ends — the car switched off with its session open (§4.5) —
// so that it can be cut there and read as a closed one does, bar the footer.
struct Tail {
  size_t end = 0;          // just past the last whole record; the file header's end with none
  uint32_t records = 0;    // whole records, the session header's included
  uint32_t sessionId = 0;  // the session header's; 0 without one
};
// Fills `buf` with up to `capacity` bytes of the file from `offset`; how many it read.
using ReadAt = size_t (*)(void* context, size_t offset, uint8_t* buf, size_t capacity);
// Reads the file in pieces of `capacity`, which must hold the largest record there can be,
// kRecordOverhead + kMaxPayload, and gives up `gap` bytes past the last record found: what follows
// in a pre-allocated extent is whatever the card held. False for anything but an .rcl.
bool findTail(ReadAt read, void* context, uint8_t* buf, size_t capacity, size_t gap, Tail& out);

// --- Payloads ----------------------------------------------------------------------
// encode(): the payload's size, or 0 when it does not fit. decode(): false for a payload too short
// for its layout, or with more samples than the struct holds.

struct SessionHeader {
  uint32_t sessionId = 0;
  uint8_t device[6] = {};
  uint16_t post = 0;          // the POST's failed-check mask
  char firmware[16] = {};     // zero-padded; no terminator when all 16 are used
  uint16_t epochMs = 100;
  uint16_t rcPeriodUs = 20000;
  uint16_t imuRawPeriodUs = 0;  // 0: raw logging off
  uint8_t accelRangeG = 0;      // 0: no IMU
  uint16_t gyroRangeDps = 0;
  uint8_t carProfile = 0;
};
constexpr size_t kSessionHeaderBytes = 40;

struct AxisSummary {
  int16_t min, max, mean;
};
struct ImuAggregate {
  uint16_t count = 0;
  AxisSummary axes[6] = {};  // ax, ay, az, gx, gy, gz
};
constexpr size_t kImuAggregateBytes = 38;

constexpr uint8_t kMaxSamples = 64;  // of either batch, a record
struct ImuRaw {
  uint8_t count = 0;
  int16_t samples[kMaxSamples][6] = {};  // ax, ay, az, gx, gy, gz
};
struct RcSamples {
  uint8_t count = 0;
  uint16_t samples[kMaxSamples][3] = {};  // steering, throttle, aux, in µs; 0 for none
};

enum class MarkerSource : uint8_t { Key = 0, Aux = 1 };
struct Marker {
  uint16_t number = 0;
  uint8_t source = 0;  // a MarkerSource
};
constexpr size_t kMarkerBytes = 4;

constexpr uint8_t kLapCut = 0x01, kLapOff = 0x02;
enum class LineSource : uint8_t { Auto = 0, Device = 1, Desktop = 2 };
struct Lap {
  uint16_t lap = 0;  // 0: the first crossing, which ends none
  uint8_t flags = 0;
  uint8_t line = 0;  // a LineSource
  uint32_t crossingMs = 0;
  uint32_t crossingItowMs = 0;
  uint32_t lapTimeMs = 0;
};
constexpr size_t kLapBytes = 16;

enum class Close : uint8_t { ByHand = 0, AutoStop = 1, PowerOff = 2, CardError = 3 };
struct Footer {
  uint8_t close = 0;  // a Close
  uint32_t ubxFrames = 0, records = 0;
  uint32_t uartOverruns = 0, uartErrors = 0, ubxBadChecksums = 0, ubxOversize = 0;
  uint32_t gnssQueueFull = 0, ringOverflow = 0;
  uint32_t rcRejects[3] = {};  // steering, throttle, aux
  uint32_t writes = 0, slowWrites = 0, writeMaxMs = 0, writeP99Ms = 0, ringHighWater = 0;
};
constexpr size_t kFooterBytes = 68;

size_t encode(const SessionHeader& in, uint8_t* out, size_t capacity);
size_t encode(const ImuAggregate& in, uint8_t* out, size_t capacity);
size_t encode(const ImuRaw& in, uint8_t* out, size_t capacity);
size_t encode(const RcSamples& in, uint8_t* out, size_t capacity);
size_t encode(const Marker& in, uint8_t* out, size_t capacity);
size_t encode(const Lap& in, uint8_t* out, size_t capacity);
size_t encode(const Footer& in, uint8_t* out, size_t capacity);

bool decode(const uint8_t* payload, size_t length, SessionHeader& out);
bool decode(const uint8_t* payload, size_t length, ImuAggregate& out);
bool decode(const uint8_t* payload, size_t length, ImuRaw& out);
bool decode(const uint8_t* payload, size_t length, RcSamples& out);
bool decode(const uint8_t* payload, size_t length, Marker& out);
bool decode(const uint8_t* payload, size_t length, Lap& out);
bool decode(const uint8_t* payload, size_t length, Footer& out);

}  // namespace rcl
