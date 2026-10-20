// Host tests for lib/ui's GNSS acquisition — run with `make test`.
//
// What the GNSS screen says while the module looks for satellites (PLAN-ESP32.md §3.4): Acquiring,
// Fix once a 3D fix within 5 m has held 3 s, Timeout after 90 s; and what counts as losing it.
// The timing is the device's own (ui_config.h), epochs at the 10 Hz of §5.2.
#include <unity.h>

#include "ui_config.h"
#include "ui_gnss.h"

using ui::AcqState;
using ui::Acquisition;

void setUp() {}
void tearDown() {}

#define ASSERT_STATE(expected, acq, nowMs) \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AcqState::expected), static_cast<int>((acq).state(nowMs)))

constexpr uint32_t kEpochMs = 100;
constexpr uint32_t kGood = 1200, kPoor = 8000;  // hAcc, mm

// Epochs from `fromMs` up to, not including, `toMs`; the clock left at `toMs`. Counted, not
// compared, so that a span across the counter's wrap works.
static uint32_t epochs(Acquisition& a, uint32_t fromMs, uint32_t toMs, bool fix3d, uint32_t hAcc) {
  uint32_t t = fromMs;
  for (uint32_t n = (toMs - fromMs) / kEpochMs; n; --n, t += kEpochMs) {
    a.epoch(fix3d, hAcc, t);
    a.tick(t);
  }
  return toMs;
}

static void a_fix_comes_once_it_has_held_three_seconds() {
  Acquisition a(acquisitionTiming());
  a.restart(0);
  uint32_t t = epochs(a, 0, 20000, false, 0xFFFFFFFF);  // 20 s of nothing
  ASSERT_STATE(Acquiring, a, t);
  TEST_ASSERT_EQUAL_UINT32(20, a.elapsedS(t));
  t = epochs(a, t, t + GNSS_FIX_HOLD_MS, true, kGood);  // 2.9 s of a good fix: not yet
  TEST_ASSERT_FALSE(a.fix());
  a.epoch(true, kGood, t);  // the 3 s are up
  TEST_ASSERT_TRUE(a.fix());
  ASSERT_STATE(Fix, a, t);
  TEST_ASSERT_EQUAL_UINT32(23, a.ttffS());
}

static void a_poor_or_broken_hold_starts_over() {
  Acquisition a(acquisitionTiming());
  a.restart(0);
  uint32_t t = epochs(a, 0, 10000, true, kPoor);  // 3D, but 8 m: never a fix
  TEST_ASSERT_FALSE(a.fix());
  t = epochs(a, t, t + 2500, true, kGood);
  a.epoch(false, kGood, t);  // one epoch without the fix: the hold is gone
  t += kEpochMs;
  t = epochs(a, t, t + 2500, true, kGood);
  TEST_ASSERT_FALSE(a.fix());
  t = epochs(a, t, t + 600, true, kGood);
  TEST_ASSERT_TRUE(a.fix());  // 3 s from the epoch after the break
}

static void timeout_after_ninety_seconds_and_a_fix_after_still_counts() {
  Acquisition a(acquisitionTiming());
  a.restart(0);
  uint32_t t = epochs(a, 0, GNSS_ACQ_TIMEOUT_MS - kEpochMs, false, 0xFFFFFFFF);
  ASSERT_STATE(Acquiring, a, t);
  t = epochs(a, t, GNSS_ACQ_TIMEOUT_MS, false, 0xFFFFFFFF);
  ASSERT_STATE(Timeout, a, t);
  t = epochs(a, t, t + GNSS_FIX_HOLD_MS + kEpochMs, true, kGood);
  ASSERT_STATE(Fix, a, t);
  TEST_ASSERT_EQUAL_UINT32(93, a.ttffS());
}

static void accuracy_that_drifts_does_not_end_a_fix_but_losing_3d_does() {
  Acquisition a(acquisitionTiming());
  a.restart(0);
  uint32_t t = epochs(a, 0, 3100, true, kGood);
  TEST_ASSERT_TRUE(a.fix());
  t = epochs(a, t, t + 5000, true, kPoor);  // under trees: worse, still a 3D fix
  TEST_ASSERT_TRUE(a.fix());
  a.epoch(false, kPoor, t);  // gone
  TEST_ASSERT_FALSE(a.fix());
  ASSERT_STATE(Acquiring, a, t);
  TEST_ASSERT_EQUAL_UINT32(0, a.elapsedS(t));  // a new acquisition, from the loss
  t = epochs(a, t + kEpochMs, t + 3200, true, kGood);
  TEST_ASSERT_TRUE(a.fix());
  TEST_ASSERT_EQUAL_UINT32(3, a.ttffS());  // the second one, timed from the loss
}

static void epochs_that_stop_coming_end_the_fix() {
  Acquisition a(acquisitionTiming());
  a.restart(0);
  uint32_t t = epochs(a, 0, 3100, true, kGood);
  TEST_ASSERT_TRUE(a.fix());
  a.tick(t + GNSS_STALE_MS - 200);  // a pause shorter than the limit
  TEST_ASSERT_TRUE(a.fix());
  a.tick(t + GNSS_STALE_MS);
  TEST_ASSERT_FALSE(a.fix());
  ASSERT_STATE(Acquiring, a, t + GNSS_STALE_MS);
}

static void holding_ok_restarts_the_clock() {
  Acquisition a(acquisitionTiming());
  a.restart(0);
  uint32_t t = epochs(a, 0, 3100, true, kGood);
  TEST_ASSERT_TRUE(a.fix());
  a.restart(t);
  TEST_ASSERT_FALSE(a.fix());
  t = epochs(a, t, t + 3100, true, kGood);
  TEST_ASSERT_TRUE(a.fix());
  TEST_ASSERT_EQUAL_UINT32(3, a.ttffS());
}

static void survives_the_millisecond_counter_wrapping() {
  Acquisition a(acquisitionTiming());
  uint32_t start = 0xFFFFFFFFu - 1500;
  a.restart(start);
  uint32_t t = epochs(a, start, start + 3100, true, kGood);  // wraps half-way through the hold
  TEST_ASSERT_TRUE(a.fix());
  TEST_ASSERT_EQUAL_UINT32(3, a.ttffS());
  ASSERT_STATE(Fix, a, t);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(a_fix_comes_once_it_has_held_three_seconds);
  RUN_TEST(a_poor_or_broken_hold_starts_over);
  RUN_TEST(timeout_after_ninety_seconds_and_a_fix_after_still_counts);
  RUN_TEST(accuracy_that_drifts_does_not_end_a_fix_but_losing_3d_does);
  RUN_TEST(epochs_that_stop_coming_end_the_fix);
  RUN_TEST(holding_ok_restarts_the_clock);
  RUN_TEST(survives_the_millisecond_counter_wrapping);
  return UNITY_END();
}
