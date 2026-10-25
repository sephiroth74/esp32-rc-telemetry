// Track and lap geometry — PLAN-ESP32.md §7.1, docs/algorithms.md.
//
// The tracks and sessions on the card (ROADMAP.md step 5.4): the ~1 km cell id that names a track's
// folder, the distance and the geo-fence test that recognise a track from a fix, the choice among
// several, the name of a new one, and the session numbers within it. The rules, to the detail, are
// docs/algorithms.md's. The laps are laps.h's; the s/d projection and sectors are still to come.
//
// Shaped around u-blox's native integer degrees × 1e-7, so no floating point enters the path between
// a fix and a folder name; only the distance, a comparison against a radius, uses it.
//
// Framework-free (§6.1), and mirrored by packages/rct_core in Dart: both are checked against the
// same rules and fixtures, because the phone shows a lap time the desktop app then has to reproduce.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace geo {

struct Point {
  int32_t latE7 = 0;
  int32_t lonE7 = 0;
};

// Metres, by the equirectangular approximation: for points a few hundred metres apart.
double distanceM(Point a, Point b);

// The even-odd test on the fence's `n` vertices, exact in integers. False for fewer than three
// vertices, or a fence spanning more than 10° either way.
bool contains(const Point* fence, size_t n, Point p);

constexpr size_t kCellIdMax = 11;   // N4512E00934
constexpr size_t kTrackIdMax = 14;  // N4512E00934-99
void cellId(Point p, char (&out)[kCellIdMax + 1]);

struct Track {
  const char* id = "";  // the folder's name
  const Point* fence = nullptr;
  size_t fenceCount = 0;  // 0: no fence
  bool hasStartLine = false;
  Point startLine;
  Point centre;
  uint32_t lastUsed = 0;  // larger is more recent
};

enum class Match : uint8_t { None, Fence, Radius };
struct Resolution {
  int index = -1;  // into the tracks given; -1 for none
  Match by = Match::None;
};

// Which of `n` tracks a fix belongs to (§7.1, step 2).
Resolution resolve(const Track* tracks, size_t n, Point fix, uint32_t radiusM);

// A new track's folder at `fix`: the cell id if none of the `n` names has it, else the cell id with
// the first free suffix, -2 to -99. False when all are taken.
bool newTrackId(Point fix, const char* const* existing, size_t n, char (&out)[kTrackIdMax + 1]);

// --- Sessions within a track ----------------------------------------------------------

constexpr size_t kSessionNameMax = 18;  // session_ and up to ten digits

// The number of a folder named session_ followed by digits only.
bool sessionNumber(const char* name, uint32_t& out);
// The highest number among the `n` names plus one, or 1 with none.
uint32_t nextSession(const char* const* names, size_t n);
// session_001 … session_999, then session_1000.
void sessionName(uint32_t number, char (&out)[kSessionNameMax + 1]);

}  // namespace geo
