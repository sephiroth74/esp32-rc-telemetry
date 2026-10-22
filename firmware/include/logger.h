// The session log on the card — PLAN-ESP32.md §4.2, §4.4, §4.5, ROADMAP.md step 5.3.
//
// Producers — the gnss task, the ui's markers, later the RC sampling and the IMU — append whole
// .rcl records to the PSRAM ring of check 1 (lib/logbuf), under a mutex held for the copy. The
// logger task, on core 0, only writes the ring out: 32 KB at a time, everything waiting every 5 s
// with an fsync, into a file grown 4 MB ahead of what it holds so the FAT never has to grow in the
// middle of a burst. A stalled card stalls nobody but the logger; the ring takes over a minute of it
// before a record is dropped, and a dropped record is counted.
//
// A file starts with the .rcl file header and the session header record, both through the ring, so
// nothing can come before them; it ends, on a graceful close, with the footer, and is cut to the
// length written. After a power cut it keeps what the last fsync saw, followed by the unwritten end
// of its extent, which readers skip (docs/protocol.md).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "rcl.h"

struct LogStats {
  bool open = false;
  uint32_t records = 0;       // this session's, written or waiting
  uint32_t dropped = 0;       // found the ring full
  uint64_t written = 0;       // bytes on the card
  uint64_t allocated = 0;     // the file's size, extents included
  uint32_t writeMaxMs = 0;
  uint32_t slowWrites = 0;
  size_t ringUsed = 0;
};

// After the POST, with the card mounted and check 1's ring allocated; false without either.
bool loggerBegin();

// A session's file at `path`, in a folder that exists: the file header and `header` go into the
// ring at once, and the logger task creates the file. False while a session is open or closing.
bool logOpen(const char* path, const rcl::SessionHeader& header);
// The footer, everything waiting, the file cut to its length and closed. Waits up to `waitMs` for
// the card, which SAVING needs (§4.5); false when it took longer, and the close goes on.
bool logClose(rcl::Close reason, uint32_t waitMs);
// Open, or opening: records are taken.
bool logActive();
// A card write that failed: the session's file is closed, and nothing more is taken.
bool logFailed();

// A record at `eventMs` (millis()): whole into the ring, or dropped and counted. False when no
// session is open, or there was no room.
bool logAppend(rcl::Type type, uint32_t eventMs, const void* payload, size_t length);

void logStats(LogStats& out);
// The footer the last graceful close wrote, for meta.json; false before the first.
bool logLastFooter(rcl::Footer& out);
