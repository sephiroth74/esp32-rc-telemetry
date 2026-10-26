// The board states the keys drive — PLAN-ESP32.md §3.6–§3.7.
//
// IDLE is the menu (ui_menu.h). SESSION, GEOMETRY, CALIBRATION and TRANSFER are modes with their
// own keys: while one is active the menu gets no events, and when it ends the menu picks up where
// it was. Like the menu, Board decides nothing about the device — every action comes out as a
// Command with a toast for the screen — and reads what it shows from Facts.
//
// SLEEP comes from the receiver's pulses and the keys (ui_power.h). POST and FATAL are the POST's
// own (ui_post.h). Framework-free (§6.1).
#pragma once

#include <stdint.h>

#include "ui_auto.h"
#include "ui_gnss.h"
#include "ui_keys.h"
#include "ui_menu.h"
#include "ui_post.h"
#include "ui_power.h"

namespace ui {

enum class Mode : uint8_t { Idle, Session, Geometry, Calibration, Transfer };

// Everything the screens show, kept current by the caller. The menu's own facts are inside.
struct Facts {
  MenuContext menu;
  const char* const* profileNames = nullptr;  // menu.profileCount of them, up to 8 characters each
  bool profileCalibrated = false;             // the active profile
  // Settings › Calibrate's summary (§3.8): the widths captured, each as negative end, neutral and
  // positive end, 0 for none; and why they cannot be saved, nullptr when they can (lib/rc).
  uint16_t capturedSteering[3] = {};
  uint16_t capturedThrottle[3] = {};
  const char* calibrationProblem = nullptr;

  bool gnssLink = false;  // the module answered POST check 8, and its messages keep coming
  uint32_t gnssBaud = 0;
  uint8_t fixType = 0;           // UBX fixType: 0 none, 2 2D, 3 3D
  int32_t latE7 = 0, lonE7 = 0;  // degrees × 1e7, as NAV-PVT gives them; meaningful with a fix
  uint8_t sats = 0;       // used in the solution
  uint8_t satsSeen = 0;   // tracked, with a signal (NAV-SAT)
  uint16_t hAccCm = 0;
  uint16_t pdopX100 = 0;
  AcqState acq = AcqState::Acquiring;  // ui_gnss.h
  uint16_t acqS = 0;  // Fix: what it took; otherwise the time since the acquisition started
  uint16_t speedKmh = 0;
  uint32_t gnssLoss = 0;

  uint32_t freeMb = 0;  // card
  uint16_t cardGb = 0;
  uint16_t cardKbps = 0;

  const char* trackId = "";    // the resolved track's folder; empty without a fix, or with none here
  const char* trackName = "";  // its name in track.json, given in the apps; empty until then
  // Track › Choose: menu.trackChoices of them, each as the screens name a track, up to 14 columns.
  const char* const* trackChoiceNames = nullptr;
  uint8_t startLineSource = 0;  // with menu.hasStartLine: 0 auto, 1 device, 2 desktop (§7.5)
  bool hasGeometry = false;
  uint16_t trackSessions = 0;  // recorded at this track
  uint16_t cardSessions = 0;   // on the card, for Delete sessions
  uint16_t nextSession = 1;
  bool autoStart = true;

  uint16_t laps = 0;
  uint32_t lastLapMs = 0;  // 0: none yet
  uint32_t bestLapMs = 0;
  int32_t deltaMs = 0;  // last lap against the best

  int8_t steeringPct = 0;  // known with the input calibrated and a pulse to make it of
  int8_t throttlePct = 0;
  bool steeringPctKnown = false;
  bool throttlePctKnown = false;
  uint16_t steeringUs = 0;
  uint16_t throttleUs = 0;

  uint32_t geometryM = 0;
  uint16_t geometryPoints = 0;

  bool usbHost = false;
  bool usbEjected = false;  // TRANSFER: the host ejected the disk, so leaving is safe (§7.3)
  bool bleConnected = false;
  bool warning = false;  // a capability withdrawn, or a loss counted (§3.2)

  const char* firmware = "";
  const char* postSummary = "";
  const PostResult* post = nullptr;  // check by check, for Settings › About; null before the POST
  const char* configNote = "";       // where config.json came from, and its keys at their defaults
};

// Neutral, left, right, throttle, brake, aux; then the summary.
constexpr uint8_t kCalibrationSteps = 6;

struct BoardOutput {
  Command command = Command::None;
  uint8_t arg = 0;
  char toast[22] = "";              // empty: none; 21 columns
  Refusal refused = Refusal::None;  // an action refused: its notice is open now (notice())
  bool swallow = false;  // the press closed a notice: the rest of its gesture is not ours
};

class Board {
 public:
  explicit Board(const MenuTiming& timing) : timing_(timing), menu_(timing) {}

  BoardOutput handle(Event event, const Facts& facts, uint32_t nowMs);
  void tick(uint32_t nowMs);
  // Every tick, with what the car is doing: the automatic start from IDLE and the automatic stop
  // of a session (§3.6). A start closes whatever the menu had open and goes through Home; one the
  // POST refuses shows its notice once, and Home says why from then on.
  BoardOutput onMotion(const Motion& motion, const Facts& facts, uint32_t nowMs);
  void setAutoTiming(const AutoSessionTiming& timing) { auto_ = AutoSession(timing); }
  // Every tick, with the receiver's pulses and the USB host (§4.5). In IDLE, the pulses stopping —
  // the car switched off, on a device powered otherwise — or a long time with neither them, a
  // host nor a key opens the sleep notice, and Sleep follows unless a key cancels it. Nothing
  // sleeps outside IDLE, and a USB host keeps the device awake.
  BoardOutput onPower(const Power& power, const Facts& facts, uint32_t nowMs);
  void setPowerTiming(const PowerTiming& timing) { powerTiming_ = timing; }
  // A session the card could not take, at its start or in its middle: back to IDLE, and the
  // reason on screen until a key (§3.5).
  void abortSession(Refusal why, uint8_t arg, uint32_t nowMs);
  // TRANSFER the card could not enter: back to IDLE, and the reason on screen until a key.
  void abortTransfer(Refusal why, uint32_t nowMs);
  const AutoSession& autoSession() const { return auto_; }
  // A key that only woke the dimmed display (§3.2): activity for the menu's timeout, nothing more,
  // so that it does not close a notice before it was read.
  void wake(uint32_t nowMs) {
    menu_.restartTimer(nowMs);
    activeMs_ = nowMs;
  }

  Mode mode() const { return mode_; }
  const Menu& menu() const { return menu_; }
  // A dialog of the board's own, over its mode; the menu's dialogs are the menu's.
  DialogKind dialog() const { return confirm_.kind(); }
  // UP/DOWN on a screen that scrolls (Menu::scrolls(), and the session): a page count that the
  // screens wrap, since only they know how many pages there are.
  int16_t scroll() const { return scroll_; }
  uint16_t markers() const { return markers_; }
  uint32_t sessionElapsedMs(uint32_t nowMs) const { return nowMs - sessionStartMs_; }
  uint8_t calibrationStep() const { return step_; }  // kCalibrationSteps is the summary
  // The refusal on screen, until the next key closes it (§3.5): an action that cannot happen says
  // why on the whole body, not in a toast that is gone before it is read.
  Refusal notice() const { return notice_; }
  uint8_t noticeArg() const { return noticeArg_; }
  // The sleep notice of IDLE, over whatever is open, and the seconds it has left.
  SleepWhy sleepNotice() const { return sleep_; }
  uint32_t sleepInS(uint32_t nowMs) const;
  uint32_t idleSleepMin() const { return powerTiming_.idleMs / 60000; }

 private:
  BoardOutput fromMenu(const MenuResult& result, const Facts& facts, uint32_t nowMs);
  BoardOutput answer(Confirm::Answer answer, DialogKind kind, const Facts& facts, uint32_t nowMs);
  BoardOutput inSession(Event event, uint32_t nowMs);
  BoardOutput inGeometry(Event event, uint32_t nowMs);
  BoardOutput inCalibration(Event event, const Facts& facts, uint32_t nowMs);
  BoardOutput inTransfer(Event event, const Facts& facts, uint32_t nowMs);
  void enter(Mode mode, uint32_t nowMs);
  void backToIdle(uint32_t nowMs);

  MenuTiming timing_;
  Menu menu_;
  Confirm confirm_;
  Mode mode_ = Mode::Idle;
  uint32_t sessionStartMs_ = 0;
  uint16_t markers_ = 0;
  int16_t scroll_ = 0;
  uint8_t step_ = 0;
  Refusal notice_ = Refusal::None;
  uint8_t noticeArg_ = 0;
  AutoSession auto_{AutoSessionTiming{}};
  bool autoRefused_ = false;  // its notice shown: once a boot

  PowerTiming powerTiming_;
  bool rcLive_ = false;    // last tick's, to see the pulses stop
  uint32_t activeMs_ = 0;  // a key, the RC signal, a USB host, or a mode: SLEEP counts from here
  SleepWhy sleep_ = SleepWhy::None;
  uint32_t sleepAtMs_ = 0;
};

// A refusal of §3.4 in one line of 21 columns, for the serial log.
void refusalText(Refusal refusal, uint8_t arg, char (&out)[22]);
// What a failed POST check that refuses logging means, in a few words; nullptr for any other check.
const char* checkReason(uint8_t check);
// Whether a session cannot start now, and so Home says why (§3.4).
inline bool loggingBlocked(const MenuContext& m) { return !m.cardOk || !m.loggingAllowed; }
const char* profileName(const Facts& facts, uint8_t index);  // "?" if there is no such profile
const char* trackChoiceName(const Facts& facts, uint8_t index);  // "?" if there is no such track
// The name New car gives profile `index`, "Car 4" for index 3, until an app renames it (§3.8).
void newProfileName(uint8_t index, char (&out)[9]);

}  // namespace ui
