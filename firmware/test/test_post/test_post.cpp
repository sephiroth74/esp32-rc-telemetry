// Host tests for lib/ui's power-on self-test — run with `make test`.
//
// The sequence of PLAN-ESP32.md §3.3 against a fake device whose every check can be made to pass,
// warn, fail or be skipped: the order, the fatal latch and its retry, the list of what failed, and
// the screens at both text sizes.
#include <string.h>
#include <unity.h>

#include <initializer_list>

#include "ui_config.h"
#include "ui_screens.h"

using ui::CheckReport;
using ui::Event;
using ui::kPostChecks;
using ui::Outcome;
using ui::Post;
using ui::ScreenText;
using ui::TextSize;

#define ASSERT_PHASE(expected, post) \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Post::Phase::expected), static_cast<int>((post).phase()))
#define ASSERT_OUTCOME(expected, report) \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Outcome::expected), static_cast<int>((report).outcome))

// The device, faked: what each check reports, and how many times each ran.
struct Fake {
  CheckReport reports[kPostChecks];
  uint8_t runs[kPostChecks] = {};
  Fake() {
    for (auto& r : reports) r = CheckReport::pass();
  }
};

// Runs the checks the way setup() does — mark one running, run it, hand its report back — until the
// POST needs a key or the clock.
static void runChecks(Post& post, Fake& fake, uint32_t nowMs) {
  while (post.phase() == Post::Phase::Running) {
    post.start();
    ASSERT_OUTCOME(Running, post.result().checks[post.current()]);
    ++fake.runs[post.current()];
    post.finish(fake.reports[post.current()], nowMs);
  }
}

void setUp() {}
void tearDown() {}

// --- The sequence ---------------------------------------------------------------

static void all_passing_goes_straight_to_idle() {
  Post post(DISPLAY_POST_DEGRADED_MS);
  Fake fake;
  runChecks(post, fake, 1000);
  ASSERT_PHASE(Done, post);
  for (uint8_t id = 0; id < kPostChecks; ++id) TEST_ASSERT_EQUAL_UINT8(1, fake.runs[id]);
  TEST_ASSERT_EQUAL_UINT16(0, post.result().failedMask());
  TEST_ASSERT_FALSE(post.result().warning());
  TEST_ASSERT_EQUAL_INT8(-1, post.result().refusingCheck());
}

static void a_degraded_check_is_listed_then_idle() {
  Post post(DISPLAY_POST_DEGRADED_MS);
  Fake fake;
  fake.reports[7] = CheckReport::fail("no IMU");
  runChecks(post, fake, 1000);
  ASSERT_PHASE(Listing, post);
  post.tick(1000 + DISPLAY_POST_DEGRADED_MS - 1);
  ASSERT_PHASE(Listing, post);
  post.tick(1000 + DISPLAY_POST_DEGRADED_MS);
  ASSERT_PHASE(Done, post);
  TEST_ASSERT_EQUAL_UINT16(1u << 7, post.result().failedMask());
  TEST_ASSERT_TRUE(post.result().warning());
  TEST_ASSERT_EQUAL_INT8(-1, post.result().refusingCheck());  // degraded, logging still allowed
}

static void the_list_puts_the_worst_first() {
  Post post(DISPLAY_POST_DEGRADED_MS);
  Fake fake;
  fake.reports[6] = CheckReport::warn("slow");
  fake.reports[7] = CheckReport::fail();
  fake.reports[8] = CheckReport::fail("no reply");
  fake.reports[4] = CheckReport::fail("12MB");
  runChecks(post, fake, 0);
  uint8_t ids[kPostChecks];
  TEST_ASSERT_EQUAL_UINT8(4, post.listed(ids));
  const uint8_t expected[] = {4, 8, 7, 6};  // refusing logging, degraded, then the warning
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, ids, 4);
  TEST_ASSERT_EQUAL_INT8(4, post.result().refusingCheck());  // the first that refuses: the toast's
}

static void a_warning_alone_is_listed_but_refuses_nothing() {
  Post post(DISPLAY_POST_DEGRADED_MS);
  Fake fake;
  fake.reports[4] = CheckReport::warn("200MB");
  runChecks(post, fake, 0);
  ASSERT_PHASE(Listing, post);
  TEST_ASSERT_EQUAL_UINT16(0, post.result().failedMask());
  TEST_ASSERT_TRUE(post.result().warning());
  TEST_ASSERT_EQUAL_INT8(-1, post.result().refusingCheck());
}

static void skipped_checks_are_not_failures() {
  Post post(DISPLAY_POST_DEGRADED_MS);
  Fake fake;
  fake.reports[9] = CheckReport::skipped();
  fake.reports[10] = CheckReport::skipped();
  runChecks(post, fake, 0);
  ASSERT_PHASE(Done, post);
  TEST_ASSERT_FALSE(post.result().warning());
}

// --- Fatal ----------------------------------------------------------------------

static void a_fatal_check_latches_and_holding_ok_retries() {
  Post post(DISPLAY_POST_DEGRADED_MS);
  Fake fake;
  fake.reports[3] = CheckReport::fail("no card");
  runChecks(post, fake, 0);
  ASSERT_PHASE(Fatal, post);
  TEST_ASSERT_EQUAL_INT8(3, post.result().fatalCheck());
  for (uint8_t id = 4; id < kPostChecks; ++id) {
    ASSERT_OUTCOME(NotRun, post.result().checks[id]);  // nothing after a fatal check runs
    TEST_ASSERT_EQUAL_UINT8(0, fake.runs[id]);
  }
  for (Event e : {Event::Press, Event::Open, Event::Down, Event::Back, Event::Up}) {
    post.handle(e, 100);
    ASSERT_PHASE(Fatal, post);  // only a hold of OK retries
  }
  fake.reports[3] = CheckReport::pass("62GB");  // the card is in now
  post.handle(Event::Hold, 200);
  ASSERT_PHASE(Running, post);
  TEST_ASSERT_EQUAL_UINT8(0, post.current());  // the whole sequence again, from the display
  ASSERT_OUTCOME(NotRun, post.result().checks[3]);
  runChecks(post, fake, 300);
  ASSERT_PHASE(Done, post);
  TEST_ASSERT_EQUAL_UINT8(2, fake.runs[0]);
  TEST_ASSERT_EQUAL_UINT8(1, fake.runs[4]);
}

static void psram_is_fatal_too_and_the_rest_is_not() {
  for (uint8_t id = 0; id < kPostChecks; ++id) {
    Post post(DISPLAY_POST_DEGRADED_MS);
    Fake fake;
    fake.reports[id] = CheckReport::fail();
    runChecks(post, fake, 0);
    bool fatal = id == 1 || id == 3;
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        fatal ? static_cast<int>(Post::Phase::Fatal) : static_cast<int>(Post::Phase::Listing),
        static_cast<int>(post.phase()), ui::checkInfo(id).name);
  }
}

static void the_keys_are_dead_while_running_and_listing() {
  Post post(DISPLAY_POST_DEGRADED_MS);
  Fake fake;
  post.handle(Event::Hold, 0);
  ASSERT_PHASE(Running, post);
  TEST_ASSERT_EQUAL_UINT8(0, post.current());
  fake.reports[7] = CheckReport::fail();
  runChecks(post, fake, 0);
  post.handle(Event::Hold, 10);
  ASSERT_PHASE(Listing, post);
}

// --- The result -----------------------------------------------------------------

static void the_summary_names_failures_or_counts_the_skipped() {
  char text[17];
  ui::PostResult r;
  for (auto& c : r.checks) c = CheckReport::pass();
  ui::postSummary(r, text);
  TEST_ASSERT_EQUAL_STRING("ok", text);
  r.checks[9] = CheckReport::skipped();
  r.checks[10] = CheckReport::skipped();
  ui::postSummary(r, text);
  TEST_ASSERT_EQUAL_STRING("ok, 2 skipped", text);
  r.checks[4] = CheckReport::fail();
  r.checks[8] = CheckReport::fail();
  ui::postSummary(r, text);
  TEST_ASSERT_EQUAL_STRING("fail 4,8", text);
  for (auto& c : r.checks) c = CheckReport::fail();
  ui::postSummary(r, text);  // too many to name: as many as fit
  TEST_ASSERT_LESS_OR_EQUAL(16, strlen(text));
  TEST_ASSERT_EQUAL_STRING_LEN("fail 0,1,2", text, 10);
}

// --- The screens ----------------------------------------------------------------

static void expectFits(const Post& post, TextSize size, const char* where) {
  ScreenText text;
  ui::renderPost(post, size, text);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, text.overflow, where);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(ui::screenLines(size), text.count, where);
  for (const auto& line : text.lines) TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(21, strlen(line), where);
}

static void every_post_screen_fits_in_21_columns() {
  for (TextSize size : {TextSize::Small, TextSize::Large}) {
    // Running, at every check, with the longest details there are.
    Post running(DISPLAY_POST_DEGRADED_MS);
    while (running.phase() == Post::Phase::Running) {
      running.start();
      expectFits(running, size, ui::checkInfo(running.current()).name);
      running.finish(CheckReport::pass("1234567890"), 0);
    }
    // Fatal, both of them, with a full-length reason.
    for (uint8_t id : {1, 3}) {
      Post fatal(DISPLAY_POST_DEGRADED_MS);
      Fake fake;
      fake.reports[id] = CheckReport::fail("1234567890");
      runChecks(fatal, fake, 0);
      expectFits(fatal, size, "fatal");
    }
    // Listing, with more failures than rows.
    Post listing(DISPLAY_POST_DEGRADED_MS);
    Fake fake;
    for (uint8_t id : {0, 2, 4, 5, 6, 7, 8, 9, 10})
      fake.reports[id] = CheckReport::fail("1234567890");
    runChecks(listing, fake, 0);
    expectFits(listing, size, "listing");
  }
}

static void the_screens_read_as_designed() {
  ScreenText t;
  Post post(DISPLAY_POST_DEGRADED_MS);
  post.start();
  ui::renderPost(post, TextSize::Large, t);
  TEST_ASSERT_EQUAL_STRING("Self-test", t.lines[0]);
  TEST_ASSERT_EQUAL_STRING(" 0 Display ...", t.lines[1]);
  TEST_ASSERT_EQUAL_STRING("check 1 of 11", t.lines[4]);

  // Five checks in, the large grid shows the last three, the running one at the bottom.
  for (int i = 0; i < 4; ++i) post.finish(CheckReport::pass(), 0);
  post.start();
  ui::renderPost(post, TextSize::Large, t);
  TEST_ASSERT_EQUAL_STRING(" 2 Config  ok", t.lines[1]);
  TEST_ASSERT_EQUAL_STRING(" 4 Free    ...", t.lines[3]);

  Post fatal(DISPLAY_POST_DEGRADED_MS);
  Fake fake;
  fake.reports[3] = CheckReport::fail("no card");
  runChecks(fatal, fake, 0);
  ui::renderPost(fatal, TextSize::Large, t);
  TEST_ASSERT_EQUAL_STRING("FATAL 3 Card", t.lines[0]);
  TEST_ASSERT_EQUAL_STRING("no card", t.lines[1]);
  TEST_ASSERT_EQUAL_STRING("hold OK: retry", t.lines[4]);

  Post listing(DISPLAY_POST_DEGRADED_MS);
  Fake fake2;
  fake2.reports[5] = CheckReport::fail("read back");
  runChecks(listing, fake2, 0);
  ui::renderPost(listing, TextSize::Large, t);
  TEST_ASSERT_EQUAL_STRING("Self-test: 1 issue", t.lines[0]);
  TEST_ASSERT_EQUAL_STRING(" 5 Write   read back", t.lines[1]);  // the reason, in the list
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(all_passing_goes_straight_to_idle);
  RUN_TEST(a_degraded_check_is_listed_then_idle);
  RUN_TEST(the_list_puts_the_worst_first);
  RUN_TEST(a_warning_alone_is_listed_but_refuses_nothing);
  RUN_TEST(skipped_checks_are_not_failures);
  RUN_TEST(a_fatal_check_latches_and_holding_ok_retries);
  RUN_TEST(psram_is_fatal_too_and_the_rest_is_not);
  RUN_TEST(the_keys_are_dead_while_running_and_listing);
  RUN_TEST(the_summary_names_failures_or_counts_the_skipped);
  RUN_TEST(every_post_screen_fits_in_21_columns);
  RUN_TEST(the_screens_read_as_designed);
  return UNITY_END();
}
