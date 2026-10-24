// The car profiles' calibrations — PLAN-ESP32.md §3.8, ROADMAP.md step 6.3.
//
// One calibration per profile, in NVS under the profile's index, so that it survives a card swap.
// The active profile's is the one in use: it sets its inputs' accepted windows (§4.3), turns widths
// into percentages for the screens and the automatic start (§3.6), and goes into every session's
// meta.json. lib/rc holds what a calibration is and what it makes of a width.
#pragma once

#include <stdint.h>

#include "rc.h"

// The calibration in use; nullptr when the active profile has none.
const rc::Calibration* calibrationActive();
// Puts `profile`'s saved calibration in use, if it has one, and the inputs' windows with it.
void calibrationUse(uint8_t profile);
// Saves `cal` as `profile`'s and puts it in use; false when NVS took nothing.
bool calibrationSave(uint8_t profile, const rc::Calibration& cal);
// New car (§3.8): whatever NVS still holds at `profile`, left by a profile deleted in an app.
void calibrationClear(uint8_t profile);
