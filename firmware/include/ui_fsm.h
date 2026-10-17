// The ui task — PLAN-ESP32.md §3, §4.1.
//
// One task owns the keys, the display and the board states of §3.7, so none of them needs a lock.
// It feeds the key events to lib/ui's Board, renders the screens and logs events, commands and
// screen changes on the serial console. Until the modules behind the commands exist, the facts on
// screen are placeholders (ROADMAP.md step 2.6).
#pragma once

void uiStart();  // once, from setup(), after keysBegin()
