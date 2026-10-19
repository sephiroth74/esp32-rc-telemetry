#include "ui_fsm.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "card.h"
#include "config.h"
#include "display.h"
#include "gnss_link.h"
#include "keys.h"
#include "settings.h"
#include "ui_board.h"
#include "ui_config.h"
#include "ui_screens.h"

namespace {

constexpr uint32_t kStackBytes = 6144;

// --- Placeholder facts (ROADMAP.md step 2.6) -----------------------------------
// Every screen shows plausible values until the modules that own them arrive: the card in phase 3,
// GNSS in phase 4, sessions in phase 5, RC and profiles in phase 6, laps in phase 8.

// Writable, so that New car and New track show their result. Every profile is named the way New car
// names it, so a profile count read back from NVS (step 2.7) needs no names of its own.
char profileNames[CAR_PROFILES_MAX][CAR_PROFILE_NAME_MAX + 1];
const char* profileNamePtrs[CAR_PROFILES_MAX];
char trackId[16] = "N4512E00934";
uint8_t trackSuffix = 1;

ui::Facts sampleFacts() {
  ui::Facts f;
  f.menu.hasFix = true;
  f.menu.rcSignal = true;
  f.menu.profileCount = 3;
  f.menu.profileMax = CAR_PROFILES_MAX;
  f.menu.textSize = DISPLAY_TEXT_SIZE_DEFAULT;
  for (uint8_t i = 0; i < CAR_PROFILES_MAX; ++i) {
    ui::newProfileName(i, profileNames[i]);
    profileNamePtrs[i] = profileNames[i];
  }
  f.profileNames = profileNamePtrs;
  f.gnssLink = true;
  f.gnssBaud = 115200;
  f.fixType = 3;
  f.latE7 = 451234560;  // inside the sample cell, N4512E00934
  f.lonE7 = 93456780;
  f.sats = 14;
  f.hAccCm = 120;
  f.pdopX100 = 132;
  f.ttffS = 34;
  f.freeMb = 59600;
  f.cardGb = 62;
  f.cardKbps = 3495;
  f.trackId = trackId;
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
    case ui::Command::NewProfile:
      facts.menu.profileCount = o.arg + 1;
      facts.menu.activeProfile = o.arg;
      facts.profileCalibrated = false;
      break;
    case ui::Command::NewTrack:  // the next free suffix of the cell id, as in §7.1
      snprintf(trackId, sizeof trackId, "N4512E00934-%u", ++trackSuffix);
      facts.menu.hasStartLine = false;
      facts.hasGeometry = false;
      facts.trackSessions = 0;
      facts.nextSession = 1;
      break;
    case ui::Command::SetBrightness:  // the ui task applies it to the panel on the next tick
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
  settingsLoad(facts.menu);
  uint32_t lastKeyMs = millis();

  for (;;) {
    uint32_t now = millis();
    keysTick(now);

    // Dimmed after the delay of the setting (§3.2). The press that brings it back does only that:
    // the rest of its gesture is swallowed, and the Press itself goes on as activity, so the menu
    // stays where it was. Errors will brighten it too, once phase 3 brings them.
    uint32_t dimAfter = ui::kDimDelayMs[facts.menu.dimDelay % 4];
    bool dimmed = dimAfter && now - lastKeyMs >= dimAfter;

    ui::Event event;
    while (keysNext(event)) {
      if (event == ui::Event::Press && dimmed) {
        keysSwallow();
        dimmed = false;
        Serial.printf("[%7lu] woken\n", (unsigned long)now);
      }
      lastKeyMs = now;
      if (event == ui::Event::Hold) displayAcknowledge(now);
      ui::BoardOutput out = board.handle(event, facts, now);
      if (event != ui::Event::Press) {
        Serial.printf("[%7lu] event %s\n", (unsigned long)now, ui::eventName(event));
      }
      if (out.command != ui::Command::None) {
        Serial.printf("[%7lu] command %s (%u)\n", (unsigned long)now, ui::commandName(out.command),
                      out.arg);
        runPlaceholder(out);
        settingsSave(facts.menu);  // only what the command changed reaches NVS
      }
      if (out.toast[0]) {
        displayToast(out.toast, now);
        Serial.printf("[%7lu] toast %s\n", (unsigned long)now, out.toast);
      }
      if (out.command == ui::Command::Restart) {
        // Settings is reached from IDLE only, so nothing is open; the settings are in NVS already.
        // The toast goes out first, and stays long enough to be read.
        displayFlush(now);
        vTaskDelay(pdMS_TO_TICKS(800));
        ESP.restart();
      }
    }
    board.tick(now);

    displaySetContrast(facts.menu.brightness, dimmed);
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

void uiStart(const ui::PostResult& post) {
  static char summary[17];
  ui::postSummary(post, summary);
  int8_t refusing = post.refusingCheck();
  facts.menu.loggingAllowed = refusing < 0;
  facts.menu.refusingCheck = refusing < 0 ? 0 : refusing;
  facts.warning = post.warning();
  facts.postSummary = summary;
  // The card as the POST left it; the rest of the facts are still placeholders (step 2.6).
  facts.menu.cardOk = cardMounted();
  facts.freeMb = cardFreeMb();
  facts.cardGb = cardTotalMb() / 1024;
  facts.cardKbps = cardLastProbe().writeKbps;
  // The GNSS link as check 8 found it. Without one there is no fix either: the sample fix of the
  // placeholders would be a lie on the GNSS screen.
  facts.gnssLink = gnssLinkBaud() != 0;
  facts.gnssBaud = gnssLinkBaud();
  if (!facts.gnssLink) {
    facts.fixType = 0;
    facts.sats = 0;
    facts.menu.hasFix = false;
  }
  // Again, after the POST's own lines: those can come before a USB host has opened the port.
  Serial.printf("POST %s, failed mask 0x%03X\n", summary, post.failedMask());
  xTaskCreatePinnedToCore(uiTask, "ui", kStackBytes, nullptr, TASK_UI_PRIO, nullptr, TASK_UI_CORE);
}
