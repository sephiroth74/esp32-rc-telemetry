// lib/ui's parameters (PLAN-ESP32.md §3.1, §3.4), taken from config.h.
//
// One place, shared by the device glue and the host tests, so the tests check the constants the
// device actually ships with — including the ladder bounds measured on the bench.
#pragma once

#include "config.h"
#include "ui_auto.h"
#include "ui_gnss.h"
#include "ui_keys.h"
#include "ui_menu.h"
#include "ui_power.h"

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

inline ui::AcquisitionTiming acquisitionTiming() {
  ui::AcquisitionTiming t{};
  t.fixHAccMm = GNSS_FIX_HACC_MM;
  t.fixHoldMs = GNSS_FIX_HOLD_MS;
  t.timeoutMs = GNSS_ACQ_TIMEOUT_MS;
  t.staleMs = GNSS_STALE_MS;
  return t;
}

// The automatic start and stop of §3.6; the standstill limit is config.json's session.auto_stop_min.
inline ui::AutoSessionTiming autoSessionTiming(uint16_t autoStopMin) {
  ui::AutoSessionTiming t{};
  t.startSpeedMmS = static_cast<uint32_t>(SESSION_START_SPEED_KMH / 3.6f * 1000 + 0.5f);
  t.startSpeedMs = SESSION_START_SPEED_MS;
  t.startThrottlePct = SESSION_START_THROTTLE_PCT;
  t.startThrottleMs = SESSION_START_THROTTLE_MS;
  t.stillSpeedMmS = static_cast<uint32_t>(SESSION_STILL_SPEED_KMH / 3.6f * 1000 + 0.5f);
  t.stillThrottlePct = SESSION_STILL_THROTTLE_PCT;
  t.stopMs = uint32_t(autoStopMin) * 60 * 1000;
  t.rearmMs = SESSION_REARM_MS;
  return t;
}

inline ui::MenuTiming menuTiming() {
  ui::MenuTiming t{};
  t.menuTimeoutMs = UI_MENU_TIMEOUT_MS;
  t.dialogTimeoutMs = UI_DIALOG_TIMEOUT_MS;
  return t;
}

// Sleep (§4.5).
inline ui::PowerTiming powerTiming() {
  ui::PowerTiming t{};
  t.noticeMs = SLEEP_NOTICE_MS;
  t.idleMs = SLEEP_IDLE_MS;
  return t;
}
