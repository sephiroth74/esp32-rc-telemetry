// The power-on self-test — PLAN-ESP32.md §3.3, ROADMAP.md step 3.1.
//
// The eleven checks in their fixed order, what a failure of each means, and the sequence around
// them: run them one by one, latch on a fatal failure until OK is held, list what failed for a few
// seconds, then hand over to IDLE. The checks themselves are the caller's — on the device they
// touch the display, the card and the GNSS; in the host tests they are fakes — so this only orders
// them and decides what their results add up to.
//
// Framework-free (§6.1): the caller runs each check, feeds the key events and supplies the clock.
#pragma once

#include <stdint.h>

#include "ui_keys.h"

namespace ui {

constexpr uint8_t kPostChecks = 11;

// What a failed check means for the rest of the boot (§3.3).
enum class Severity : uint8_t {
  Fatal,           // latched until OK is held: PSRAM and the card
  RefusesLogging,  // IDLE, but a session start is refused naming the check: space, write, GNSS
  Degraded,        // IDLE without that capability
};

struct CheckInfo {
  const char* name;  // up to 7 characters, for the POST screen
  Severity severity;
};
const CheckInfo& checkInfo(uint8_t id);

enum class Outcome : uint8_t {
  NotRun,   // not reached: a fatal check stopped the run before it
  Running,  // started, not finished: what the screen shows while a check blocks
  Pass,
  Warn,  // passed, with a warning: free space above the floor but below the warning level
  Fail,
  Skipped,  // not run, by design or because the firmware does not have it yet
};

constexpr uint8_t kCheckDetailMax = 10;  // the columns a POST line leaves after the name

struct CheckReport {
  Outcome outcome = Outcome::NotRun;
  char detail[kCheckDetailMax + 1] = "";  // instead of "ok" or "FAIL": "62GB", "no card", "exFAT"

  static CheckReport pass(const char* detail = "");
  static CheckReport warn(const char* detail = "");
  static CheckReport fail(const char* detail = "");
  static CheckReport skipped(const char* detail = "");
};

struct PostResult {
  CheckReport checks[kPostChecks];

  // Bit n set: check n failed. The identifier /post.log, the BLE status and meta.json carry (§7.4).
  uint16_t failedMask() const;
  int8_t fatalCheck() const;     // the failed fatal check, or -1
  int8_t refusingCheck() const;  // the first failed check that refuses logging, or -1
  bool warning() const;          // any failure or warning: the status bar's mark (§3.2)
};

// For Settings › About: "ok", "ok, 9 skipped", or the failed checks, "fail 4,8".
void postSummary(const PostResult& result, char (&out)[17]);

class Post {
 public:
  enum class Phase : uint8_t {
    Running,  // current() is the next check to run
    Fatal,    // latched; holding OK runs the whole sequence again
    Listing,  // the failed and warned checks, worst first, for listMs
    Done,     // over to IDLE
  };

  explicit Post(uint32_t listMs) : listMs_(listMs) {}

  Phase phase() const { return phase_; }
  uint8_t current() const { return current_; }
  const PostResult& result() const { return result_; }

  // Phase Running: marks current() as running, for the screen to show before a check that blocks.
  void start();
  // Phase Running: current()'s report. Moves on to the next check, to Fatal, or past the last one.
  void finish(const CheckReport& report, uint32_t nowMs);
  // The keys are dead (§3.3) except for a hold of OK in Fatal, which retries from check 0.
  void handle(Event event, uint32_t nowMs);
  void tick(uint32_t nowMs);  // ends Listing after listMs

  // The checks Listing shows, worst first: refusing logging, then degraded, then warnings, each
  // in check order. Returns how many.
  uint8_t listed(uint8_t (&ids)[kPostChecks]) const;

 private:
  void end(uint32_t nowMs);

  uint32_t listMs_;
  Phase phase_ = Phase::Running;
  uint8_t current_ = 0;
  uint32_t listUntilMs_ = 0;
  PostResult result_;
};

}  // namespace ui
