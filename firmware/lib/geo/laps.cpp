#include "laps.h"

#include <algorithm>
#include <cmath>

namespace geo {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthM = 6371008.8;

constexpr uint32_t kMaxGapMs = 1000;
constexpr uint32_t kDebounceMs = 5000;
constexpr uint32_t kMovingMmS = 1000;
constexpr uint32_t kLoopMinMs = 20000;
constexpr double kLoopRadiusM = 10;
constexpr int32_t kLoopHeadingE5 = 45 * 100000;
constexpr uint32_t kSmoothMs = 500;
constexpr double kAutoWidthM = 15;
constexpr double kAgreeLength = 0.90;     // the shorter lap at least this much of the longer
constexpr double kAgreeRadiusM = 10;      // an epoch this close to the other lap's path...
constexpr uint32_t kAgreeSharePct = 95;   // ...for this many of them

double radians(int32_t e7) { return e7 * 1e-7 * kPi / 180; }

// b − a, modulo the week: both are times of week.
uint32_t since(uint32_t a, uint32_t b) { return (b + kWeekMs - a) % kWeekMs; }

// The local plane of docs/algorithms.md, around `o`: x east, y north, in metres.
void plane(Point o, double cosPhi0, Point p, double& x, double& y) {
  double dl = radians(p.lonE7) - radians(o.lonE7);
  while (dl > kPi) dl -= 2 * kPi;
  while (dl < -kPi) dl += 2 * kPi;
  x = kEarthM * dl * cosPhi0;
  y = kEarthM * (radians(p.latE7) - radians(o.latE7));
}

}  // namespace

// --- With a start line ---------------------------------------------------------------------

Laps::Laps(const Gate& gate) : gate_(gate) {
  cosPhi0_ = std::cos(radians(gate.centre.latE7));
  double h = gate.headingE5 * 1e-5 * kPi / 180;
  sinH_ = std::sin(h);
  cosH_ = std::cos(h);
}

bool Laps::update(const Epoch& e, Crossing& out) {
  if (!e.takesPart) return false;
  double x, y;
  plane(gate_.centre, cosPhi0_, e.at, x, y);
  double u = x * sinH_ + y * cosH_;  // ahead of the line
  double v = x * cosH_ - y * sinH_;  // along it, to the right
  if (!havePrev_) {
    havePrev_ = true;
    prev_ = e;
    prevX_ = x, prevY_ = y, prevU_ = u, prevV_ = v;
    return false;
  }
  double step = std::hypot(x - prevX_, y - prevY_);
  bool crossed = false;
  uint32_t dt = since(prev_.iTowMs, e.iTowMs);
  if (dt <= kMaxGapMs && prevU_ < 0 && 0 <= u) {
    double f = prevU_ / (prevU_ - u);
    if (std::fabs(prevV_ + f * (v - prevV_)) <= gate_.widthM / 2) {
      double tau = prev_.iTowMs + f * dt;
      uint32_t c = static_cast<uint32_t>(static_cast<int64_t>(std::floor(tau + 0.5)) % kWeekMs);
      if (!started_ || since(last_.iTowMs, c) >= kDebounceMs) {
        crossed = true;
        Crossing k;
        k.iTowMs = c;
        k.tMs = prev_.tMs + since(prev_.iTowMs, c);
        if (started_) {
          k.lap = last_.lap + 1;
          k.lapMs = since(last_.iTowMs, c);
          k.distanceM = acc_ + f * step;
        }
        started_ = true;
        last_ = k;
        acc_ = (1 - f) * step;
        out = k;
      }
    }
  }
  if (!crossed) acc_ += step;
  prev_ = e;
  prevX_ = x, prevY_ = y, prevU_ = u, prevV_ = v;
  return crossed;
}

// --- Flags ---------------------------------------------------------------------------------

double medianM(double* d, size_t n) {
  if (!n) return 0;
  std::sort(d, d + n);
  return n % 2 ? d[n / 2] : (d[n / 2 - 1] + d[n / 2]) / 2;
}

Flag flagOf(double distanceM, double median) {
  if (distanceM < 0.85 * median) return Flag::Cut;
  if (distanceM > 1.30 * median) return Flag::Off;
  return Flag::None;
}

// --- Without a start line ------------------------------------------------------------------

LineFinder::LineFinder(Epoch* trace, size_t capacity) : trace_(trace), capacity_(capacity) {}

void LineFinder::xy(Point p, double& x, double& y) const { plane(origin_, cosPhi0_, p, x, y); }

// 1. The first loop: an earlier moving epoch 20 s back or more, within 10 m, heading within 45°.
// The tests in integers first: on the device the doubles are software's, and a trace that never
// closes holds 6000 epochs.
bool LineFinder::closesLoop(const Epoch& e) const {
  double jx, jy;
  xy(e.at, jx, jy);
  for (size_t k = 0; k + 1 < count_; ++k) {
    const Epoch& c = (*this)[k];
    if (c.speedMmS < kMovingMmS || since(c.iTowMs, e.iTowMs) < kLoopMinMs) continue;
    int32_t dh = std::abs(e.headingE5 - c.headingE5) % 36000000;
    dh = std::min(dh, 36000000 - dh);
    if (dh > kLoopHeadingE5) continue;
    double cx, cy;
    xy(c.at, cx, cy);
    if (std::hypot(jx - cx, jy - cy) <= kLoopRadiusM) return true;
  }
  return false;
}

// At least 95 % of the epochs [p0, p1) within 10 m of the path of [q0, q1): the segments between
// its consecutive epochs. Any segment that close will do, so each epoch looks from the segment at
// the same share of the other lap outwards, and stops at the first; and the count stops once the
// epochs too far are over the 5 %. The answer is the document's, at a fraction of the cost.
bool LineFinder::covered(uint32_t p0, uint32_t p1, uint32_t q0, uint32_t q1) const {
  if (q1 - q0 < 2 || p1 == p0) return false;
  const uint32_t n = p1 - p0, segments = q1 - q0 - 1;
  uint32_t far = 0;
  for (uint32_t p = p0; p < p1; ++p) {
    double px, py;
    xy(bySeq(p).at, px, py);
    uint32_t guess = static_cast<uint32_t>(static_cast<uint64_t>(p - p0) * segments / n);
    bool near = false;
    for (uint32_t step = 0; !near && step <= segments; ++step) {
      for (int side = 0; side < 2 && !near; ++side) {
        if (step == 0 && side) break;
        int64_t s = side ? static_cast<int64_t>(guess) - step : static_cast<int64_t>(guess) + step;
        if (s < 0 || s >= segments) continue;
        double ax, ay, bx, by;
        xy(bySeq(q0 + static_cast<uint32_t>(s)).at, ax, ay);
        xy(bySeq(q0 + static_cast<uint32_t>(s) + 1).at, bx, by);
        double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy, t = 0;
        if (l2 > 0) t = std::max(0.0, std::min(1.0, ((px - ax) * dx + (py - ay) * dy) / l2));
        double ex = px - (ax + t * dx), ey = py - (ay + t * dy);
        near = ex * ex + ey * ey <= kAgreeRadiusM * kAgreeRadiusM;
      }
    }
    if (!near && ++far * 100 > (100 - kAgreeSharePct) * n) return false;
  }
  return true;
}

// 3. Two laps agree: the shorter at least 90 % of the longer, and each covered by the other.
bool LineFinder::agree(uint32_t a0, uint32_t a1, double da, uint32_t b1, double db) const {
  return std::min(da, db) >= kAgreeLength * std::max(da, db) && covered(a0, a1, a1, b1) &&
         covered(a1, b1, a0, a1);
}

bool LineFinder::lapEnded(uint32_t seq, const Crossing& c, Gate& out) {
  if (endsKept_ == 3) {
    ends_[0] = ends_[1], ends_[1] = ends_[2];
    --endsKept_;
  }
  ends_[endsKept_++] = seq;
  if (c.lap) distances_[0] = distances_[1], distances_[1] = c.distanceM;
  if (endsKept_ < 3 || c.lap < 2) return false;
  uint32_t oldest = seq_ - static_cast<uint32_t>(count_);
  if (ends_[0] < oldest) return false;  // the earlier lap is no longer in the trace
  if (!agree(ends_[0], ends_[1], distances_[0], ends_[2], distances_[1])) return false;

  // 4. Their fastest point, on the speed averaged over the trace's epochs within 500 ms either
  // side, up to the epoch that completed the second lap: in whole mm/s, then the one division.
  uint32_t fastest = ends_[0];
  double top = -1;
  for (uint32_t k = ends_[0]; k < ends_[2]; ++k) {
    uint32_t t = bySeq(k).iTowMs;
    uint64_t sum = 0;
    uint32_t n = 0;
    for (uint32_t m = k + 1; m-- > oldest;) {  // back from k...
      const Epoch& e = bySeq(m);
      if (since(e.iTowMs, t) > kSmoothMs) break;
      sum += e.speedMmS;
      ++n;
    }
    for (uint32_t m = k + 1; m <= ends_[2]; ++m) {  // ...and on to the second lap's end
      const Epoch& e = bySeq(m);
      if (since(t, e.iTowMs) > kSmoothMs) break;
      sum += e.speedMmS;
      ++n;
    }
    double mean = static_cast<double>(sum) / n;
    if (mean > top) {
      top = mean;
      fastest = k;
    }
  }

  // 5. The gate there.
  const Epoch& k = bySeq(fastest);
  out = Gate();
  out.centre = k.at;
  out.headingE5 = k.headingE5;
  out.widthM = kAutoWidthM;
  out.source = LineSource::Auto;
  found_ = true;
  return true;
}

bool LineFinder::update(const Epoch& e, Gate& out) {
  if (!e.takesPart || !capacity_ || found_) return false;
  if (!haveOrigin_) {  // the plane around the trace's first epoch, for good
    haveOrigin_ = true;
    origin_ = e.at;
    cosPhi0_ = std::cos(radians(origin_.latE7));
  }
  if (count_ == capacity_) {  // the oldest goes
    head_ = (head_ + 1) % capacity_;
    --count_;
  }
  trace_[(head_ + count_) % capacity_] = e;
  ++count_;
  uint32_t seq = seq_++;
  Crossing c;
  if (closed_) return provisional_.update(e, c) && lapEnded(seq, c, out);
  if (e.speedMmS < kMovingMmS || !closesLoop(e)) return false;

  // 2. A provisional gate at the loop's end, and its laps over the trace so far.
  closed_ = true;
  Gate g;
  g.centre = e.at;
  g.headingE5 = e.headingE5;
  g.widthM = kAutoWidthM;
  provisional_ = Laps(g);
  for (uint32_t s = seq_ - static_cast<uint32_t>(count_); s < seq_; ++s) {
    if (provisional_.update(bySeq(s), c) && lapEnded(s, c, out)) return true;
  }
  return false;
}

}  // namespace geo
