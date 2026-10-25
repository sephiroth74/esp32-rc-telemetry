// Laps — PLAN-ESP32.md §7.5, docs/algorithms.md "Laps", ROADMAP.md steps 8.1–8.3.
//
// A start line is a gate: the session's epochs are fed in order, and each forward crossing within
// its width, interpolated between the two fixes either side, ends a lap. Without a start line, the
// first loop the trace closes gives one, at its fastest point; the laps so far are then found again
// from the trace kept. A lap's distance flags it against the median of the session's: `cut` under
// 85 %, `off` over 130 %. The rules, to the detail two implementations need to agree on, are
// docs/algorithms.md's, and docs/fixtures/laps-v1.json holds a session worked out by a third.
//
// Framework-free (§6.1), and mirrored by packages/rct_core in Dart.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "geo.h"

namespace geo {

constexpr uint32_t kWeekMs = 604800000;  // a GPS time of week wraps here

// One NAV-PVT epoch, as the laps read it.
struct Epoch {
  uint32_t iTowMs = 0;
  uint32_t tMs = 0;  // the session time of its record
  Point at;
  uint32_t speedMmS = 0;   // gSpeed
  int32_t headingE5 = 0;   // headMot: degrees × 1e5, clockwise from north
  bool takesPart = false;  // gnssFixOK with a 3D fix; any other is passed over
};

enum class LineSource : uint8_t { Auto = 0, Device = 1, Desktop = 2 };  // the Lap record's `line`

struct Gate {
  Point centre;
  int32_t headingE5 = 0;  // the direction of travel across it
  double widthM = 15;
  LineSource source = LineSource::Auto;
};

// A crossing that counts, and the lap it ends.
struct Crossing {
  uint16_t lap = 0;  // the lap it ends: 0 for the first crossing, which ends none
  uint32_t iTowMs = 0;
  uint32_t tMs = 0;
  uint32_t lapMs = 0;    // 0 for the first
  double distanceM = 0;  // the lap's; 0 for the first
};

class Laps {
 public:
  explicit Laps(const Gate& gate);

  // Every epoch of the session, in order. True when it completed a crossing that counts, in `out`.
  bool update(const Epoch& e, Crossing& out);
  const Gate& gate() const { return gate_; }
  uint16_t completed() const { return started_ ? last_.lap : 0; }

 private:
  Gate gate_;
  double cosPhi0_, sinH_, cosH_;
  bool havePrev_ = false, started_ = false;
  Epoch prev_;
  double prevX_ = 0, prevY_ = 0, prevU_ = 0, prevV_ = 0;
  double acc_ = 0;  // the path since the last crossing counted
  Crossing last_;
};

enum class Flag : uint8_t { None = 0, Cut = 1, Off = 2 };  // the Lap record's flag bits

// The median of `n` lap distances, sorting them in place; of an even count, the mean of the two in
// the middle; 0 with none.
double medianM(double* distances, size_t n);
Flag flagOf(double distanceM, double medianM);

// Without a start line: the first loop the trace closes gives a provisional gate, which cuts the
// trace into laps; the first two consecutive laps that agree, in length and in path, give the
// gate, where the car was fastest in them.
class LineFinder {
 public:
  // Room for `capacity` epochs of trace, the caller's — PSRAM on the device. When it is full, the
  // oldest goes.
  LineFinder(Epoch* trace, size_t capacity);

  // Every epoch of the session, in order. True once, when two laps agree, with the gate in `out`.
  bool update(const Epoch& e, Gate& out);
  bool found() const { return found_; }
  bool closed() const { return closed_; }             // the first loop: a provisional gate
  const Gate& provisional() const { return provisional_.gate(); }

  // The trace kept, oldest first: every epoch that took part, to find the laps so far again.
  size_t size() const { return count_; }
  const Epoch& operator[](size_t i) const { return trace_[(head_ + i) % capacity_]; }

 private:
  // An epoch by its number since the session's start, while the trace still holds it.
  const Epoch& bySeq(uint32_t seq) const { return (*this)[seq - (seq_ - count_)]; }
  void xy(Point p, double& x, double& y) const;
  bool closesLoop(const Epoch& e) const;
  // A provisional lap ended on epoch `seq`: true when it and the one before agree, the gate in
  // `out`.
  bool lapEnded(uint32_t seq, const Crossing& c, Gate& out);
  bool agree(uint32_t a0, uint32_t a1, double da, uint32_t b1, double db) const;
  bool covered(uint32_t p0, uint32_t p1, uint32_t q0, uint32_t q1) const;

  Epoch* trace_;
  size_t capacity_, head_ = 0, count_ = 0;
  uint32_t seq_ = 0;  // epochs ever kept
  Point origin_;
  double cosPhi0_ = 1;
  bool haveOrigin_ = false, closed_ = false, found_ = false;
  Laps provisional_{Gate()};
  uint32_t ends_[3] = {};  // the epochs that completed the last three provisional crossings
  double distances_[2] = {};  // the last two provisional laps'
  uint8_t endsKept_ = 0;
};

}  // namespace geo
