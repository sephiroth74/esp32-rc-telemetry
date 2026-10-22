#include "ui_fsm.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "card.h"
#include "config.h"
#include "display.h"
#include "gnss_link.h"
#include "gnss_task.h"
#include "keys.h"
#include "logger.h"
#include "session.h"
#include "settings.h"
#include "ui_board.h"
#include "ui_config.h"
#include "ui_screens.h"

namespace {

constexpr uint32_t kStackBytes = 6144;

// --- Placeholder facts (ROADMAP.md step 2.6) -----------------------------------
// Every screen shows plausible values until the modules that own them arrive: the card in phase 3,
// GNSS in phase 4 (here now), sessions in phase 5, RC and profiles in phase 6, laps in phase 8.

// The profiles' names are config.json's (settings.h); the track id is writable for New track.
const char* profileNamePtrs[CAR_PROFILES_MAX];
char trackId[16] = "N4512E00934";
uint8_t trackSuffix = 1;

ui::Facts sampleFacts() {
  ui::Facts f;
  f.menu.rcSignal = true;
  f.menu.profileMax = CAR_PROFILES_MAX;
  f.menu.textSize = DISPLAY_TEXT_SIZE_DEFAULT;
  f.profileNames = profileNamePtrs;
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

// The POST's result, kept for About, and the checks retried since (uiPostUpdate).
ui::PostResult postResult;
bool postWarning = false;  // what the POST and config.json add to the warning mark
char postSummaryText[17];
struct PostUpdate {
  uint8_t id;
  ui::CheckReport report;
};
QueueHandle_t postUpdates = nullptr;

// What IDLE takes from the POST's result: the refusal, the warning mark, the summary of About.
void applyPost() {
  ui::postSummary(postResult, postSummaryText);
  facts.postSummary = postSummaryText;
  int8_t refusing = postResult.refusingCheck();
  facts.menu.loggingAllowed = refusing < 0;
  facts.menu.refusingCheck = refusing < 0 ? 0 : refusing;
  // A failed check, or a config.json that is not all it should be: the warning mark (§3.2).
  postWarning = postResult.warning() || settingsProblems().count ||
                settingsSource() == SettingsSource::CardUnreadable;
  facts.warning = postWarning || facts.gnssLoss;
  facts.gnssBaud = gnssLinkBaud();  // check 9 moves it
}

// The gnss task's live state into the facts (§3.4): the link while the module keeps sending, the
// last epoch while it is fresh, and the acquisition the GNSS screen tells. Without a link at the
// POST there is no task, and the facts uiStart set stay.
void applyGnss(ui::Acquisition& acq, uint32_t& seenEpochs, uint32_t now, ui::Motion& motion) {
  motion = ui::Motion();  // the throttle joins with the RC capture of step 6.2
  if (!gnssTaskRunning()) return;
  GnssLive l;
  gnssLiveRead(l);
  if (l.epochs != seenEpochs) {
    seenEpochs = l.epochs;
    acq.epoch(l.pvt.fix3d(), l.pvt.hAccMm, l.pvtAtMs);
  }
  acq.tick(now);

  uint32_t heard = l.lastByteMs ? l.lastByteMs : l.startMs;
  facts.gnssLink = now - heard < GNSS_STALE_MS;
  facts.menu.gnssOk = facts.gnssLink;
  const ubx::NavPvt& p = l.pvt;
  bool fresh = l.havePvt && now - l.pvtAtMs < GNSS_STALE_MS;
  // The status bar's "3D" is the module's own word for it, gnssFixOK and all: a time-only or a
  // dead-reckoning solution is no fix here.
  bool fix2d = p.fixType == ubx::FixType::Fix2D && p.gnssFixOk() && !p.invalidLlh();
  facts.fixType = !fresh ? 0 : p.fix3d() ? 3 : fix2d ? 2 : 0;
  facts.latE7 = p.latE7;
  facts.lonE7 = p.lonE7;
  facts.sats = fresh ? p.numSv : 0;
  facts.satsSeen = l.haveSat ? l.satsTracked : 0;
  facts.hAccCm = p.hAccMm / 10 > 65535 ? 65535 : p.hAccMm / 10;
  facts.pdopX100 = p.pDopE2;
  int32_t kmh = p.gSpeedMmS * 36 / 10000;  // mm/s to km/h
  facts.speedKmh = !fresh || kmh < 0 ? 0 : kmh;
  facts.gnssLoss = l.loss.total();
  facts.acq = acq.state(now);
  uint32_t s = acq.fix() ? acq.ttffS() : acq.elapsedS(now);
  facts.acqS = s > 65535 ? 65535 : s;
  facts.menu.hasFix = acq.fix();
  facts.warning = postWarning || facts.gnssLoss;  // a loss counted (§3.5)
  // The automatic start's speed (§3.6): a fresh 3D fix's, from Doppler.
  motion.speedKnown = fresh && p.fix3d();
  motion.speedMmS = p.gSpeedMmS > 0 ? p.gSpeedMmS : 0;
}

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
  const ui::Layout layout = keysLayout();
  ui::Board board(menuTiming());
  board.setAutoTiming(autoSessionTiming(settings().autoStopMin));
  ui::ScreenText text;
  char shownState[ui::kScreenCols + 1] = "";
  settingsFill(facts.menu);
  uint32_t lastKeyMs = millis();
  ui::Acquisition acq(acquisitionTiming());
  uint32_t seenEpochs = 0;
  {
    GnssLive l;
    gnssLiveRead(l);
    acq.restart(l.startMs);  // acquisition began with the gnss task, started before this one
  }

  // A session's start, stop and markers, carried out on the card (session.h, logger.h). False when
  // the start could not be: the board goes back to IDLE with the reason.
  auto runSession = [&](ui::BoardOutput& out, uint32_t now, bool automatic) {
    switch (out.command) {
      case ui::Command::StartSession: {
        SessionStart s;
        s.haveFix = acq.fix();
        s.fix = {facts.latE7, facts.lonE7};
        s.automatic = automatic;
        s.profile = facts.menu.activeProfile;
        s.profileName = settingsProfileName(facts.menu.activeProfile);
        s.postMask = postResult.failedMask();
        s.postSummary = postSummaryText;
        SessionInfo info;
        if (!sessionBegin(s, info)) {
          Serial.printf("[%7lu] session: the card took no session\n", (unsigned long)now);
          board.abortSession(ui::Refusal::CardNotWritable, 0, now);
          out.toast[0] = '\0';
          return;
        }
        facts.nextSession = info.number;  // the toasts' number, start and stop
        snprintf(out.toast, sizeof out.toast, "Session %lu", (unsigned long)info.number);
        return;
      }
      case ui::Command::StopSession:
        sessionEnd(automatic ? rcl::Close::AutoStop : rcl::Close::ByHand, acq.fix(),
                   {facts.latE7, facts.lonE7});
        return;
      case ui::Command::Marker: {
        rcl::Marker m;
        m.number = board.markers();
        m.source = static_cast<uint8_t>(rcl::MarkerSource::Key);
        uint8_t payload[rcl::kMarkerBytes];
        logAppend(rcl::Type::Marker, now, payload, rcl::encode(m, payload, sizeof payload));
        return;
      }
      default:
        return;
    }
  };

  // What the board asks for, from a key or on its own: logged, carried out, shown.
  auto act = [&](ui::BoardOutput out, uint32_t now, bool automatic) {
    if (out.command != ui::Command::None) {
      Serial.printf("[%7lu] command %s (%u)\n", (unsigned long)now, ui::commandName(out.command),
                    out.arg);
      runSession(out, now, automatic);
      if (out.command == ui::Command::RestartGnss) acq.restart(now);  // the clock, §3.4
      runPlaceholder(out);
      settingsSave(facts.menu);  // only what the command changed reaches NVS
    }
    if (out.swallow) keysSwallow();  // the press closed a notice and does nothing else
    if (out.refused != ui::Refusal::None) {
      char why[22];
      ui::refusalText(out.refused, out.arg, why);
      Serial.printf("[%7lu] refused: %s\n", (unsigned long)now, why);
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
  };

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
      lastKeyMs = now;
      if (event == ui::Event::Press && dimmed) {
        keysSwallow();
        dimmed = false;
        board.wake(now);  // a timer restart only: a notice on screen stays to be read
        Serial.printf("[%7lu] woken\n", (unsigned long)now);
        continue;
      }
      if (event == ui::Event::Hold) displayAcknowledge(now);
      ui::BoardOutput out = board.handle(event, facts, now);
      if (event != ui::Event::Press) {
        Serial.printf("[%7lu] event %s\n", (unsigned long)now, ui::eventName(event));
      }
      act(out, now, /*automatic=*/false);
    }
    board.tick(now);
    // A card that failed in the middle of a session: closed, and said so (§3.5).
    if (board.mode() == ui::Mode::Session && logFailed()) {
      sessionEnd(rcl::Close::CardError, acq.fix(), {facts.latE7, facts.lonE7});
      board.abortSession(ui::Refusal::CardNotWritable, 0, now);
      lastKeyMs = now;
      Serial.printf("[%7lu] session: card write failed, closed\n", (unsigned long)now);
    }
    ui::Motion motion;
    applyGnss(acq, seenEpochs, now, motion);
    // The automatic start and stop (§3.6). What they do is worth seeing: a dimmed display wakes.
    ui::BoardOutput automatic = board.onMotion(motion, facts, now);
    if (automatic.command != ui::Command::None || automatic.refused != ui::Refusal::None) {
      lastKeyMs = now;
      Serial.printf("[%7lu] automatic, at %lu mm/s\n", (unsigned long)now,
                    (unsigned long)motion.speedMmS);
      act(automatic, now, /*automatic=*/true);
    }

    PostUpdate update;
    while (xQueueReceive(postUpdates, &update, 0) == pdTRUE) {
      postResult.checks[update.id] = update.report;
      applyPost();
      Serial.printf("[%7lu] check %u now %s\n", (unsigned long)now, update.id,
                    update.report.detail);
    }

    displaySetContrast(facts.menu.brightness, dimmed);
    displaySetTextSize(textSize());
    ui::render(board, facts, layout, textSize(), now, text);
    // Bench log: the status line that names the state, each time it changes — the digits left
    // out, or a session's clock would log a line a second.
    const char* state = text.lines[ui::statusRows(textSize()) - 1];
    char shape[12];
    for (int i = 0; i < 11; ++i) {
      char c = state[i];
      shape[i] = c >= '0' && c <= '9' ? '#' : c;
      if (!c) break;
    }
    shape[11] = '\0';
    if (strcmp(shownState, shape) != 0) {
      snprintf(shownState, sizeof shownState, "%s", shape);
      Serial.printf("[%7lu] screen %.11s\n", (unsigned long)now, state);
    }
    displayShow(text);
    displayHoldBar(keysHoldPercent(now));
    displayRefresh(now);
    vTaskDelayUntil(&wake, period);  // a fixed rate, whatever the work above took
  }
}

}  // namespace

void uiStart(const ui::PostResult& post) {
  postResult = post;
  facts.post = &postResult;  // for About, which shows it check by check
  postUpdates = xQueueCreate(4, sizeof(PostUpdate));
  // The card as the POST left it; the rest of the facts are still placeholders (step 2.6).
  // The settings of config.json, and where they came from (§3.8).
  for (uint8_t i = 0; i < CAR_PROFILES_MAX; ++i) profileNamePtrs[i] = settingsProfileName(i);
  facts.autoStart = settings().autoStart;
  static char configNote[22];
  if (settingsProblems().count) {
    snprintf(configNote, sizeof configNote, "%s, %u bad", settingsSourceName(),
             settingsProblems().count);
  } else {
    snprintf(configNote, sizeof configNote, "%s", settingsSourceName());
  }
  facts.configNote = configNote;
  facts.menu.cardOk = cardMounted();
  facts.menu.cardWritable = post.checks[5].outcome != ui::Outcome::Fail;
  facts.freeMb = cardFreeMb();
  facts.cardGb = cardTotalMb() / 1024;
  facts.cardKbps = cardLastProbe().writeKbps;
  // The GNSS link as check 8 found it; from now on the gnss task's live state (applyGnss).
  facts.gnssLink = gnssLinkBaud() != 0;
  facts.menu.gnssOk = facts.gnssLink;
  applyPost();
  xTaskCreatePinnedToCore(uiTask, "ui", kStackBytes, nullptr, TASK_UI_PRIO, nullptr, TASK_UI_CORE);
}

void uiPostUpdate(uint8_t id, const ui::CheckReport& report) {
  if (!postUpdates || id >= ui::kPostChecks) return;
  PostUpdate update = {id, report};
  xQueueSend(postUpdates, &update, 0);
}
