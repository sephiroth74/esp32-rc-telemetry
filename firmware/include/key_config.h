// The key path's parameters (lib/ui, PLAN-ESP32.md §3.1), taken from config.h.
//
// One place, shared by the device glue and the host tests, so the tests check the constants the
// device actually ships with — including the ladder bounds measured on the bench.
#pragma once

#include "config.h"
#include "ui_keys.h"

inline ui::KeyConfig keyConfig(KeyLayout layout = KEY_LAYOUT_DEFAULT) {
  ui::KeyConfig c{};
  for (int i = 0; i < 5; ++i) c.boundsMv[i] = KEY_MV_BOUNDS[i];
  c.confirmSamples = KEY_CONFIRM_SAMPLES;
  c.layout = layout == KeyLayout::TwoButtons ? ui::Layout::TwoButtons : ui::Layout::FiveWay;
  c.navKey = static_cast<ui::Key>(KEY_NAV_WINDOW);
  c.shortMaxMs = KEY_SHORT_MAX_MS;
  c.longMs = KEY_LONG_MS;
  c.holdBarFromMs = KEY_HOLD_BAR_FROM_MS;
  c.repeatDelayMs = KEY_REPEAT_DELAY_MS;
  c.repeatMs = KEY_REPEAT_MS;
  c.navBackMs = KEY_NAV_BACK_MS;
  return c;
}
