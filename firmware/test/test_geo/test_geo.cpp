// Host tests for lib/geo — run with `make test`.
//
// The tracks and sessions on the card, by the rules of docs/algorithms.md (PLAN-ESP32.md §7.1):
// cell ids, distances, the geo-fence test, which track a fix belongs to, a new track's name and the
// session numbers. The laps are test_laps's.
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <math.h>

#include "geo.h"

using geo::Point;

// Unity is built without its double assertions; distances need more digits than a float holds.
#define ASSERT_METRES(tolerance, expected, actual)                                         \
  do {                                                                                     \
    double got_ = (actual);                                                                \
    char msg_[64];                                                                         \
    snprintf(msg_, sizeof msg_, "expected %.3f m, got %.3f m", (double)(expected), got_); \
    TEST_ASSERT_TRUE_MESSAGE(fabs(got_ - (expected)) <= (tolerance), msg_);              \
  } while (0)

void setUp() {}
void tearDown() {}

static Point at(double lat, double lon) {
  // Rounded to the nearest 1e-7, as a literal in the tests reads.
  return {static_cast<int32_t>(lat * 1e7 + (lat < 0 ? -0.5 : 0.5)),
          static_cast<int32_t>(lon * 1e7 + (lon < 0 ? -0.5 : 0.5))};
}

static void expectCell(const char* want, Point p) {
  char id[geo::kCellIdMax + 1];
  geo::cellId(p, id);
  TEST_ASSERT_EQUAL_STRING(want, id);
}

// --- Cell ids ------------------------------------------------------------------------

static void the_cell_id_of_the_plan() {
  expectCell("N4512E00934", at(45.1234, 9.3456));  // §7.1's own example
}

static void cell_ids_truncate_and_mirror_about_zero() {
  expectCell("N4512E00934", {451299999, 93499999});  // 45.1299999 is still 45.12
  expectCell("S4512W00934", {-451234000, -93456000});
  expectCell("N0000E00000", {0, 0});
  expectCell("S0000W00000", {-100, -100});  // −0.00001: south and west, of nothing
  expectCell("N9000E18000", {900000000, 1800000000});
  expectCell("S8999W17999", {-899999999, -1799999999});  // the widest there are
}

// --- Distance and fences ----------------------------------------------------------------

static void distances_on_the_scale_of_a_track() {
  ASSERT_METRES(0.01, 111195.08, geo::distanceM(at(45, 9), at(46, 9)));  // 1° north
  // A hundredth of a degree east at 45°: the cell's width, 0.79 km (§7.1).
  ASSERT_METRES(0.5, 786.3, geo::distanceM(at(45, 9), at(45, 9.01)));
  ASSERT_METRES(1e-9, 0, geo::distanceM(at(45, 9), at(45, 9)));
  // Across the antimeridian, the short way round.
  ASSERT_METRES(0.5, 155.7, geo::distanceM(at(0, 179.9993), at(0, -179.9993)));
}

// A square of about 220 m around a track, and an L of six vertices, concave at the corner.
static const Point kSquare[] = {{451220000, 93440000}, {451220000, 93470000},
                                {451240000, 93470000}, {451240000, 93440000}};
static const Point kEll[] = {{0, 0}, {0, 30000}, {10000, 30000},
                             {10000, 10000}, {30000, 10000}, {30000, 0}};

static void inside_and_outside_a_fence() {
  TEST_ASSERT_TRUE(geo::contains(kSquare, 4, at(45.123, 9.3456)));
  TEST_ASSERT_FALSE(geo::contains(kSquare, 4, at(45.125, 9.3456)));  // just north
  TEST_ASSERT_FALSE(geo::contains(kSquare, 4, at(45.123, 9.3480)));  // just east
  TEST_ASSERT_TRUE(geo::contains(kEll, 6, {5000, 25000}));   // the long arm
  TEST_ASSERT_TRUE(geo::contains(kEll, 6, {25000, 5000}));   // the short one
  TEST_ASSERT_FALSE(geo::contains(kEll, 6, {20000, 20000}));  // the notch between them
  // The ray passes through a vertex level with the point: counted once, not twice.
  TEST_ASSERT_TRUE(geo::contains(kEll, 6, {10000, 5000}));
  TEST_ASSERT_FALSE(geo::contains(kEll, 6, {10000, 40000}));
}

static void a_fence_that_cannot_be_one_contains_nothing() {
  TEST_ASSERT_FALSE(geo::contains(kSquare, 2, at(45.123, 9.3456)));
  TEST_ASSERT_FALSE(geo::contains(nullptr, 0, at(45.123, 9.3456)));
  const Point huge[] = {{0, 0}, {0, 110000000}, {10000000, 110000000}, {10000000, 0}};  // 11° wide
  TEST_ASSERT_FALSE(geo::contains(huge, 4, {5000000, 50000000}));
  // The widest allowed, with coordinates at their extremes: no overflow in the products.
  const Point wide[] = {{800000000, 1700000000}, {800000000, 1799999999},
                        {899999999, 1799999999}, {899999999, 1700000000}};
  TEST_ASSERT_TRUE(geo::contains(wide, 4, {850000000, 1750000000}));
}

// --- Which track -----------------------------------------------------------------------

static geo::Track track(const char* id, Point centre, uint32_t lastUsed) {
  geo::Track t;
  t.id = id;
  t.centre = centre;
  t.lastUsed = lastUsed;
  return t;
}

static void a_fix_near_a_track_finds_it() {
  geo::Track tracks[] = {track("N4512E00934", at(45.1230, 9.3456), 5),
                         track("N4520E00910", at(45.2000, 9.1000), 9)};
  geo::Resolution r = geo::resolve(tracks, 2, at(45.1240, 9.3470), 300);  // ~150 m away
  TEST_ASSERT_EQUAL_INT(0, r.index);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(geo::Match::Radius), static_cast<int>(r.by));
  r = geo::resolve(tracks, 2, at(45.1300, 9.3456), 300);  // 780 m: a new venue
  TEST_ASSERT_EQUAL_INT(-1, r.index);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(geo::Match::None), static_cast<int>(r.by));
  TEST_ASSERT_EQUAL_INT(-1, geo::resolve(tracks, 0, at(45.1240, 9.3470), 300).index);
}

static void the_start_line_counts_before_the_centre() {
  // The centre where the first fix landed, in the car park; the line 400 m away, on the track.
  geo::Track t = track("N4512E00934", at(45.1200, 9.3456), 1);
  geo::Track tracks[] = {t};
  Point onTrack = at(45.1236, 9.3456);
  TEST_ASSERT_EQUAL_INT(-1, geo::resolve(tracks, 1, onTrack, 300).index);
  tracks[0].hasStartLine = true;
  tracks[0].startLine = at(45.1235, 9.3456);
  TEST_ASSERT_EQUAL_INT(0, geo::resolve(tracks, 1, onTrack, 300).index);
}

static void among_several_the_one_used_last() {
  geo::Track tracks[] = {track("N4512E00934", at(45.1230, 9.3456), 7),
                         track("N4512E00934-2", at(45.1231, 9.3457), 12),  // a new layout
                         track("N4512E00934-3", at(45.1232, 9.3458), 3)};
  TEST_ASSERT_EQUAL_INT(1, geo::resolve(tracks, 3, at(45.1231, 9.3456), 300).index);
  tracks[2].lastUsed = 12;  // a tie: the first listed
  TEST_ASSERT_EQUAL_INT(1, geo::resolve(tracks, 3, at(45.1231, 9.3456), 300).index);
}

static void a_fence_decides_and_only_for_its_own() {
  // Two clubs 250 m apart; only the first has a fence.
  geo::Track fenced = track("N4512E00934", at(45.1230, 9.3455), 1);
  fenced.fence = kSquare;
  fenced.fenceCount = 4;
  geo::Track other = track("N4512E00937", at(45.1230, 9.3770), 9);  // far: matches nothing here
  geo::Track tracks[] = {other, fenced};
  // Inside the fence: the fenced track, though another lies within the radius and was used later.
  geo::Track near = track("N4512E00935", at(45.1235, 9.3460), 20);
  geo::Track three[] = {near, fenced};
  geo::Resolution r = geo::resolve(three, 2, at(45.1230, 9.3456), 300);
  TEST_ASSERT_EQUAL_INT(1, r.index);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(geo::Match::Fence), static_cast<int>(r.by));
  // At the unfenced club next door, 250 m east of the fenced one's centre: not the fenced one,
  // though its centre is within the radius.
  Point nextDoor = at(45.1230, 9.3487);
  TEST_ASSERT_FALSE(geo::contains(kSquare, 4, nextDoor));
  TEST_ASSERT_TRUE(geo::distanceM(fenced.centre, nextDoor) < 300);
  TEST_ASSERT_EQUAL_INT(-1, geo::resolve(tracks, 2, nextDoor, 300).index);
}

// --- A new track's name ------------------------------------------------------------------

static void a_new_track_takes_the_cell_or_the_next_free_suffix() {
  char id[geo::kTrackIdMax + 1];
  Point fix = at(45.1234, 9.3456);
  TEST_ASSERT_TRUE(geo::newTrackId(fix, nullptr, 0, id));
  TEST_ASSERT_EQUAL_STRING("N4512E00934", id);
  const char* taken[] = {"N4520E00910", "N4512E00934", "N4512E00934-3"};
  TEST_ASSERT_TRUE(geo::newTrackId(fix, taken, 3, id));
  TEST_ASSERT_EQUAL_STRING("N4512E00934-2", id);  // the first free, not the next after the last
  const char* two[] = {"N4512E00934", "N4512E00934-2"};
  TEST_ASSERT_TRUE(geo::newTrackId(fix, two, 2, id));
  TEST_ASSERT_EQUAL_STRING("N4512E00934-3", id);
}

static void ninety_nine_tracks_in_a_cell_are_all_there_can_be() {
  static char names[99][geo::kTrackIdMax + 1];
  const char* all[99];
  strcpy(names[0], "N4512E00934");
  for (int i = 1; i < 99; ++i) snprintf(names[i], sizeof names[i], "N4512E00934-%d", i + 1);
  for (int i = 0; i < 99; ++i) all[i] = names[i];
  char id[geo::kTrackIdMax + 1];
  TEST_ASSERT_FALSE(geo::newTrackId(at(45.1234, 9.3456), all, 99, id));
  TEST_ASSERT_TRUE(geo::newTrackId(at(45.1234, 9.3456), all, 98, id));
  TEST_ASSERT_EQUAL_STRING("N4512E00934-99", id);  // the longest id: 14 characters
}

// --- Session numbers -----------------------------------------------------------------------

static void sessions_follow_the_highest_number() {
  const char* none[] = {"track.json", "start_line.json"};
  TEST_ASSERT_EQUAL_UINT32(1, geo::nextSession(none, 2));
  const char* some[] = {"session_001", "track.json", "session_007", "session_003"};
  TEST_ASSERT_EQUAL_UINT32(8, geo::nextSession(some, 4));  // a gap is not filled
  const char* odd[] = {"session_", "session_12a", "session_0x9", "Session_050", "session_004"};
  TEST_ASSERT_EQUAL_UINT32(5, geo::nextSession(odd, 5));  // only session_ and digits count
}

static void session_names_widen_rather_than_wrap() {
  char name[geo::kSessionNameMax + 1];
  geo::sessionName(1, name);
  TEST_ASSERT_EQUAL_STRING("session_001", name);
  geo::sessionName(999, name);
  TEST_ASSERT_EQUAL_STRING("session_999", name);
  geo::sessionName(1000, name);
  TEST_ASSERT_EQUAL_STRING("session_1000", name);
  const char* past[] = {"session_999", "session_1000"};
  TEST_ASSERT_EQUAL_UINT32(1001, geo::nextSession(past, 2));
  uint32_t v;
  TEST_ASSERT_TRUE(geo::sessionNumber("session_4294967295", v));
  TEST_ASSERT_EQUAL_UINT32(4294967295u, v);
  TEST_ASSERT_FALSE(geo::sessionNumber("session_4294967296", v));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(the_cell_id_of_the_plan);
  RUN_TEST(cell_ids_truncate_and_mirror_about_zero);
  RUN_TEST(distances_on_the_scale_of_a_track);
  RUN_TEST(inside_and_outside_a_fence);
  RUN_TEST(a_fence_that_cannot_be_one_contains_nothing);
  RUN_TEST(a_fix_near_a_track_finds_it);
  RUN_TEST(the_start_line_counts_before_the_centre);
  RUN_TEST(among_several_the_one_used_last);
  RUN_TEST(a_fence_decides_and_only_for_its_own);
  RUN_TEST(a_new_track_takes_the_cell_or_the_next_free_suffix);
  RUN_TEST(ninety_nine_tracks_in_a_cell_are_all_there_can_be);
  RUN_TEST(sessions_follow_the_highest_number);
  RUN_TEST(session_names_widen_rather_than_wrap);
  return UNITY_END();
}
