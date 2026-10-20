// The UART to the GNSS module — PLAN-ESP32.md §5.2, POST check 8 (§3.3), ROADMAP.md step 3.5.
//
// gnssLinkScan() finds the rate the module talks at: first the rate it was found at last time, kept
// in NVS, then GNSS_BAUD_SCAN in order, each for up to GNSS_BAUD_SCAN_TIMEOUT_MS, until a whole
// frame with its checksum arrives (ubx::LinkProbe). A MON-VER poll goes out at each rate, so that a
// module whose periodic output is off answers too. The UART stays open at the rate found; check 9
// (gnss_config.h) moves the module to GNSS_BAUD and the UART with it, through gnssLinkSwitch().
#pragma once

#include <HardwareSerial.h>
#include <stdint.h>

uint32_t gnssLinkScan();  // the baud rate found, 0 for none; from the POST, before any task
uint32_t gnssLinkBaud();  // as the scan found it, or as gnssLinkSwitch() left it
// Once the module has been told to change rate: the UART follows, and a poll must be answered at
// the new rate, which is then the one NVS keeps. Otherwise the UART goes back to the old rate, and
// when nothing answers there either the link is gone (gnssLinkBaud() 0).
bool gnssLinkSwitch(uint32_t baud);
HardwareSerial& gnssSerial();
