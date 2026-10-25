// Host tests for sleep — run with `make test`.
//
// PLAN-ESP32.md §3.7 and §4.5 with the device's own timings (ui_config.h): in IDLE the receiver's
// pulses stopping, or five minutes with nothing, give a notice that any key cancels, and then
// sleep; a USB host keeps the device awake; and a session is never ended by the pulses, since
// without a backup battery the car switched off is the device switched off (§2.7).
#include <unity.h>

#include <string.h>

#include <initializer_list>

#include "ui_board.h"
#include "ui_config.h"
#include "ui_screens.h"

using namespace ui;

void setUp() {}
void tearDown() {}

#define ASSERT_COMMAND(expected, output) \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Command::expected), static_cast<int>((output).command))
#define ASSERT_MODE(expected, board) \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Mode::expected), static_cast<int>((board).mode()))
#define ASSERT_SLEEP(expected, board)                        \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SleepWhy::expected), \
                        static_cast<int>((board).sleepNotice()))

constexpr uint32_t kTick = 10;  // the ui task's 100 Hz

static Power carOn() {
  Power p;
  p.rcLive = true;
  return p;
}
static Power carOff() { return Power(); }
static Power withHost(Power p) {
  p.usbHost = true;
  return p;
}

struct Run {
  Board board{menuTiming()};
  Facts facts;
  uint32_t nowMs = 1000;

  Run() {
    board.setPowerTiming(powerTiming());
    facts.nextSession = 4;
    facts.menu.hasFix = true;  // what a session needs to start (§3.6)
  }
  // `p` every tick for `ms`; the last output that did something.
  BoardOutput feed(const Power& p, uint32_t ms) {
    BoardOutput seen;
    for (uint32_t t = 0; t < ms; t += kTick) {
      nowMs += kTick;
      board.tick(nowMs);
      BoardOutput o = board.onPower(p, facts, nowMs);
      if (o.command != Command::None || o.toast[0]) seen = o;
    }
    return seen;
  }
  BoardOutput key(Event e) {
    nowMs += 100;
    return board.handle(e, facts, nowMs);
  }
  // A session started by hand, with the car on.
  void startSession() {
    feed(carOn(), 1000);
    key(Event::Hold);
    feed(carOn(), 1000);
  }
  ScreenText screen() {
    ScreenText t;
    ui::render(board, facts, Layout::TwoButtons, TextSize::Large, nowMs, t);
    return t;
  }
};

// --- Sessions -----------------------------------------------------------------------

// The pulses stopping in a session — the transmitter switched off, say, with the car still on —
// neither end it nor bring a notice: a session ends by hand or standing still.
static void a_session_ignores_the_pulses_stopping() {
  Run r;
  r.startSession();
  ASSERT_MODE(Session, r.board);
  ASSERT_COMMAND(None, r.feed(carOff(), 20 * 60 * 1000));
  ASSERT_MODE(Session, r.board);
  ASSERT_SLEEP(None, r.board);
}

// Stopped by hand with the car off, it is the five minutes from the stop that bring the notice.
static void the_idle_minutes_count_from_a_session_end() {
  Run r;
  r.startSession();
  r.feed(carOff(), 10 * 60 * 1000);
  r.key(Event::Hold);
  r.key(Event::Hold);  // confirmed
  ASSERT_MODE(Idle, r.board);
  r.feed(carOff(), SLEEP_IDLE_MS - 300);
  ASSERT_SLEEP(None, r.board);
  r.feed(carOff(), 300);
  ASSERT_SLEEP(Idle, r.board);
}

// --- IDLE -----------------------------------------------------------------------------

// The car switched off in IDLE: ten seconds of notice, counted down, then sleep.
static void the_car_switched_off_in_idle_gives_ten_seconds() {
  Run r;
  r.feed(carOn(), 1000);
  ASSERT_SLEEP(None, r.board);
  r.feed(carOff(), kTick);
  ASSERT_SLEEP(CarOff, r.board);
  ScreenText t = r.screen();
  TEST_ASSERT_EQUAL_STRING_LEN("Sleep ", t.lines[0], 6);
  TEST_ASSERT_EQUAL_STRING("Car switched off", t.lines[1]);
  TEST_ASSERT_EQUAL_STRING("Sleep in 10 s", t.lines[2]);
  TEST_ASSERT_EQUAL_STRING("any key: cancel", t.lines[4]);
  r.feed(carOff(), 3000);
  TEST_ASSERT_EQUAL_STRING("Sleep in 7 s", r.screen().lines[2]);
  ASSERT_COMMAND(None, r.feed(carOff(), SLEEP_NOTICE_MS - 3000 - 2 * kTick));
  ASSERT_COMMAND(Sleep, r.feed(carOff(), 2 * kTick));
}

// Any key cancels the notice, and does nothing else: its Press is swallowed. The five minutes count
// from that key.
static void any_key_cancels_the_notice() {
  Run r;
  r.feed(carOn(), 1000);
  r.feed(carOff(), 3000);
  BoardOutput o = r.key(Event::Press);
  TEST_ASSERT_TRUE(o.swallow);
  TEST_ASSERT_EQUAL_STRING("Sleep cancelled", o.toast);
  ASSERT_SLEEP(None, r.board);
  ASSERT_COMMAND(None, r.feed(carOff(), SLEEP_IDLE_MS - 2 * kTick));
  ASSERT_SLEEP(None, r.board);
  r.feed(carOff(), 2 * kTick);
  ASSERT_SLEEP(Idle, r.board);
}

// The car back on, or a USB host plugged in, cancels it as well.
static void the_car_back_on_or_a_host_cancels_the_notice() {
  Run r;
  r.feed(carOn(), 1000);
  r.feed(carOff(), 5000);
  r.feed(carOn(), kTick);
  ASSERT_SLEEP(None, r.board);
  r.feed(carOff(), 5000);
  ASSERT_SLEEP(CarOff, r.board);
  r.feed(withHost(carOff()), kTick);
  ASSERT_SLEEP(None, r.board);
  ASSERT_COMMAND(None, r.feed(withHost(carOff()), 20 * 60 * 1000));
}

// Five minutes in IDLE with no RC signal, no USB host and no key: the same notice, then sleep.
static void five_minutes_with_nothing_then_the_notice() {
  Run r;
  r.feed(carOff(), SLEEP_IDLE_MS - 1000 - kTick);  // from boot, the clock at 1 s
  ASSERT_SLEEP(None, r.board);
  r.key(Event::Down);  // a key: the count starts over
  r.feed(carOff(), SLEEP_IDLE_MS - 200);
  ASSERT_SLEEP(None, r.board);
  r.feed(carOff(), 200);
  ASSERT_SLEEP(Idle, r.board);
  TEST_ASSERT_EQUAL_STRING("Nothing for 5 min", r.screen().lines[1]);
  ASSERT_COMMAND(Sleep, r.feed(carOff(), SLEEP_NOTICE_MS));
}

// The car on, parked in the pits, is activity: no sleep however long.
static void a_car_left_on_keeps_it_awake() {
  Run r;
  ASSERT_COMMAND(None, r.feed(carOn(), 30 * 60 * 1000));
  ASSERT_SLEEP(None, r.board);
}

// Outside IDLE nothing sleeps — TRANSFER, CALIBRATION — and the five minutes count from the end of
// the mode.
static void only_idle_sleeps() {
  Run r;
  r.key(Event::Open);  // Menu
  for (int i = 0; i < 3; ++i) r.key(Event::Down);
  r.key(Event::Open);  // Transfer
  ASSERT_MODE(Transfer, r.board);
  ASSERT_COMMAND(None, r.feed(carOff(), 20 * 60 * 1000));
  r.key(Event::Open);  // out, no host attached
  ASSERT_MODE(Idle, r.board);
  r.feed(carOff(), SLEEP_IDLE_MS - 300);
  ASSERT_SLEEP(None, r.board);
  r.feed(carOff(), 300);
  ASSERT_SLEEP(Idle, r.board);
}

// The notice in 21 columns, with both text sizes and both reasons.
static void the_notice_fits() {
  Run i;
  i.feed(carOff(), SLEEP_IDLE_MS + kTick);
  ASSERT_SLEEP(Idle, i.board);
  Run c;
  c.feed(carOn(), 1000);
  c.feed(carOff(), kTick);
  ASSERT_SLEEP(CarOff, c.board);
  for (Run* r : {&i, &c}) {
    for (TextSize size : {TextSize::Small, TextSize::Large}) {
      ScreenText t;
      ui::render(r->board, r->facts, Layout::FiveWay, size, r->nowMs, t);
      TEST_ASSERT_EQUAL_UINT8(0, t.overflow);
      for (const auto& line : t.lines) TEST_ASSERT_LESS_OR_EQUAL(21, strlen(line));
    }
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(a_session_ignores_the_pulses_stopping);
  RUN_TEST(the_idle_minutes_count_from_a_session_end);
  RUN_TEST(the_car_switched_off_in_idle_gives_ten_seconds);
  RUN_TEST(any_key_cancels_the_notice);
  RUN_TEST(the_car_back_on_or_a_host_cancels_the_notice);
  RUN_TEST(five_minutes_with_nothing_then_the_notice);
  RUN_TEST(a_car_left_on_keeps_it_awake);
  RUN_TEST(only_idle_sleeps);
  RUN_TEST(the_notice_fits);
  return UNITY_END();
}
