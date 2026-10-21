// The automatic start and stop of a session — PLAN-ESP32.md §3.6, ROADMAP.md step 6.4.
//
// Fed what the car is doing at every tick: its speed, while a fresh 3D fix gives one, and its
// throttle off neutral, while a calibrated profile and a live throttle input give one. It says
// when a session should start — more than 5 km/h for 2 s, or the throttle more than ±10 % from
// neutral for 1 s, whichever comes first — and when it should stop: standing still, below 2 km/h
// with the throttle within ±5 %, for session.auto_stop_min. What is not known does not count either
// way; with neither known the car is never taken for still, and only switching it off ends a run.
//
// After a stop by hand the start waits until the car has stood still for rearmMs: a run stopped
// while moving would otherwise start again at once. After the automatic stop it has stood still
// for minutes already.
//
// Framework-free (§6.1): the board feeds it and the clock comes from the caller.
#pragma once

#include <stdint.h>

namespace ui {

struct AutoSessionTiming {
  uint32_t startSpeedMmS = 1389;  // 5 km/h
  uint32_t startSpeedMs = 2000;
  uint8_t startThrottlePct = 10;
  uint32_t startThrottleMs = 1000;
  uint32_t stillSpeedMmS = 556;  // 2 km/h
  uint8_t stillThrottlePct = 5;
  uint32_t stopMs = 5 * 60 * 1000;  // session.auto_stop_min
  uint32_t rearmMs = 5000;
};

// What the car is doing, as far as anything tells.
struct Motion {
  bool speedKnown = false;  // a fresh 3D fix
  uint32_t speedMmS = 0;
  bool throttleKnown = false;  // a calibrated profile, and the throttle input delivering pulses
  int8_t throttlePct = 0;      // from neutral: -100 full brake … 100 full throttle
};

class AutoSession {
 public:
  explicit AutoSession(const AutoSessionTiming& timing) : t_(timing) {}

  void update(const Motion& motion, uint32_t nowMs);

  bool startDue(uint32_t nowMs) const;  // armed, and moving for long enough
  bool stopDue(uint32_t nowMs) const;   // still for stopMs
  bool still() const { return still_.on; }
  bool armed() const { return armed_; }
  void disarm() { armed_ = false; }  // a stop by hand
  // A session has begun: standing still counts from now, not from before it, so a run started by
  // hand on a car parked for an hour is not stopped at once.
  void restartStill(uint32_t nowMs) {
    if (still_.on) still_.sinceMs = nowMs;
  }

 private:
  // A condition and since when it has held unbroken.
  struct Held {
    bool on = false;
    uint32_t sinceMs = 0;
    void set(bool now, uint32_t nowMs);
    bool for_(uint32_t ms, uint32_t nowMs) const { return on && nowMs - sinceMs >= ms; }
  };

  AutoSessionTiming t_;
  Held fast_, throttle_, still_;
  bool armed_ = true;  // at boot: a device that restarted mid-run picks the run up again
};

}  // namespace ui
