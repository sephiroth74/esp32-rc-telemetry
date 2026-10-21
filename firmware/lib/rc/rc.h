// RC calibration and what it makes of a pulse width — PLAN-ESP32.md §3.8, §4.3, ROADMAP.md step 6.3.
//
// A car profile's calibration holds, for each of steering and throttle, the width the receiver sends
// at rest and at each end: steering at full left and full right, throttle at full brake and full
// throttle. Kept by meaning, not by value, so a radio whose channel runs the other way needs no
// setting of its own. A width becomes a percentage from neutral, -100 at the negative end (left,
// brake) to 100 at the positive one (right, throttle), worked out on each side separately: an ESC
// with more travel forward than back is the rule, and one scale for both halves would be wrong on
// one of them. Beyond an end it holds at ±100.
//
// The calibration procedure (§3.8) captures those widths one step at a time; check() says whether
// what was captured can be a calibration at all before it is saved, and window() gives the pulse
// widths a calibrated input accepts (§4.3).
//
// Framework-free (§6.1), and mirrored by packages/rct_core, which shows the same percentages.
#pragma once

#include <stdint.h>

namespace rc {

struct Axis {
  uint16_t negativeUs = 0;  // full left, full brake
  uint16_t neutralUs = 0;
  uint16_t positiveUs = 0;  // full right, full throttle
};

enum class AxisCheck : uint8_t {
  Ok,
  NotCaptured,  // a width of 0: that step was skipped, or the input is absent
  NotMoved,     // an end within kMinTravelUs of neutral: the stick or the wheel stayed put
  SameSide,     // both ends on one side of neutral: a step captured the wrong way
};

constexpr uint16_t kMinTravelUs = 50;

AxisCheck check(const Axis& axis);

// From neutral, -100 to 100, rounded to the nearest; meaningful for an axis that checks Ok.
int8_t percent(const Axis& axis, uint16_t widthUs);

// The widths an input with this calibration accepts: its two ends, widened by `marginUs` each way.
void window(const Axis& axis, uint16_t marginUs, uint16_t& minUs, uint16_t& maxUs);

// The steps of Settings › Calibrate, in their order (§3.8).
enum class Step : uint8_t { Neutral, SteerLeft, SteerRight, FullThrottle, FullBrake, Aux, Count };

struct Calibration {
  Axis steering;
  Axis throttle;
  uint16_t auxUs = 0;  // the aux channel at the position the step captured; 0 without one
};

// One capture of the live widths, 0 for an input that delivers none. Neutral sets both axes' rest;
// each other step sets the end it is about, from the input it is about.
void capture(Calibration& cal, Step step, uint16_t steeringUs, uint16_t throttleUs, uint16_t auxUs);

}  // namespace rc
