#include "calibration.h"

#include <Arduino.h>
#include <Preferences.h>

#include "config.h"
#include "rc_capture.h"

namespace {

constexpr const char* kNvs = "calib";
constexpr uint8_t kVersion = 1;

// As NVS keeps it, one blob a profile; a layout change takes a new version, and an older blob is
// read as none.
struct Stored {
  uint8_t version = kVersion;
  uint8_t reserved = 0;
  uint16_t steering[3] = {};  // negative end, neutral, positive end
  uint16_t throttle[3] = {};
  uint16_t aux = 0;
};

rc::Calibration active;
bool haveActive = false;

void key(uint8_t profile, char (&out)[4]) { snprintf(out, sizeof out, "p%u", profile); }

// A calibrated input accepts its own ends, widened by the margin (docs/algorithms.md); any other,
// config.h's constants.
void applyWindow(RcInput input, const rc::Axis* axis) {
  if (axis && rc::calibrated(*axis)) {
    uint16_t lo, hi;
    rc::window(*axis, RC_WINDOW_MARGIN_US, lo, hi);
    rcSetWindow(input, lo, hi);
  } else {
    rcSetWindow(input, RC_PULSE_MIN_US, RC_PULSE_MAX_US);
  }
}

void applyWindows() {
  applyWindow(RcInput::Steering, haveActive ? &active.steering : nullptr);
  applyWindow(RcInput::Throttle, haveActive ? &active.throttle : nullptr);
}

}  // namespace

const rc::Calibration* calibrationActive() { return haveActive ? &active : nullptr; }

void calibrationUse(uint8_t profile) {
  char k[4];
  key(profile, k);
  Stored s;
  Preferences nvs;
  nvs.begin(kNvs, /*readOnly=*/true);
  haveActive = nvs.getBytesLength(k) == sizeof s && nvs.getBytes(k, &s, sizeof s) == sizeof s &&
               s.version == kVersion;
  nvs.end();
  if (haveActive) {
    active.steering = {s.steering[0], s.steering[1], s.steering[2]};
    active.throttle = {s.throttle[0], s.throttle[1], s.throttle[2]};
    active.auxUs = s.aux;
  }
  applyWindows();
}

bool calibrationSave(uint8_t profile, const rc::Calibration& cal) {
  Stored s;
  s.steering[0] = cal.steering.negativeUs;
  s.steering[1] = cal.steering.neutralUs;
  s.steering[2] = cal.steering.positiveUs;
  s.throttle[0] = cal.throttle.negativeUs;
  s.throttle[1] = cal.throttle.neutralUs;
  s.throttle[2] = cal.throttle.positiveUs;
  s.aux = cal.auxUs;
  char k[4];
  key(profile, k);
  Preferences nvs;
  nvs.begin(kNvs, /*readOnly=*/false);
  bool ok = nvs.putBytes(k, &s, sizeof s) == sizeof s;
  nvs.end();
  if (!ok) return false;
  active = cal;
  haveActive = true;
  applyWindows();
  return true;
}

void calibrationClear(uint8_t profile) {
  char k[4];
  key(profile, k);
  Preferences nvs;
  nvs.begin(kNvs, /*readOnly=*/false);
  nvs.remove(k);
  nvs.end();
}
