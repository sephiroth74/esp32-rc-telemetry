#include "ui_auto.h"

namespace ui {
namespace {

uint8_t magnitude(int8_t pct) { return pct < 0 ? static_cast<uint8_t>(-pct) : pct; }

}  // namespace

void AutoSession::Held::set(bool now, uint32_t nowMs) {
  if (now && !on) sinceMs = nowMs;
  on = now;
}

void AutoSession::update(const Motion& m, uint32_t nowMs) {
  fast_.set(m.speedKnown && m.speedMmS > t_.startSpeedMmS, nowMs);
  throttle_.set(m.throttleKnown && magnitude(m.throttlePct) > t_.startThrottlePct, nowMs);
  bool slow = !m.speedKnown || m.speedMmS < t_.stillSpeedMmS;
  bool idle = !m.throttleKnown || magnitude(m.throttlePct) <= t_.stillThrottlePct;
  still_.set((m.speedKnown || m.throttleKnown) && slow && idle, nowMs);
  if (!armed_ && still_.for_(t_.rearmMs, nowMs)) armed_ = true;
}

bool AutoSession::startDue(uint32_t nowMs) const {
  return armed_ &&
         (fast_.for_(t_.startSpeedMs, nowMs) || throttle_.for_(t_.startThrottleMs, nowMs));
}

bool AutoSession::stopDue(uint32_t nowMs) const { return still_.for_(t_.stopMs, nowMs); }

}  // namespace ui
