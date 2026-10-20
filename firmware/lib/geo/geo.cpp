#include "geo.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace geo {
namespace {

constexpr double kEarthRadiusM = 6371008.8;  // the mean radius
constexpr double kPi = 3.14159265358979323846;
constexpr int64_t kMaxFenceSpanE7 = 100000000;  // 10°

double radians(int32_t e7) { return e7 * 1e-7 * kPi / 180.0; }

bool better(const Track& candidate, const Track* chosen) {
  return !chosen || candidate.lastUsed > chosen->lastUsed;  // a tie keeps the first
}

}  // namespace

double distanceM(Point a, Point b) {
  double phi1 = radians(a.latE7), phi2 = radians(b.latE7);
  double dLambda = radians(b.lonE7) - radians(a.lonE7);
  if (dLambda > kPi) dLambda -= 2 * kPi;
  if (dLambda < -kPi) dLambda += 2 * kPi;
  double x = dLambda * cos((phi1 + phi2) / 2);
  double y = phi2 - phi1;
  return kEarthRadiusM * sqrt(x * x + y * y);
}

bool contains(const Point* fence, size_t n, Point p) {
  if (!fence || n < 3) return false;
  int64_t minLat = fence[0].latE7, maxLat = minLat, minLon = fence[0].lonE7, maxLon = minLon;
  for (size_t i = 1; i < n; ++i) {
    if (fence[i].latE7 < minLat) minLat = fence[i].latE7;
    if (fence[i].latE7 > maxLat) maxLat = fence[i].latE7;
    if (fence[i].lonE7 < minLon) minLon = fence[i].lonE7;
    if (fence[i].lonE7 > maxLon) maxLon = fence[i].lonE7;
  }
  if (maxLat - minLat > kMaxFenceSpanE7 || maxLon - minLon > kMaxFenceSpanE7) return false;
  // Relative to the point: the point outside the box is outside, and inside it every difference
  // is within the span, so the products below stay far inside 64 bits.
  if (p.latE7 < minLat || p.latE7 > maxLat || p.lonE7 < minLon || p.lonE7 > maxLon) return false;

  bool inside = false;
  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    int64_t xi = int64_t(fence[i].lonE7) - p.lonE7, yi = int64_t(fence[i].latE7) - p.latE7;
    int64_t xj = int64_t(fence[j].lonE7) - p.lonE7, yj = int64_t(fence[j].latE7) - p.latE7;
    if ((yi > 0) == (yj > 0)) continue;  // the edge does not cross the ray's line
    // Whether the crossing lies right of the point: 0 < xi + (0 − yi)(xj − xi)/(yj − yi).
    int64_t lhs = -xi * (yj - yi), rhs = -yi * (xj - xi);
    if (yj > yi ? lhs < rhs : lhs > rhs) inside = !inside;
  }
  return inside;
}

void cellId(Point p, char (&out)[kCellIdMax + 1]) {
  uint32_t lat = p.latE7 < 0 ? uint32_t(-int64_t(p.latE7)) : uint32_t(p.latE7);
  uint32_t lon = p.lonE7 < 0 ? uint32_t(-int64_t(p.lonE7)) : uint32_t(p.lonE7);
  snprintf(out, sizeof out, "%c%02lu%02lu%c%03lu%02lu", p.latE7 < 0 ? 'S' : 'N',
           (unsigned long)(lat / 10000000), (unsigned long)(lat % 10000000 / 100000),
           p.lonE7 < 0 ? 'W' : 'E', (unsigned long)(lon / 10000000),
           (unsigned long)(lon % 10000000 / 100000));
}

Resolution resolve(const Track* tracks, size_t n, Point fix, uint32_t radiusM) {
  const Track* chosen = nullptr;
  Resolution r;
  for (size_t i = 0; i < n; ++i) {
    const Track& t = tracks[i];
    if (t.fenceCount && contains(t.fence, t.fenceCount, fix) && better(t, chosen)) {
      chosen = &t;
      r = {int(i), Match::Fence};
    }
  }
  if (chosen) return r;
  for (size_t i = 0; i < n; ++i) {
    const Track& t = tracks[i];
    if (t.fenceCount) continue;  // a fenced track is its fence's, and nothing else's
    Point ref = t.hasStartLine ? t.startLine : t.centre;
    if (distanceM(ref, fix) <= radiusM && better(t, chosen)) {
      chosen = &t;
      r = {int(i), Match::Radius};
    }
  }
  return r;
}

bool newTrackId(Point fix, const char* const* existing, size_t n, char (&out)[kTrackIdMax + 1]) {
  char cell[kCellIdMax + 1];
  cellId(fix, cell);
  for (int suffix = 1; suffix <= 99; ++suffix) {
    if (suffix == 1) {
      snprintf(out, sizeof out, "%s", cell);
    } else {
      snprintf(out, sizeof out, "%s-%d", cell, suffix);
    }
    bool taken = false;
    for (size_t i = 0; i < n && !taken; ++i) taken = strcmp(existing[i], out) == 0;
    if (!taken) return true;
  }
  out[0] = '\0';
  return false;
}

bool sessionNumber(const char* name, uint32_t& out) {
  static const char kPrefix[] = "session_";
  size_t prefix = sizeof kPrefix - 1;
  if (strncmp(name, kPrefix, prefix) != 0 || !name[prefix]) return false;
  uint64_t v = 0;
  for (const char* c = name + prefix; *c; ++c) {
    if (*c < '0' || *c > '9') return false;
    v = v * 10 + (*c - '0');
    if (v > 0xFFFFFFFFull) return false;
  }
  out = static_cast<uint32_t>(v);
  return true;
}

uint32_t nextSession(const char* const* names, size_t n) {
  uint32_t highest = 0;
  for (size_t i = 0; i < n; ++i) {
    uint32_t v;
    if (sessionNumber(names[i], v) && v > highest) highest = v;
  }
  return highest + 1;
}

void sessionName(uint32_t number, char (&out)[kSessionNameMax + 1]) {
  snprintf(out, sizeof out, "session_%03lu", (unsigned long)number);
}

}  // namespace geo
