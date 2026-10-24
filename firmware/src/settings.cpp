#include "settings.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <string.h>

#include "card.h"
#include "config.h"

namespace {

constexpr const char* kPath = "/config.json";
constexpr const char* kNvs = "ui";
constexpr size_t kMaxText = 2048;  // a full config.json is under 600 bytes

cfg::Config current;
cfg::Problems problems;
SettingsSource source = SettingsSource::Defaults;
uint8_t activeProfile = 0;

cfg::Config defaults() {
  cfg::Config c;
  c.keysLayout = KEY_LAYOUT_DEFAULT == KeyLayout::FiveWay ? 1 : 0;
  c.brightness = DISPLAY_BRIGHTNESS_DEFAULT;
  c.dimDelay = DISPLAY_DIM_DELAY_DEFAULT;
  c.textSize = DISPLAY_TEXT_SIZE_DEFAULT;
  c.minFreeMb = POST_MIN_FREE_MB;
  c.warnFreeMb = POST_WARN_FREE_MB;
  c.autoStopMin = SESSION_AUTO_STOP_MIN;
  c.matchRadiusM = static_cast<uint16_t>(TRACK_MATCH_RADIUS_M);
  c.profileCount = 1;
  cfg::defaultName(0, c.profiles[0].name);
  return c;
}

bool readFile(fs::FS& fs, String& out) {
  File f = fs.open(kPath, FILE_READ);
  if (!f) return false;
  out = f.readString();
  f.close();
  return true;
}

bool writeFile(fs::FS& fs, const char* text, size_t n) {
  File f = fs.open(kPath, FILE_WRITE);
  if (!f) return false;
  bool ok = f.write(reinterpret_cast<const uint8_t*>(text), n) == n;
  f.close();
  return ok;
}

// What step 2.7 kept in NVS, for a first boot that finds no config.json anywhere.
void fromLegacyNvs(cfg::Config& c) {
  Preferences nvs;
  nvs.begin(kNvs, /*readOnly=*/false);
  if (nvs.isKey("bright")) c.brightness = nvs.getUChar("bright") % 3;
  if (nvs.isKey("dim")) c.dimDelay = nvs.getUChar("dim") % 4;
  if (nvs.isKey("text")) c.textSize = nvs.getUChar("text") % 2;
  uint8_t count = nvs.getUChar("profiles", c.profileCount);
  if (count >= 1 && count <= cfg::kMaxProfiles) {
    for (uint8_t i = c.profileCount; i < count; ++i) cfg::defaultName(i, c.profiles[i].name);
    c.profileCount = count;
  }
  nvs.end();
}

uint8_t readActiveProfile() {
  Preferences nvs;
  nvs.begin(kNvs, /*readOnly=*/false);
  uint8_t p = nvs.getUChar("profile", 0);
  nvs.end();
  return p < current.profileCount ? p : 0;
}

// Both copies, from `current`; the card's only when it may be written.
void writeBoth(bool toCard) {
  static char text[kMaxText];
  size_t n = cfg::write(current, text, sizeof text);
  if (!n) return;
  String local;
  if (!readFile(LittleFS, local) || local != text) writeFile(LittleFS, text, n);
  if (toCard && cardMounted()) writeFile(SD_MMC, text, n);
}

}  // namespace

void settingsLoadLocal() {
  cfg::Config d = defaults();
  String text;
  cfg::Problems ignored;  // a copy the device wrote itself; the card's problems are what matter
  if (readFile(LittleFS, text) &&
      cfg::read(text.c_str(), text.length(), d, current, ignored) == cfg::Read::Ok) {
    source = SettingsSource::LittleFs;
  } else {
    current = d;
    fromLegacyNvs(current);
    source = SettingsSource::Defaults;
  }
  activeProfile = readActiveProfile();
}

void settingsLoadCard() {
  String text;
  problems = cfg::Problems();
  if (!readFile(SD_MMC, text)) {
    source = SettingsSource::Created;  // whatever was in force, the LittleFS copy or the defaults
    writeBoth(/*toCard=*/true);
    return;
  }
  cfg::Config fromCard;
  switch (cfg::read(text.c_str(), text.length(), defaults(), fromCard, problems)) {
    case cfg::Read::NotJson:
      source = SettingsSource::CardUnreadable;  // left as it is, for a person to fix
      problems = cfg::Problems();
      return;
    case cfg::Read::Empty:
    case cfg::Read::Ok:
      current = fromCard;
      source = SettingsSource::Card;
      writeBoth(/*toCard=*/false);  // the card's file stays as written, mistakes and all
      break;
  }
  activeProfile = readActiveProfile();
}

const cfg::Config& settings() { return current; }

const cfg::Problems& settingsProblems() { return problems; }

SettingsSource settingsSource() { return source; }

const char* settingsSourceName() {
  switch (source) {
    case SettingsSource::Defaults:
      return "defaults";
    case SettingsSource::LittleFs:
      return "LittleFS copy";
    case SettingsSource::Card:
      return "card";
    case SettingsSource::Created:
      return "created";
    case SettingsSource::CardUnreadable:
      return "card unread";
  }
  return "?";
}

void settingsFill(ui::MenuContext& menu) {
  menu.brightness = current.brightness;
  menu.dimDelay = current.dimDelay;
  menu.textSize = current.textSize;
  menu.requireFix = current.requireFix;
  menu.profileCount = current.profileCount;
  menu.activeProfile = activeProfile;
  Serial.printf("settings: %s; brightness %s, dim after %s, text %s, require fix %s, "
                "profile %u of %u\n",
                settingsSourceName(), ui::kBrightnessLabels[menu.brightness],
                ui::kDimDelayLabels[menu.dimDelay], ui::kTextSizeLabels[menu.textSize],
                ui::kRequireFixLabels[menu.requireFix ? 1 : 0], menu.activeProfile + 1,
                menu.profileCount);
}

const char* settingsProfileName(uint8_t index) {
  return index < current.profileCount ? current.profiles[index].name : "?";
}

void settingsSave(const ui::MenuContext& menu) {
  if (menu.activeProfile != activeProfile) {
    activeProfile = menu.activeProfile;
    Preferences nvs;
    nvs.begin(kNvs, /*readOnly=*/false);
    nvs.putUChar("profile", activeProfile);
    nvs.end();
  }
  bool changed = current.brightness != menu.brightness || current.dimDelay != menu.dimDelay ||
                 current.textSize != menu.textSize || current.profileCount != menu.profileCount ||
                 current.requireFix != menu.requireFix;
  if (!changed) return;
  current.brightness = menu.brightness;
  current.dimDelay = menu.dimDelay;
  current.textSize = menu.textSize;
  current.requireFix = menu.requireFix;
  for (uint8_t i = current.profileCount; i < menu.profileCount && i < cfg::kMaxProfiles; ++i) {
    cfg::defaultName(i, current.profiles[i].name);  // New car
  }
  current.profileCount = menu.profileCount;
  writeBoth(menu.cardOk && menu.cardWritable);
}
