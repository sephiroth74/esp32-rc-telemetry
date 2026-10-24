// Host tests for the automatic start and stop of a session — run with `make test`.
//
// PLAN-ESP32.md §3.6 with the device's own thresholds (ui_config.h): what starts a run — speed with
// a fix, throttle with a calibrated profile — what counts as standing still, and how the board acts
// on them: from IDLE only, through Home, refused once when the POST refused logging, stopped after
// session.auto_stop_min still, and not started again by a car still rolling after a stop by hand.
#include <unity.h>

#include <initializer_list>

#include "ui_board.h"
#include "ui_config.h"

using namespace ui;

void setUp() {}
void tearDown() {}

#define ASSERT_COMMAND(expected, output) \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Command::expected), static_cast<int>((output).command))
#define ASSERT_MODE(expected, board) \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Mode::expected), static_cast<int>((board).mode()))

constexpr uint32_t kTick = 10;  // the ui task's 100 Hz

static Motion speed(double kmh) {
  Motion m;
  m.speedKnown = true;
  m.speedMmS = static_cast<uint32_t>(kmh / 3.6 * 1000 + 0.5);
  return m;
}

static Motion throttle(int8_t pct) {
  Motion m;
  m.throttleKnown = true;
  m.throttlePct = pct;
  return m;
}

static Motion both(double kmh, int8_t pct) {
  Motion m = speed(kmh);
  m.throttleKnown = true;
  m.throttlePct = pct;
  return m;
}

// --- The detector ----------------------------------------------------------------------

// Feeds `m` every tick for `ms`; the clock left after it.
static uint32_t hold(AutoSession& a, const Motion& m, uint32_t fromMs, uint32_t ms) {
  for (uint32_t t = 0; t < ms; t += kTick) a.update(m, fromMs + t);
  return fromMs + ms;
}

static void moving_for_two_seconds_with_a_fix_starts() {
  AutoSession a(autoSessionTiming(5));
  uint32_t t = hold(a, speed(8), 0, 2000);  // samples from 0 to 1990 ms
  TEST_ASSERT_FALSE(a.startDue(t - kTick));
  a.update(speed(8), t);
  TEST_ASSERT_TRUE(a.startDue(t));
  // 5 km/h is not more than 5 km/h; and a speed without a fix is no speed.
  AutoSession b(autoSessionTiming(5));
  t = hold(b, speed(5), 0, 5000);
  TEST_ASSERT_FALSE(b.startDue(t));
  Motion noFix = speed(20);
  noFix.speedKnown = false;
  t = hold(b, noFix, t, 5000);
  TEST_ASSERT_FALSE(b.startDue(t));
}

static void a_dip_in_speed_starts_the_two_seconds_over() {
  AutoSession a(autoSessionTiming(5));
  uint32_t t = hold(a, speed(8), 0, 1500);
  t = hold(a, speed(3), t, kTick);  // one tick slower
  t = hold(a, speed(8), t, 1500);
  TEST_ASSERT_FALSE(a.startDue(t));
  t = hold(a, speed(8), t, 600);
  TEST_ASSERT_TRUE(a.startDue(t));
}

static void throttle_either_way_for_a_second_starts() {
  for (int8_t pct : {int8_t(11), int8_t(-11), int8_t(100), int8_t(-100)}) {
    AutoSession a(autoSessionTiming(5));
    uint32_t t = hold(a, throttle(pct), 0, 1000);
    TEST_ASSERT_FALSE(a.startDue(t - kTick));
    a.update(throttle(pct), t);
    TEST_ASSERT_TRUE(a.startDue(t));
  }
  AutoSession a(autoSessionTiming(5));
  uint32_t t = hold(a, throttle(10), 0, 5000);  // ±10 % is not more than 10 %
  TEST_ASSERT_FALSE(a.startDue(t));
  Motion uncalibrated = throttle(60);
  uncalibrated.throttleKnown = false;
  t = hold(a, uncalibrated, t, 5000);
  TEST_ASSERT_FALSE(a.startDue(t));
}

static void standing_still_needs_both_slow_and_idle() {
  AutoSession a(autoSessionTiming(5));
  uint32_t t = hold(a, both(1.5, 3), 0, 1000);
  TEST_ASSERT_TRUE(a.still());
  hold(a, both(1.5, 6), t, kTick);  // the throttle held off neutral: not still
  TEST_ASSERT_FALSE(a.still());
  hold(a, both(2.5, 0), t, kTick);  // rolling: not still
  TEST_ASSERT_FALSE(a.still());
  hold(a, speed(0), t, kTick);  // the speed alone, with no throttle known
  TEST_ASSERT_TRUE(a.still());
  hold(a, throttle(-5), t, kTick);  // the throttle alone, with no fix
  TEST_ASSERT_TRUE(a.still());
  hold(a, Motion{}, t, kTick);  // nothing known: never taken for still
  TEST_ASSERT_FALSE(a.still());
}

static void five_minutes_still_is_the_stop() {
  AutoSession a(autoSessionTiming(5));
  uint32_t t = hold(a, both(0, 0), 0, 5 * 60 * 1000);
  TEST_ASSERT_FALSE(a.stopDue(t - kTick));
  a.update(both(0, 0), t);
  TEST_ASSERT_TRUE(a.stopDue(t));
  AutoSession two(autoSessionTiming(2));  // session.auto_stop_min = 2
  t = hold(two, both(0, 0), 0, 2 * 60 * 1000 + kTick);
  TEST_ASSERT_TRUE(two.stopDue(t));
}

// --- On the board -------------------------------------------------------------------------

struct Run {
  Board board{menuTiming()};
  Facts facts;
  uint32_t nowMs = 1000;
  BoardOutput last;

  Run() {
    board.setAutoTiming(autoSessionTiming(5));
    facts.nextSession = 4;
    facts.menu.hasFix = true;  // what a session needs to start (§3.6)
  }
  // `m` every tick for `ms`; the last output that did something.
  BoardOutput drive(const Motion& m, uint32_t ms) {
    BoardOutput seen;
    for (uint32_t t = 0; t < ms; t += kTick) {
      nowMs += kTick;
      board.tick(nowMs);
      BoardOutput o = board.onMotion(m, facts, nowMs);
      if (o.command != Command::None || o.refused != Refusal::None) seen = o;
    }
    last = seen;
    return seen;
  }
  BoardOutput key(Event e) {
    nowMs += 100;
    return board.handle(e, facts, nowMs);
  }
};

static void a_run_starts_and_stops_on_its_own() {
  Run r;
  BoardOutput o = r.drive(speed(12), 2500);
  ASSERT_COMMAND(StartSession, o);
  TEST_ASSERT_EQUAL_STRING("Session 4", o.toast);
  ASSERT_MODE(Session, r.board);
  r.drive(speed(30), 60000);  // a minute of driving
  ASSERT_MODE(Session, r.board);
  o = r.drive(both(0, 0), 5 * 60 * 1000 + 100);  // parked
  ASSERT_COMMAND(StopSession, o);
  TEST_ASSERT_EQUAL_STRING("Session 4 saved", o.toast);
  ASSERT_MODE(Idle, r.board);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenId::Home), static_cast<int>(r.board.menu().screen()));
  r.facts.nextSession = 5;
  o = r.drive(speed(12), 2500);  // the next run is the next session
  ASSERT_COMMAND(StartSession, o);
  TEST_ASSERT_EQUAL_STRING("Session 5", o.toast);
}

static void a_start_closes_the_menu_and_its_dialog() {
  Run r;
  r.key(Event::Open);  // Menu
  for (int i = 0; i < 4; ++i) r.key(Event::Down);
  r.key(Event::Open);  // Settings
  for (int i = 0; i < 3; ++i) r.key(Event::Down);
  r.key(Event::Open);  // Delete sessions
  r.key(Event::Hold);  // its dialog
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DialogKind::DeleteSessions),
                        static_cast<int>(r.board.menu().dialog()));
  ASSERT_COMMAND(StartSession, r.drive(throttle(40), 1100));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DialogKind::None),
                        static_cast<int>(r.board.menu().dialog()));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenId::Home), static_cast<int>(r.board.menu().screen()));
}

static void only_from_idle_and_only_when_on() {
  Run r;
  r.facts.autoStart = false;  // session.auto_start
  ASSERT_COMMAND(None, r.drive(speed(20), 5000));
  ASSERT_MODE(Idle, r.board);
  r.facts.autoStart = true;
  r.facts.menu.rcSignal = true;
  r.key(Event::Open);  // Menu
  for (int i = 0; i < 4; ++i) r.key(Event::Down);
  r.key(Event::Open);  // Settings
  r.key(Event::Down);
  r.key(Event::Open);  // Calibrate
  r.key(Event::Hold);  // into CALIBRATION
  ASSERT_MODE(Calibration, r.board);
  ASSERT_COMMAND(None, r.drive(throttle(90), 3000));  // full throttle, for the calibration
  ASSERT_MODE(Calibration, r.board);
}

static void a_refused_start_says_why_once() {
  Run r;
  r.facts.menu.loggingAllowed = false;
  r.facts.menu.refusingCheck = 8;
  BoardOutput o = r.drive(speed(15), 2500);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Refusal::LoggingRefused), static_cast<int>(o.refused));
  TEST_ASSERT_EQUAL_UINT8(8, o.arg);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Refusal::LoggingRefused),
                        static_cast<int>(r.board.notice()));
  r.key(Event::Press);  // read and closed
  o = r.drive(speed(15), 10000);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Refusal::None), static_cast<int>(o.refused));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Refusal::None), static_cast<int>(r.board.notice()));
  ASSERT_MODE(Idle, r.board);
}

static void a_stop_by_hand_while_rolling_does_not_start_again() {
  Run r;
  r.drive(speed(20), 2500);
  ASSERT_MODE(Session, r.board);
  r.key(Event::Hold);  // the stop dialog
  BoardOutput o = r.key(Event::Hold);  // yes
  ASSERT_COMMAND(StopSession, o);
  ASSERT_COMMAND(None, r.drive(speed(20), 10000));  // still rolling: no new run
  ASSERT_MODE(Idle, r.board);
  r.drive(both(0, 0), 4000);  // stopped, not yet long enough to re-arm
  ASSERT_COMMAND(None, r.drive(speed(20), 2500));
  r.drive(both(0, 0), 5100);  // now it has stood still
  ASSERT_COMMAND(StartSession, r.drive(speed(20), 2500));
}

static void a_run_started_by_hand_on_a_parked_car_is_not_stopped_at_once() {
  Run r;
  r.drive(both(0, 0), 10 * 60 * 1000);  // parked for ten minutes
  r.key(Event::Hold);  // hold OK on Home: start
  ASSERT_MODE(Session, r.board);
  ASSERT_COMMAND(None, r.drive(both(0, 0), 4 * 60 * 1000));
  ASSERT_MODE(Session, r.board);
  ASSERT_COMMAND(StopSession, r.drive(both(0, 0), 61 * 1000));  // five minutes from the start
}

static void the_automatic_stop_closes_a_stop_dialog_left_open() {
  Run r;
  r.drive(speed(20), 2500);
  r.key(Event::Hold);  // the stop dialog, then nobody answers it...
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DialogKind::StopSession), static_cast<int>(r.board.dialog()));
  ASSERT_COMMAND(StopSession, r.drive(both(0, 0), 5 * 60 * 1000 + 100));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DialogKind::None), static_cast<int>(r.board.dialog()));
  ASSERT_MODE(Idle, r.board);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(moving_for_two_seconds_with_a_fix_starts);
  RUN_TEST(a_dip_in_speed_starts_the_two_seconds_over);
  RUN_TEST(throttle_either_way_for_a_second_starts);
  RUN_TEST(standing_still_needs_both_slow_and_idle);
  RUN_TEST(five_minutes_still_is_the_stop);
  RUN_TEST(a_run_starts_and_stops_on_its_own);
  RUN_TEST(a_start_closes_the_menu_and_its_dialog);
  RUN_TEST(only_from_idle_and_only_when_on);
  RUN_TEST(a_refused_start_says_why_once);
  RUN_TEST(a_stop_by_hand_while_rolling_does_not_start_again);
  RUN_TEST(a_run_started_by_hand_on_a_parked_car_is_not_stopped_at_once);
  RUN_TEST(the_automatic_stop_closes_a_stop_dialog_left_open);
  return UNITY_END();
}
