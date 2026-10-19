// The settings of config.json — PLAN-ESP32.md §3.8, ROADMAP.md step 3.6.
//
// One struct with every setting, and the JSON it lives in twice: on the card, the copy people edit
// from a computer or an app, and on LittleFS, the last good one, for a boot whose card copy is gone
// or unreadable. read() starts from the defaults it is given and takes each key the JSON gets
// right; a key it gets wrong keeps its default and is listed, so one typo does not cost the whole
// file, and a key it lacks keeps its default silently. write() gives every key back, so that both
// copies stay complete.
//
// Framework-free (§6.1): ArduinoJson builds on the host as well, which is where this is tested.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace cfg {

constexpr uint8_t kMaxProfiles = 8;
constexpr uint8_t kNameMax = 8;  // what the status bar has room for

struct Profile {
  char name[kNameMax + 1] = "";
};

struct Config {
  uint8_t keysLayout = 0;       // keys.layout: "two" (0) or "five" (1), §3.1
  uint8_t brightness = 2;       // display.brightness: "low", "medium", "high"
  uint8_t dimDelay = 1;         // display.dim_after_s: 15, 30, 60, or 0 for never, as an index
  uint8_t textSize = 1;         // display.text: "small" or "large", §3.2
  uint32_t minFreeMb = 64;      // sd.min_free_mb: below it, logging is refused (POST check 4)
  uint32_t warnFreeMb = 256;    // sd.warn_free_mb: below it, a warning
  bool autoStart = true;        // session.auto_start, §3.6
  uint16_t autoStopMin = 5;     // session.auto_stop_min: standing still this long ends a session
  uint16_t matchRadiusM = 300;  // track.match_radius_m, §7.1
  uint8_t profileCount = 1;     // profiles[]: 1 to kMaxProfiles, each with its name
  Profile profiles[kMaxProfiles];
};

// The keys that fell back to their defaults, by their path in the JSON.
constexpr uint8_t kMaxProblems = 12;
struct Problems {
  uint8_t count = 0;
  const char* keys[kMaxProblems] = {};
};

enum class Read : uint8_t {
  Ok,       // parsed; `problems` lists what did not hold
  Empty,    // nothing there: every setting at its default
  NotJson,  // not JSON at all: every setting at its default, and the caller decides
};

Read read(const char* json, size_t length, const Config& defaults, Config& out, Problems& problems);

// Pretty-printed, every key; returns the length, or 0 when it does not fit in `capacity`.
size_t write(const Config& config, char* out, size_t capacity);

// "Car 4" for index 3: the name a profile gets until an app renames it.
void defaultName(uint8_t index, char (&out)[kNameMax + 1]);

}  // namespace cfg
