// A session's place on the card — PLAN-ESP32.md §7.1, ROADMAP.md steps 5.4 and 5.5.
//
// Starting one: the track the fix belongs to, by docs/algorithms.md's rules (lib/geo), among the
// folders of /tracks — or a new folder named from the cell, or /tracks/_unassigned without a fix;
// the next session_NNN in it; the global session id from NVS; meta.json; then the log (logger.h).
// Ending one: the log closed, meta.json completed with the footer's numbers, and a session that
// began without a fix moved into its track if a fix came before the end.
//
// track.json, as the device writes it and reads it back:
//
//   {"version": 1, "id": "N4512E00934", "name": "", "last_used": 42,
//    "centre": {"lat_e7": 451234560, "lon_e7": 93456780},
//    "fence": [[lat_e7, lon_e7], …]}
//
// "name" and "fence" are the apps'; the device leaves them as it found them. "last_used" is the
// global id of the last session filed there: it only grows, and needs no clock. start_line.json,
// when there is one, gives the gate's position as "lat_e7" and "lon_e7" (§7.5).
#pragma once

#include <stdint.h>

#include "geo.h"
#include "rcl.h"

struct SessionStart {
  bool haveFix = false;  // the acquisition's fix (§3.4), not a bare 3D
  geo::Point fix;
  bool automatic = false;
  uint8_t profile = 0;
  const char* profileName = "";
  uint16_t postMask = 0;
  const char* postSummary = "";
};

struct SessionInfo {
  uint32_t id = 0;      // global, from NVS: what the apps key on
  uint32_t number = 0;  // within its track
  char track[geo::kTrackIdMax + 1] = "";
  bool newTrack = false;
};

// False with nothing open: no card to write to, or a folder or a file that could not be made.
bool sessionBegin(const SessionStart& start, SessionInfo& out);
// `haveFix`/`fix`: what the acquisition has now, for a session that began without a fix.
void sessionEnd(rcl::Close reason, bool haveFix, geo::Point fix);
bool sessionOpen();
const SessionInfo& sessionCurrent();
