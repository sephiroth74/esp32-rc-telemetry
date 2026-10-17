// The display — PLAN-ESP32.md §2.4, §3.2.
//
// A text grid of 21 columns × 8 rows, each row one 8-pixel page of the SSD1306: rows 0–1 are the
// status bar (the yellow band), rows 2–7 the six body lines. The functions only change a model;
// displayRefresh() sends the pages that changed, at most every DISPLAY_REFRESH_MS, so the bus is
// left to the IMU for most of the time (§2.4, rules 2 and 3).
//
// Owned by the ui task, like the keys: no locking other than the I²C bus lock inside.
#pragma once

#include <stdint.h>

constexpr uint8_t kDisplayCols = 21;
constexpr uint8_t kStatusLines = 2;
constexpr uint8_t kBodyLines = 6;

bool displayBegin();  // false if nothing answers at DISPLAY_I2C_ADDR; after i2cBusBegin()

void displayStatus(uint8_t line, const char* text);  // line 0..1; cut to 21 columns
void displayBody(uint8_t line, const char* text);    // line 0..5; the last one is the key hint
void displayToast(const char* text, uint32_t nowMs);  // over the last body line, DISPLAY_TOAST_MS
void displayHoldBar(uint8_t percent);                 // the bottom pixel row, 0 = hidden
void displayAcknowledge(uint32_t nowMs);              // inverts the panel DISPLAY_ACK_INVERT_MS

void displayRefresh(uint32_t nowMs);  // call every tick; sends at most every DISPLAY_REFRESH_MS

// Bench figures for the bus budget of §2.4, measured once in displayBegin().
uint32_t displayFullFrameUs();
uint32_t displayOnePageUs();
