// Host tests for lib/ui's menu — run with `make test`.
//
// The menu of PLAN-ESP32.md §3.4 walked with the events of §3.1, using the timeouts of config.h
// (ui_config.h). Only DOWN, OK and back are needed to reach everything, as with the two buttons;
// UP is checked on its own.
#include <string.h>
#include <unity.h>

#include <initializer_list>

#include "ui_config.h"

using ui::Command;
using ui::DialogKind;
using ui::Event;
using ui::Menu;
using ui::MenuContext;
using ui::MenuResult;
using ui::Refusal;
using ui::ScreenId;

// Unity compares integers; the menu's types are enum classes.
#define ASSERT_SCREEN(expected, menu) \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenId::expected), static_cast<int>((menu).screen()))
#define ASSERT_COMMAND(expected, result) \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Command::expected), static_cast<int>((result).command))
#define ASSERT_REFUSAL(expected, result) \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Refusal::expected), static_cast<int>((result).refusal))

// A menu, the facts it reads, and a clock that moves 100 ms per key.
struct Walk {
  Menu menu{menuTiming()};
  MenuContext context;
  uint32_t nowMs = 1000;
  MenuResult last;

  MenuResult key(Event event) {
    nowMs += 100;
    last = menu.handle(event, context, nowMs);
    return last;
  }
  void keys(std::initializer_list<Event> events) {
    for (Event e : events) key(e);
  }
  void down(int times) {
    for (int i = 0; i < times; ++i) key(Event::Down);
  }
};

void setUp() {}
void tearDown() {}

// --- Navigation ---------------------------------------------------------------

static void ok_on_home_opens_the_menu_and_back_returns() {
  Walk w;
  ASSERT_SCREEN(Home, w.menu);
  w.key(Event::Open);
  ASSERT_SCREEN(Menu, w.menu);
  TEST_ASSERT_EQUAL_UINT8(0, w.menu.cursor());
  w.key(Event::Back);
  ASSERT_SCREEN(Home, w.menu);
}

static void home_ignores_down_and_back() {
  Walk w;
  w.keys({Event::Down, Event::Back, Event::Up});
  ASSERT_SCREEN(Home, w.menu);
}

static void down_reaches_every_entry_and_wraps() {
  Walk w;
  w.key(Event::Open);
  uint8_t count = 0;
  Menu::entries(ScreenId::Menu, count);
  for (uint8_t i = 1; i < count; ++i) {
    w.key(Event::Down);
    TEST_ASSERT_EQUAL_UINT8(i, w.menu.cursor());
  }
  w.key(Event::Down);
  TEST_ASSERT_EQUAL_UINT8(0, w.menu.cursor());  // wrapped: DOWN alone reaches everything
  w.key(Event::Up);
  TEST_ASSERT_EQUAL_UINT8(count - 1, w.menu.cursor());  // and UP wraps the other way
}

static void walk_to_brightness_and_save() {
  Walk w;
  w.context.brightness = 2;
  w.key(Event::Open);  // Menu
  w.down(4);           // Settings
  w.key(Event::Open);
  ASSERT_SCREEN(Settings, w.menu);
  w.down(2);  // Display
  w.key(Event::Open);
  ASSERT_SCREEN(DisplaySettings, w.menu);
  w.key(Event::Open);  // Brightness
  ASSERT_SCREEN(Brightness, w.menu);
  TEST_ASSERT_EQUAL_UINT8(2, w.menu.cursor());  // opens on the value in force
  w.down(2);                                    // Back, then wraps to "low"
  TEST_ASSERT_EQUAL_UINT8(0, w.menu.cursor());
  MenuResult r = w.key(Event::Open);
  ASSERT_COMMAND(SetBrightness, r);
  TEST_ASSERT_EQUAL_UINT8(0, r.arg);
  ASSERT_SCREEN(DisplaySettings, w.menu);
  TEST_ASSERT_EQUAL_UINT8(0, w.menu.cursor());  // still on the Brightness entry
}

static void back_in_an_editor_cancels() {
  Walk w;
  w.context.dimDelay = 3;
  w.key(Event::Open);
  w.down(4);
  w.key(Event::Open);
  w.down(2);
  w.keys({Event::Open, Event::Down, Event::Open});  // Display › Dim after
  ASSERT_SCREEN(DimDelay, w.menu);
  TEST_ASSERT_EQUAL_UINT8(3, w.menu.cursor());
  w.key(Event::Down);
  MenuResult r = w.key(Event::Back);
  ASSERT_COMMAND(None, r);
  ASSERT_SCREEN(DisplaySettings, w.menu);
}

static void back_keeps_each_lists_cursor() {
  Walk w;
  w.key(Event::Open);
  w.down(4);
  w.key(Event::Open);  // Settings
  w.down(3);           // Delete sessions
  w.key(Event::Back);
  ASSERT_SCREEN(Menu, w.menu);
  TEST_ASSERT_EQUAL_UINT8(4, w.menu.cursor());
  w.key(Event::Open);
  TEST_ASSERT_EQUAL_UINT8(3, w.menu.cursor());
}

static void transfer_is_a_command_not_a_screen() {
  Walk w;
  w.key(Event::Open);
  w.down(3);
  MenuResult r = w.key(Event::Open);
  ASSERT_COMMAND(EnterTransfer, r);
  ASSERT_SCREEN(Menu, w.menu);
}

static void text_size_is_a_display_setting() {
  Walk w;
  w.context.textSize = 1;  // large
  w.key(Event::Open);
  w.down(4);
  w.key(Event::Open);
  w.down(2);
  w.key(Event::Open);  // Settings › Display
  w.down(2);
  w.key(Event::Open);  // › Text size
  ASSERT_SCREEN(TextSize, w.menu);
  TEST_ASSERT_EQUAL_UINT8(1, w.menu.cursor());
  w.down(2);  // Back, then wraps to small
  MenuResult r = w.key(Event::Open);
  ASSERT_COMMAND(SetTextSize, r);
  TEST_ASSERT_EQUAL_UINT8(0, r.arg);  // small
  ASSERT_SCREEN(DisplaySettings, w.menu);
}

static void every_list_and_editor_ends_in_back() {
  for (ScreenId list :
       {ScreenId::Menu, ScreenId::Track, ScreenId::Settings, ScreenId::DisplaySettings}) {
    uint8_t count = 0;
    const ui::Entry* entries = Menu::entries(list, count);
    TEST_ASSERT_EQUAL_STRING("Back", entries[count - 1].label);
  }
  // Settings › Display › Back, then Settings › Back, then Menu › Back: Home, NAV and OK only.
  Walk w;
  w.key(Event::Open);
  w.down(4);
  w.key(Event::Open);
  w.down(2);
  w.key(Event::Open);
  w.down(3);
  w.key(Event::Open);
  ASSERT_SCREEN(Settings, w.menu);
  w.down(3);  // from Display to Back
  w.key(Event::Open);
  ASSERT_SCREEN(Menu, w.menu);
  w.down(1);  // from Settings to Back
  w.key(Event::Open);
  ASSERT_SCREEN(Home, w.menu);
}

static void an_editors_back_saves_nothing() {
  Walk w;
  w.context.brightness = 1;
  w.key(Event::Open);
  w.down(4);
  w.keys({Event::Open, Event::Down, Event::Down, Event::Open, Event::Open});  // › Brightness
  ASSERT_SCREEN(Brightness, w.menu);
  w.down(2);  // high, then Back
  MenuResult r = w.key(Event::Open);
  ASSERT_COMMAND(None, r);
  ASSERT_SCREEN(DisplaySettings, w.menu);
}

static void ok_on_a_leaf_goes_back() {
  for (int down : {0, 2}) {  // Session, then GNSS
    Walk w;
    w.key(Event::Open);
    w.down(down);
    w.key(Event::Open);
    TEST_ASSERT_TRUE(Menu::scrolls(w.menu.screen()));
    ASSERT_COMMAND(None, w.key(Event::Open));
    ASSERT_SCREEN(Menu, w.menu);
    TEST_ASSERT_EQUAL_UINT8(down, w.menu.cursor());
  }
}

static void car_profile_editor_spans_the_profiles() {
  Walk w;
  w.context.profileCount = 3;
  w.context.activeProfile = 1;
  w.key(Event::Open);
  w.down(4);
  w.keys({Event::Open, Event::Open});  // Settings › Car profile
  ASSERT_SCREEN(CarProfile, w.menu);
  TEST_ASSERT_EQUAL_UINT8(1, w.menu.cursor());
  w.down(3);  // 2, Back, then wraps to 0
  TEST_ASSERT_EQUAL_UINT8(0, w.menu.cursor());
  MenuResult r = w.key(Event::Open);
  ASSERT_COMMAND(SelectProfile, r);
  TEST_ASSERT_EQUAL_UINT8(0, r.arg);
  ASSERT_SCREEN(Settings, w.menu);
}

// --- Actions and refusals -----------------------------------------------------

static void holding_ok_on_home_or_session_starts_a_session() {
  Walk w;
  ASSERT_COMMAND(StartSession, w.key(Event::Hold));
  w.keys({Event::Open, Event::Open});  // Menu › Session
  ASSERT_SCREEN(Session, w.menu);
  ASSERT_COMMAND(StartSession, w.key(Event::Hold));
}

static void a_session_needs_the_card_and_logging() {
  Walk w;
  w.context.cardOk = false;
  MenuResult r = w.key(Event::Hold);
  ASSERT_COMMAND(None, r);
  ASSERT_REFUSAL(NoCard, r);
  w.context.cardOk = true;
  w.context.loggingAllowed = false;
  w.context.refusingCheck = 8;
  r = w.key(Event::Hold);
  ASSERT_REFUSAL(LoggingRefused, r);
  TEST_ASSERT_EQUAL_UINT8(8, r.arg);
}

static void start_line_is_recorded_directly_when_there_is_none() {
  Walk w;
  w.context.hasFix = true;
  w.keys({Event::Open, Event::Down, Event::Open, Event::Open});  // Track › Start line
  ASSERT_SCREEN(StartLine, w.menu);
  ASSERT_COMMAND(RecordStartLine, w.key(Event::Hold));
}

static void overwriting_a_start_line_asks_first() {
  Walk w;
  w.context.hasFix = true;
  w.context.hasStartLine = true;
  w.keys({Event::Open, Event::Down, Event::Open, Event::Open});
  MenuResult r = w.key(Event::Hold);
  ASSERT_COMMAND(None, r);
  ASSERT_SCREEN(Dialog, w.menu);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DialogKind::OverwriteStartLine),
                        static_cast<int>(w.menu.dialog()));
  r = w.key(Event::Hold);  // hold OK: yes
  ASSERT_COMMAND(RecordStartLine, r);
  ASSERT_SCREEN(StartLine, w.menu);
}

static void any_other_key_answers_no() {
  for (Event no : {Event::Open, Event::Down, Event::Up, Event::Back}) {
    Walk w;
    w.context.hasFix = true;
    w.context.hasStartLine = true;
    w.keys({Event::Open, Event::Down, Event::Open, Event::Open, Event::Hold});
    ASSERT_SCREEN(Dialog, w.menu);
    MenuResult r = w.key(Event::Press);  // the press alone decides nothing
    ASSERT_SCREEN(Dialog, w.menu);
    r = w.key(no);
    ASSERT_COMMAND(None, r);
    ASSERT_SCREEN(StartLine, w.menu);
  }
}

static void start_line_and_geometry_need_card_and_fix() {
  Walk w;
  w.keys({Event::Open, Event::Down, Event::Open, Event::Open});  // Start line
  ASSERT_REFUSAL(NoFix, w.key(Event::Hold));
  w.context.hasFix = true;
  w.context.cardOk = false;
  ASSERT_REFUSAL(NoCard, w.key(Event::Hold));
  w.keys({Event::Back, Event::Down, Event::Open});  // Geometry
  ASSERT_SCREEN(Geometry, w.menu);
  ASSERT_REFUSAL(NoCard, w.key(Event::Hold));
  w.context.cardOk = true;
  ASSERT_COMMAND(EnterGeometry, w.key(Event::Hold));
}

static void deleting_sessions_always_asks() {
  Walk w;
  w.key(Event::Open);
  w.down(4);
  w.key(Event::Open);
  w.down(3);
  w.key(Event::Open);  // Settings › Delete sessions
  ASSERT_SCREEN(DeleteSessions, w.menu);
  w.key(Event::Hold);
  ASSERT_SCREEN(Dialog, w.menu);
  ASSERT_COMMAND(DeleteSessions, w.key(Event::Hold));
  ASSERT_SCREEN(DeleteSessions, w.menu);
}

static void calibrate_needs_an_rc_signal() {
  Walk w;
  w.key(Event::Open);
  w.down(4);
  w.key(Event::Open);
  w.down(1);
  w.key(Event::Open);  // Settings › Calibrate
  ASSERT_SCREEN(Calibrate, w.menu);
  ASSERT_REFUSAL(NoRcSignal, w.key(Event::Hold));
  w.context.rcSignal = true;
  ASSERT_COMMAND(EnterCalibration, w.key(Event::Hold));
}

static void gnss_hold_restarts_acquisition() {
  Walk w;
  w.key(Event::Open);
  w.down(2);
  w.key(Event::Open);
  ASSERT_SCREEN(Gnss, w.menu);
  ASSERT_COMMAND(RestartGnss, w.key(Event::Hold));
}

// --- Timeouts -----------------------------------------------------------------

static void a_dialog_left_alone_answers_no() {
  Walk w;
  w.context.hasFix = true;
  w.context.hasStartLine = true;
  w.keys({Event::Open, Event::Down, Event::Open, Event::Open, Event::Hold});
  uint32_t opened = w.nowMs;
  w.menu.tick(opened + UI_DIALOG_TIMEOUT_MS - 1);
  ASSERT_SCREEN(Dialog, w.menu);
  w.menu.tick(opened + UI_DIALOG_TIMEOUT_MS);
  ASSERT_SCREEN(StartLine, w.menu);
}

static void the_menu_falls_back_home_and_forgets_its_cursors() {
  Walk w;
  w.key(Event::Open);
  w.down(4);
  w.key(Event::Open);
  w.down(2);
  uint32_t lastKey = w.nowMs;
  w.menu.tick(lastKey + UI_MENU_TIMEOUT_MS - 1);
  ASSERT_SCREEN(Settings, w.menu);
  w.menu.tick(lastKey + UI_MENU_TIMEOUT_MS);
  ASSERT_SCREEN(Home, w.menu);
  w.nowMs = lastKey + UI_MENU_TIMEOUT_MS;
  w.key(Event::Open);
  TEST_ASSERT_EQUAL_UINT8(0, w.menu.cursor());
}

static void a_press_counts_as_activity() {
  Walk w;
  w.key(Event::Open);
  uint32_t first = w.nowMs;
  w.menu.handle(Event::Press, w.context, first + 20000);  // e.g. OK pressed, released too late
  w.menu.tick(first + UI_MENU_TIMEOUT_MS);
  ASSERT_SCREEN(Menu, w.menu);
  w.menu.tick(first + 20000 + UI_MENU_TIMEOUT_MS);
  ASSERT_SCREEN(Home, w.menu);
}

// --- The tables ---------------------------------------------------------------

static void every_entry_fits_and_knows_its_way_back() {
  for (ScreenId list :
       {ScreenId::Menu, ScreenId::Track, ScreenId::Settings, ScreenId::DisplaySettings}) {
    uint8_t count = 0;
    const ui::Entry* entries = Menu::entries(list, count);
    TEST_ASSERT_NOT_NULL(entries);
    for (uint8_t i = 0; i < count; ++i) {
      // "> " in front of the label, in 21 columns.
      TEST_ASSERT_LESS_OR_EQUAL(19, strlen(entries[i].label));
      if (entries[i].command == Command::None && entries[i].screen != ui::kBackEntry) {
        TEST_ASSERT_EQUAL_INT(static_cast<int>(list),
                              static_cast<int>(Menu::parent(entries[i].screen)));
      }
    }
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(ok_on_home_opens_the_menu_and_back_returns);
  RUN_TEST(home_ignores_down_and_back);
  RUN_TEST(down_reaches_every_entry_and_wraps);
  RUN_TEST(walk_to_brightness_and_save);
  RUN_TEST(back_in_an_editor_cancels);
  RUN_TEST(back_keeps_each_lists_cursor);
  RUN_TEST(transfer_is_a_command_not_a_screen);
  RUN_TEST(text_size_is_a_display_setting);
  RUN_TEST(every_list_and_editor_ends_in_back);
  RUN_TEST(an_editors_back_saves_nothing);
  RUN_TEST(ok_on_a_leaf_goes_back);
  RUN_TEST(car_profile_editor_spans_the_profiles);
  RUN_TEST(holding_ok_on_home_or_session_starts_a_session);
  RUN_TEST(a_session_needs_the_card_and_logging);
  RUN_TEST(start_line_is_recorded_directly_when_there_is_none);
  RUN_TEST(overwriting_a_start_line_asks_first);
  RUN_TEST(any_other_key_answers_no);
  RUN_TEST(start_line_and_geometry_need_card_and_fix);
  RUN_TEST(deleting_sessions_always_asks);
  RUN_TEST(calibrate_needs_an_rc_signal);
  RUN_TEST(gnss_hold_restarts_acquisition);
  RUN_TEST(a_dialog_left_alone_answers_no);
  RUN_TEST(the_menu_falls_back_home_and_forgets_its_cursors);
  RUN_TEST(a_press_counts_as_activity);
  RUN_TEST(every_entry_fits_and_knows_its_way_back);
  return UNITY_END();
}
