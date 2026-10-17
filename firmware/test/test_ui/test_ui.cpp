// Host tests for lib/ui — run with `make test`.
//
// The key path of PLAN-ESP32.md §3.1, checked against the constants the device ships with
// (ui_config.h): the ladder bounds measured on the bench (docs/hardware.md) and the gesture
// timings of config.h. Timestamps are fed explicitly, one sample tick at a time.
#include <unity.h>

#include <initializer_list>

#include "ui_config.h"

using ui::Event;
using ui::Gestures;
using ui::Key;
using ui::KeyReader;

static constexpr uint32_t kTickMs = 1000 / KEY_SAMPLE_HZ;

// Unity compares integers; the key and event types are enum classes.
#define ASSERT_KEY(expected, actual) \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual))

// The events a gesture produced, drained after every tick.
struct Log {
  Event events[32];
  int count = 0;

  void drain(Gestures& g) {
    Event e;
    while (g.next(e)) {
      TEST_ASSERT_LESS_THAN(32, count);
      events[count++] = e;
    }
  }
};

// Feeds `key` on every tick from `fromMs` up to `toMs`, both included.
static void feed(Gestures& g, Log& log, Key key, uint32_t fromMs, uint32_t toMs) {
  for (uint32_t t = fromMs; t <= toMs; t += kTickMs) {
    g.update(key, t);
    log.drain(g);
  }
}

static void expectEvents(const Log& log, std::initializer_list<Event> expected) {
  TEST_ASSERT_EQUAL_INT_MESSAGE(expected.size(), log.count, "number of events");
  int i = 0;
  for (Event e : expected) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(e), static_cast<int>(log.events[i]), "event");
    ++i;
  }
}

// Holds `key` on every tick before `releaseMs`, then releases it at exactly `releaseMs`.
static Log tap(KeyLayout layout, Key key, uint32_t releaseMs) {
  Gestures g(keyConfig(layout));
  Log log;
  for (uint32_t t = 0; t < releaseMs; t += kTickMs) {
    g.update(key, t);
    log.drain(g);
  }
  g.update(Key::None, releaseMs);
  log.drain(g);
  return log;
}

void setUp() {}
void tearDown() {}

// --- Windows ------------------------------------------------------------------

static void measured_levels_classify() {
  const auto& b = KEY_MV_BOUNDS;
  ASSERT_KEY(Key::Ok, ui::classify(0, b));
  // NAV's whole measured range, through 10 kΩ: the LEFT window.
  ASSERT_KEY(Key::Left, ui::classify(1609, b));
  ASSERT_KEY(Key::Left, ui::classify(1623, b));
  ASSERT_KEY(Key::Left, ui::classify(1637, b));
  // No key, including the lowest single sample seen during a release.
  ASSERT_KEY(Key::None, ui::classify(2910, b));
  ASSERT_KEY(Key::None, ui::classify(3120, b));
  // The nominal levels of the keys the two buttons do not have.
  ASSERT_KEY(Key::Up, ui::classify(600, b));
  ASSERT_KEY(Key::Down, ui::classify(1050, b));
  ASSERT_KEY(Key::Right, ui::classify(2270, b));
}

static void bounds_are_exclusive_above() {
  const auto& b = KEY_MV_BOUNDS;
  for (int i = 0; i < 5; ++i) {
    ASSERT_KEY(static_cast<Key>(i), ui::classify(b[i] - 1, b));
    ASSERT_KEY(static_cast<Key>(i + 1), ui::classify(b[i], b));
  }
}

static void nav_window_is_left() { ASSERT_KEY(Key::Left, keyConfig().navKey); }

// --- KeyReader ----------------------------------------------------------------

static void reader_needs_equal_readings_in_a_row() {
  KeyReader r(keyConfig());
  TEST_ASSERT_FALSE(r.sample(0));
  ASSERT_KEY(Key::None, r.key());
  TEST_ASSERT_TRUE(r.sample(0));
  ASSERT_KEY(Key::Ok, r.key());
  TEST_ASSERT_FALSE(r.sample(0));  // no change, no report
}

static void reader_ignores_a_single_glitch() {
  KeyReader r(keyConfig());
  for (uint16_t mv : {3120, 3120, 0, 3120, 1623, 3120, 3120}) TEST_ASSERT_FALSE(r.sample(mv));
  ASSERT_KEY(Key::None, r.key());
}

static void reader_skips_windows_passed_on_the_way() {
  KeyReader r(keyConfig());
  r.sample(1623);
  r.sample(1623);
  ASSERT_KEY(Key::Left, r.key());
  // Released: one reading on the way up lands in the RIGHT window, and must not count.
  TEST_ASSERT_FALSE(r.sample(2300));
  TEST_ASSERT_FALSE(r.sample(3120));
  TEST_ASSERT_TRUE(r.sample(3120));
  ASSERT_KEY(Key::None, r.key());
}

// --- OK, both layouts ---------------------------------------------------------

static void ok_short_opens() {
  expectEvents(tap(KeyLayout::TwoButtons, Key::Ok, KEY_SHORT_MAX_MS - 1),
               {Event::Press, Event::Open});
  expectEvents(tap(KeyLayout::FiveWay, Key::Ok, 50), {Event::Press, Event::Open});
}

static void ok_released_between_short_and_long_does_nothing() {
  expectEvents(tap(KeyLayout::TwoButtons, Key::Ok, KEY_SHORT_MAX_MS), {Event::Press});
  expectEvents(tap(KeyLayout::TwoButtons, Key::Ok, KEY_LONG_MS - 10), {Event::Press});
}

static void ok_hold_fires_while_still_held() {
  Gestures g(keyConfig());
  Log log;
  feed(g, log, Key::Ok, 0, KEY_LONG_MS - kTickMs);
  expectEvents(log, {Event::Press});
  g.update(Key::Ok, KEY_LONG_MS);
  log.drain(g);
  expectEvents(log, {Event::Press, Event::Hold});
  // Held on, then released: nothing more.
  feed(g, log, Key::Ok, KEY_LONG_MS + kTickMs, 3000);
  g.update(Key::None, 3010);
  log.drain(g);
  expectEvents(log, {Event::Press, Event::Hold});
}

static void hold_bar_fills_from_its_start_to_the_hold() {
  Gestures g(keyConfig());
  Log log;
  feed(g, log, Key::Ok, 0, KEY_HOLD_BAR_FROM_MS - kTickMs);
  TEST_ASSERT_EQUAL_UINT8(0, g.holdPercent(KEY_HOLD_BAR_FROM_MS - 1));
  TEST_ASSERT_EQUAL_UINT8(0, g.holdPercent(KEY_HOLD_BAR_FROM_MS));
  uint32_t half = (KEY_HOLD_BAR_FROM_MS + KEY_LONG_MS) / 2;
  TEST_ASSERT_EQUAL_UINT8(50, g.holdPercent(half));
  TEST_ASSERT_EQUAL_UINT8(99, g.holdPercent(KEY_LONG_MS - 1));
  feed(g, log, Key::Ok, KEY_HOLD_BAR_FROM_MS, KEY_LONG_MS);
  TEST_ASSERT_EQUAL_UINT8(0, g.holdPercent(KEY_LONG_MS));  // fired: the bar is gone
}

static void hold_bar_is_for_ok_only() {
  Gestures g(keyConfig(KeyLayout::FiveWay));
  Log log;
  feed(g, log, Key::Down, 0, KEY_LONG_MS - kTickMs);
  TEST_ASSERT_EQUAL_UINT8(0, g.holdPercent(KEY_LONG_MS - kTickMs));
}

// --- Two buttons --------------------------------------------------------------

static void nav_short_is_down() {
  expectEvents(tap(KeyLayout::TwoButtons, Key::Left, KEY_NAV_BACK_MS - 1),
               {Event::Press, Event::Down});
}

static void nav_held_is_back_and_fires_while_held() {
  Gestures g(keyConfig(KeyLayout::TwoButtons));
  Log log;
  feed(g, log, Key::Left, 0, KEY_NAV_BACK_MS - kTickMs);
  expectEvents(log, {Event::Press});
  feed(g, log, Key::Left, KEY_NAV_BACK_MS, 2000);
  g.update(Key::None, 2010);
  log.drain(g);
  expectEvents(log, {Event::Press, Event::Back});
}

static void two_buttons_ignore_the_other_windows() {
  for (Key k : {Key::Up, Key::Down, Key::Right}) {
    expectEvents(tap(KeyLayout::TwoButtons, k, 100), {});
    expectEvents(tap(KeyLayout::TwoButtons, k, KEY_LONG_MS + 100), {});
  }
}

static void one_button_straight_to_the_other() {
  // NAV held, OK pressed as well (the ladder reads OK), OK let go (NAV again), all released.
  Gestures g(keyConfig(KeyLayout::TwoButtons));
  Log log;
  feed(g, log, Key::Left, 0, 200);
  feed(g, log, Key::Ok, 210, 400);
  feed(g, log, Key::Left, 410, 600);
  g.update(Key::None, 610);
  log.drain(g);
  expectEvents(log,
               {Event::Press, Event::Down, Event::Press, Event::Open, Event::Press, Event::Down});
}

// --- Five-way switch ----------------------------------------------------------

static void directions_act_at_the_press() {
  expectEvents(tap(KeyLayout::FiveWay, Key::Up, 50), {Event::Press, Event::Up});
  expectEvents(tap(KeyLayout::FiveWay, Key::Down, 50), {Event::Press, Event::Down});
  expectEvents(tap(KeyLayout::FiveWay, Key::Left, 50), {Event::Press, Event::Back});
  expectEvents(tap(KeyLayout::FiveWay, Key::Right, 50), {Event::Press, Event::Open});
}

static void up_and_down_repeat_when_held() {
  // First repeat at KEY_REPEAT_DELAY_MS, then every KEY_REPEAT_MS: three within this hold.
  uint32_t releaseMs = KEY_REPEAT_DELAY_MS + 2 * KEY_REPEAT_MS + kTickMs;
  expectEvents(tap(KeyLayout::FiveWay, Key::Down, releaseMs),
               {Event::Press, Event::Down, Event::Down, Event::Down, Event::Down});
}

static void left_and_right_do_not_repeat() {
  expectEvents(tap(KeyLayout::FiveWay, Key::Left, 2000), {Event::Press, Event::Back});
  expectEvents(tap(KeyLayout::FiveWay, Key::Right, 2000), {Event::Press, Event::Open});
}

// --- Swallowing and the clock -------------------------------------------------

static void a_swallowed_gesture_says_nothing_more() {
  Gestures g(keyConfig());
  Log log;
  g.update(Key::Ok, 0);
  log.drain(g);
  g.swallow();  // e.g. the press closed an error
  feed(g, log, Key::Ok, kTickMs, KEY_LONG_MS + 100);
  TEST_ASSERT_EQUAL_UINT8(0, g.holdPercent(KEY_LONG_MS));
  g.update(Key::None, KEY_LONG_MS + 110);
  log.drain(g);
  expectEvents(log, {Event::Press});
  // The next press starts afresh.
  g.update(Key::Ok, 2000);
  g.update(Key::None, 2050);
  log.drain(g);
  expectEvents(log, {Event::Press, Event::Press, Event::Open});
}

static void survives_the_millisecond_counter_wrapping() {
  const uint32_t start = 0xFFFFFFFFu - 700;  // wraps 700 ms into the hold
  Gestures g(keyConfig());
  Log log;
  uint32_t t = start;
  for (uint32_t held = 0; held < KEY_LONG_MS; held += kTickMs, t += kTickMs) g.update(Key::Ok, t);
  log.drain(g);
  expectEvents(log, {Event::Press});
  g.update(Key::Ok, t);  // exactly KEY_LONG_MS after the press, past the wrap
  log.drain(g);
  expectEvents(log, {Event::Press, Event::Hold});

  Gestures five(keyConfig(KeyLayout::FiveWay));
  Log repeats;
  t = 0xFFFFFFFFu - 300;
  for (uint32_t held = 0; held <= KEY_REPEAT_DELAY_MS; held += kTickMs, t += kTickMs) {
    five.update(Key::Up, t);
  }
  repeats.drain(five);
  expectEvents(repeats, {Event::Press, Event::Up, Event::Up});
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(measured_levels_classify);
  RUN_TEST(bounds_are_exclusive_above);
  RUN_TEST(nav_window_is_left);
  RUN_TEST(reader_needs_equal_readings_in_a_row);
  RUN_TEST(reader_ignores_a_single_glitch);
  RUN_TEST(reader_skips_windows_passed_on_the_way);
  RUN_TEST(ok_short_opens);
  RUN_TEST(ok_released_between_short_and_long_does_nothing);
  RUN_TEST(ok_hold_fires_while_still_held);
  RUN_TEST(hold_bar_fills_from_its_start_to_the_hold);
  RUN_TEST(hold_bar_is_for_ok_only);
  RUN_TEST(nav_short_is_down);
  RUN_TEST(nav_held_is_back_and_fires_while_held);
  RUN_TEST(two_buttons_ignore_the_other_windows);
  RUN_TEST(one_button_straight_to_the_other);
  RUN_TEST(directions_act_at_the_press);
  RUN_TEST(up_and_down_repeat_when_held);
  RUN_TEST(left_and_right_do_not_repeat);
  RUN_TEST(a_swallowed_gesture_says_nothing_more);
  RUN_TEST(survives_the_millisecond_counter_wrapping);
  return UNITY_END();
}
