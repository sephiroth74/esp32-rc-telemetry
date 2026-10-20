// UBX frame scanning for the u-blox M10 — PLAN-ESP32.md §5.3, §5.4, ROADMAP.md step 3.4.
//
// Finds whole frames in the byte stream from the module: the sync 0xB5 0x62, class, id, a length up
// to kMaxPayload, the payload, and the Fletcher-8 checksum over class to payload. Anything between
// frames — NMEA, noise, the rest of a frame cut short — is skipped and counted. A frame comes out
// whole and unchanged, sync to checksum, because that is what gets logged (§5.3): the scanner
// validates, it never transforms. When a frame fails, the search starts again one byte past its
// sync, so a false sync — 0xB5 0x62 inside a payload, or a length mangled by a lost byte — costs no
// frame that follows it.
//
// The decode of NAV-PVT and NAV-SAT arrives in phase 4 (step 4.2); encode() builds the polls, and
// the configuration messages of config_db.h.
//
// Framework-free by design: nothing under lib/ may include Arduino.h (§6.1), which is what lets
// this be tested on the host against the frames captured in docs/fixtures/.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ubx {

constexpr uint8_t kSync1 = 0xB5, kSync2 = 0x62;
constexpr size_t kHeaderBytes = 6;  // sync, class, id, length
constexpr size_t kChecksumBytes = 2;
// Above any frame the firmware asks for: NAV-SAT is 8 bytes plus 12 per satellite, about 750 with
// every constellation in view. A longer length is taken for a corrupt one.
constexpr uint16_t kMaxPayload = 1024;
constexpr size_t kMaxFrame = kHeaderBytes + kMaxPayload + kChecksumBytes;

// UBX's Fletcher-8 over `n` bytes: two 8-bit running sums, A then B.
void checksum(const uint8_t* data, size_t n, uint8_t& a, uint8_t& b);

// Builds a frame into `out` and returns its size, or 0 when it does not fit in `capacity`.
size_t encode(uint8_t cls, uint8_t id, const uint8_t* payload, uint16_t length, uint8_t* out,
              size_t capacity);

struct Frame {
  uint8_t cls = 0, id = 0;
  uint16_t length = 0;  // of the payload
  const uint8_t* payload = nullptr;
  const uint8_t* bytes = nullptr;  // the whole frame, sync to checksum: what is logged
  size_t size = 0;                 // kHeaderBytes + length + kChecksumBytes
};

struct Stats {
  uint32_t frames = 0;
  uint32_t badChecksum = 0;
  uint32_t oversize = 0;  // a length above kMaxPayload
  uint32_t skipped = 0;   // bytes in no frame: NMEA, noise, the bytes of a failed frame's sync
};

class Scanner {
 public:
  // Takes as many of the `n` bytes as there is room for and returns how many. Draining next() until
  // it returns false always leaves room for at least kMaxFrame more.
  size_t push(const uint8_t* data, size_t n);
  // The next whole frame, or false when the bytes so far hold none yet. The frame points into the
  // scanner and stays valid until the next push() or next().
  bool next(Frame& frame);
  // Forgets the bytes so far, for an exchange that wants only what answers it.
  void reset() { count_ = consumed_ = 0; }

  const Stats& stats() const { return stats_; }
  size_t buffered() const { return count_ - consumed_; }

 private:
  void drop(size_t n);
  void skip(size_t n);  // drop() and count

  uint8_t buf_[2 * kMaxFrame];
  size_t count_ = 0;
  size_t consumed_ = 0;  // the frame next() returned last, dropped at the next call
  Stats stats_;
};

}  // namespace ubx
