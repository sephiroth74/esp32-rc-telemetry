// The on-device settings — PLAN-ESP32.md §3.8, ROADMAP.md step 2.7.
//
// Brightness, dim delay and text size, and the car profiles: how many there are and which one is
// active. All in NVS for now; step 3.6 moves the display settings and the profiles into
// config.json, written to both copies, and only the active profile stays in NVS (§3.8). A value
// read back is checked against its range, so one left by another firmware falls back to its default
// instead of indexing past a table.
//
// Owned by the ui task.
#pragma once

#include "ui_menu.h"

// Fills brightness, dimDelay, textSize, profileCount and activeProfile. What NVS does not hold
// keeps the default of config.h, or for the profile count the value already in `menu`.
void settingsLoad(ui::MenuContext& menu);
// Writes those fields of `menu` that changed since the last load or save; a no-op otherwise.
void settingsSave(const ui::MenuContext& menu);
