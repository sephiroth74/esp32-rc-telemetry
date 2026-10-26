#include "lap_timer.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <string.h>

#include "config.h"
#include "logger.h"
#include "rcl.h"
#include "session.h"

namespace {

constexpr uint16_t kMaxLaps = 512;  // a session's laps kept for the median; more count, unstored

geo::Epoch* trace = nullptr;  // PSRAM, allocated with the first session that needs it
geo::LineFinder finder(nullptr, 0);
geo::Laps laps{geo::Gate()};
bool running = false, haveGate = false;
LapView view;

uint32_t lapMs[kMaxLaps];
double lapM[kMaxLaps];
double scratch[kMaxLaps];
uint16_t stored = 0;

void record(const geo::Crossing& c, geo::Flag flag) {
  rcl::Lap l;
  l.lap = c.lap;
  l.flags = static_cast<uint8_t>(flag);
  l.line = static_cast<uint8_t>(laps.gate().source);
  l.crossingMs = c.tMs;
  l.crossingItowMs = c.iTowMs;
  l.lapTimeMs = c.lapMs;
  uint8_t payload[rcl::kLapBytes];
  size_t n = rcl::encode(l, payload, sizeof payload);
  if (n) logAppend(rcl::Type::Lap, millis(), payload, n);
}

// A crossing that counts: its lap kept and flagged against the session's median as it is now, the
// best worked out again over the laps not flagged now, the record written.
void crossed(const geo::Crossing& c, LapEvent& out) {
  geo::Flag flag = geo::Flag::None;
  if (c.lap) {
    if (stored < kMaxLaps) {
      lapMs[stored] = c.lapMs;
      lapM[stored] = c.distanceM;
      ++stored;
    }
    memcpy(scratch, lapM, stored * sizeof lapM[0]);
    double median = geo::medianM(scratch, stored);
    flag = geo::flagOf(c.distanceM, median);
    view.bestMs = 0;
    for (uint16_t i = 0; i < stored; ++i) {
      if (geo::flagOf(lapM[i], median) != geo::Flag::None) continue;
      if (!view.bestMs || lapMs[i] < view.bestMs) view.bestMs = lapMs[i];
    }
    view.laps = c.lap;
    view.lastMs = c.lapMs;
    view.lastFlag = flag;
    view.deltaMs = view.bestMs ? static_cast<int32_t>(c.lapMs - view.bestMs) : 0;
    out.lap = c.lap;
    out.lapMs = c.lapMs;
    out.flag = flag;
  }
  record(c, flag);
}

const char* sourceName(geo::LineSource s) {
  return s == geo::LineSource::Desktop  ? "desktop"
         : s == geo::LineSource::Device ? "device"
                                        : "auto";
}

}  // namespace

void lapTimerBegin() {
  running = true;
  view = LapView();
  stored = 0;
  geo::Gate g;
  haveGate = sessionStartLine(g);
  if (haveGate) {
    laps = geo::Laps(g);
    view.haveLine = true;
    view.source = g.source;
    Serial.printf("laps: the track's start line, %s\n", sourceName(g.source));
    return;
  }
  if (!trace) {
    trace = static_cast<geo::Epoch*>(
        heap_caps_malloc(LAP_TRACE_EPOCHS * sizeof(geo::Epoch), MALLOC_CAP_SPIRAM));
  }
  finder = geo::LineFinder(trace, trace ? LAP_TRACE_EPOCHS : 0);
  Serial.printf("laps: no start line, looking for one%s\n", trace ? "" : " (no PSRAM: none)");
}

bool lapTimerEpoch(const geo::Epoch& e, LapEvent& out) {
  if (!running) return false;
  out = LapEvent();
  geo::Crossing c;
  if (haveGate) {
    if (!laps.update(e, c)) return false;
    crossed(c, out);
    return c.lap > 0;
  }
  geo::Gate g;
  uint32_t t0 = micros();
  bool found = finder.update(e, g);
  uint32_t us = micros() - t0;
  if (us > 20000) Serial.printf("laps: the search took %lu ms\n", (unsigned long)(us / 1000));
  if (!found) return false;
  // Two laps agreed (§7.5): the line, kept for the track, and the laps so far found again from the
  // trace, the epoch that found it included.
  haveGate = true;
  laps = geo::Laps(g);
  view.haveLine = true;
  view.source = g.source;
  bool saved = sessionSaveAutoLine(g);
  for (size_t i = 0; i < finder.size(); ++i) {
    if (laps.update(finder[i], c)) crossed(c, out);
  }
  out.lineFound = true;
  Serial.printf("laps: start line found at %ld, %ld, heading %ld.%02ld, %s; %u laps so far\n",
                (long)g.centre.latE7, (long)g.centre.lonE7, (long)(g.headingE5 / 100000),
                (long)(g.headingE5 % 100000 / 1000), saved ? "saved" : "not saved", view.laps);
  return true;
}

void lapTimerEnd() { running = false; }

const LapView& lapTimerView() { return view; }
