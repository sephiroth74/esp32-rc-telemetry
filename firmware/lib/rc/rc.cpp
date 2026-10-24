#include "rc.h"

namespace rc {
namespace {

int32_t travel(uint16_t endUs, uint16_t neutralUs) { return int32_t(endUs) - int32_t(neutralUs); }

int32_t magnitude(int32_t v) { return v < 0 ? -v : v; }

}  // namespace

AxisCheck check(const Axis& a) {
  if (!a.negativeUs || !a.neutralUs || !a.positiveUs) return AxisCheck::NotCaptured;
  int32_t neg = travel(a.negativeUs, a.neutralUs), pos = travel(a.positiveUs, a.neutralUs);
  if (magnitude(neg) < kMinTravelUs || magnitude(pos) < kMinTravelUs) return AxisCheck::NotMoved;
  if ((neg < 0) == (pos < 0)) return AxisCheck::SameSide;
  return AxisCheck::Ok;
}

int8_t percent(const Axis& a, uint16_t widthUs) {
  int32_t d = travel(widthUs, a.neutralUs);
  int32_t pos = travel(a.positiveUs, a.neutralUs), neg = travel(a.negativeUs, a.neutralUs);
  if (!d || !pos || !neg) return 0;
  // The side the width is on, and that side's full travel, both as positive numbers.
  bool positiveSide = (d < 0) == (pos < 0);
  int32_t full = magnitude(positiveSide ? pos : neg);
  int32_t p = (magnitude(d) * 100 + full / 2) / full;  // rounded to the nearest
  if (p > 100) p = 100;
  return static_cast<int8_t>(positiveSide ? p : -p);
}

void window(const Axis& a, uint16_t marginUs, uint16_t& minUs, uint16_t& maxUs) {
  uint16_t lo = a.negativeUs < a.positiveUs ? a.negativeUs : a.positiveUs;
  uint16_t hi = a.negativeUs < a.positiveUs ? a.positiveUs : a.negativeUs;
  minUs = lo > marginUs ? lo - marginUs : 0;
  maxUs = uint32_t(hi) + marginUs > 0xFFFF ? 0xFFFF : hi + marginUs;
}

void capture(Calibration& cal, Step step, uint16_t steeringUs, uint16_t throttleUs, uint16_t auxUs) {
  switch (step) {
    case Step::Neutral:
      cal.steering.neutralUs = steeringUs;
      cal.throttle.neutralUs = throttleUs;
      break;
    case Step::SteerLeft:
      cal.steering.negativeUs = steeringUs;
      break;
    case Step::SteerRight:
      cal.steering.positiveUs = steeringUs;
      break;
    case Step::FullThrottle:
      cal.throttle.positiveUs = throttleUs;
      break;
    case Step::FullBrake:
      cal.throttle.negativeUs = throttleUs;
      break;
    case Step::Aux:
      cal.auxUs = auxUs;
      break;
    default:
      break;
  }
}

bool absent(const Axis& a) { return !a.negativeUs && !a.neutralUs && !a.positiveUs; }

bool calibrated(const Axis& a) { return check(a) == AxisCheck::Ok; }

const char* problem(const Calibration& cal) {
  static const char* const kWhy[2][4] = {
      {nullptr, "Steering step missed", "Steering not moved", "Steering wrong way"},
      {nullptr, "Throttle step missed", "Throttle not moved", "Throttle wrong way"},
  };
  const Axis* axes[2] = {&cal.steering, &cal.throttle};
  for (uint8_t i = 0; i < 2; ++i) {
    if (absent(*axes[i])) continue;
    AxisCheck c = check(*axes[i]);
    if (c != AxisCheck::Ok) return kWhy[i][static_cast<uint8_t>(c)];
  }
  if (absent(cal.steering) && absent(cal.throttle)) return "Nothing captured";
  return nullptr;
}

}  // namespace rc
