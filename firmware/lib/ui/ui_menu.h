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
  Gnss,
  Settings,
  CarProfile,
  Calibrate,
  DisplaySettings,
  Brightness,
  DimDelay,
  DeleteSessions,
  About,
  Dialog,
  Count
};

// What an entry asks the device to do. `arg` carries the choice of an editor.
enum class Command : uint8_t {
  None,
  StartSession,
  RecordStartLine,
  EnterGeometry,
  RestartGnss,
  EnterTransfer,
  EnterCalibration,
  SelectProfile,
  SetBrightness,
  SetDimDelay,
  DeleteSessions,
};

// Why an entry was refused; the screen shows it as a toast (§3.4).
enum class Refusal : uint8_t { None, NoCard, NoFix, LoggingRefused, NoRcSignal };

enum class DialogKind : uint8_t { None, OverwriteStartLine, DeleteSessions };

// The facts the menu reads. The caller keeps them current; the menu never changes them.
struct MenuContext {
  bool cardOk = true;          // false: entries that write to the card are refused
  bool loggingAllowed = true;  // false: the POST refused logging...
  uint8_t refusingCheck = 0;   // ...and this is the check that did, for the toast
  bool hasFix = false;
  bool rcSignal = false;  // edges on the RC inputs: calibration needs them
  bool hasStartLine = false;
  uint8_t profileCount = 1;  // 1..CAR_PROFILES_MAX
  uint8_t activeProfile = 0;
  uint8_t brightness = 2;  // index into kBrightnessLabels
  uint8_t dimDelay = 1;    // index into kDimDelayLabels
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

extern const char* const kBrightnessLabels[3];  // low, medium, high
extern const char* const kDimDelayLabels[4];    // 15 s, 30 s, 60 s, never

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
  DialogKind dialog() const { return dialog_; }

  // The entries of a list screen, or nullptr and 0 for any other screen.
  static const Entry* entries(ScreenId screen, uint8_t& count);
  static ScreenId parent(ScreenId screen);

 private:
  MenuResult open(ScreenId current, const MenuContext& context);
  MenuResult hold(const MenuContext& context, uint32_t nowMs);
  void openDialog(DialogKind kind, uint32_t nowMs);
  void closeDialog();
  uint8_t choiceCount(ScreenId screen, const MenuContext& context) const;
  void move(int8_t step, const MenuContext& context);
  uint8_t& cursorOf(ScreenId screen) { return cursors_[static_cast<uint8_t>(screen)]; }

  MenuTiming timing_;
  ScreenId screen_ = ScreenId::Home;
  uint8_t cursors_[static_cast<uint8_t>(ScreenId::Count)] = {};
  DialogKind dialog_ = DialogKind::None;
  ScreenId dialogReturn_ = ScreenId::Home;
  uint32_t dialogOpenedMs_ = 0;
  uint32_t lastKeyMs_ = 0;
};

}  // namespace ui
