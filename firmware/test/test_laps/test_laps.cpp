// Host tests for the laps of lib/geo — run with `make test`.
//
// Against docs/fixtures/laps-v1.json, a simulated session whose laps a third implementation worked
// out from docs/algorithms.md: with a start line, a run off the track, a cut, the line missed wide,
// fixes lost across it and a car going back over it; without one, the first laps disagreeing, the
// gate where the first two that agree are fastest, and the laps from it. Then each rule on its own,
// on hand-made epochs: a straight line, and laps of a stadium.
#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <string>
#include <vector>

#include "laps.h"

#ifndef FIXTURES_DIR
#error "FIXTURES_DIR comes from the native_test environment of platformio.ini"
#endif

using geo::Crossing;
using geo::Epoch;
using geo::Gate;
using geo::Laps;
using geo::LineFinder;

void setUp() {}
void tearDown() {}

#define ASSERT_METRES(tolerance, expected, actual)                                         \
  do {                                                                                     \
    double got_ = (actual);                                                                \
    char msg_[64];                                                                         \
    snprintf(msg_, sizeof msg_, "expected %.6f m, got %.6f m", (double)(expected), got_); \
    TEST_ASSERT_TRUE_MESSAGE(fabs(got_ - (expected)) <= (tolerance), msg_);              \
  } while (0)

// --- The fixture ---------------------------------------------------------------------------

static JsonDocument& fixture() {
  static JsonDocument doc = [] {
    JsonDocument d;
    char path[256];
    snprintf(path, sizeof path, "%s/laps-v1.json", FIXTURES_DIR);
    FILE* f = fopen(path, "r");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    std::string text;
    char buf[4096];
    for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
    fclose(f);
    TEST_ASSERT_TRUE(deserializeJson(d, text) == DeserializationError::Ok);
    return d;
  }();
  return doc;
}

static std::vector<Epoch> epochs() {
  std::vector<Epoch> out;
  for (JsonArrayConst e : fixture()["epochs"].as<JsonArrayConst>()) {
    Epoch x;
    x.iTowMs = e[0];
    x.tMs = e[1];
    x.at = {e[2].as<int32_t>(), e[3].as<int32_t>()};
    x.speedMmS = e[4];
    x.headingE5 = e[5];
    x.takesPart = e[6].as<int>() != 0;
    out.push_back(x);
  }
  return out;
}

static Gate gateOf(JsonObjectConst g) {
  Gate out;
  out.centre = {g["lat_e7"].as<int32_t>(), g["lon_e7"].as<int32_t>()};
  out.headingE5 = g["heading_e5"];
  out.widthM = g["width_m"];
  return out;
}

static std::vector<Crossing> lapsOf(const Gate& gate, const std::vector<Epoch>& es) {
  Laps laps(gate);
  std::vector<Crossing> out;
  Crossing c;
  for (const Epoch& e : es) {
    if (laps.update(e, c)) out.push_back(c);
  }
  return out;
}

static void expectLaps(JsonArrayConst want, const std::vector<Crossing>& got) {
  TEST_ASSERT_EQUAL_UINT(want.size(), got.size());
  for (size_t i = 0; i < got.size(); ++i) {
    JsonObjectConst w = want[i];
    TEST_ASSERT_EQUAL_UINT16(w["lap"].as<uint16_t>(), got[i].lap);
    TEST_ASSERT_EQUAL_UINT32(w["itow"].as<uint32_t>(), got[i].iTowMs);
    TEST_ASSERT_EQUAL_UINT32(w["t"].as<uint32_t>(), got[i].tMs);
    TEST_ASSERT_EQUAL_UINT32(w["lap_ms"].as<uint32_t>(), got[i].lapMs);
    ASSERT_METRES(1e-6, w["distance_m"].as<double>(), got[i].distanceM);
  }
}

static void expectFlags(JsonArrayConst want, const std::vector<Crossing>& laps) {
  std::vector<double> d;
  for (const Crossing& c : laps) {
    if (c.lap) d.push_back(c.distanceM);
  }
  std::vector<double> sorted = d;
  double median = geo::medianM(sorted.data(), sorted.size());
  TEST_ASSERT_EQUAL_UINT(want.size(), d.size());
  for (size_t i = 0; i < d.size(); ++i) {
    const char* w = want[i];
    geo::Flag expected = !strcmp(w, "cut")   ? geo::Flag::Cut
                         : !strcmp(w, "off") ? geo::Flag::Off
                                             : geo::Flag::None;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(geo::flagOf(d[i], median)));
  }
}

static void the_fixture_s_laps_with_its_line() {
  std::vector<Crossing> got = lapsOf(gateOf(fixture()["gate"]), epochs());
  expectLaps(fixture()["laps"], got);
  expectFlags(fixture()["flags"], got);
}

// Without a line: the first laps disagree — off, then a cut — and the gate comes on the epoch the
// document says, where it says; the laps found again from the trace, then on as the epochs come,
// are the fixture's.
static void the_fixture_s_laps_without_a_line() {
  std::vector<Epoch> es = epochs();
  std::vector<Epoch> room(6000);
  LineFinder finder(room.data(), room.size());
  Gate gate;
  size_t foundAt = 0;
  for (; foundAt < es.size(); ++foundAt) {
    if (finder.update(es[foundAt], gate)) break;
  }
  JsonObjectConst want = fixture()["auto"];
  TEST_ASSERT_EQUAL_UINT(want["found_at"].as<size_t>(), foundAt);
  Gate expected = gateOf(want["gate"]);
  TEST_ASSERT_EQUAL_INT32(expected.centre.latE7, gate.centre.latE7);
  TEST_ASSERT_EQUAL_INT32(expected.centre.lonE7, gate.centre.lonE7);
  TEST_ASSERT_EQUAL_INT32(expected.headingE5, gate.headingE5);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(geo::LineSource::Auto), static_cast<int>(gate.source));

  Laps laps(gate);
  std::vector<Crossing> got;
  Crossing c;
  for (size_t i = 0; i < finder.size(); ++i) {
    if (laps.update(finder[i], c)) got.push_back(c);
  }
  for (size_t i = foundAt + 1; i < es.size(); ++i) {
    if (laps.update(es[i], c)) got.push_back(c);
  }
  expectLaps(want["laps"], got);
  expectFlags(want["flags"], got);
  Gate again;
  TEST_ASSERT_FALSE(finder.update(es.back(), again));  // once
}

// --- The rules, one by one --------------------------------------------------------------------

constexpr double kR = 6371008.8;
constexpr double kPi = 3.14159265358979323846;
constexpr int32_t kLat0 = 451234560, kLon0 = 93456780;

// A point `x` m east and `y` m north of the sample point.
static geo::Point metres(double x, double y) {
  double phi0 = kLat0 * 1e-7 * kPi / 180;
  return {kLat0 + static_cast<int32_t>(lround(y / kR * 180 / kPi * 1e7)),
          kLon0 + static_cast<int32_t>(lround(x / (kR * cos(phi0)) * 180 / kPi * 1e7))};
}

static Epoch fix(uint32_t iTow, double x, double y, uint32_t speed = 10000,
                 int32_t heading = 9000000) {
  Epoch e;
  e.iTowMs = iTow;
  e.tMs = iTow - 400000000 + 50;  // the record a little after the epoch
  e.at = metres(x, y);
  e.speedMmS = speed;
  e.headingE5 = heading;
  e.takesPart = true;
  return e;
}

// A line at the sample point, crossed eastwards.
static Gate eastward() {
  Gate g;
  g.centre = metres(0, 0);
  g.headingE5 = 9000000;
  return g;
}

static std::vector<Crossing> run(const std::vector<Epoch>& es, const Gate& g = eastward()) {
  return lapsOf(g, es);
}

// Four metres behind the line, twelve beyond, 100 ms apart: a quarter of the way, 25 ms in.
static void a_crossing_is_interpolated_between_the_fixes() {
  std::vector<Crossing> c = run({fix(400010000, -4, 0), fix(400010100, 12, 0)});
  TEST_ASSERT_EQUAL_UINT(1, c.size());
  TEST_ASSERT_EQUAL_UINT16(0, c[0].lap);
  TEST_ASSERT_EQUAL_UINT32(400010025, c[0].iTowMs);
  TEST_ASSERT_EQUAL_UINT32(10075, c[0].tMs);  // the record's time, plus the same 25 ms
}

static void going_back_over_the_line_crosses_nothing() {
  TEST_ASSERT_EQUAL_UINT(0, run({fix(400010000, 3, 0), fix(400010100, -1, 0)}).size());
}

// The gate is 15 m wide: 7.5 m either side of its centre counts, 7.6 m does not.
static void only_within_the_gate_s_width() {
  TEST_ASSERT_EQUAL_UINT(1, run({fix(400010000, -1, 7.4), fix(400010100, 1, 7.4)}).size());
  TEST_ASSERT_EQUAL_UINT(0, run({fix(400010000, -1, 7.6), fix(400010100, 1, 7.6)}).size());
  TEST_ASSERT_EQUAL_UINT(0, run({fix(400010000, -1, -7.6), fix(400010100, 1, -7.6)}).size());
}

// Fixes lost: 1000 ms apart still crosses, 1100 ms does not, and nor do the epochs that do not
// take part in between.
static void not_over_a_gap() {
  TEST_ASSERT_EQUAL_UINT(1, run({fix(400010000, -1, 0), fix(400011000, 1, 0)}).size());
  TEST_ASSERT_EQUAL_UINT(0, run({fix(400010000, -1, 0), fix(400011100, 1, 0)}).size());
  Epoch lost = fix(400010500, 0.5, 0);
  lost.takesPart = false;
  TEST_ASSERT_EQUAL_UINT(0, run({fix(400010000, -1, 0), lost, fix(400011100, 1, 0)}).size());
}

// Within 5000 ms of the last crossing counted, another is ignored; at 5000 it counts, and ends
// lap 1.
static void not_within_five_seconds() {
  std::vector<Epoch> wobble = {fix(400010000, -1, 0), fix(400010100, 1, 0),   // 400010050
                               fix(400012000, 1, 0),  fix(400012100, -1, 0),  // back over
                               fix(400014000, -1, 0), fix(400014100, 1, 0)};  // 4000 ms on
  TEST_ASSERT_EQUAL_UINT(1, run(wobble).size());
  std::vector<Crossing> c = run({fix(400010000, -1, 0), fix(400010100, 1, 0),
                                 fix(400015000, -1, 0), fix(400015100, 1, 0)});
  TEST_ASSERT_EQUAL_UINT(2, c.size());
  TEST_ASSERT_EQUAL_UINT16(1, c[1].lap);
  TEST_ASSERT_EQUAL_UINT32(5000, c[1].lapMs);
}

// A lap across the end of the GPS week, and a crossing within its last step: the times wrap, and
// the lap's is still the difference.
static void the_week_wraps() {
  uint32_t end = geo::kWeekMs;
  std::vector<Crossing> c = run({fix(end - 30100, -1, 0), fix(end - 30000, 1, 0),  // end − 30 050
                                 fix(end - 20, -1, 0), fix(80, 1, 0)});           // 30, past it
  TEST_ASSERT_EQUAL_UINT(2, c.size());
  TEST_ASSERT_EQUAL_UINT32(end - 30050, c[0].iTowMs);
  TEST_ASSERT_EQUAL_UINT32(30, c[1].iTowMs);
  TEST_ASSERT_EQUAL_UINT32(30080, c[1].lapMs);
}

// A lap's distance: the steps between its crossings, the ones they fall in split where they do.
static void a_lap_s_distance() {
  // East 50 m from the line, 10 m north, 100 m back west, 10 m south and east to the line again:
  // a 220 m loop, crossed twice 5.3 s apart.
  std::vector<Epoch> es;
  uint32_t t = 400010000;
  es.push_back(fix(t, -2, 0));
  for (int x = 2; x <= 50; x += 4) es.push_back(fix(t += 100, x, 0));
  es.push_back(fix(t += 100, 50, 10, 10000, 0));
  for (int x = 50; x >= -50; x -= 4) es.push_back(fix(t += 100, x, 10, 10000, 27000000));
  es.push_back(fix(t += 100, -50, 0, 10000, 18000000));
  for (int x = -46; x <= 10; x += 4) es.push_back(fix(t += 100, x, 0));
  std::vector<Crossing> c = run(es);
  TEST_ASSERT_EQUAL_UINT(2, c.size());
  ASSERT_METRES(0.05, 220.0, c[1].distanceM);
}

static void the_median_and_the_flags() {
  double odd[] = {300, 100, 200};
  TEST_ASSERT_TRUE(geo::medianM(odd, 3) == 200);
  double even[] = {400, 100, 300, 200};
  TEST_ASSERT_TRUE(geo::medianM(even, 4) == 250);
  TEST_ASSERT_TRUE(geo::medianM(nullptr, 0) == 0);
  auto flag = [](double d) { return static_cast<int>(geo::flagOf(d, 100)); };
  TEST_ASSERT_EQUAL_INT(static_cast<int>(geo::Flag::None), flag(85));  // 85 % is not under it
  TEST_ASSERT_EQUAL_INT(static_cast<int>(geo::Flag::Cut), flag(84.9));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(geo::Flag::None), flag(130));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(geo::Flag::Off), flag(130.1));
}

// The first loop closes on a heading within 45°, across north as well: 359.9° against 1°; 50°
// apart it does not. And not before 20 s, nor at a standstill. It gives only a provisional gate.
static bool closeLoop(int32_t headingThere, int32_t headingBack, uint32_t after, uint32_t speed) {
  std::vector<Epoch> room(100);
  LineFinder f(room.data(), room.size());
  Gate g;
  f.update(fix(400010000, 0, 0, 12000, headingThere), g);
  f.update(fix(400010100, 0, 1.2, 15000, headingThere), g);
  TEST_ASSERT_FALSE(f.update(fix(400010000 + after, 3, 1, speed, headingBack), g));
  return f.closed();
}

static void the_first_loop_closes_on_place_heading_and_time() {
  TEST_ASSERT_TRUE(closeLoop(35990000, 100000, 20000, 9000));
  TEST_ASSERT_FALSE(closeLoop(0, 5000000, 20000, 9000));  // 50° apart
  TEST_ASSERT_FALSE(closeLoop(0, 0, 19900, 9000));        // too soon
  TEST_ASSERT_FALSE(closeLoop(0, 0, 20000, 900));         // at a standstill
}

// Laps of a 250 m stadium at 10 m/s, a metre an epoch: east along the bottom straight from the
// origin, 75 m, round a 50 m bend, west along the top straight 31.8 m north, round the other bend.
// The first loop closes on epoch 240, 9.8 m short of the start, and the provisional gate there is
// crossed every 250 epochs: provisional lap n is epochs (240 + 250 (n − 1), 240 + 250 n]. `fast`
// is the middle of 1.1 s at 15 m/s; `bend(k, x, y)` moves the car on epoch k.
struct Stadium {
  std::vector<Epoch> room = std::vector<Epoch>(2000), es;
  LineFinder finder{room.data(), room.size()};
  Gate gate;
  int foundAt = -1;

  // Where the stadium puts the car `d` metres from the origin, and its heading.
  static void place(double d, double& x, double& y, double& h) {
    const double r = 50 / kPi;
    d = fmod(d, 250.0);
    if (d < 75) {
      x = d, y = 0, h = 90;
    } else if (d < 125) {
      double a = (d - 75) / r;
      x = 75 + r * sin(a), y = r - r * cos(a), h = 90 - a * 180 / kPi;
    } else if (d < 200) {
      x = 75 - (d - 125), y = 2 * r, h = 270;
    } else {
      double a = (d - 200) / r;
      x = -r * sin(a), y = r + r * cos(a), h = 270 - a * 180 / kPi;
    }
  }

  void add(double x, double y, double h, uint32_t v) {
    int32_t heading = static_cast<int32_t>(lround(fmod(h + 360, 360) * 1e5));
    es.push_back(fix(400010000 + 100 * static_cast<uint32_t>(es.size()), x, y, v, heading));
    if (foundAt < 0 && finder.update(es.back(), gate)) foundAt = static_cast<int>(es.size()) - 1;
  }

  template <typename Bend>
  void drive(int epochs, int fast, Bend bend) {
    for (int k = 0; k < epochs && foundAt < 0; ++k) {
      double x, y, h;
      place(k, x, y, h);
      bend(k, x, y);
      add(x, y, h, k >= fast - 5 && k <= fast + 5 ? 15000 : 10000);
    }
  }
};

// The first two laps agree: the gate goes at their fastest point, the 1.1 s at 15 m/s, at the end
// of the second, epoch 740.
static void the_gate_goes_where_two_agreeing_laps_are_fastest() {
  Stadium s;
  s.drive(1200, 605, [](int, double&, double&) {});
  TEST_ASSERT_TRUE(s.finder.closed());
  TEST_ASSERT_TRUE(s.finder.found());
  TEST_ASSERT_INT_WITHIN(1, 740, s.foundAt);
  TEST_ASSERT_EQUAL_INT32(s.es[605].at.latE7, s.gate.centre.latE7);
  TEST_ASSERT_EQUAL_INT32(s.es[605].at.lonE7, s.gate.centre.lonE7);
  TEST_ASSERT_EQUAL_INT32(s.es[605].headingE5, s.gate.headingE5);
  TEST_ASSERT_TRUE(s.gate.widthM == 15);
}

// The first lap weaves 5 m either side of the bottom straight for 60 m: the same path within 10 m,
// but over 10 % longer. It disagrees on length alone, and the gate waits for laps 2 and 3.
static void a_lap_longer_by_more_than_ten_percent_disagrees() {
  Stadium s;
  s.drive(1200, 805, [](int k, double& x, double& y) {
    if (k >= 260 && k < 320) y = 5 * sin(2 * kPi * x / 10);
  });
  TEST_ASSERT_INT_WITHIN(1, 990, s.foundAt);
  TEST_ASSERT_EQUAL_INT32(s.es[805].at.lonE7, s.gate.centre.lonE7);
}

// The first lap runs 15 m north of the top straight for 25 m, ramps of 10 m either side: within
// 7 % of the length, but a tenth of its epochs over 10 m from the next lap's path. It disagrees on
// the path alone, and the gate waits for laps 2 and 3.
static void a_lap_off_the_path_disagrees() {
  Stadium s;
  s.drive(1200, 805, [](int k, double&, double& y) {
    double d = k - 250;  // along the second turn of the stadium
    if (k < 250 || k >= 500 || d < 140 || d > 185) return;
    y += d < 150 ? 1.5 * (d - 140) : d < 175 ? 15 : 1.5 * (185 - d);
  });
  TEST_ASSERT_INT_WITHIN(1, 990, s.foundAt);
  TEST_ASSERT_EQUAL_INT32(s.es[805].at.lonE7, s.gate.centre.lonE7);
}

// The first lap takes a spur 12.5 m north off the top straight and back, slowly, at 2.5 m/s: the
// next lap's path lies all along its own, but its epochs at the spur's end, over 10 m from the
// next lap's path, are 6 % of them. Covered one way only, it disagrees, and the gate waits for laps
// 2 and 3 — 100 epochs later than without the spur.
static void a_lap_must_cover_the_other_both_ways() {
  Stadium s;
  for (int k = 0; k < 1200 && s.foundAt < 0; ++k) {
    double x, y, h;
    Stadium::place(k, x, y, h);
    s.add(x, y, h, k >= 800 && k <= 810 ? 15000 : 10000);
    if (k == 250 + 162) {
      for (int n = 1; n <= 50; ++n) s.add(x, y + 0.25 * n, 0, 2500);
      for (int n = 49; n >= 0; --n) s.add(x, y + 0.25 * n, 18000000, 2500);
    }
  }
  TEST_ASSERT_INT_WITHIN(1, 1090, s.foundAt);
  TEST_ASSERT_EQUAL_INT32(s.es[905].at.lonE7, s.gate.centre.lonE7);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(the_fixture_s_laps_with_its_line);
  RUN_TEST(the_fixture_s_laps_without_a_line);
  RUN_TEST(a_crossing_is_interpolated_between_the_fixes);
  RUN_TEST(going_back_over_the_line_crosses_nothing);
  RUN_TEST(only_within_the_gate_s_width);
  RUN_TEST(not_over_a_gap);
  RUN_TEST(not_within_five_seconds);
  RUN_TEST(the_week_wraps);
  RUN_TEST(a_lap_s_distance);
  RUN_TEST(the_median_and_the_flags);
  RUN_TEST(the_first_loop_closes_on_place_heading_and_time);
  RUN_TEST(the_gate_goes_where_two_agreeing_laps_are_fastest);
  RUN_TEST(a_lap_longer_by_more_than_ten_percent_disagrees);
  RUN_TEST(a_lap_off_the_path_disagrees);
  RUN_TEST(a_lap_must_cover_the_other_both_ways);
  return UNITY_END();
}
