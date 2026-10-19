#include "ui_post.h"

#include <stdio.h>
#include <string.h>

namespace ui {
namespace {

// The table of §3.3. Only 1 and 3 are fatal: anything else boots into IDLE, because a latched
// device is one whose card cannot be offloaded.
const CheckInfo kChecks[kPostChecks] = {
    {"Display", Severity::Degraded},      // 0: headless, logging goes on
    {"PSRAM", Severity::Fatal},           // 1: the ring buffer of §4.4
    {"Config", Severity::Degraded},       // 2: LittleFS, built-in defaults instead
    {"Card", Severity::Fatal},            // 3: mounted in 1-bit mode
    {"Free", Severity::RefusesLogging},   // 4: below sd.min_free_mb
    {"Write", Severity::RefusesLogging},  // 5: the 256 KB probe
    {"Speed", Severity::Degraded},        // 6: the same probe, timed
    {"IMU", Severity::Degraded},          // 7: the bus scan and WHO_AM_I
    {"GNSS", Severity::RefusesLogging},   // 8: the auto-baud scan of §5.2
    {"GNSScfg", Severity::Degraded},      // 9: VALSET, retried in the background
    {"RC", Severity::Degraded},           // 10: edges on either input
};

CheckReport make(Outcome outcome, const char* detail) {
  CheckReport r;
  r.outcome = outcome;
  snprintf(r.detail, sizeof r.detail, "%s", detail ? detail : "");
  return r;
}

bool reached(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b) >= 0; }

}  // namespace

const CheckInfo& checkInfo(uint8_t id) { return kChecks[id % kPostChecks]; }

CheckReport CheckReport::pass(const char* detail) { return make(Outcome::Pass, detail); }
CheckReport CheckReport::warn(const char* detail) { return make(Outcome::Warn, detail); }
CheckReport CheckReport::fail(const char* detail) { return make(Outcome::Fail, detail); }
CheckReport CheckReport::skipped(const char* detail) { return make(Outcome::Skipped, detail); }

uint16_t PostResult::failedMask() const {
  uint16_t mask = 0;
  for (uint8_t i = 0; i < kPostChecks; ++i) {
    if (checks[i].outcome == Outcome::Fail) mask |= 1u << i;
  }
  return mask;
}

int8_t PostResult::fatalCheck() const {
  for (uint8_t i = 0; i < kPostChecks; ++i) {
    if (checks[i].outcome == Outcome::Fail && kChecks[i].severity == Severity::Fatal) return i;
  }
  return -1;
}

int8_t PostResult::refusingCheck() const {
  for (uint8_t i = 0; i < kPostChecks; ++i) {
    if (checks[i].outcome == Outcome::Fail && kChecks[i].severity == Severity::RefusesLogging) {
      return i;
    }
  }
  return -1;
}

bool PostResult::warning() const {
  for (const CheckReport& c : checks) {
    if (c.outcome == Outcome::Fail || c.outcome == Outcome::Warn) return true;
  }
  return false;
}

void postSummary(const PostResult& result, char (&out)[17]) {
  uint8_t skipped = 0;
  int n = 0;
  for (uint8_t i = 0; i < kPostChecks; ++i) {
    if (result.checks[i].outcome == Outcome::Skipped) ++skipped;
    if (result.checks[i].outcome != Outcome::Fail) continue;
    // At most "fail 0,1,2,3,4,5" fits; the rest are in /post.log.
    int w = snprintf(out + n, sizeof out - n, n ? ",%u" : "fail %u", i);
    if (w < 0 || n + w >= static_cast<int>(sizeof out)) {
      out[n] = '\0';  // no half number, nor a trailing comma
      break;
    }
    n += w;
  }
  if (n) return;
  if (skipped) {
    snprintf(out, sizeof out, "ok, %u skipped", skipped);
  } else {
    snprintf(out, sizeof out, "ok");
  }
}

void Post::start() {
  if (phase_ == Phase::Running) result_.checks[current_].outcome = Outcome::Running;
}

void Post::finish(const CheckReport& report, uint32_t nowMs) {
  if (phase_ != Phase::Running) return;
  result_.checks[current_] = report;
  if (report.outcome == Outcome::Fail && kChecks[current_].severity == Severity::Fatal) {
    phase_ = Phase::Fatal;  // the checks after it stay NotRun
    return;
  }
  if (++current_ == kPostChecks) end(nowMs);
}

void Post::handle(Event event, uint32_t) {
  if (phase_ != Phase::Fatal || event != Event::Hold) return;
  // The whole sequence again, from the display: the common case is a card that was not inserted.
  result_ = PostResult();
  current_ = 0;
  phase_ = Phase::Running;
}

void Post::tick(uint32_t nowMs) {
  if (phase_ == Phase::Listing && reached(nowMs, listUntilMs_)) phase_ = Phase::Done;
}

void Post::end(uint32_t nowMs) {
  if (!result_.warning()) {
    phase_ = Phase::Done;
    return;
  }
  phase_ = Phase::Listing;
  listUntilMs_ = nowMs + listMs_;
}

uint8_t Post::listed(uint8_t (&ids)[kPostChecks]) const {
  uint8_t count = 0;
  // Worst first: the failures that refuse logging, the other failures, then the warnings.
  for (uint8_t pass = 0; pass < 3; ++pass) {
    for (uint8_t i = 0; i < kPostChecks; ++i) {
      const CheckReport& c = result_.checks[i];
      bool refuses = kChecks[i].severity == Severity::RefusesLogging;
      bool take = pass == 0   ? c.outcome == Outcome::Fail && refuses
                  : pass == 1 ? c.outcome == Outcome::Fail && !refuses
                              : c.outcome == Outcome::Warn;
      if (take) ids[count++] = i;
    }
  }
  return count;
}

}  // namespace ui
