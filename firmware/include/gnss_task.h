// The gnss task — PLAN-ESP32.md §4.1, §4.2, §5.3, ROADMAP.md step 4.3.
//
// UART1, as checks 8 and 9 left it, into a ubx::Scanner, on core 1 at the highest priority. Each
// whole frame goes two ways:
//
// - While a session is open, into the logger's ring as an .rcl record, verbatim, at the time it was
//   received (§4.2, logger.h). A full ring is counted there.
// - NAV-PVT and NAV-SAT also update the live state the ui reads, decoded (lib/ubx/nav.h). Every
//   NAV-PVT also goes into a queue of its own, for the laps, which need them all (lap_timer.h).
//
// Losses are counted, never ignored (§5.3): UART overruns, frames failing their checksum, lengths
// out of range. The task also takes over the background retry of check 9 from loop(), since it
// owns the UART now.
#pragma once

#include <stdint.h>

#include "nav.h"
#include "ubx.h"

struct GnssLoss {
  uint32_t uartOverruns = 0;  // the UART's FIFO or its 2048 B buffer overflowed: bytes lost
  uint32_t uartErrors = 0;    // framing, parity, break
  uint32_t badChecksum = 0;
  uint32_t oversize = 0;  // a length above ubx::kMaxPayload

  uint32_t total() const { return uartOverruns + uartErrors + badChecksum + oversize; }
};

struct GnssLive {
  uint32_t startMs = 0;     // when acquisition began: the task's start
  uint32_t lastByteMs = 0;  // 0 before the first; a link gone quiet shows here
  uint32_t epochs = 0;      // NAV-PVTs since the start
  bool havePvt = false;
  ubx::NavPvt pvt = {};  // the last epoch
  uint32_t pvtAtMs = 0;
  uint32_t firstFixMs = 0;  // the first epoch with ubx::NavPvt::fix3d(), 0 before it
  bool haveSat = false;
  uint8_t satsTracked = 0;  // in the last NAV-SAT, with a signal
  uint8_t satsUsed = 0;     // ...and in the solution
  GnssLoss loss;
};

// A NAV-PVT as the laps read it.
struct GnssEpoch {
  uint32_t atMs = 0;  // millis() when it came: the time its record is logged at
  uint32_t iTowMs = 0;
  int32_t latE7 = 0, lonE7 = 0;
  int32_t gSpeedMmS = 0;
  int32_t headMotE5 = 0;
  bool fix3d = false;  // ubx::NavPvt::fix3d(): the status bar's 3D
};

// After the POST, when check 8 found the link.
void gnssTaskStart();
bool gnssTaskRunning();
// A consistent copy, for any task.
void gnssLiveRead(GnssLive& out);
// The oldest NAV-PVT not taken yet, false with none: 3.2 s of them wait, and the ones that find the
// queue full are counted and dropped (gnssEpochsDropped()).
bool gnssNextEpoch(GnssEpoch& out);
uint32_t gnssEpochsDropped();
#ifdef BENCH_SIM
void gnssInjectEpoch(const GnssEpoch& e);  // the bench simulation's car (bench_sim.h)
#endif
