#include "ui_menu.h"

#include <stddef.h>

namespace ui {
namespace {

// Wrap-safe "a is at or after b" for millisecond counters.
bool reached(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b) >= 0; }

// The lists of §3.4. Transfer is the one entry that is a command rather than a screen.
const Entry kMenu[] = {
    {"Session", ScreenId::Session, Command::None},
    {"Track", ScreenId::Track, Command::None},
    {"GNSS", ScreenId::Gnss, Command::None},
    {"Transfer", ScreenId::Menu, Command::EnterTransfer},
    {"Settings", ScreenId::Settings, Command::None},
};
const Entry kTrack[] = {
    {"Start line", ScreenId::StartLine, Command::None},
    {"Geometry", ScreenId::Geometry, Command::None},
    {"Info", ScreenId::TrackInfo, Command::None},
};
const Entry kSettings[] = {
    {"Car profile", ScreenId::CarProfile, Command::None},
    {"Calibrate", ScreenId::Calibrate, Command::None},
    {"Display", ScreenId::DisplaySettings, Command::None},
    {"Delete sessions", ScreenId::DeleteSessions, Command::None},
    {"About", ScreenId::About, Command::None},
};
const Entry kDisplay[] = {
    {"Brightness", ScreenId::Brightness, Command::None},
    {"Dim after", ScreenId::DimDelay, Command::None},
    {"Text size", ScreenId::TextSize, Command::None},
};

template <size_t N>
constexpr uint8_t countOf(const Entry (&)[N]) {
  return N;
}

MenuResult refused(Refusal refusal, uint8_t arg = 0) {
  MenuResult r;
  r.refusal = refusal;
  r.arg = arg;
  return r;
}

MenuResult issue(Command command, uint8_t arg = 0) {
  MenuResult r;
  r.command = command;
  r.arg = arg;
  return r;
}

// A session needs a card it may write to; the POST can have refused logging for other reasons too.
MenuResult startSession(const MenuContext& c) {
  if (!c.cardOk) return refused(Refusal::NoCard);
  if (!c.loggingAllowed) return refused(Refusal::LoggingRefused, c.refusingCheck);
  return issue(Command::StartSession);
}

}  // namespace

const char* commandName(Command command) {
  static const char* const kNames[] = {
      "none",           "start session", "stop session",  "marker",          "record start line",
      "enter geometry", "stop geometry", "restart gnss",  "enter transfer",  "leave transfer",
      "enter calib",    "calib capture", "save calib",    "cancel calib",    "select profile",
      "set brightness", "set dim delay", "set text size", "delete sessions",
  };
  return kNames[static_cast<uint8_t>(command)];
}

const char* const kBrightnessLabels[3] = {"low", "medium", "high"};
const char* const kDimDelayLabels[4] = {"15 s", "30 s", "60 s", "never"};
const char* const kTextSizeLabels[2] = {"small", "large"};

const Entry* Menu::entries(ScreenId screen, uint8_t& count) {
  switch (screen) {
    case ScreenId::Menu:
      count = countOf(kMenu);
      return kMenu;
    case ScreenId::Track:
      count = countOf(kTrack);
      return kTrack;
    case ScreenId::Settings:
      count = countOf(kSettings);
      return kSettings;
    case ScreenId::DisplaySettings:
      count = countOf(kDisplay);
      return kDisplay;
    default:
      count = 0;
      return nullptr;
  }
}

ScreenId Menu::parent(ScreenId screen) {
  switch (screen) {
    case ScreenId::Session:
    case ScreenId::Track:
    case ScreenId::Gnss:
    case ScreenId::Settings:
      return ScreenId::Menu;
    case ScreenId::StartLine:
    case ScreenId::Geometry:
    case ScreenId::TrackInfo:
      return ScreenId::Track;
    case ScreenId::CarProfile:
    case ScreenId::Calibrate:
    case ScreenId::DisplaySettings:
    case ScreenId::DeleteSessions:
    case ScreenId::About:
      return ScreenId::Settings;
    case ScreenId::Brightness:
    case ScreenId::DimDelay:
    case ScreenId::TextSize:
      return ScreenId::DisplaySettings;
    default:
      return ScreenId::Home;
  }
}

bool Menu::scrolls(ScreenId screen) {
  uint8_t count = 0;
  if (entries(screen, count)) return false;
  switch (screen) {
    case ScreenId::CarProfile:
    case ScreenId::Brightness:
    case ScreenId::DimDelay:
    case ScreenId::TextSize:
    case ScreenId::Dialog:
      return false;
    default:
      return true;
  }
}

uint8_t Menu::choiceCount(ScreenId screen, const MenuContext& c) const {
  uint8_t count = 0;
  if (entries(screen, count)) return count;
  switch (screen) {
    case ScreenId::CarProfile:
      return c.profileCount ? c.profileCount : 1;
    case ScreenId::Brightness:
      return 3;
    case ScreenId::DimDelay:
      return 4;
    case ScreenId::TextSize:
      return 2;
    default:
      return 0;
  }
}

// UP and DOWN wrap around: with the two buttons there is no UP, so DOWN alone must reach every
// entry.
void Menu::move(int8_t step, const MenuContext& c) {
  uint8_t count = choiceCount(screen_, c);
  if (count == 0) return;
  uint8_t& cursor = cursorOf(screen_);
  cursor = static_cast<uint8_t>((cursor + count + step) % count);
}

MenuResult Menu::handle(Event event, const MenuContext& c, uint32_t nowMs) {
  lastKeyMs_ = nowMs;
  if (screen_ == ScreenId::Dialog) {
    DialogKind kind = confirm_.kind();
    Confirm::Answer answer = confirm_.handle(event);
    if (answer == Confirm::Answer::Pending) return {};
    screen_ = dialogReturn_;
    if (answer == Confirm::Answer::No) return {};
    return issue(kind == DialogKind::OverwriteStartLine ? Command::RecordStartLine
                                                        : Command::DeleteSessions);
  }

  if (event == Event::Press) return {};  // activity only: the gesture's own event follows

  switch (event) {
    case Event::Up:
      move(-1, c);
      return {};
    case Event::Down:
      move(+1, c);
      return {};
    case Event::Back:  // in an editor, this is cancel: nothing is saved
      screen_ = parent(screen_);
      return {};
    case Event::Open:
      return open(screen_, c);
    case Event::Hold:
      return hold(c, nowMs);
    default:
      return {};
  }
}

MenuResult Menu::open(ScreenId current, const MenuContext& c) {
  switch (current) {
    case ScreenId::Home:
      screen_ = ScreenId::Menu;
      return {};
    case ScreenId::CarProfile:
    case ScreenId::Brightness:
    case ScreenId::DimDelay:
    case ScreenId::TextSize: {
      // An editor: the highlighted choice is saved, and the editor closes.
      Command command = current == ScreenId::CarProfile   ? Command::SelectProfile
                        : current == ScreenId::Brightness ? Command::SetBrightness
                        : current == ScreenId::DimDelay   ? Command::SetDimDelay
                                                          : Command::SetTextSize;
      uint8_t choice = cursor();
      screen_ = parent(current);
      return issue(command, choice);
    }
    case ScreenId::Calibrate:
      if (!c.rcSignal) return refused(Refusal::NoRcSignal);
      return issue(Command::EnterCalibration);
    default:
      break;
  }

  uint8_t count = 0;
  const Entry* list = entries(current, count);
  if (!list) return {};  // a leaf without an OK action
  const Entry& entry = list[cursor()];
  if (entry.command != Command::None) return issue(entry.command);

  // An editor opens on the value in force, so OK without a change saves nothing new.
  switch (entry.screen) {
    case ScreenId::CarProfile:
      cursorOf(entry.screen) = c.activeProfile;
      break;
    case ScreenId::Brightness:
      cursorOf(entry.screen) = c.brightness;
      break;
    case ScreenId::DimDelay:
      cursorOf(entry.screen) = c.dimDelay;
      break;
    case ScreenId::TextSize:
      cursorOf(entry.screen) = c.textSize;
      break;
    default:
      break;
  }
  screen_ = entry.screen;
  return {};
}

MenuResult Menu::hold(const MenuContext& c, uint32_t nowMs) {
  switch (screen_) {
    case ScreenId::Home:
    case ScreenId::Session:
      return startSession(c);
    case ScreenId::StartLine:
      if (!c.cardOk) return refused(Refusal::NoCard);
      if (!c.hasFix) return refused(Refusal::NoFix);
      if (c.hasStartLine) {
        openDialog(DialogKind::OverwriteStartLine, nowMs);
        return {};
      }
      return issue(Command::RecordStartLine);
    case ScreenId::Geometry:
      if (!c.cardOk) return refused(Refusal::NoCard);
      if (!c.hasFix) return refused(Refusal::NoFix);
      return issue(Command::EnterGeometry);
    case ScreenId::Gnss:
      return issue(Command::RestartGnss);
    case ScreenId::DeleteSessions:
      if (!c.cardOk) return refused(Refusal::NoCard);
      openDialog(DialogKind::DeleteSessions, nowMs);
      return {};
    default:
      return {};
  }
}

void Menu::openDialog(DialogKind kind, uint32_t nowMs) {
  dialogReturn_ = screen_;
  confirm_.open(kind, nowMs);
  screen_ = ScreenId::Dialog;
}

void Menu::tick(uint32_t nowMs) {
  if (confirm_.expired(nowMs, timing_.dialogTimeoutMs)) {
    confirm_.close();
    screen_ = dialogReturn_;
  }
  if (screen_ != ScreenId::Home && reached(nowMs, lastKeyMs_ + timing_.menuTimeoutMs)) goHome();
}

void Menu::goHome() {
  screen_ = ScreenId::Home;
  confirm_.close();
  for (uint8_t& cursor : cursors_) cursor = 0;
}

}  // namespace ui
