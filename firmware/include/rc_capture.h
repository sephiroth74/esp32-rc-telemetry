// RC capture — PLAN-ESP32.md §4.3, ROADMAP.md step 6.2.
//
// The receiver's steering and throttle pulses, timed by the MCPWM capture unit: both edges stamped
// in hardware at 80 MHz, on one capture timer, as tools/rc-live does for one channel. A pulse counts
// once its width and its frame period fit the accepted window; anything else is counted as a reject
// and dropped, so that an ESC's telemetry on the throttle wire never reaches the trace (§2.8). An
// input is present once it has delivered a valid pulse, and either may be missing.
//
// While a session is open, a 50 Hz sampler writes the latest widths to the log as RC records, five
// to an epoch: 0 for an input with no valid pulse in the last RC_STALE_MS, never a stale width
// (docs/protocol.md, 0x05). The aux channel has no input yet (D1, §8), and logs 0.
#pragma once

#include <stdint.h>

enum class RcInput : uint8_t { Steering, Throttle, Aux, Count };

struct RcLive {
  uint16_t widthUs[3] = {};   // the latest valid pulse; 0 with none in the last RC_STALE_MS
  bool present[3] = {};       // has delivered a valid pulse since boot
  uint32_t pulses[3] = {};    // valid, since boot
  uint32_t rejects[3] = {};   // since boot
  uint32_t lastPulseMs = 0;   // millis() of the latest valid pulse on any input; 0 for never
};

// The capture and the sampler; false when the capture unit would not start.
bool rcBegin();
void rcRead(RcLive& out);
// An input's accepted widths: config.h's until a calibration gives its own (§4.3, step 6.3).
void rcSetWindow(RcInput input, uint16_t minUs, uint16_t maxUs);

#ifdef BENCH_SIM
// The bench simulation (bench_sim.h): a receiver sending these widths at 60 Hz, as pulses through
// the inputs' windows; 0 for an input off.
void rcSimulate(uint16_t steeringUs, uint16_t throttleUs);
#endif
