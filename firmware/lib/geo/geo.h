// Track and lap geometry — PLAN-ESP32.md §7.1, docs/algorithms.md.
//
// Skeleton only. To implement: the ~1 km track cell id (§7.1) and, in phase 6,
// interpolated gate crossing, s/d projection, sector splits and lap detection.
// Shaped around u-blox's native integer degrees × 1e-7 so no floating point
// enters the path between a fix and a folder name.
//
// Framework-free (§6.1), and mirrored by packages/rct_core in Dart: both are
// checked against the same fixtures, because the phone shows a lap time the
// desktop app then has to reproduce.
#pragma once
