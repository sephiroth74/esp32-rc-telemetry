// The laps of the open session — PLAN-ESP32.md §7.5, ROADMAP.md steps 8.2, 8.3 and 8.5.
//
// lib/geo's laps on the device, from the ui task (§4.1). At a session's start, the track's start
// line when it has one; without one, geo::LineFinder over a 10-minute trace in PSRAM, and once two
// laps agree the line it finds saved as the track's, source `auto`, and the laps so far found again
// from the trace. Each crossing goes into the log as a Lap record (docs/protocol.md, 0x07), with
// the flags its lap has then against the session's median; the screens read the count, the last,
// the best of the unflagged laps and the difference of the two.
#pragma once

#include <stdint.h>

#include "laps.h"

struct LapView {
  bool haveLine = false;  // a gate: the track's, or one found
  geo::LineSource source = geo::LineSource::Auto;
  uint16_t laps = 0;  // completed
  uint32_t lastMs = 0;  // 0: none yet
  geo::Flag lastFlag = geo::Flag::None;
  uint32_t bestMs = 0;  // of the laps not flagged; 0: none yet
  int32_t deltaMs = 0;  // the last against the best
};

// A lap just completed, for its toast.
struct LapEvent {
  uint16_t lap = 0;
  uint32_t lapMs = 0;
  geo::Flag flag = geo::Flag::None;
  bool lineFound = false;  // the automatic line came with it
};

// At a session's start, once sessionBegin() has filed it.
void lapTimerBegin();
// Every NAV-PVT while the session is open, in order. True when a lap was completed, the last one in
// `out`; a line found brings the laps so far at once.
bool lapTimerEpoch(const geo::Epoch& e, LapEvent& out);
void lapTimerEnd();  // the view stays, for Home
const LapView& lapTimerView();
