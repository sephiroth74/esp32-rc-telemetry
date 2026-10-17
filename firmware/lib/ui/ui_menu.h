// The menu — PLAN-ESP32.md §3.1, §3.4.
//
// Home, the list menu and its entries, the value editors and the confirmation dialogs, as data and
// as a small state machine driven by the key events of ui_keys.h. It decides nothing about the
// device: what an entry does comes out as a Command for the caller to carry out, and what the menu
// needs to know — is there a card, a fix, a start line — goes in through MenuContext.
//
// The board states of §3.7 sit above it: while the device is in SESSION, TRANSFER or another mode,
// the menu simply gets no events, and picks up where it was when the device is back in IDLE.
//
// Framework-free (§6.1): the caller supplies the events, the facts and the milliseconds.
#pragma once

#include <stdint.h>

#include "ui_keys.h"

namespace ui {

enum class ScreenId : uint8_t {
  Home,
  Menu,
  Session,
  Track,
  StartLine,
  Geometry,
  TrackInfo,
  NewTrack,
  Gnss,
  Settings,
  CarProfile,
  Calibrate,
  DisplaySettings,
  Brightness,
  DimDelay,
  TextSize,
  DeleteSessions,
  About,
  Dialog,
  Count
};

// What the menu, or a board state (ui_board.h), asks the device to do. `arg` carries the choice
// of an editor, or the step of a calibration.
enum class Command : uint8_t {
  None,
  StartSession,
  StopSession,
  Marker,
  RecordStartLine,
  EnterGeometry,
  StopGeometry,
  NewTrack,
  RestartGnss,
  EnterTransfer,
  LeaveTransfer,
  EnterCalibration,
  CalibrationCapture,
  SaveCalibration,
  CancelCalibration,
  SelectProfile,
  NewProfile,
  SetBrightness,
  SetDimDelay,
  SetTextSize,
  DeleteSessions,
};

const char* commandName(Command command);  // for logs

// Why an entry was refused; the screen shows it as a toast (§3.4).
enum class Refusal : uint8_t { None, NoCard, NoFix, LoggingRefused, NoRcSignal, ProfilesFull };

// The four confirmations of §3.1, plus leaving Transfer while a host has the card (§7.3).
enum class DialogKind : uint8_t {
  None,
  OverwriteStartLine,
  DeleteSessions,
  StopSession,
  OverwriteCalibration,
  LeaveTransfer,
};

// A confirmation dialog (§3.1): hold OK is yes; any other key, or the timeout, is no. The press
// that opens a gesture decides nothing — an OK press may still become the hold.
class Confirm {
 public:
  enum class Answer : uint8_t { Pending, Yes, No };

  void open(DialogKind kind, uint32_t nowMs) {
    kind_ = kind;
    openedMs_ = nowMs;
  }
  void close() { kind_ = DialogKind::None; }
  bool isOpen() const { return kind_ != DialogKind::None; }
  DialogKind kind() const { return kind_; }

  // Closes the dialog on a yes or a no.
  Answer handle(Event event) {
    if (event == Event::Press) return Answer::Pending;
    Answer answer = event == Event::Hold ? Answer::Yes : Answer::No;
    close();
    return answer;
  }
  bool expired(uint32_t nowMs, uint32_t timeoutMs) const {
    return isOpen() && static_cast<int32_t>(nowMs - (openedMs_ + timeoutMs)) >= 0;
  }

 private:
  DialogKind kind_ = DialogKind::None;
  uint32_t openedMs_ = 0;
};

// The facts the menu reads. The caller keeps them current; the menu never changes them.
struct MenuContext {
  bool cardOk = true;          // false: entries that write to the card are refused
  bool loggingAllowed = true;  // false: the POST refused logging...
  uint8_t refusingCheck = 0;   // ...and this is the check that did, for the toast
  bool hasFix = false;
  bool rcSignal = false;  // edges on the RC inputs: calibration needs them
  bool hasStartLine = false;
  uint8_t profileCount = 1;  // 1..profileMax
  uint8_t profileMax = 8;    // CAR_PROFILES_MAX: New car is refused once there are this many
  uint8_t activeProfile = 0;
  uint8_t brightness = 2;  // index into kBrightnessLabels
  uint8_t dimDelay = 1;    // index into kDimDelayLabels
  uint8_t textSize = 1;    // index into kTextSizeLabels: small or large (§3.2)
};

struct MenuTiming {
  uint32_t menuTimeoutMs;    // without a key, back to Home
  uint32_t dialogTimeoutMs;  // a dialog left alone answers "no"
};

// An entry of a list: it opens a screen, or issues a command when chosen.
struct Entry {
  const char* label;
  ScreenId screen;
  Command command;
};

// The screen of every list's last entry, "Back": choosing it goes up a level, so getting out never
// needs the held NAV of the two-button layout. Editors end with a Back choice the same way.
constexpr ScreenId kBackEntry = ScreenId::Count;

extern const char* const kBrightnessLabels[3];  // low, medium, high
extern const char* const kDimDelayLabels[4];    // 15 s, 30 s, 60 s, never
extern const char* const kTextSizeLabels[2];    // small, large

struct MenuResult {
  Command command = Command::None;
  uint8_t arg = 0;  // the chosen value of an editor, or the refusing POST check
  Refusal refusal = Refusal::None;
};

class Menu {
 public:
  explicit Menu(const MenuTiming& timing) : timing_(timing) {}

  MenuResult handle(Event event, const MenuContext& context, uint32_t nowMs);
  void tick(uint32_t nowMs);  // the two timeouts; call every tick
  void goHome();
  void restartTimer(uint32_t nowMs) { lastKeyMs_ = nowMs; }  // e.g. back from a board mode

  ScreenId screen() const { return screen_; }
  // The highlighted entry of a list, or the highlighted choice of an editor.
  uint8_t cursor() const { return cursors_[static_cast<uint8_t>(screen_)]; }
  DialogKind dialog() const { return confirm_.kind(); }

  // The entries of a list screen, or nullptr and 0 for any other screen.
  static const Entry* entries(ScreenId screen, uint8_t& count);
  static ScreenId parent(ScreenId screen);
  // An editor's values, without its New and Back choices; 0 for any other screen.
  static uint8_t valueCount(ScreenId screen, const MenuContext& context);
  // Whether the choice after an editor's values adds one: New car, in Car profile (§3.8).
  static bool hasNewChoice(ScreenId screen) { return screen == ScreenId::CarProfile; }
  // Home and the screens that are neither a list nor an editor: UP/DOWN scroll their content,
  // and the board keeps the position (ui_board.h).
  static bool scrolls(ScreenId screen);

 private:
  MenuResult open(ScreenId current, const MenuContext& context);
  MenuResult hold(const MenuContext& context, uint32_t nowMs);
  void openDialog(DialogKind kind, uint32_t nowMs);
  uint8_t choiceCount(ScreenId screen, const MenuContext& context) const;
  void move(int8_t step, const MenuContext& context);
  uint8_t& cursorOf(ScreenId screen) { return cursors_[static_cast<uint8_t>(screen)]; }

  MenuTiming timing_;
  ScreenId screen_ = ScreenId::Home;
  uint8_t cursors_[static_cast<uint8_t>(ScreenId::Count)] = {};
  Confirm confirm_;
  ScreenId dialogReturn_ = ScreenId::Home;
  uint32_t lastKeyMs_ = 0;
};

}  // namespace ui
