// The ui task — PLAN-ESP32.md §3, §4.1.
//
// One task owns the keys, the display and the board states of §3.7, so none of them needs a lock.
// It feeds the key events to lib/ui's Board, renders the screens and logs events, commands and
// screen changes on the serial console. Until the modules behind the commands exist, the facts on
// screen are placeholders (ROADMAP.md step 2.6).
#pragma once

#include "ui_post.h"

// Once, from setup(), after the POST, whose result decides what IDLE allows: a check that refuses
// logging makes a session start a refusal naming it, and any failure or warning shows the warning
// mark (§3.3).
void uiStart(const ui::PostResult& post);

// A check retried after the POST, from another task: its new report replaces the POST's, and the
// warning mark, the refusal and About follow it on the next tick.
void uiPostUpdate(uint8_t id, const ui::CheckReport& report);
