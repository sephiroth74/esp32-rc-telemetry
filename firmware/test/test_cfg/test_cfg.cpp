// Host tests for lib/cfg — run with `make test`.
//
// config.json as PLAN-ESP32.md §3.8 has it: every key read against its default, a wrong one falling
// back alone and listed, a missing one silently, and the file written back whole.
#include <string.h>
#include <unity.h>

#include <string>

#include "cfg.h"

using cfg::Config;
using cfg::Problems;
using cfg::Read;

static Config defaults() {
  Config c;
  c.profileCount = 1;
  cfg::defaultName(0, c.profiles[0].name);
  return c;
}

static Read readText(const std::string& json, Config& out, Problems& problems) {
  return cfg::read(json.data(), json.size(), defaults(), out, problems);
}

static bool listed(const Problems& p, const char* key) {
  for (uint8_t i = 0; i < p.count; ++i) {
    if (strcmp(p.keys[i], key) == 0) return true;
  }
  return false;
}

void setUp() {}
void tearDown() {}

static void nothing_there_is_every_default() {
  Config c;
  Problems p;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Read::Empty), static_cast<int>(readText("  \n", c, p)));
  TEST_ASSERT_EQUAL_UINT8(0, p.count);
  TEST_ASSERT_EQUAL_UINT8(2, c.brightness);
  TEST_ASSERT_EQUAL_STRING("Car 1", c.profiles[0].name);
}

static void every_key_is_read() {
  Config c;
  Problems p;
  const char* json = R"({
    "keys": {"layout": "five"},
    "display": {"brightness": "low", "dim_after_s": 0, "text": "small"},
    "sd": {"min_free_mb": 128, "warn_free_mb": 512},
    "session": {"auto_start": false, "require_fix": false, "auto_stop_min": 10},
    "track": {"match_radius_m": 500},
    "profiles": [{"name": "Inferno"}, {"name": "Typhon"}]
  })";
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Read::Ok), static_cast<int>(readText(json, c, p)));
  TEST_ASSERT_EQUAL_UINT8(0, p.count);
  TEST_ASSERT_EQUAL_UINT8(1, c.keysLayout);
  TEST_ASSERT_EQUAL_UINT8(0, c.brightness);
  TEST_ASSERT_EQUAL_UINT8(3, c.dimDelay);  // never
  TEST_ASSERT_EQUAL_UINT8(0, c.textSize);
  TEST_ASSERT_EQUAL_UINT32(128, c.minFreeMb);
  TEST_ASSERT_EQUAL_UINT32(512, c.warnFreeMb);
  TEST_ASSERT_FALSE(c.autoStart);
  TEST_ASSERT_FALSE(c.requireFix);
  TEST_ASSERT_EQUAL_UINT16(10, c.autoStopMin);
  TEST_ASSERT_EQUAL_UINT16(500, c.matchRadiusM);
  TEST_ASSERT_EQUAL_UINT8(2, c.profileCount);
  TEST_ASSERT_EQUAL_STRING("Typhon", c.profiles[1].name);
}

static void a_wrong_key_falls_back_alone_and_is_listed() {
  Config c;
  Problems p;
  const char* json = R"({
    "keys": {"layout": "three"},
    "display": {"brightness": "bright", "dim_after_s": 45, "text": "large"},
    "sd": {"min_free_mb": -1},
    "session": {"auto_start": "yes", "require_fix": 0, "auto_stop_min": 500},
    "track": {"match_radius_m": 300}
  })";
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Read::Ok), static_cast<int>(readText(json, c, p)));
  for (const char* key : {"keys.layout", "display.brightness", "display.dim_after_s",
                          "sd.min_free_mb", "session.auto_start", "session.require_fix",
                          "session.auto_stop_min"}) {
    TEST_ASSERT_TRUE_MESSAGE(listed(p, key), key);
  }
  TEST_ASSERT_EQUAL_UINT8(7, p.count);
  Config d = defaults();
  TEST_ASSERT_EQUAL_UINT8(d.brightness, c.brightness);
  TEST_ASSERT_EQUAL_UINT32(d.minFreeMb, c.minFreeMb);
  TEST_ASSERT_EQUAL_UINT8(1, c.textSize);  // the keys next to the wrong ones still count
}

static void a_section_that_is_not_an_object_is_wrong() {
  Config c;
  Problems p;
  readText(R"({"display": 3, "sd": {"min_free_mb": 32}})", c, p);
  TEST_ASSERT_TRUE(listed(p, "display"));
  TEST_ASSERT_EQUAL_UINT32(32, c.minFreeMb);
}

static void a_warning_level_under_the_floor_is_raised() {
  Config c;
  Problems p;
  readText(R"({"sd": {"min_free_mb": 300, "warn_free_mb": 100}})", c, p);
  TEST_ASSERT_TRUE(listed(p, "sd.warn_free_mb"));
  TEST_ASSERT_EQUAL_UINT32(300, c.minFreeMb);
  TEST_ASSERT_TRUE(c.warnFreeMb >= c.minFreeMb);
}

static void not_json_at_all_is_said_so() {
  Config c;
  Problems p;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Read::NotJson),
                        static_cast<int>(readText("{\"display\": {", c, p)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Read::NotJson),
                        static_cast<int>(readText("[1, 2]", c, p)));
  TEST_ASSERT_EQUAL_UINT8(2, c.brightness);
}

static void profiles_keep_what_holds() {
  Config c;
  Problems p;
  readText(R"({"profiles": [{"name": "Inferno"}, {"name": "much too long"}, {"nome": "x"}]})", c,
           p);
  TEST_ASSERT_EQUAL_UINT8(3, c.profileCount);
  TEST_ASSERT_EQUAL_STRING("Inferno", c.profiles[0].name);
  TEST_ASSERT_EQUAL_STRING("Car 2", c.profiles[1].name);  // its place keeps a name, and its index
  TEST_ASSERT_EQUAL_STRING("Car 3", c.profiles[2].name);
  TEST_ASSERT_TRUE(listed(p, "profiles[].name"));

  std::string nine = R"({"profiles": [)";
  for (int i = 0; i < 9; ++i)
    nine += std::string(i ? "," : "") + R"({"name": "C)" + std::to_string(i) + "\"}";
  nine += "]}";
  readText(nine, c, p);
  TEST_ASSERT_EQUAL_UINT8(cfg::kMaxProfiles, c.profileCount);  // the first eight
  TEST_ASSERT_TRUE(listed(p, "profiles"));

  readText(R"({"profiles": []})", c, p);
  TEST_ASSERT_EQUAL_UINT8(1, c.profileCount);  // the default: a device always has a profile
  TEST_ASSERT_TRUE(listed(p, "profiles"));
}

static void written_and_read_back_it_is_the_same() {
  Config c = defaults();
  c.keysLayout = 1;
  c.brightness = 0;
  c.dimDelay = 3;
  c.textSize = 0;
  c.minFreeMb = 100;
  c.warnFreeMb = 400;
  c.autoStart = false;
  c.requireFix = false;
  c.autoStopMin = 7;
  c.matchRadiusM = 250;
  c.profileCount = 2;
  snprintf(c.profiles[1].name, sizeof c.profiles[1].name, "Inferno");
  char text[1024];
  size_t n = cfg::write(c, text, sizeof text);
  TEST_ASSERT_TRUE(n > 0);
  Config back;
  Problems p;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Read::Ok),
                        static_cast<int>(cfg::read(text, n, defaults(), back, p)));
  TEST_ASSERT_EQUAL_UINT8(0, p.count);
  // Field by field: the struct's padding is not part of it.
  TEST_ASSERT_EQUAL_UINT8(c.keysLayout, back.keysLayout);
  TEST_ASSERT_EQUAL_UINT8(c.brightness, back.brightness);
  TEST_ASSERT_EQUAL_UINT8(c.dimDelay, back.dimDelay);
  TEST_ASSERT_EQUAL_UINT8(c.textSize, back.textSize);
  TEST_ASSERT_EQUAL_UINT32(c.minFreeMb, back.minFreeMb);
  TEST_ASSERT_EQUAL_UINT32(c.warnFreeMb, back.warnFreeMb);
  TEST_ASSERT_EQUAL(c.autoStart, back.autoStart);
  TEST_ASSERT_EQUAL(c.requireFix, back.requireFix);
  TEST_ASSERT_EQUAL_UINT16(c.autoStopMin, back.autoStopMin);
  TEST_ASSERT_EQUAL_UINT16(c.matchRadiusM, back.matchRadiusM);
  TEST_ASSERT_EQUAL_UINT8(c.profileCount, back.profileCount);
  for (uint8_t i = 0; i < c.profileCount; ++i) {
    TEST_ASSERT_EQUAL_STRING(c.profiles[i].name, back.profiles[i].name);
  }
  TEST_ASSERT_EQUAL_UINT(0, cfg::write(c, text, 40));  // too small: nothing half-written
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(nothing_there_is_every_default);
  RUN_TEST(every_key_is_read);
  RUN_TEST(a_wrong_key_falls_back_alone_and_is_listed);
  RUN_TEST(a_section_that_is_not_an_object_is_wrong);
  RUN_TEST(a_warning_level_under_the_floor_is_raised);
  RUN_TEST(not_json_at_all_is_said_so);
  RUN_TEST(profiles_keep_what_holds);
  RUN_TEST(written_and_read_back_it_is_the_same);
  return UNITY_END();
}
