// .rcl record packing — PLAN-ESP32.md §7.2.
//
// Skeleton only, and it stays that way until docs/protocol.md is written: that
// file is the single source of truth for the format, and this is one of its two
// implementations (the other being packages/rct_core in Dart).
//
// Record types to come: session header, raw UBX passthrough, IMU aggregate, IMU
// raw, RC sample, marker, lap event, session footer. Two properties the format
// has to keep — records are self-delimiting, so a log truncated by a power cut
// stays parseable (§4.5), and UBX payloads are stored verbatim (§5.3).
#pragma once
