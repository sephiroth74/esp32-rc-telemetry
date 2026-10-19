// The board states the keys drive — PLAN-ESP32.md §3.6–§3.7.
//
// IDLE is the menu (ui_menu.h). SESSION, GEOMETRY, CALIBRATION and TRANSFER are modes with their
// own keys: while one is active the menu gets no events, and when it ends the menu picks up where
// it was. Like the menu, Board decides nothing about the device — every action comes out as a
// Command with a toast for the screen — and reads what it shows from Facts.
//
// Not here yet: POST and FATAL (ROADMAP.md phase 3), SAVING, SLEEP and the automatic start and stop
// (phase 6). Framework-free (§6.1).
#pragma once

#include <stdint.h>

#include "ui_keys.h"
#include "ui_menu.h"
#include "ui_post.h"

namespace ui {

enum class Mode : uint8_t { Idle, Session, Geometry, Calibration, Transfer };

// Everything the screens show, kept current by the caller. The menu's own facts are inside.
struct Facts {
  MenuContext menu;
  const char* const* profileNames = nullptr;  // menu.profileCount of them, up to 8 characters each
  bool profileCalibrated = false;             // the active profile

  bool gnssLink = false;  // the module answered POST check 8, and its messages keep coming
  uint32_t gnssBaud = 0;
  uint8_t fixType = 0;           // UBX fixType: 0 none, 2 2D, 3 3D
  int32_t latE7 = 0, lonE7 = 0;  // degrees × 1e7, as NAV-PVT gives them; meaningful with a fix
  uint8_t sats = 0;
  uint16_t hAccCm = 0;
  uint16_t pdopX100 = 0;
  uint16_t ttffS = 0;  // time to first fix
  uint16_t speedKmh = 0;
  uint32_t gnssLoss = 0;

  uint32_t freeMb = 0;  // card
  uint16_t cardGb = 0;
  uint16_t cardKbps = 0;

  const char* trackId = "";    // the resolved track's cell id, empty without a fix
  const char* trackName = "";  // its name in track.json, given in the apps; empty until then
  bool hasGeometry = false;
  uint16_t trackSessions = 0;  // recorded at this track
  uint16_t cardSessions = 0;   // on the card, for Delete sessions
  uint16_t nextSession = 1;
  bool autoStart = true;

  uint16_t laps = 0;
  uint32_t lastLapMs = 0;  // 0: none yet
  uint32_t bestLapMs = 0;
  int32_t deltaMs = 0;  // last lap against the best

  int8_t steeringPct = 0;
  int8_t throttlePct = 0;
  uint16_t steeringUs = 0;
  uint16_t throttleUs = 0;

  uint32_t geometryM = 0;
  uint16_t geometryPoints = 0;

  bool usbHost = false;
  bool bleConnected = false;
  bool lipoLow = false;
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
  // A key that only woke the dimmed display (§3.2): activity for the menu's timeout, nothing more,
  // so that it does not close a notice before it was read.
  void wake(uint32_t nowMs) { menu_.restartTimer(nowMs); }

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
};

// A refusal of §3.4 in one line of 21 columns, for the serial log.
void refusalText(Refusal refusal, uint8_t arg, char (&out)[22]);
// What a failed POST check that refuses logging means, in a few words; nullptr for any other check.
const char* checkReason(uint8_t check);
// Whether a session cannot start now, and so Home says why (§3.4).
inline bool loggingBlocked(const MenuContext& m) { return !m.cardOk || !m.loggingAllowed; }
const char* profileName(const Facts& facts, uint8_t index);  // "?" if there is no such profile
// The name New car gives profile `index`, "Car 4" for index 3, until an app renames it (§3.8).
void newProfileName(uint8_t index, char (&out)[9]);

}  // namespace ui
