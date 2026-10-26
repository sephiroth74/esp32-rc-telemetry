#include "ui_fsm.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "bench_sim.h"
#include "calibration.h"
#include "card.h"
#include "config.h"
#include "display.h"
#include "gnss_link.h"
#include "gnss_task.h"
#include "keys.h"
#include "lap_timer.h"
#include "logger.h"
#include "power.h"
#include "rc_capture.h"
#include "session.h"
#include "settings.h"
#include "transfer.h"
#include "ui_board.h"
#include "ui_config.h"
#include "ui_screens.h"

namespace {

constexpr uint32_t kStackBytes = 6144;

// --- Placeholder facts (ROADMAP.md step 2.6) -----------------------------------
// Every screen shows plausible values until the modules that own them arrive: the card in phase 3,
// GNSS in phase 4 (here now), sessions in phase 5, RC and profiles in phase 6, laps in phase 8
// (here now, lap_timer.h).

// The profiles' names are config.json's (settings.h).
const char* profileNamePtrs[CAR_PROFILES_MAX];

ui::Facts sampleFacts() {
  ui::Facts f;
  f.menu.profileMax = CAR_PROFILES_MAX;
  f.menu.textSize = DISPLAY_TEXT_SIZE_DEFAULT;
  f.profileNames = profileNamePtrs;
  f.freeMb = 59600;
  f.cardGb = 62;
  f.cardKbps = 3495;
  f.cardSessions = 3;
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
int32_t headingE5 = 0;  // the last epoch's heading of motion, for Track › Start line

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
  // Track › Start line takes the heading of the motion, which means something at 1 m/s (§7.5).
  facts.menu.moving = motion.speedKnown && p.gSpeedMmS >= 1000;
  headingE5 = p.headMotE5;
}

// The track the screens name (§3.4), and the number the next session takes: read from the card in
// IDLE, where a session started now would be filed. Again when the fix comes or goes, after moving
// TRACK_LOOKUP_MOVE_M, and after anything that may change the card: a session, a transfer, a command.
TrackView trackView;
const char* trackChoicePtrs[kTrackChoicesMax];
bool trackStale = true;
bool trackFix = false;
geo::Point trackAt;

void lookUpTrack(const ui::Board& board, bool fix) {
  if (board.mode() != ui::Mode::Idle || !facts.menu.cardOk) {
    trackStale = true;
    if (!facts.menu.cardOk && trackView.found) trackView = TrackView();  // no card, no track
  } else {
    geo::Point here{facts.latE7, facts.lonE7};
    if (!trackStale && fix == trackFix &&
        (!fix || geo::distanceM(here, trackAt) < TRACK_LOOKUP_MOVE_M)) {
      return;
    }
    sessionLookup(fix, here, trackView);
    trackStale = false;
    trackFix = fix;
    trackAt = here;
    Serial.printf("track: %s, %u sessions, next %lu\n",
                  trackView.found ? trackView.id : fix ? "none here" : "_unassigned",
                  trackView.sessions, (unsigned long)trackView.next);
  }
  facts.trackId = trackView.id;
  facts.trackName = trackView.name;
  facts.menu.hasStartLine = trackView.hasStartLine;
  facts.startLineSource = static_cast<uint8_t>(trackView.lineSource);
  facts.hasGeometry = trackView.hasGeometry;
  facts.trackSessions = trackView.sessions;
  for (uint8_t i = 0; i < trackView.choices; ++i) trackChoicePtrs[i] = trackView.choiceNames[i];
  facts.trackChoiceNames = trackChoicePtrs;
  facts.menu.trackChoices = trackView.choices;
  facts.menu.trackResolved = trackView.resolved;
  if (board.mode() == ui::Mode::Idle) {  // in a session it is the session's own, for the toasts
    facts.nextSession = trackView.next > 65535 ? 65535 : trackView.next;
  }
}

// Settings › Calibrate and the profiles' calibrations (§3.8, step 6.3). The steps capture the live
// widths into `working`, whose summary the screens show; a save puts it in the active profile's
// NVS slot. A profile chosen or added brings its own, or none.
rc::Calibration working;

void showWorking() {
  const rc::Axis& s = working.steering;
  const rc::Axis& t = working.throttle;
  const uint16_t st[3] = {s.negativeUs, s.neutralUs, s.positiveUs};
  const uint16_t th[3] = {t.negativeUs, t.neutralUs, t.positiveUs};
  for (uint8_t i = 0; i < 3; ++i) {
    facts.capturedSteering[i] = st[i];
    facts.capturedThrottle[i] = th[i];
  }
  facts.calibrationProblem = rc::problem(working);
}

void runCalibration(const ui::BoardOutput& o) {
  switch (o.command) {
    case ui::Command::EnterCalibration:
      working = rc::Calibration();
      showWorking();
      break;
    case ui::Command::CalibrationCapture:  // the steps of lib/ui and lib/rc go in the same order
      rc::capture(working, static_cast<rc::Step>(o.arg), facts.steeringUs, facts.throttleUs, 0);
      showWorking();
      break;
    case ui::Command::SaveCalibration:
      if (calibrationSave(facts.menu.activeProfile, working)) facts.profileCalibrated = true;
      break;
    case ui::Command::SelectProfile:
      calibrationUse(o.arg);
      facts.profileCalibrated = calibrationActive();
      break;
    case ui::Command::NewProfile:
      calibrationClear(o.arg);
      calibrationUse(o.arg);
      facts.profileCalibrated = calibrationActive();
      break;
    default:
      break;
  }
}

// Stand-ins for the modules that will carry the commands out. Each only updates the sample facts,
// so the screens react as they will once the real thing is behind them. A start line and a
// geometry are the card's own (lookUpTrack), until phase 8 records them.
void runPlaceholder(const ui::BoardOutput& o) {
  switch (o.command) {
    case ui::Command::StopSession:  // the track's own numbers come from the card (lookUpTrack)
      ++facts.cardSessions;
      break;
    case ui::Command::SelectProfile:  // its calibration: runCalibration
      facts.menu.activeProfile = o.arg;
      break;
    case ui::Command::NewProfile:
      facts.menu.profileCount = o.arg + 1;
      facts.menu.activeProfile = o.arg;
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
    case ui::Command::SetRequireFix:  // Home and the next start read it from there (§3.6)
      facts.menu.requireFix = o.arg != 0;
      break;
    case ui::Command::DeleteSessions:
      facts.cardSessions = 0;
      break;
    default:
      break;
  }
}

// Every NAV-PVT since the last tick into the laps while a session is open, and what they make of
// them into the facts; a lap completed, or the line found, as a toast. Outside a session the queue
// is only emptied.
void runLaps(uint32_t now) {
  GnssEpoch g;
  while (gnssNextEpoch(g)) {
    if (!sessionOpen()) continue;
    geo::Epoch e;
    e.iTowMs = g.iTowMs;
    e.tMs = logSessionMs(g.atMs);  // as its record has it
    e.at = {g.latE7, g.lonE7};
    e.speedMmS = g.gSpeedMmS > 0 ? g.gSpeedMmS : 0;
    e.headingE5 = g.headMotE5;
    e.takesPart = g.fix3d;
    LapEvent ev;
    if (!lapTimerEpoch(e, ev)) continue;
    char toast[22], time[16];
    snprintf(time, sizeof time, "%lu:%02lu.%03lu", (unsigned long)(ev.lapMs / 60000),
             (unsigned long)(ev.lapMs / 1000 % 60), (unsigned long)(ev.lapMs % 1000));
    if (ev.lineFound) {
      snprintf(toast, sizeof toast, "Line found, %u laps", lapTimerView().laps);
    } else {
      snprintf(toast, sizeof toast, "Lap %u  %s%s", ev.lap, time,
               ev.flag == geo::Flag::Cut ? " cut" : ev.flag == geo::Flag::Off ? " off" : "");
    }
    displayToast(toast, now);
    Serial.printf("[%7lu] %s\n", (unsigned long)now, toast);
  }
  const LapView& v = lapTimerView();
  facts.laps = v.laps;
  facts.lastLapMs = v.lastMs;
  facts.bestLapMs = v.bestMs;
  facts.deltaMs = v.deltaMs;
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
  board.setPowerTiming(powerTiming());
  ui::ScreenText text;
  char shownState[ui::kScreenCols + 1] = "";
  settingsFill(facts.menu);
  calibrationUse(facts.menu.activeProfile);  // and the inputs' windows with it (§4.3)
  facts.profileCalibrated = calibrationActive();
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
        lapTimerBegin();  // the track's start line, or the search for one (§7.5)
        return;
      }
      case ui::Command::StopSession:
        lapTimerEnd();
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
      runCalibration(out);
      if (out.command == ui::Command::RestartGnss) acq.restart(now);  // the clock, §3.4
      // TRANSFER (§7.3): the card to the USB host and back; one that will not go, a notice.
      if (out.command == ui::Command::EnterTransfer && !transferEnter()) {
        board.abortTransfer(ui::Refusal::NoCard, now);
      }
      if (out.command == ui::Command::LeaveTransfer) {
        facts.menu.cardOk = transferLeave();
        facts.freeMb = cardFreeMb();  // the host may have deleted or copied files
      }
      // Track › New track and Track › Choose (§7.1): the card's, read again below like any change.
      if (out.command == ui::Command::NewTrack) {
        char id[geo::kTrackIdMax + 1];
        bool made = acq.fix() && sessionNewTrack({facts.latE7, facts.lonE7}, id);
        Serial.printf("[%7lu] new track: %s\n", (unsigned long)now, made ? id : "none made");
      }
      // Track › Start line (§3.4): where the car is, the way it goes, source `device`. Where no
      // track is yet, the track first, as New track makes one.
      if (out.command == ui::Command::RecordStartLine) {
        geo::Gate g;
        g.centre = {facts.latE7, facts.lonE7};
        g.headingE5 = headingE5;
        char id[geo::kTrackIdMax + 1];
        const char* track = trackView.found ? trackView.id : nullptr;
        if (!track && sessionNewTrack(g.centre, id)) track = id;
        bool saved = track && sessionRecordStartLine(track, g);
        Serial.printf("[%7lu] start line %s: %s, heading %ld.%02ld\n", (unsigned long)now,
                      saved ? "recorded" : "not recorded", track ? track : "no track",
                      (long)(headingE5 / 100000), (long)(headingE5 % 100000 / 1000));
        if (!saved) snprintf(out.toast, sizeof out.toast, "Start line not saved");
      }
      if (out.command == ui::Command::ChooseTrack && out.arg < trackView.choices) {
        sessionChoose(trackView.choiceIds[out.arg]);
        Serial.printf("[%7lu] track chosen: %s\n", (unsigned long)now, trackView.choiceIds[out.arg]);
      }
      runPlaceholder(out);
      trackStale = true;         // whatever it did, the card is read again
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
    if (out.command == ui::Command::Sleep) powerSleep();  // from IDLE: no session is open
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
    auto onKey = [&](ui::Event event) {
      lastKeyMs = now;
      if (event == ui::Event::Press && dimmed) {
        keysSwallow();
        dimmed = false;
        board.wake(now);  // a timer restart only: a notice on screen stays to be read
        Serial.printf("[%7lu] woken\n", (unsigned long)now);
        return;
      }
      if (event == ui::Event::Hold) displayAcknowledge(now);
      ui::BoardOutput out = board.handle(event, facts, now);
      if (event != ui::Event::Press) {
        Serial.printf("[%7lu] event %s\n", (unsigned long)now, ui::eventName(event));
      }
      act(out, now, /*automatic=*/false);
    };
    while (keysNext(event)) onKey(event);
#ifdef BENCH_SIM
    benchSimPoll();  // the console's keys, as if pressed (bench_sim.h)
    while (benchSimNextEvent(event)) onKey(event);
#endif
    board.tick(now);
    // A card that failed in the middle of a session: closed, and said so (§3.5).
    if (board.mode() == ui::Mode::Session && logFailed()) {
      lapTimerEnd();
      sessionEnd(rcl::Close::CardError, acq.fix(), {facts.latE7, facts.lonE7});
      board.abortSession(ui::Refusal::CardNotWritable, 0, now);
      lastKeyMs = now;
      Serial.printf("[%7lu] session: card write failed, closed\n", (unsigned long)now);
    }
    ui::Motion motion;
    applyGnss(acq, seenEpochs, now, motion);
    runLaps(now);
    // The receiver's widths as captured (§4.3), and what the active calibration makes of them: the
    // percentages on the screens, and the throttle the automatic start counts (§3.6), known only
    // with a calibrated throttle and a live input.
    RcLive rc;
    rcRead(rc);
    facts.steeringUs = rc.widthUs[static_cast<uint8_t>(RcInput::Steering)];
    facts.throttleUs = rc.widthUs[static_cast<uint8_t>(RcInput::Throttle)];
    facts.menu.rcSignal = facts.steeringUs || facts.throttleUs;
    const rc::Calibration* cal = calibrationActive();
    facts.steeringPctKnown = cal && rc::calibrated(cal->steering) && facts.steeringUs;
    facts.throttlePctKnown = cal && rc::calibrated(cal->throttle) && facts.throttleUs;
    facts.steeringPct = facts.steeringPctKnown ? rc::percent(cal->steering, facts.steeringUs) : 0;
    facts.throttlePct = facts.throttlePctKnown ? rc::percent(cal->throttle, facts.throttleUs) : 0;
    motion.throttleKnown = facts.throttlePctKnown;
    motion.throttlePct = facts.throttlePct;
    // Bench log, as the gnss task's line: every 10 s, each input's range over those 10 s, its valid
    // pulses and its rejects since boot.
    static uint32_t rcLoggedMs = 0;
    static uint16_t rcMin[2] = {UINT16_MAX, UINT16_MAX}, rcMax[2] = {0, 0};
    for (uint8_t i = 0; i < 2; ++i) {
      if (rc.widthUs[i] && rc.widthUs[i] < rcMin[i]) rcMin[i] = rc.widthUs[i];
      if (rc.widthUs[i] > rcMax[i]) rcMax[i] = rc.widthUs[i];
    }
    if (now - rcLoggedMs >= 10000) {
      rcLoggedMs = now;
      Serial.printf("[%7lu] rc: steer %u-%u us, %lu ok, %lu rej; throttle %u-%u us, %lu ok, %lu rej\n",
                    (unsigned long)now, rcMax[0] ? rcMin[0] : 0, rcMax[0], (unsigned long)rc.pulses[0],
                    (unsigned long)rc.rejects[0], rcMax[1] ? rcMin[1] : 0, rcMax[1],
                    (unsigned long)rc.pulses[1], (unsigned long)rc.rejects[1]);
      for (uint8_t i = 0; i < 2; ++i) {
        rcMin[i] = UINT16_MAX;
        rcMax[i] = 0;
      }
    }
    lookUpTrack(board, acq.fix());
    facts.usbHost = transferHostAttached();
    facts.usbEjected = transferEjected();
    // The automatic start and stop (§3.6). What they do is worth seeing: a dimmed display wakes.
    ui::BoardOutput automatic = board.onMotion(motion, facts, now);
    if (automatic.command != ui::Command::None || automatic.refused != ui::Refusal::None) {
      lastKeyMs = now;
      Serial.printf("[%7lu] automatic, at %lu mm/s\n", (unsigned long)now,
                    (unsigned long)motion.speedMmS);
      act(automatic, now, /*automatic=*/true);
    }
    // Sleep from IDLE (§4.5): the pulses stopping, or nothing for a while. The notice wakes a
    // dimmed display, so that it is seen.
    ui::Power power;
    power.rcLive = rc.lastPulseMs && static_cast<int32_t>(now - rc.lastPulseMs) < RC_SIGNAL_LOST_MS;
    power.usbHost = facts.usbHost;
#ifdef BENCH_SIM
    if (benchSimHostHidden()) power.usbHost = false;  // to see the bench sleep (bench_sim.h)
#endif
    ui::SleepWhy noticeBefore = board.sleepNotice();
    ui::BoardOutput slept = board.onPower(power, facts, now);
    if (board.sleepNotice() != noticeBefore) lastKeyMs = now;
    if (slept.command != ui::Command::None) act(slept, now, /*automatic=*/true);

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
#ifdef BENCH_SIM
    if (benchSimTakeScreenRequest()) {
      for (const auto& l : text.lines) Serial.printf("screen| %s\n", l);
    }
#endif
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
