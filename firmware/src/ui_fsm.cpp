#include "ui_fsm.h"

#include <Arduino.h>

#include "config.h"
#include "display.h"
#include "keys.h"

namespace {

constexpr uint32_t kStackBytes = 4096;

// Bench screen for ROADMAP.md steps 2.3–2.4: the live key and reading, the last event as a toast,
// the hold bar, and the measured bus figures. The menu replaces it in steps 2.5–2.6.
void drawBenchScreen(uint32_t nowMs) {
  char line[32];
  displayStatus(0, "RC Telemetry " FW_VERSION);
  displayStatus(1, "Display test");
  snprintf(line, sizeof line, "frame %6lu us", (unsigned long)displayFullFrameUs());
  displayBody(0, line);
  snprintf(line, sizeof line, "page  %6lu us", (unsigned long)displayOnePageUs());
  displayBody(1, line);
  snprintf(line, sizeof line, "key %-5s %4u mV", ui::keyName(keysHeld()), keysLastMv());
  displayBody(2, line);
  displayBody(5, "OK open/hold  NAV");
  displayHoldBar(keysHoldPercent(nowMs));
}

void uiTask(void*) {
  const TickType_t period = pdMS_TO_TICKS(1000 / KEY_SAMPLE_HZ);
  TickType_t wake = xTaskGetTickCount();
  ui::Key logged = ui::Key::None;
  uint32_t lastReportMs = 0;

  for (;;) {
    uint32_t now = millis();
    keysTick(now);

    // Bench diagnostics: the accepted key with its reading, then the events it produced.
    if (keysHeld() != logged) {
      logged = keysHeld();
      Serial.printf("[%7lu] key  %-5s %4u mV\n", (unsigned long)now, ui::keyName(logged),
                    keysLastMv());
    }
    ui::Event event;
    while (keysNext(event)) {
      Serial.printf("[%7lu] event %s\n", (unsigned long)now, ui::eventName(event));
      if (event == ui::Event::Press) continue;
      if (event == ui::Event::Hold) displayAcknowledge(now);
      char toast[24];
      snprintf(toast, sizeof toast, "event: %s", ui::eventName(event));
      displayToast(toast, now);
    }
    if (now - lastReportMs >= 10000) {
      lastReportMs = now;
      Serial.printf("display: full frame %lu us, one page %lu us\n",
                    (unsigned long)displayFullFrameUs(), (unsigned long)displayOnePageUs());
    }

    drawBenchScreen(now);
    displayRefresh(now);
    vTaskDelayUntil(&wake, period);  // a fixed rate, whatever the work above took
  }
}

}  // namespace

void uiStart() {
  xTaskCreatePinnedToCore(uiTask, "ui", kStackBytes, nullptr, TASK_UI_PRIO, nullptr, TASK_UI_CORE);
}
