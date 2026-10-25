#include "power.h"

#include <Arduino.h>
#include <esp_sleep.h>

#include "card.h"
#include "config.h"
#include "display.h"
#include "pins.h"

void powerSleep() {
  displayOff();   // an OLED left on in deep sleep keeps its last frame, and burns it in (§2.4)
  cardUnmount();  // nothing is open by now; the FAT is left consistent all the same
  esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(PIN_KEYS), 0);
#ifdef BENCH_SIM
  esp_sleep_enable_timer_wakeup(uint64_t(SLEEP_SIM_WAKE_S) * 1000000);
  Serial.printf("power: asleep, until OK or %lu s\n", (unsigned long)SLEEP_SIM_WAKE_S);
#else
  Serial.println("power: asleep, until OK");
#endif
  Serial.flush();
  esp_deep_sleep_start();
}

const char* powerWakeName() {
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_EXT0:
      return "OK key";
    case ESP_SLEEP_WAKEUP_TIMER:
      return "timer";
    default:
      return "power-on";
  }
}
