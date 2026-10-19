// The power-on self-test on the device — PLAN-ESP32.md §3.3, ROADMAP.md steps 3.1–3.5.
//
// Runs lib/ui's Post from setup(), before any task exists (§4.1): each check on the hardware, the
// POST screen on the display, and in the fatal latch the keys polled here directly. Returns once
// the device may enter IDLE, with a key that is still held swallowed, so that a press made during
// the POST does nothing afterwards either.
//
// So far check 0 is real; the others report "--" until their steps bring them: 1–2 in 3.2, 3–6 in
// 3.3, 8 in 3.5, 7 and 9 in phase 4, 10 in phase 6.
#pragma once

#include "ui_post.h"

ui::PostResult postRun();  // after i2cBusBegin() and keysBegin()
