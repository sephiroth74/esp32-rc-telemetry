// Keys on the device — PLAN-ESP32.md §3.1.
//
// Samples the ladder on PIN_KEYS and runs the readings through lib/ui's KeyReader and Gestures.
// The ui task owns all of it, so there is no locking: it calls keysTick() every sample period and
// drains the events with keysNext() in the same loop (§4.1).
#pragma once

#include <stdint.h>

#include "ui_keys.h"

void keysBegin();               // once, from setup()
void keysTick(uint32_t nowMs);  // one sample: every 1000 / KEY_SAMPLE_HZ ms
bool keysNext(ui::Event& event);
void keysSwallow();
uint8_t keysHoldPercent(uint32_t nowMs);

// For diagnostics: the key the reader has accepted, and the last reading behind it.
ui::Key keysHeld();
uint16_t keysLastMv();
