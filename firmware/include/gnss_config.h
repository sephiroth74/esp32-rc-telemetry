// The GNSS module's configuration — PLAN-ESP32.md §5.2, POST check 9 (§3.3), ROADMAP.md step 4.1.
//
// What the firmware needs of the M10: NAV-PVT at 10 Hz and NAV-SAT at 1 Hz on UART1, no NMEA, SBAS,
// the automotive dynamic model, 230400 baud. Every boot reads back what the module runs on (VALGET,
// the RAM layer) and writes only what differs, to RAM and the battery-backed RAM, which must be
// acknowledged: a module that kept its settings is left alone, with no GNSS restart for a signal
// key written over with its own value, and the read tells whether it kept them. When something had
// to change, the same goes to the FLASH layer too: the GM10 has config flash and takes it, so a
// module that was set up once comes up set even with its BBR gone. The baud rate goes last, on its
// own: the UART follows it and a poll must answer at the new rate. Then the BBR and FLASH layers
// are read back, for /post.log: what the module would come up with after a power cut.
//
// The POST runs it as check 9, after check 8 found the link. A module that did not answer is tried
// again every GNSS_CFG_RETRY_MS by the gnss task, which owns the UART after the POST (gnss_task.h).
#pragma once

#include <Print.h>

#include "ui_post.h"

ui::CheckReport gnssConfigure();
bool gnssConfigPending();  // the last attempt went unanswered, with a link to try again on

// The last attempt for /post.log: "gnss: " lines, the keys changed with their old values.
void gnssConfigLog(Print& out);
