// Deep sleep and the wake — PLAN-ESP32.md §4.5, ROADMAP.md step 6.5.
//
// The board decides when (ui_power.h); this carries it out: the display off, the card unmounted,
// and the chip into deep sleep until OK pulls D0 low (ext0). Every wake runs the POST, as a
// power-on does. Only a device powered without the car sleeps, a LiPo through the regulator, say:
// in the car there is no backup battery (§2.7), and the car switched off is a power-off. The bench
// simulation also wakes on a timer, so that a sleep can be seen through without a person there.
#pragma once

void powerSleep();  // does not return

// What started this boot, for /post.log: "power-on", "OK key" or "timer".
const char* powerWakeName();
