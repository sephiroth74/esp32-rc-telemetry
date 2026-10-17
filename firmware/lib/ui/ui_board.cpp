#include "ui_board.h"

#include <stdarg.h>
#include <stdio.h>

namespace ui {
namespace {

BoardOutput command(Command c, uint8_t arg = 0) {
  BoardOutput o;
  o.command = c;
  o.arg = arg;
  return o;
}

void toast(BoardOutput& o, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(o.toast, sizeof o.toast, fmt, ap);
  va_end(ap);
}

}  // namespace

const char* profileName(const Facts& f, uint8_t index) {
  if (!f.profileNames || index >= f.menu.profileCount) return "?";
  return f.profileNames[index];
}

void refusalText(Refusal refusal, uint8_t arg, char (&out)[22]) {
  switch (refusal) {
    case Refusal::NoCard:
      snprintf(out, sizeof out, "Refused: no card");
      break;
    case Refusal::NoFix:
      snprintf(out, sizeof out, "Refused: no fix");
      break;
    case Refusal::LoggingRefused:
      snprintf(out, sizeof out, "Logging refused: %u", arg);  // the POST check that refused it
      break;
    case Refusal::NoRcSignal:
      snprintf(out, sizeof out, "No RC signal");
      break;
    default:
      out[0] = '\0';
      break;
  }
}

BoardOutput Board::handle(Event event, const Facts& facts, uint32_t nowMs) {
  if (confirm_.isOpen()) {
    DialogKind kind = confirm_.kind();
    Confirm::Answer a = confirm_.handle(event);
    if (a == Confirm::Answer::Pending) return {};
    return answer(a, kind, facts, nowMs);
  }
  switch (mode_) {
    case Mode::Idle: {
      ScreenId before = menu_.screen();
      if ((event == Event::Up || event == Event::Down) && Menu::scrolls(before)) {
        scroll_ += event == Event::Down ? 1 : -1;
        menu_.restartTimer(nowMs);  // scrolling is activity too
        return {};
      }
      BoardOutput o = fromMenu(menu_.handle(event, facts.menu, nowMs), facts, nowMs);
      if (menu_.screen() != before) scroll_ = 0;
      return o;
    }
    case Mode::Session:
      return inSession(event, nowMs);
    case Mode::Geometry:
      return inGeometry(event, nowMs);
    case Mode::Calibration:
      return inCalibration(event, facts, nowMs);
    case Mode::Transfer:
      return inTransfer(event, facts, nowMs);
  }
  return {};
}

BoardOutput Board::fromMenu(const MenuResult& r, const Facts& f, uint32_t nowMs) {
  BoardOutput o;
  if (r.refusal != Refusal::None) {
    refusalText(r.refusal, r.arg, o.toast);
    return o;
  }
  o.command = r.command;
  o.arg = r.arg;
  switch (r.command) {
    case Command::StartSession:
      enter(Mode::Session, nowMs);
      toast(o, "Session %u", f.nextSession);
      break;
    case Command::EnterGeometry:
      enter(Mode::Geometry, nowMs);
      break;
    case Command::EnterCalibration:
      enter(Mode::Calibration, nowMs);
      break;
    case Command::EnterTransfer:
      enter(Mode::Transfer, nowMs);
      break;
    case Command::RecordStartLine:
      toast(o, "Start line saved");
      break;
    case Command::RestartGnss:
      toast(o, "GNSS restarted");
      break;
    case Command::SelectProfile:
      toast(o, "Profile: %s", profileName(f, r.arg));
      break;
    case Command::SetBrightness:
      toast(o, "Brightness: %s", kBrightnessLabels[r.arg % 3]);
      break;
    case Command::SetDimDelay:
      toast(o, "Dim after: %s", kDimDelayLabels[r.arg % 4]);
      break;
    case Command::SetTextSize:
      toast(o, "Text size: %s", kTextSizeLabels[r.arg % 2]);
      break;
    case Command::DeleteSessions:
      toast(o, "Sessions deleted");
      break;
    default:
      break;
  }
  return o;
}

BoardOutput Board::answer(Confirm::Answer a, DialogKind kind, const Facts& f, uint32_t nowMs) {
  if (a != Confirm::Answer::Yes) return {};  // no: stay where the dialog was opened
  BoardOutput o;
  switch (kind) {
    case DialogKind::StopSession:
      o = command(Command::StopSession);
      toast(o, "Session %u saved", f.nextSession);
      backToIdle(nowMs);
      break;
    case DialogKind::OverwriteCalibration:
      o = command(Command::SaveCalibration);
      toast(o, "Calibration saved");
      backToIdle(nowMs);
      break;
    case DialogKind::LeaveTransfer:
      o = command(Command::LeaveTransfer);
      backToIdle(nowMs);
      break;
    default:
      break;
  }
  return o;
}

// The menu is locked during a session (§3.6): OK drops a marker, UP/DOWN scroll the session screen
// a page at a time, and holding OK asks before stopping — the automatic stop covers the normal
// case.
BoardOutput Board::inSession(Event event, uint32_t nowMs) {
  switch (event) {
    case Event::Up:
      --scroll_;
      return {};
    case Event::Down:
      ++scroll_;
      return {};
    case Event::Open: {
      BoardOutput o = command(Command::Marker);
      toast(o, "Marker %u", ++markers_);
      return o;
    }
    case Event::Hold:
      confirm_.open(DialogKind::StopSession, nowMs);
      return {};
    default:
      return {};
  }
}

BoardOutput Board::inGeometry(Event event, uint32_t nowMs) {
  if (event != Event::Hold) return {};
  BoardOutput o = command(Command::StopGeometry);
  toast(o, "Geometry saved");
  backToIdle(nowMs);
  return o;
}

// Six steps, each captured with OK; DOWN skips the aux step, LEFT goes back a step and cancels at
// the first. Then a summary, saved with a hold — confirmed first over an existing calibration.
BoardOutput Board::inCalibration(Event event, const Facts& f, uint32_t nowMs) {
  if (step_ < kCalibrationSteps) {
    switch (event) {
      case Event::Open: {
        BoardOutput o = command(Command::CalibrationCapture, step_);
        ++step_;
        return o;
      }
      case Event::Down:
        if (step_ == kCalibrationSteps - 1) ++step_;
        return {};
      case Event::Back: {
        if (step_ > 0) {
          --step_;
          return {};
        }
        BoardOutput o = command(Command::CancelCalibration);
        toast(o, "Calibration cancelled");
        backToIdle(nowMs);
        return o;
      }
      default:
        return {};
    }
  }
  switch (event) {
    case Event::Hold: {
      if (f.profileCalibrated) {
        confirm_.open(DialogKind::OverwriteCalibration, nowMs);
        return {};
      }
      BoardOutput o = command(Command::SaveCalibration);
      toast(o, "Calibration saved");
      backToIdle(nowMs);
      return o;
    }
    case Event::Back:
      --step_;
      return {};
    default:
      return {};
  }
}

// Leaving while a host still has the card is how filesystems get damaged (§7.3): it asks first.
BoardOutput Board::inTransfer(Event event, const Facts& f, uint32_t nowMs) {
  if (event != Event::Back) return {};
  if (f.usbHost) {
    confirm_.open(DialogKind::LeaveTransfer, nowMs);
    return {};
  }
  backToIdle(nowMs);
  return command(Command::LeaveTransfer);
}

void Board::enter(Mode mode, uint32_t nowMs) {
  mode_ = mode;
  scroll_ = 0;
  step_ = 0;
  if (mode == Mode::Session) {
    sessionStartMs_ = nowMs;
    markers_ = 0;
  }
}

void Board::backToIdle(uint32_t nowMs) {
  mode_ = Mode::Idle;
  scroll_ = 0;
  menu_.restartTimer(nowMs);  // the menu's 30 s count from now, not from before the mode
}

void Board::tick(uint32_t nowMs) {
  if (confirm_.expired(nowMs, timing_.dialogTimeoutMs)) confirm_.close();
  if (mode_ == Mode::Idle) menu_.tick(nowMs);
}

}  // namespace ui
