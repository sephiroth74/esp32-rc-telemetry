#include "settings.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "config.h"

namespace {

constexpr const char* kNamespace = "ui";

template <typename T, size_t N>
constexpr uint8_t countOf(T (&)[N]) {
  return N;
}

// What NVS holds, as last read or written: a key is written only when its field differs.
struct Stored {
  uint8_t brightness, dimDelay, textSize, profileCount, activeProfile;
} stored;

uint8_t inRange(uint8_t value, uint8_t count, uint8_t fallback) {
  return value < count ? value : fallback;
}

}  // namespace

void settingsLoad(ui::MenuContext& menu) {
  Preferences nvs;
  // Read-write, which creates the namespace on the first boot: read-only would fail and log it.
  nvs.begin(kNamespace, /*readOnly=*/false);
  stored.brightness = inRange(nvs.getUChar("bright", DISPLAY_BRIGHTNESS_DEFAULT),
                              countOf(ui::kBrightnessLabels), DISPLAY_BRIGHTNESS_DEFAULT);
  stored.dimDelay = inRange(nvs.getUChar("dim", DISPLAY_DIM_DELAY_DEFAULT),
                            countOf(ui::kDimDelayLabels), DISPLAY_DIM_DELAY_DEFAULT);
  stored.textSize = inRange(nvs.getUChar("text", DISPLAY_TEXT_SIZE_DEFAULT),
                            countOf(ui::kTextSizeLabels), DISPLAY_TEXT_SIZE_DEFAULT);
  uint8_t count = nvs.getUChar("profiles", menu.profileCount);
  stored.profileCount = count >= 1 && count <= CAR_PROFILES_MAX ? count : menu.profileCount;
  stored.activeProfile = inRange(nvs.getUChar("profile", 0), stored.profileCount, 0);
  nvs.end();

  menu.brightness = stored.brightness;
  menu.dimDelay = stored.dimDelay;
  menu.textSize = stored.textSize;
  menu.profileCount = stored.profileCount;
  menu.activeProfile = stored.activeProfile;
  Serial.printf("settings: brightness %s, dim after %s, text %s, profile %u of %u\n",
                ui::kBrightnessLabels[menu.brightness], ui::kDimDelayLabels[menu.dimDelay],
                ui::kTextSizeLabels[menu.textSize], menu.activeProfile + 1, menu.profileCount);
}

void settingsSave(const ui::MenuContext& menu) {
  Stored now = {menu.brightness, menu.dimDelay, menu.textSize, menu.profileCount,
                menu.activeProfile};
  if (memcmp(&now, &stored, sizeof now) == 0) return;
  Preferences nvs;
  if (!nvs.begin(kNamespace, /*readOnly=*/false)) return;
  if (now.brightness != stored.brightness) nvs.putUChar("bright", now.brightness);
  if (now.dimDelay != stored.dimDelay) nvs.putUChar("dim", now.dimDelay);
  if (now.textSize != stored.textSize) nvs.putUChar("text", now.textSize);
  if (now.profileCount != stored.profileCount) nvs.putUChar("profiles", now.profileCount);
  if (now.activeProfile != stored.activeProfile) nvs.putUChar("profile", now.activeProfile);
  nvs.end();
  stored = now;
}
