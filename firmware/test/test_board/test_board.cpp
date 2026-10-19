// Host tests for lib/ui's board states and screens — run with `make test`.
//
// The modes of PLAN-ESP32.md §3.6–§3.7 entered and left with the events of §3.1, and every screen
// of §3.4 rendered in both layouts with the longest values the device can show, to prove each line
// fits in 21 columns.
#include <string.h>
#include <unity.h>

#include <initializer_list>

#include "ui_config.h"
#include "ui_screens.h"

using ui::Board;
using ui::BoardOutput;
using ui::Command;
using ui::DialogKind;
using ui::Event;
using ui::Facts;
using ui::Layout;
using ui::Mode;
using ui::ScreenId;
using ui::ScreenText;
using ui::TextSize;

#define ASSERT_MODE(expected, board) \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Mode::expected), static_cast<int>((board).mode()))
#define ASSERT_SCREEN(expected, board)                        \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenId::expected), \
                        static_cast<int>((board).menu().screen()))
#define ASSERT_COMMAND(expected, output) \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Command::expected), static_cast<int>((output).command))
#define ASSERT_DIALOG(expected, board) \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DialogKind::expected), static_cast<int>((board).dialog()))

static const char* const kLongNames[] = {"Typhon 6", "Kraton 8", "Outcast8"};

// The longest values each field can take on the device.
static Facts worstCase() {
  Facts f;
  f.menu.cardOk = true;
  f.menu.hasFix = true;
  f.menu.rcSignal = true;
  f.menu.hasStartLine = true;
  f.menu.profileCount = 3;
  f.menu.activeProfile = 0;
  f.profileNames = kLongNames;
  f.profileCalibrated = true;
  f.gnssLink = true;
  f.gnssBaud = 230400;
  f.fixType = 3;
  f.latE7 = -899999999;  // the widest coordinates there are
  f.lonE7 = -1799999999;
  f.sats = 32;
  f.hAccCm = 9999;
  f.pdopX100 = 9999;
  f.ttffS = 9999;
  f.speedKmh = 199;
  f.gnssLoss = 4294967295u;
  f.freeMb = 60000;
  f.cardGb = 2000;
  f.cardKbps = 65535;
  f.trackId = "N4512E00934-99";
  f.trackSessions = 999;
  f.cardSessions = 999;
  f.nextSession = 999;
  f.laps = 999;
  f.lastLapMs = 599999;
  f.bestLapMs = 599999;
  f.deltaMs = -9999;
  f.steeringPct = -100;
  f.throttlePct = 100;
  f.steeringUs = 2200;
  f.throttleUs = 2200;
  f.geometryM = 99999;
  f.geometryPoints = 65535;
  f.usbHost = true;
  f.bleConnected = true;
  f.lipoLow = true;
  f.warning = true;
  f.firmware = "0.1.0-dev";
  f.postSummary = "all checks ok";
  return f;
}

struct Walk {
  Board board{menuTiming()};
  Facts facts = worstCase();
  uint32_t nowMs = 1000;
  BoardOutput last;

  BoardOutput key(Event e) {
    nowMs += 100;
    last = board.handle(e, facts, nowMs);
    return last;
  }
  void keys(std::initializer_list<Event> events) {
    for (Event e : events) key(e);
  }
  void down(int times) {
    for (int i = 0; i < times; ++i) key(Event::Down);
  }
  // To Settings, from Home.
  void toSettings() {
    key(Event::Open);
    down(4);
    key(Event::Open);
  }
};

void setUp() {}
void tearDown() {}

// --- Modes --------------------------------------------------------------------

static void a_session_starts_from_home_and_stops_after_confirming() {
  Walk w;
  BoardOutput o = w.key(Event::Hold);
  ASSERT_COMMAND(StartSession, o);
  ASSERT_MODE(Session, w.board);
  TEST_ASSERT_EQUAL_STRING("Session 999", o.toast);

  o = w.key(Event::Hold);  // asks first
  ASSERT_COMMAND(None, o);
  ASSERT_DIALOG(StopSession, w.board);
  w.key(Event::Back);  // any other key: no
  ASSERT_MODE(Session, w.board);
  ASSERT_DIALOG(None, w.board);

  w.key(Event::Hold);
  o = w.key(Event::Hold);  // yes
  ASSERT_COMMAND(StopSession, o);
  TEST_ASSERT_EQUAL_STRING("Session 999 saved", o.toast);
  ASSERT_MODE(Idle, w.board);
  ASSERT_SCREEN(Home, w.board);
}

static void a_session_locks_the_menu() {
  Walk w;
  w.key(Event::Hold);
  BoardOutput o = w.key(Event::Open);
  ASSERT_COMMAND(Marker, o);
  TEST_ASSERT_EQUAL_STRING("Marker 1", o.toast);
  o = w.key(Event::Open);
  TEST_ASSERT_EQUAL_STRING("Marker 2", o.toast);
  w.key(Event::Back);  // locked: no way out but the stop
  ASSERT_MODE(Session, w.board);
  w.down(1);
  TEST_ASSERT_EQUAL_INT16(1, w.board.scroll());  // DOWN turns the page instead
  w.key(Event::Up);
  TEST_ASSERT_EQUAL_INT16(0, w.board.scroll());
}

static void the_session_screen_wraps_its_pages() {
  Walk w;
  w.key(Event::Hold);
  ScreenText t;
  // Small: 11 lines in pages of 5, so three DOWNs come back to the first page.
  w.down(3);
  ui::render(w.board, w.facts, Layout::TwoButtons, TextSize::Small, w.nowMs, t);
  TEST_ASSERT_EQUAL_STRING("Lap 999", t.lines[2]);
  // Large: pages of 3, so the same three DOWNs land on the fourth page.
  ui::render(w.board, w.facts, Layout::TwoButtons, TextSize::Large, w.nowMs, t);
  TEST_ASSERT_EQUAL_STRING("Steering -100 %", t.lines[1]);
  TEST_ASSERT_EQUAL_UINT8(11, t.scrollTotal);
  TEST_ASSERT_EQUAL_UINT8(9, t.scrollFirst);
}

static void down_on_home_scrolls_and_keeps_the_menu_awake() {
  Walk w;
  w.key(Event::Open);
  w.keys({Event::Down, Event::Open});  // Track
  w.key(Event::Down);
  w.key(Event::Open);  // Track › Geometry: four lines, more than the large body shows
  ASSERT_SCREEN(Geometry, w.board);
  uint32_t entered = w.nowMs;
  w.nowMs += 20000;
  w.key(Event::Down);  // a scroll, 20 s later
  TEST_ASSERT_EQUAL_INT16(1, w.board.scroll());
  ASSERT_SCREEN(Geometry, w.board);
  w.board.tick(entered + UI_MENU_TIMEOUT_MS);  // 30 s after entering, but 10 s after the scroll
  ASSERT_SCREEN(Geometry, w.board);
  w.key(Event::Back);
  TEST_ASSERT_EQUAL_INT16(0, w.board.scroll());  // a new screen starts at its top
}

static void the_stop_dialog_times_out_into_the_session() {
  Walk w;
  w.key(Event::Hold);
  w.key(Event::Hold);
  ASSERT_DIALOG(StopSession, w.board);
  w.board.tick(w.nowMs + UI_DIALOG_TIMEOUT_MS);
  ASSERT_DIALOG(None, w.board);
  ASSERT_MODE(Session, w.board);
}

static void geometry_records_until_held() {
  Walk w;
  w.keys({Event::Open, Event::Down, Event::Open, Event::Down, Event::Open});  // Track › Geometry
  ASSERT_SCREEN(Geometry, w.board);
  ASSERT_COMMAND(EnterGeometry, w.key(Event::Hold));
  ASSERT_MODE(Geometry, w.board);
  w.keys({Event::Open, Event::Down, Event::Back});
  ASSERT_MODE(Geometry, w.board);
  BoardOutput o = w.key(Event::Hold);
  ASSERT_COMMAND(StopGeometry, o);
  TEST_ASSERT_EQUAL_STRING("Geometry saved", o.toast);
  ASSERT_MODE(Idle, w.board);
  ASSERT_SCREEN(Geometry, w.board);  // back where it was entered from
}

static void calibration_walks_its_steps() {
  Walk w;
  w.toSettings();
  w.down(1);
  w.key(Event::Open);  // Settings › Calibrate
  ASSERT_COMMAND(EnterCalibration, w.key(Event::Hold));
  ASSERT_MODE(Calibration, w.board);
  for (uint8_t step = 0; step < ui::kCalibrationSteps - 1; ++step) {
    BoardOutput o = w.key(Event::Open);
    ASSERT_COMMAND(CalibrationCapture, o);
    TEST_ASSERT_EQUAL_UINT8(step, o.arg);
  }
  w.key(Event::Back);  // a step back...
  TEST_ASSERT_EQUAL_UINT8(ui::kCalibrationSteps - 2, w.board.calibrationStep());
  w.key(Event::Open);  // ...and forward again
  w.key(Event::Down);  // DOWN skips the aux step
  TEST_ASSERT_EQUAL_UINT8(ui::kCalibrationSteps, w.board.calibrationStep());

  w.key(Event::Hold);  // the profile was calibrated: asks first
  ASSERT_DIALOG(OverwriteCalibration, w.board);
  BoardOutput o = w.key(Event::Hold);
  ASSERT_COMMAND(SaveCalibration, o);
  TEST_ASSERT_EQUAL_STRING("Calibration saved", o.toast);
  ASSERT_MODE(Idle, w.board);
  ASSERT_SCREEN(Calibrate, w.board);
}

static void calibration_cancels_at_its_first_step() {
  Walk w;
  w.facts.profileCalibrated = false;
  w.toSettings();
  w.down(1);
  w.keys({Event::Open, Event::Hold});
  ASSERT_MODE(Calibration, w.board);
  BoardOutput o = w.key(Event::Back);
  ASSERT_COMMAND(CancelCalibration, o);
  TEST_ASSERT_EQUAL_STRING("Calibration cancelled", o.toast);
  ASSERT_MODE(Idle, w.board);
}

static void an_uncalibrated_profile_saves_without_asking() {
  Walk w;
  w.facts.profileCalibrated = false;
  w.toSettings();
  w.down(1);
  w.keys({Event::Open, Event::Hold});
  for (uint8_t i = 0; i < ui::kCalibrationSteps; ++i) w.key(Event::Open);
  ASSERT_COMMAND(SaveCalibration, w.key(Event::Hold));
  ASSERT_MODE(Idle, w.board);
}

static void transfer_asks_before_leaving_a_host() {
  Walk w;
  w.key(Event::Open);
  w.down(3);
  ASSERT_COMMAND(EnterTransfer, w.key(Event::Open));
  ASSERT_MODE(Transfer, w.board);
  w.key(Event::Back);
  ASSERT_DIALOG(LeaveTransfer, w.board);
  ASSERT_COMMAND(LeaveTransfer, w.key(Event::Hold));
  ASSERT_MODE(Idle, w.board);

  w.facts.usbHost = false;  // no host: straight out
  w.key(Event::Open);
  ASSERT_MODE(Transfer, w.board);
  ASSERT_COMMAND(LeaveTransfer, w.key(Event::Back));
  ASSERT_MODE(Idle, w.board);
}

static void refusals_and_settings_come_back_as_toasts() {
  Walk w;
  w.facts.menu.loggingAllowed = false;
  w.facts.menu.refusingCheck = 8;
  BoardOutput o = w.key(Event::Hold);
  ASSERT_MODE(Idle, w.board);
  TEST_ASSERT_EQUAL_STRING("Logging refused: 8", o.toast);

  w.toSettings();
  w.key(Event::Open);  // Car profile
  w.down(2);
  o = w.key(Event::Open);
  ASSERT_COMMAND(SelectProfile, o);
  TEST_ASSERT_EQUAL_STRING("Profile: Outcast8", o.toast);
}

static void the_menu_timeout_counts_from_the_end_of_a_mode() {
  Walk w;
  w.key(Event::Open);
  w.down(3);
  w.key(Event::Open);  // Transfer
  w.nowMs += 60000;    // a minute in Transfer
  w.facts.usbHost = false;
  w.key(Event::Back);
  uint32_t back = w.nowMs;
  w.board.tick(back + UI_MENU_TIMEOUT_MS - 1);
  ASSERT_SCREEN(Menu, w.board);
  w.board.tick(back + UI_MENU_TIMEOUT_MS);
  ASSERT_SCREEN(Home, w.board);
}

// --- Screens ------------------------------------------------------------------

static void expectFits(const Walk& w, Layout layout, TextSize size, const char* where) {
  ScreenText text;
  // Two hours into a session, for the longest elapsed time.
  ui::render(w.board, w.facts, layout, size, w.nowMs + 7200000, text);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, text.overflow, where);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(ui::screenLines(size), text.count, where);
  for (const auto& line : text.lines) TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(21, strlen(line), where);
}

// Every screen of §3.4 and every mode, by the keys that reach it from Home.
struct Path {
  const char* name;
  std::initializer_list<Event> keys;
};

static void every_screen_fits_in_21_columns() {
  const Path paths[] = {
      {"Home", {}},
      {"Home, page 2", {Event::Down}},
      {"Home, page 3", {Event::Down, Event::Down}},
      {"Menu", {Event::Open}},
      {"Menu, on Back",
       {Event::Open, Event::Down, Event::Down, Event::Down, Event::Down, Event::Down}},
      {"Session entry", {Event::Open, Event::Open}},
      {"Track", {Event::Open, Event::Down, Event::Open}},
      {"Track, on Back",
       {Event::Open, Event::Down, Event::Open, Event::Down, Event::Down, Event::Down, Event::Down}},
      {"Start line", {Event::Open, Event::Down, Event::Open, Event::Open}},
      {"Start line dialog", {Event::Open, Event::Down, Event::Open, Event::Open, Event::Hold}},
      {"Geometry entry", {Event::Open, Event::Down, Event::Open, Event::Down, Event::Open}},
      {"Track info",
       {Event::Open, Event::Down, Event::Open, Event::Down, Event::Down, Event::Open}},
      {"New track",
       {Event::Open, Event::Down, Event::Open, Event::Down, Event::Down, Event::Down, Event::Open}},
      {"GNSS", {Event::Open, Event::Down, Event::Down, Event::Open}},
      {"Settings", {Event::Open, Event::Down, Event::Down, Event::Down, Event::Down, Event::Open}},
      {"Car profile",
       {Event::Open, Event::Down, Event::Down, Event::Down, Event::Down, Event::Open, Event::Open}},
      {"Car profile, on New car",
       {Event::Open, Event::Down, Event::Down, Event::Down, Event::Down, Event::Open, Event::Open,
        Event::Down, Event::Down, Event::Down}},
      {"Car profile, on Back",
       {Event::Open, Event::Down, Event::Down, Event::Down, Event::Down, Event::Open, Event::Open,
        Event::Down, Event::Down, Event::Down, Event::Down}},
      {"Calibrate",
       {Event::Open, Event::Down, Event::Down, Event::Down, Event::Down, Event::Open, Event::Down,
        Event::Open}},
      {"Display",
       {Event::Open, Event::Down, Event::Down, Event::Down, Event::Down, Event::Open, Event::Down,
        Event::Down, Event::Open}},
      {"Brightness",
       {Event::Open, Event::Down, Event::Down, Event::Down, Event::Down, Event::Open, Event::Down,
        Event::Down, Event::Open, Event::Open}},
      {"Dim after",
       {Event::Open, Event::Down, Event::Down, Event::Down, Event::Down, Event::Open, Event::Down,
        Event::Down, Event::Open, Event::Down, Event::Open}},
      {"Text size",
       {Event::Open, Event::Down, Event::Down, Event::Down, Event::Down, Event::Open, Event::Down,
        Event::Down, Event::Open, Event::Down, Event::Down, Event::Open}},
      {"Delete sessions",
       {Event::Open, Event::Down, Event::Down, Event::Down, Event::Down, Event::Open, Event::Down,
        Event::Down, Event::Down, Event::Open}},
      {"Delete dialog",
       {Event::Open, Event::Down, Event::Down, Event::Down, Event::Down, Event::Open, Event::Down,
        Event::Down, Event::Down, Event::Open, Event::Hold}},
      {"About",
       {Event::Open, Event::Down, Event::Down, Event::Down, Event::Down, Event::Open, Event::Down,
        Event::Down, Event::Down, Event::Down, Event::Open}},
      {"Restart",
       {Event::Open, Event::Down, Event::Down, Event::Down, Event::Down, Event::Open, Event::Down,
        Event::Down, Event::Down, Event::Down, Event::Down, Event::Open}},
      {"Session, laps", {Event::Hold}},
      {"Session, page 2", {Event::Hold, Event::Down}},
      {"Session, page 3", {Event::Hold, Event::Down, Event::Down}},
      {"Session, page 4", {Event::Hold, Event::Down, Event::Down, Event::Down}},
      {"Stop dialog", {Event::Hold, Event::Hold}},
      {"Geometry mode",
       {Event::Open, Event::Down, Event::Open, Event::Down, Event::Open, Event::Hold}},
      {"Calibration step",
       {Event::Open, Event::Down, Event::Down, Event::Down, Event::Down, Event::Open, Event::Down,
        Event::Open, Event::Hold}},
      {"Calibration aux",
       {Event::Open, Event::Down, Event::Down, Event::Down, Event::Down, Event::Open, Event::Down,
        Event::Open, Event::Hold, Event::Open, Event::Open, Event::Open, Event::Open, Event::Open}},
      {"Calibration summary",
       {Event::Open, Event::Down, Event::Down, Event::Down, Event::Down, Event::Open, Event::Down,
        Event::Open, Event::Hold, Event::Open, Event::Open, Event::Open, Event::Open, Event::Open,
        Event::Open}},
      {"Overwrite calibration",
       {Event::Open, Event::Down, Event::Down, Event::Down, Event::Down, Event::Open, Event::Down,
        Event::Open, Event::Hold, Event::Open, Event::Open, Event::Open, Event::Open, Event::Open,
        Event::Open, Event::Hold}},
      {"Transfer", {Event::Open, Event::Down, Event::Down, Event::Down, Event::Open}},
      {"Leave transfer",
       {Event::Open, Event::Down, Event::Down, Event::Down, Event::Open, Event::Back}},
  };
  // With the cell id, and with a name from the apps longer than any screen has room for.
  for (const char* name : {"", "Pista Internazionale Ambrosiana"}) {
    for (const Path& p : paths) {
      for (Layout layout : {Layout::TwoButtons, Layout::FiveWay}) {
        for (TextSize size : {TextSize::Small, TextSize::Large}) {
          Walk w;
          w.facts.trackName = name;
          for (Event e : p.keys) w.key(e);
          expectFits(w, layout, size, p.name);
        }
      }
    }
  }
}

static void the_home_screen_reads_as_designed() {
  Walk w;
  ScreenText t;
  ui::render(w.board, w.facts, Layout::TwoButtons, TextSize::Small, w.nowMs, t);
  TEST_ASSERT_EQUAL_STRING("Typhon 6 3D 32  58GB", t.lines[0]);
  TEST_ASSERT_EQUAL_STRING("Ready          ! U Li", t.lines[1]);  // marks right-aligned
  TEST_ASSERT_EQUAL_STRING("Track N4512E00934-99", t.lines[2]);
  TEST_ASSERT_EQUAL_STRING("Last  9:59.999   L999", t.lines[4]);
  TEST_ASSERT_EQUAL_STRING("OK menu  hold: start", t.lines[7]);
  TEST_ASSERT_EQUAL_UINT8(0, t.scrollTotal);  // five lines in five rows: nothing to scroll

  ui::render(w.board, w.facts, Layout::TwoButtons, TextSize::Large, w.nowMs, t);
  TEST_ASSERT_EQUAL_STRING("Ready       3D32  !UL", t.lines[0]);
  TEST_ASSERT_EQUAL_STRING("Track N4512E00934-99", t.lines[1]);
  TEST_ASSERT_EQUAL_STRING("Last  9:59.999   L999", t.lines[3]);
  TEST_ASSERT_EQUAL_STRING("OK menu  hold: start", t.lines[4]);
  TEST_ASSERT_EQUAL_UINT8(7, t.scrollTotal);  // the profile and free space join the content
}

static void the_gnss_screen_reads_as_designed() {
  Walk w;
  w.keys({Event::Open, Event::Down, Event::Down, Event::Open});  // Menu › GNSS
  ScreenText t;
  ui::render(w.board, w.facts, Layout::TwoButtons, TextSize::Large, w.nowMs, t);
  TEST_ASSERT_EQUAL_STRING("Link ok, 230400 bd", t.lines[1]);
  TEST_ASSERT_EQUAL_STRING("Fix 3D, 32 sats", t.lines[2]);
  TEST_ASSERT_EQUAL_STRING("Track N4512E00934-99", t.lines[3]);
  w.key(Event::Down);  // the next page: the coordinates
  ui::render(w.board, w.facts, Layout::TwoButtons, TextSize::Large, w.nowMs, t);
  TEST_ASSERT_EQUAL_STRING("Lat 89.999999 S", t.lines[1]);
  TEST_ASSERT_EQUAL_STRING("Lon 179.999999 W", t.lines[2]);

  // A name from the apps takes the cell id's place; without a link or a fix, no coordinates.
  w.facts.trackName = "Ambrosiana";
  w.facts.gnssLink = false;
  w.facts.fixType = 0;
  w.key(Event::Up);
  ui::render(w.board, w.facts, Layout::TwoButtons, TextSize::Large, w.nowMs, t);
  TEST_ASSERT_EQUAL_STRING("Link: no GNSS", t.lines[1]);
  TEST_ASSERT_EQUAL_STRING("No fix, 32 sats", t.lines[2]);
  TEST_ASSERT_EQUAL_STRING("Track Ambrosiana", t.lines[3]);
  w.key(Event::Down);
  ui::render(w.board, w.facts, Layout::TwoButtons, TextSize::Large, w.nowMs, t);
  TEST_ASSERT_EQUAL_STRING("Lat --", t.lines[1]);
}

static void hints_follow_the_layout() {
  Walk w;
  w.key(Event::Open);
  ScreenText two, five;
  ui::render(w.board, w.facts, Layout::TwoButtons, TextSize::Small, w.nowMs, two);
  ui::render(w.board, w.facts, Layout::FiveWay, TextSize::Small, w.nowMs, five);
  TEST_ASSERT_EQUAL_STRING("NAV next  OK open", two.lines[7]);
  TEST_ASSERT_EQUAL_STRING("< back   OK open", five.lines[7]);
  TEST_ASSERT_EQUAL_STRING("> Session", two.lines[2]);
  TEST_ASSERT_EQUAL_STRING("  Track", two.lines[3]);
}

static void a_list_keeps_its_cursor_in_view() {
  Walk w;
  w.key(Event::Open);
  w.down(4);  // Settings, the fifth of five entries
  ScreenText t;
  ui::render(w.board, w.facts, Layout::TwoButtons, TextSize::Large, w.nowMs, t);
  TEST_ASSERT_EQUAL_STRING("  GNSS", t.lines[1]);
  TEST_ASSERT_EQUAL_STRING("  Transfer", t.lines[2]);
  TEST_ASSERT_EQUAL_STRING("> Settings", t.lines[3]);
  TEST_ASSERT_EQUAL_UINT8(6, t.scrollTotal);  // Back follows Settings
  TEST_ASSERT_EQUAL_UINT8(2, t.scrollFirst);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(a_session_starts_from_home_and_stops_after_confirming);
  RUN_TEST(a_session_locks_the_menu);
  RUN_TEST(the_session_screen_wraps_its_pages);
  RUN_TEST(down_on_home_scrolls_and_keeps_the_menu_awake);
  RUN_TEST(the_stop_dialog_times_out_into_the_session);
  RUN_TEST(geometry_records_until_held);
  RUN_TEST(calibration_walks_its_steps);
  RUN_TEST(calibration_cancels_at_its_first_step);
  RUN_TEST(an_uncalibrated_profile_saves_without_asking);
  RUN_TEST(transfer_asks_before_leaving_a_host);
  RUN_TEST(refusals_and_settings_come_back_as_toasts);
  RUN_TEST(the_menu_timeout_counts_from_the_end_of_a_mode);
  RUN_TEST(every_screen_fits_in_21_columns);
  RUN_TEST(the_home_screen_reads_as_designed);
  RUN_TEST(the_gnss_screen_reads_as_designed);
  RUN_TEST(hints_follow_the_layout);
  RUN_TEST(a_list_keeps_its_cursor_in_view);
  return UNITY_END();
}
