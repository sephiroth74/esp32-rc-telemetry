// The display — PLAN-ESP32.md §2.4, §3.2.
//
// Shows what lib/ui's render() produced, on either grid of §3.2 — small (the 5x8 font, a row per
// 8-pixel page) or large (6x13B rows, with the hint in the 5x8) — plus the toast, the hold bar and
// a scroll bar on the right edge. displayShow() only changes a model; displayRefresh() sends the
// pages that changed, at most every DISPLAY_REFRESH_MS, so the bus is left to the IMU for most of
// the time (§2.4, rules 2 and 3).
//
// Owned by the ui task, like the keys: no locking other than the I²C bus lock inside.
#pragma once

#include <stdint.h>

#include "ui_screens.h"

bool displayBegin();  // false if nothing answers at DISPLAY_I2C_ADDR; after i2cBusBegin()

void displaySetTextSize(ui::TextSize size);  // a change redraws the whole panel
// The brightness setting (an index into ui::kBrightnessLabels), or the dimmed contrast over it.
// A command on the bus only when the contrast changes.
void displaySetContrast(uint8_t brightness, bool dimmed);
void displayShow(const ui::ScreenText& text);
// For DISPLAY_TOAST_MS: over the hint with small text, over the last body row with large text,
// where it reads in the large font.
void displayToast(const char* text, uint32_t nowMs);
void displayHoldBar(uint8_t percent);     // the bottom pixel row, 0 = hidden
void displayAcknowledge(uint32_t nowMs);  // inverts the panel DISPLAY_ACK_INVERT_MS

void displayRefresh(uint32_t nowMs);  // call every tick; sends at most every DISPLAY_REFRESH_MS

// Bench figures for the bus budget of §2.4, measured once in displayBegin().
uint32_t displayFullFrameUs();
uint32_t displayOnePageUs();
