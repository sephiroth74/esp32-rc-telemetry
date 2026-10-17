#include "ui_fsm.h"

#include <Arduino.h>
#include <string.h>

#include "config.h"
#include "display.h"
#include "keys.h"
#include "ui_board.h"
#include "ui_config.h"
#include "ui_screens.h"

namespace {

constexpr uint32_t kStackBytes = 6144;

// --- Placeholder facts (ROADMAP.md step 2.6) -----------------------------------
// Every screen shows plausible values until the modules that own them arrive: the card in phase 3,
// GNSS in phase 4, sessions in phase 5, RC and profiles in phase 6, laps in phase 8.

const char* const kProfileNames[] = {"Car 1", "Car 2", "Car 3"};

ui::Facts sampleFacts() {
  ui::Facts f;
  f.menu.hasFix = true;
  f.menu.rcSignal = true;
  f.menu.profileCount = 3;
  f.menu.textSize = DISPLAY_TEXT_SIZE_DEFAULT;
  f.profileNames = kProfileNames;
  f.fixType = 3;
  f.sats = 14;
  f.hAccCm = 120;
  f.pdopX100 = 132;
  f.ttffS = 34;
  f.freeMb = 59600;
  f.cardGb = 62;
  f.cardKbps = 3495;
  f.trackId = "N4512E00934";
  f.trackSessions = 3;
  f.cardSessions = 3;
  f.nextSession = 4;
  f.laps = 12;
  f.lastLapMs = 62341;
  f.bestLapMs = 58902;
  f.deltaMs = 3439;
  f.steeringUs = 1500;
  f.throttleUs = 1500;
  f.geometryM = 412;
  f.geometryPoints = 4120;
  f.firmware = FW_VERSION;
  f.postSummary = "not run yet";
  return f;
}

ui::Facts facts = sampleFacts();

// Stand-ins for the modules that will carry the commands out. Each only updates the sample facts,
// so the screens react as they will once the real thing is behind them.
void runPlaceholder(const ui::BoardOutput& o) {
  switch (o.command) {
    case ui::Command::RecordStartLine:
      facts.menu.hasStartLine = true;
      break;
    case ui::Command::StopGeometry:
      facts.hasGeometry = true;
      break;
    case ui::Command::StopSession:
      ++facts.nextSession;
      ++facts.cardSessions;
      ++facts.trackSessions;
      break;
    case ui::Command::SaveCalibration:
      facts.profileCalibrated = true;
      break;
    case ui::Command::SelectProfile:
      facts.menu.activeProfile = o.arg;
      facts.profileCalibrated = false;
      break;
    case ui::Command::SetBrightness:  // applied to the panel in step 2.7
      facts.menu.brightness = o.arg;
      break;
    case ui::Command::SetDimDelay:
      facts.menu.dimDelay = o.arg;
      break;
    case ui::Command::SetTextSize:  // the ui task applies it to the display on the next tick
      facts.menu.textSize = o.arg;
      break;
    case ui::Command::DeleteSessions:
      facts.cardSessions = 0;
      facts.trackSessions = 0;
      break;
    default:
      break;
  }
}

// --- The task --------------------------------------------------------------------

ui::TextSize textSize() {
  return facts.menu.textSize == 0 ? ui::TextSize::Small : ui::TextSize::Large;
}

void uiTask(void*) {
  const TickType_t period = pdMS_TO_TICKS(1000 / KEY_SAMPLE_HZ);
  TickType_t wake = xTaskGetTickCount();
  const ui::Layout layout = keyConfig().layout;
  ui::Board board(menuTiming());
  ui::ScreenText text;
  char shownState[ui::kScreenCols + 1] = "";

  for (;;) {
    uint32_t now = millis();
    keysTick(now);

    ui::Event event;
    while (keysNext(event)) {
      if (event == ui::Event::Hold) displayAcknowledge(now);
      ui::BoardOutput out = board.handle(event, facts, now);
      if (event != ui::Event::Press) {
        Serial.printf("[%7lu] event %s\n", (unsigned long)now, ui::eventName(event));
      }
      if (out.command != ui::Command::None) {
        Serial.printf("[%7lu] command %s (%u)\n", (unsigned long)now, ui::commandName(out.command),
                      out.arg);
        runPlaceholder(out);
      }
      if (out.toast[0]) displayToast(out.toast, now);
    }
    board.tick(now);

    displaySetTextSize(textSize());
    ui::render(board, facts, layout, textSize(), now, text);
    // Bench log: the status line that names the state, each time it changes.
    const char* state = text.lines[ui::statusRows(textSize()) - 1];
    if (strncmp(shownState, state, 11) != 0) {
      strncpy(shownState, state, 11);
      shownState[11] = '\0';
      Serial.printf("[%7lu] screen %s\n", (unsigned long)now, shownState);
    }
    displayShow(text);
    displayHoldBar(keysHoldPercent(now));
    displayRefresh(now);
    vTaskDelayUntil(&wake, period);  // a fixed rate, whatever the work above took
  }
}

}  // namespace

void uiStart() {
  xTaskCreatePinnedToCore(uiTask, "ui", kStackBytes, nullptr, TASK_UI_PRIO, nullptr, TASK_UI_CORE);
}
