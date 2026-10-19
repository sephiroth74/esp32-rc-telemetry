// The UART to the GNSS module — PLAN-ESP32.md §5.2, POST check 8 (§3.3), ROADMAP.md step 3.5.
//
// gnssLinkScan() finds the rate the module talks at: first the rate it was found at last time, kept
// in NVS, then GNSS_BAUD_SCAN in order, each for up to GNSS_BAUD_SCAN_TIMEOUT_MS, until a whole
// frame with its checksum arrives (ubx::LinkProbe). A MON-VER poll goes out at each rate, so that a
// module whose periodic output is off answers too. The UART stays open at the rate found, for the
// GNSS task of phase 4, which reconfigures the module to GNSS_BAUD (check 9).
#pragma once

#include <HardwareSerial.h>
#include <stdint.h>

uint32_t gnssLinkScan();  // the baud rate found, 0 for none; from the POST, before any task
uint32_t gnssLinkBaud();  // as the scan found it
HardwareSerial& gnssSerial();
