// Sleep — PLAN-ESP32.md §3.7, §4.5, ROADMAP.md step 6.5.
//
// What the board is fed every tick to know whether anything is going on: the receiver's pulses and
// a USB host, which keeps the device awake. From IDLE only (ui_board.h), the pulses stopping, or a
// long time with neither them nor a key, opens a notice that any key cancels, and then the device
// sleeps until OK. There is no backup battery (§2.7): the car switched off takes the device with
// it, so a session is never ended by the pulses stopping.
//
// Framework-free (§6.1).
#pragma once

#include <stdint.h>

namespace ui {

struct PowerTiming {
  uint32_t noticeMs = 10000;        // IDLE: the notice before SLEEP, that any key cancels
  uint32_t idleMs = 5 * 60 * 1000;  // IDLE with no RC signal, no USB host and no key: SLEEP
};

struct Power {
  bool rcLive = false;   // a valid pulse on an input within RC_SIGNAL_LOST_MS
  bool usbHost = false;  // keeps the device out of SLEEP (§3.7)
};

// Why the sleep notice is up.
enum class SleepWhy : uint8_t { None, CarOff, Idle };

}  // namespace ui
