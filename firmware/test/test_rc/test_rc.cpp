// Host tests for lib/rc — run with `make test`.
//
// Calibration and percentages (PLAN-ESP32.md §3.8, §4.3), on the widths the bench car's KR-331 sent
// (docs/hardware.md: steering 1050/1500/1940 µs, throttle 1005/1500/1940 µs), on the same radio
// reversed, and on an ESC with less travel to the brake than to full throttle.
#include <unity.h>

#include "rc.h"

using rc::Axis;
using rc::AxisCheck;

void setUp() {}
void tearDown() {}

#define ASSERT_CHECK(expected, axis) \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AxisCheck::expected), static_cast<int>(rc::check(axis)))

static const Axis kSteering = {1050, 1500, 1940};  // left, neutral, right
static const Axis kReversed = {1940, 1500, 1050};  // the same radio with the channel reversed
static const Axis kEsc = {1300, 1500, 1950};       // brake 200 µs, throttle 450 µs

static void the_ends_and_the_middle() {
  TEST_ASSERT_EQUAL_INT8(0, rc::percent(kSteering, 1500));
  TEST_ASSERT_EQUAL_INT8(100, rc::percent(kSteering, 1940));
  TEST_ASSERT_EQUAL_INT8(-100, rc::percent(kSteering, 1050));
  TEST_ASSERT_EQUAL_INT8(50, rc::percent(kSteering, 1720));   // 220 of 440
  TEST_ASSERT_EQUAL_INT8(-50, rc::percent(kSteering, 1275));  // 225 of 450
}

static void each_side_on_its_own_scale() {
  TEST_ASSERT_EQUAL_INT8(-50, rc::percent(kEsc, 1400));  // half the brake: 100 of 200
  TEST_ASSERT_EQUAL_INT8(50, rc::percent(kEsc, 1725));   // half the throttle: 225 of 450
  TEST_ASSERT_EQUAL_INT8(-100, rc::percent(kEsc, 1300));
  TEST_ASSERT_EQUAL_INT8(100, rc::percent(kEsc, 1950));
}

static void a_reversed_channel_needs_no_setting() {
  TEST_ASSERT_EQUAL_INT8(-100, rc::percent(kReversed, 1940));  // still left
  TEST_ASSERT_EQUAL_INT8(100, rc::percent(kReversed, 1050));
  TEST_ASSERT_EQUAL_INT8(-50, rc::percent(kReversed, 1720));
  TEST_ASSERT_EQUAL_INT8(50, rc::percent(kReversed, 1275));
}

static void rounded_to_the_nearest_and_held_at_the_ends() {
  TEST_ASSERT_EQUAL_INT8(0, rc::percent(kSteering, 1502));   // 0.45 %
  TEST_ASSERT_EQUAL_INT8(1, rc::percent(kSteering, 1503));   // 0.68 %
  TEST_ASSERT_EQUAL_INT8(-1, rc::percent(kSteering, 1497));  // the same either way
  TEST_ASSERT_EQUAL_INT8(0, rc::percent(kSteering, 1498));
  TEST_ASSERT_EQUAL_INT8(100, rc::percent(kSteering, 2100));  // past the end: held
  TEST_ASSERT_EQUAL_INT8(-100, rc::percent(kSteering, 800));
  // The automatic start's threshold (§3.6) on the bench car's throttle: 60 µs is over 10 %.
  const Axis throttle = {1005, 1500, 1940};
  TEST_ASSERT_EQUAL_INT8(14, rc::percent(throttle, 1560));
  TEST_ASSERT_EQUAL_INT8(-10, rc::percent(throttle, 1450));  // 50 of 495: not over it
}

static void a_capture_that_cannot_be_a_calibration() {
  ASSERT_CHECK(Ok, kSteering);
  ASSERT_CHECK(Ok, kReversed);
  ASSERT_CHECK(Ok, kEsc);
  ASSERT_CHECK(NotCaptured, (Axis{0, 1500, 1940}));
  ASSERT_CHECK(NotCaptured, (Axis{}));
  ASSERT_CHECK(NotMoved, (Axis{1451, 1500, 1940}));  // the wheel barely turned left
  ASSERT_CHECK(Ok, (Axis{1450, 1500, 1550}));        // 50 µs each way is enough
  ASSERT_CHECK(SameSide, (Axis{1940, 1500, 1945}));  // left captured on the right
}

static void the_accepted_window_follows_the_car() {
  uint16_t lo, hi;
  rc::window(kSteering, 100, lo, hi);
  TEST_ASSERT_EQUAL_UINT16(950, lo);
  TEST_ASSERT_EQUAL_UINT16(2040, hi);
  rc::window(kReversed, 100, lo, hi);  // which end is which does not matter here
  TEST_ASSERT_EQUAL_UINT16(950, lo);
  TEST_ASSERT_EQUAL_UINT16(2040, hi);
  rc::window(Axis{80, 100, 65500}, 100, lo, hi);  // nothing wraps
  TEST_ASSERT_EQUAL_UINT16(0, lo);
  TEST_ASSERT_EQUAL_UINT16(65535, hi);
}

static void the_six_steps_fill_the_calibration() {
  rc::Calibration cal;
  rc::capture(cal, rc::Step::Neutral, 1500, 1500, 1000);
  rc::capture(cal, rc::Step::SteerLeft, 1050, 1500, 1000);
  rc::capture(cal, rc::Step::SteerRight, 1940, 1500, 1000);
  rc::capture(cal, rc::Step::FullThrottle, 1500, 1940, 1000);
  rc::capture(cal, rc::Step::FullBrake, 1500, 1005, 1000);
  rc::capture(cal, rc::Step::Aux, 1500, 1500, 2000);
  TEST_ASSERT_EQUAL_UINT16(1050, cal.steering.negativeUs);
  TEST_ASSERT_EQUAL_UINT16(1500, cal.steering.neutralUs);
  TEST_ASSERT_EQUAL_UINT16(1940, cal.steering.positiveUs);
  TEST_ASSERT_EQUAL_UINT16(1005, cal.throttle.negativeUs);
  TEST_ASSERT_EQUAL_UINT16(1500, cal.throttle.neutralUs);
  TEST_ASSERT_EQUAL_UINT16(1940, cal.throttle.positiveUs);
  TEST_ASSERT_EQUAL_UINT16(2000, cal.auxUs);
  ASSERT_CHECK(Ok, cal.steering);
  ASSERT_CHECK(Ok, cal.throttle);
}

// The bench's own widths, timed by the MCPWM capture (docs/hardware.md, bench session 5), taken
// through the six steps: a calibration, and its percentages and accepted window.
static void the_bench_cars_capture_is_a_calibration() {
  rc::Calibration cal;
  rc::capture(cal, rc::Step::Neutral, 1500, 1498, 0);
  rc::capture(cal, rc::Step::SteerLeft, 1054, 1498, 0);
  rc::capture(cal, rc::Step::SteerRight, 1936, 1498, 0);
  rc::capture(cal, rc::Step::FullThrottle, 1500, 1956, 0);
  rc::capture(cal, rc::Step::FullBrake, 1500, 1002, 0);
  TEST_ASSERT_NULL(rc::problem(cal));
  TEST_ASSERT_EQUAL_INT8(-100, rc::percent(cal.steering, 1054));
  TEST_ASSERT_EQUAL_INT8(100, rc::percent(cal.throttle, 1956));
  TEST_ASSERT_EQUAL_INT8(50, rc::percent(cal.throttle, 1727));  // 229 of 458
  TEST_ASSERT_EQUAL_INT8(-50, rc::percent(cal.throttle, 1250));  // 248 of 496
  uint16_t lo, hi;
  rc::window(cal.throttle, 100, lo, hi);
  TEST_ASSERT_EQUAL_UINT16(902, lo);
  TEST_ASSERT_EQUAL_UINT16(2056, hi);
}

// What the summary says of a capture it will not save; an unwired input is no problem, as long as
// the other one is calibrated.
static void the_summary_says_why_it_will_not_save() {
  rc::Calibration cal;
  TEST_ASSERT_EQUAL_STRING("Nothing captured", rc::problem(cal));
  cal.throttle = Axis{1002, 1498, 1956};  // throttle only: the steering is unwired
  TEST_ASSERT_NULL(rc::problem(cal));
  cal.steering = Axis{1054, 1500, 1520};  // right not turned
  TEST_ASSERT_EQUAL_STRING("Steering not moved", rc::problem(cal));
  cal.steering = Axis{1054, 1500, 1200};  // right captured while turned left
  TEST_ASSERT_EQUAL_STRING("Steering wrong way", rc::problem(cal));
  cal.steering = Axis{1054, 1500, 0};  // right skipped
  TEST_ASSERT_EQUAL_STRING("Steering step missed", rc::problem(cal));
  cal.steering = Axis{1054, 1500, 1936};
  cal.throttle = Axis{1002, 1498, 1498};
  TEST_ASSERT_EQUAL_STRING("Throttle not moved", rc::problem(cal));
}

static void an_absent_input_leaves_its_axis_uncaptured() {
  // Only the throttle wired (§4.3): the steering steps capture nothing, the aux step is skipped.
  rc::Calibration cal;
  rc::capture(cal, rc::Step::Neutral, 0, 1500, 0);
  rc::capture(cal, rc::Step::SteerLeft, 0, 1500, 0);
  rc::capture(cal, rc::Step::SteerRight, 0, 1500, 0);
  rc::capture(cal, rc::Step::FullThrottle, 0, 1940, 0);
  rc::capture(cal, rc::Step::FullBrake, 0, 1005, 0);
  ASSERT_CHECK(NotCaptured, cal.steering);
  ASSERT_CHECK(Ok, cal.throttle);
  TEST_ASSERT_EQUAL_UINT16(0, cal.auxUs);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(the_ends_and_the_middle);
  RUN_TEST(each_side_on_its_own_scale);
  RUN_TEST(a_reversed_channel_needs_no_setting);
  RUN_TEST(rounded_to_the_nearest_and_held_at_the_ends);
  RUN_TEST(a_capture_that_cannot_be_a_calibration);
  RUN_TEST(the_accepted_window_follows_the_car);
  RUN_TEST(the_six_steps_fill_the_calibration);
  RUN_TEST(an_absent_input_leaves_its_axis_uncaptured);
  RUN_TEST(the_bench_cars_capture_is_a_calibration);
  RUN_TEST(the_summary_says_why_it_will_not_save);
  return UNITY_END();
}
