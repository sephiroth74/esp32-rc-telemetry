// The ui task — PLAN-ESP32.md §3, §4.1.
//
// One task owns the keys, the display and the board states of §3.7, so none of them needs a lock.
// For now it samples the keys and logs their events on the serial console (ROADMAP.md step 2.3);
// the display and the menu arrive in steps 2.4–2.6.
#pragma once

void uiStart();  // once, from setup(), after keysBegin()
