// The settings — PLAN-ESP32.md §3.8, ROADMAP.md steps 2.7 and 3.6.
//
// config.json twice, through lib/cfg: on the card, the source, edited from a computer or an app; on
// LittleFS, the last good copy. POST check 2 loads the LittleFS copy, so that the rest of the POST
// runs on the last settings; check 3, once the card is in, reads the card's, takes it key by key
// and mirrors it to LittleFS. A card without the file gets one; a card whose file is not JSON at
// all keeps it, and the LittleFS copy is used. Edits made on the device go to both copies.
//
// The active car profile is not in config.json: it lives in NVS (§3.8), where a card moved from one
// device to another does not move it. The first boot with this module takes the display settings
// and the profile count step 2.7 kept in NVS, so that nothing chosen on the device is lost.
#pragma once

#include "cfg.h"
#include "ui_menu.h"

enum class SettingsSource : uint8_t {
  Defaults,        // no copy anywhere yet
  LittleFs,        // the LittleFS copy: no card read yet, or the card's file is not JSON
  Card,            // the card's file
  Created,         // written to the card, which had none
  CardUnreadable,  // the card's file is not JSON: the LittleFS copy is used, the file left alone
};

void settingsLoadLocal();  // POST check 2, once LittleFS is mounted
void settingsLoadCard();   // POST check 3, once the card is mounted

const cfg::Config& settings();
const cfg::Problems& settingsProblems();  // the keys of the card's file that fell back
SettingsSource settingsSource();
const char* settingsSourceName();  // up to 13 characters, for About and /post.log

// The display settings, whether a session needs the fix, the profiles and the active one, into
// what the menu reads.
void settingsFill(ui::MenuContext& menu);
const char* settingsProfileName(uint8_t index);
// After a command: what changed goes to both copies of config.json, the active profile to NVS. The
// card's copy only when the card took the POST's write probe; LittleFS's always.
void settingsSave(const ui::MenuContext& menu);
