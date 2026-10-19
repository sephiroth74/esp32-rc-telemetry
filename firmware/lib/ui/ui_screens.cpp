#include "ui_screens.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace ui {
namespace {

constexpr uint8_t kMaxContent = 16;

const char* postResultText(const CheckReport& c, bool failDetail);  // with the POST's screens

// A screen before it is laid out on a grid: all its lines, the cursor of a list, its hint.
struct Content {
  char lines[kMaxContent][kScreenCols + 1];
  uint8_t count = 0;
  int8_t cursor = -1;  // a list or an editor: the highlighted line, which stays in view
  char hint[kScreenCols + 1] = "";
  uint8_t overflow = 0;

  void add(const char* fmt, ...) {
    if (count == kMaxContent) {
      ++overflow;
      return;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(lines[count], sizeof lines[count], fmt, ap);
    va_end(ap);
    if (n > kScreenCols) ++overflow;
    ++count;
  }
  void setHint(const char* text) {
    if (snprintf(hint, sizeof hint, "%s", text) > kScreenCols) ++overflow;
  }
};

void put(ScreenText& out, uint8_t row, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(out.lines[row], sizeof out.lines[row], fmt, ap);
  va_end(ap);
  if (n > kScreenCols) ++out.overflow;
}

// The resolved track as the screens name it: its name from the apps, cut to the 14 columns its cell
// id can take (N4512E00934-99) so that every screen tested with the id still fits, else the id.
const char* trackText(const Facts& f) {
  static char name[15];
  if (f.trackName && *f.trackName) {
    snprintf(name, sizeof name, "%s", f.trackName);
    return name;
  }
  return f.trackId && *f.trackId ? f.trackId : "none, no fix";
}

// Six decimals of a degree, a tenth of a metre, from degrees × 1e7; the hemisphere last.
void coordText(int32_t e7, char positive, char negative, char (&out)[16]) {
  uint32_t a = e7 < 0 ? static_cast<uint32_t>(-static_cast<int64_t>(e7)) : e7;
  snprintf(out, sizeof out, "%lu.%06lu %c", (unsigned long)(a / 10000000),
           (unsigned long)(a % 10000000 / 10), e7 < 0 ? negative : positive);
}

const char* fixName(uint8_t fixType) { return fixType >= 3 ? "3D" : fixType == 2 ? "2D" : "--"; }

// m:ss.mmm, or "--" before the first lap.
void lapText(uint32_t ms, char (&out)[16]) {
  if (!ms) {
    snprintf(out, sizeof out, "--");
    return;
  }
  snprintf(out, sizeof out, "%lu:%02lu.%03lu", (unsigned long)(ms / 60000),
           (unsigned long)(ms / 1000 % 60), (unsigned long)(ms % 1000));
}

void deltaText(int32_t ms, char (&out)[16]) {
  uint32_t magnitude = ms < 0 ? -ms : ms;
  snprintf(out, sizeof out, "%c%lu.%03lu", ms < 0 ? '-' : '+', (unsigned long)(magnitude / 1000),
           (unsigned long)(magnitude % 1000));
}

// Free space in at most five columns: MB below a GB, one decimal below 10 GB, whole GB above.
void freeText(const Facts& f, char (&out)[16]) {
  if (!f.menu.cardOk) {
    snprintf(out, sizeof out, "--");
  } else if (f.freeMb < 1000) {
    snprintf(out, sizeof out, "%luMB", (unsigned long)f.freeMb);
  } else if (f.freeMb < 10240) {
    snprintf(out, sizeof out, "%lu.%luGB", (unsigned long)(f.freeMb / 1024),
             (unsigned long)(f.freeMb % 1024 * 10 / 1024));
  } else {
    snprintf(out, sizeof out, "%luGB", (unsigned long)(f.freeMb / 1024));
  }
}

const char* screenTitle(ScreenId s) {
  switch (s) {
    case ScreenId::Home:
      return "Ready";
    case ScreenId::Menu:
      return "Menu";
    case ScreenId::Session:
      return "Session";
    case ScreenId::Track:
      return "Track";
    case ScreenId::StartLine:
      return "Start line";
    case ScreenId::Geometry:
      return "Geometry";
    case ScreenId::TrackInfo:
      return "Track info";
    case ScreenId::NewTrack:
      return "New track";
    case ScreenId::Gnss:
      return "GNSS";
    case ScreenId::Settings:
      return "Settings";
    case ScreenId::CarProfile:
      return "Car profile";
    case ScreenId::Calibrate:
      return "Calibrate";
    case ScreenId::DisplaySettings:
      return "Display";
    case ScreenId::Brightness:
      return "Brightness";
    case ScreenId::DimDelay:
      return "Dim after";
    case ScreenId::TextSize:
      return "Text size";
    case ScreenId::DeleteSessions:
      return "Delete";
    case ScreenId::About:
      return "About";
    case ScreenId::Restart:
      return "Restart";
    case ScreenId::Dialog:
      return "Confirm";
    default:
      return "";
  }
}

// --- The status bar -------------------------------------------------------------

// The state word: 11 columns at most, which both grids leave for it.
void stateText(const Board& b, const Facts& f, uint32_t nowMs, char (&out)[24]) {
  if (b.notice() != Refusal::None) {
    snprintf(out, sizeof out, "Refused");
    return;
  }
  if (b.dialog() != DialogKind::None) {
    snprintf(out, sizeof out, "Confirm");
    return;
  }
  switch (b.mode()) {
    case Mode::Session: {
      uint32_t s = b.sessionElapsedMs(nowMs) / 1000;
      if (s < 3600) {
        snprintf(out, sizeof out, "REC %02lu:%02lu", (unsigned long)(s / 60),
                 (unsigned long)(s % 60));
      } else {
        snprintf(out, sizeof out, "REC %lu:%02lu:%02lu", (unsigned long)(s / 3600),
                 (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
      }
      return;
    }
    case Mode::Geometry:
      snprintf(out, sizeof out, "Recording");
      return;
    case Mode::Calibration:
      if (b.calibrationStep() < kCalibrationSteps) {
        snprintf(out, sizeof out, "Calib %u/%u", b.calibrationStep() + 1, kCalibrationSteps);
      } else {
        snprintf(out, sizeof out, "Calibration");
      }
      return;
    case Mode::Transfer:
      snprintf(out, sizeof out, "Transfer");
      return;
    case Mode::Idle:
      // Home says at once when a session cannot start: the status line is the first thing read.
      if (b.menu().screen() == ScreenId::Home && loggingBlocked(f.menu)) {
        snprintf(out, sizeof out, "No logging");
      } else {
        snprintf(out, sizeof out, "%s", screenTitle(b.menu().screen()));
      }
      return;
  }
}

// Small: profile, fix and free space; then the state and the marks, spaced out.
// Large: one line — the state, the fix, and the marks packed into four columns. The profile and the
// free space move to the Home screen's content instead.
void statusBar(const Board& b, const Facts& f, TextSize size, uint32_t nowMs, ScreenText& out) {
  char state[24], free[16];
  stateText(b, f, nowMs, state);
  bool ble = b.mode() == Mode::Transfer;  // BLE exists only in Transfer (§7.4)

  if (size == TextSize::Small) {
    char fix[16], marks[16] = "";
    snprintf(fix, sizeof fix, "%s %2u", fixName(f.fixType), f.sats);
    freeText(f, free);
    put(out, 0, "%-8s %-5s %5s", profileName(f, f.menu.activeProfile), fix, free);
    if (f.warning) strcat(marks, " !");
    if (f.usbHost) strcat(marks, " U");
    if (ble) strcat(marks, " B");
    if (f.lipoLow) strcat(marks, " Li");
    put(out, 1, "%-12s%9s", state, marks);
    return;
  }
  char fix[16], marks[8] = "";
  snprintf(fix, sizeof fix, "%s%2u", fixName(f.fixType), f.sats);
  if (f.warning) strcat(marks, "!");
  if (f.usbHost) strcat(marks, "U");
  if (ble) strcat(marks, "B");
  if (f.lipoLow) strcat(marks, "L");
  put(out, 0, "%-11s %4s %4s", state, fix, marks);
}

// --- Content, screen by screen ------------------------------------------------

// The hints differ by layout only where a key does: with the two buttons NAV moves, and the lists
// and editors end in Back (§3.1).
const char* listHint(Layout l) {
  return l == Layout::TwoButtons ? "NAV next  OK open" : "< back   OK open";
}
const char* editorHint(Layout l) {
  return l == Layout::TwoButtons ? "NAV next  OK save" : "< cancel  OK save";
}

void list(const Board& b, const Facts& f, ScreenId screen, Layout layout, Content& c) {
  uint8_t count = 0;
  const Entry* entries = Menu::entries(screen, count);
  uint8_t cursor = b.menu().cursor();
  for (uint8_t i = 0; i < count; ++i) {
    const char* mark = i == cursor ? ">" : " ";
    if (screen == ScreenId::DisplaySettings && entries[i].screen != kBackEntry) {
      const char* value =
          entries[i].screen == ScreenId::Brightness ? kBrightnessLabels[f.menu.brightness % 3]
          : entries[i].screen == ScreenId::DimDelay ? kDimDelayLabels[f.menu.dimDelay % 4]
                                                    : kTextSizeLabels[f.menu.textSize % 2];
      c.add("%s %-11s %s", mark, entries[i].label, value);
    } else {
      c.add("%s %s", mark, entries[i].label);
    }
  }
  c.cursor = cursor;
  c.setHint(listHint(layout));
}

// An editor: its choices, a star on the value in force, the cursor, New if it has one, Back last.
void editor(const Board& b, const Facts& f, ScreenId screen, Layout layout, Content& c) {
  uint8_t count = Menu::valueCount(screen, f.menu), active;
  switch (screen) {
    case ScreenId::CarProfile:
      active = f.menu.activeProfile;
      break;
    case ScreenId::Brightness:
      active = f.menu.brightness;
      break;
    case ScreenId::DimDelay:
      active = f.menu.dimDelay;
      break;
    default:
      active = f.menu.textSize;
      break;
  }
  uint8_t cursor = b.menu().cursor();
  for (uint8_t i = 0; i < count; ++i) {
    const char* label = screen == ScreenId::CarProfile   ? profileName(f, i)
                        : screen == ScreenId::Brightness ? kBrightnessLabels[i]
                        : screen == ScreenId::DimDelay   ? kDimDelayLabels[i]
                                                         : kTextSizeLabels[i];
    c.add("%s %s%s", i == cursor ? ">" : " ", label, i == active ? " *" : "");
  }
  if (Menu::hasNewChoice(screen)) c.add("%s New car", cursor == count++ ? ">" : " ");
  c.add("%s Back", cursor == count ? ">" : " ");
  c.cursor = cursor;
  c.setHint(editorHint(layout));
}

// At most two lines of question, so that the answer still shows with three body rows. A track id
// takes up to 14 columns, N4512E00934-99 (§7.1).
void dialog(DialogKind kind, const Facts& f, Content& c) {
  switch (kind) {
    case DialogKind::OverwriteStartLine:
      c.add("Overwrite start line");
      c.add("of %s?", trackText(f));
      break;
    case DialogKind::DeleteSessions:
      c.add("Delete %u sessions?", f.cardSessions);
      c.add("Tracks stay.");
      break;
    case DialogKind::StopSession:
      c.add("Stop session %u?", f.nextSession);
      break;
    case DialogKind::OverwriteCalibration:
      c.add("Overwrite calibration");
      c.add("of %s?", profileName(f, f.menu.activeProfile));
      break;
    case DialogKind::LeaveTransfer:
      c.add("Ejected on host?");
      c.add("The card comes back.");
      break;
    default:
      break;
  }
  c.add("hold OK: yes");
  c.setHint("other key: no (10s)");
}

// Why a session cannot start, on Home's first lines, so that the large grid shows it without
// scrolling (§3.4).
void blocker(const Facts& f, Content& c) {
  if (!f.menu.cardOk) {
    c.add("No card: no logging");
    return;
  }
  const char* why = checkReason(f.menu.refusingCheck);
  if (why) {
    c.add("%s", why);
  } else {
    c.add("POST check %u failed", f.menu.refusingCheck);
  }
  c.add("No logging (check %u)", f.menu.refusingCheck);
}

// A refusal, on the whole body until a key closes it: what is missing, and what to do about it.
void notice(const Board& b, Content& c) {
  switch (b.notice()) {
    case Refusal::NoCard:
      c.add("No card");
      c.add("Insert a FAT32 card");
      break;
    case Refusal::CardNotWritable:
      c.add("Card write failed");
      c.add("Use another card");
      c.add("POST check 5");
      break;
    case Refusal::NoGnss:
      c.add("GNSS not found");
      c.add("Check its wiring:");
      c.add("TX to D7, RX to D6");
      break;
    case Refusal::NoFix:
      c.add("No GNSS fix yet");
      c.add("See Menu > GNSS");
      break;
    case Refusal::LoggingRefused: {
      const char* why = checkReason(b.noticeArg());
      c.add("No logging:");
      c.add("%s", why ? why : "a POST check failed");
      c.add("POST check %u", b.noticeArg());
      break;
    }
    case Refusal::NoRcSignal:
      c.add("No RC signal");
      c.add("Switch the car on");
      break;
    case Refusal::ProfilesFull:
      c.add("%u profiles: full", b.noticeArg());
      c.add("Delete one in an app");
      break;
    default:
      break;
  }
  c.setHint("any key: close");
}

void home(const Facts& f, TextSize size, Content& c) {
  char a[16];
  if (loggingBlocked(f.menu)) blocker(f, c);
  c.add("Track %s", trackText(f));
  c.add("Start line: %s", f.menu.hasStartLine ? "set" : "none");
  lapText(f.lastLapMs, a);
  c.add("Last  %-10s L%u", a, f.laps);
  lapText(f.bestLapMs, a);
  c.add("Best  %s", a);
  c.add("Auto start: %s", f.autoStart ? "on" : "off");
  if (size == TextSize::Large) {  // the small grid has them in its status bar
    c.add("Car %s", profileName(f, f.menu.activeProfile));
    freeText(f, a);
    c.add("Card free %s", a);
  }
  c.setHint("OK menu  hold: start");
}

void idle(const Board& b, const Facts& f, Layout layout, TextSize size, Content& c) {
  ScreenId screen = b.menu().screen();
  switch (screen) {
    case ScreenId::Home:
      home(f, size, c);
      return;
    case ScreenId::Menu:
    case ScreenId::Track:
    case ScreenId::Settings:
    case ScreenId::DisplaySettings:
      list(b, f, screen, layout, c);
      return;
    case ScreenId::CarProfile:
    case ScreenId::Brightness:
    case ScreenId::DimDelay:
    case ScreenId::TextSize:
      editor(b, f, screen, layout, c);
      return;
    case ScreenId::Dialog:
      dialog(b.menu().dialog(), f, c);
      return;
    case ScreenId::Session:
      c.add("Next: session_%03u", f.nextSession);
      c.add("Track %s", trackText(f));
      c.add("Auto start: %s", f.autoStart ? "on" : "off");
      c.setHint("OK back  hold: start");
      return;
    case ScreenId::StartLine:
      c.add("Track %s", trackText(f));
      c.add("Start line: %s", f.menu.hasStartLine ? "set" : "none");
      if (!f.menu.hasFix) c.add("Needs a fix");
      c.setHint("OK back  hold: record");
      return;
    case ScreenId::Geometry:
      c.add("Track %s", trackText(f));
      c.add("Geometry: %s", f.hasGeometry ? "recorded" : "none");
      c.add("Optional; more laps");
      c.add("give a better shape");
      c.setHint("OK back  hold: record");
      return;
    case ScreenId::TrackInfo:
      c.add("Cell %s", f.trackId && *f.trackId ? f.trackId : "none, no fix");
      c.add("Name %s", f.trackName && *f.trackName ? trackText(f) : "set in the app");
      c.add("Sessions here %u", f.trackSessions);
      c.setHint("OK: back");
      return;
    case ScreenId::NewTrack:
      c.add("Track %s", trackText(f));
      c.add("A new one here, for");
      c.add("a new layout; name");
      c.add("it in the app");
      if (!f.menu.hasFix) c.add("Needs a fix");
      c.setHint("OK back  hold: create");
      return;
    case ScreenId::Gnss: {
      // The link, the fix and the track first: what a glance in the pits wants (§3.4).
      char a[16];
      if (f.gnssLink) {
        c.add("Link ok, %lu bd", (unsigned long)f.gnssBaud);
      } else {
        c.add("Link: no GNSS");
      }
      if (f.fixType >= 2) {
        c.add("Fix %s, %u sats", fixName(f.fixType), f.sats);
      } else {
        c.add("No fix, %u sats", f.sats);
      }
      c.add("Track %s", trackText(f));
      if (f.fixType >= 2) {
        coordText(f.latE7, 'N', 'S', a);
        c.add("Lat %s", a);
        coordText(f.lonE7, 'E', 'W', a);
        c.add("Lon %s", a);
      } else {
        c.add("Lat --");
        c.add("Lon --");
      }
      c.add("hAcc %u.%02u m", f.hAccCm / 100, f.hAccCm % 100);
      c.add("pDOP %u.%02u", f.pdopX100 / 100, f.pdopX100 % 100);
      c.add("First fix %u s", f.ttffS);
      c.setHint("OK back hold:restart");
      return;
    }
    case ScreenId::Calibrate:
      c.add("Profile %s", profileName(f, f.menu.activeProfile));
      c.add("Calibrated: %s", f.profileCalibrated ? "yes" : "no");
      c.add("RC signal: %s", f.menu.rcSignal ? "yes" : "no");
      c.setHint("OK back  hold: start");
      return;
    case ScreenId::DeleteSessions:
      c.add("%u sessions on card", f.cardSessions);
      c.add("Tracks, start lines");
      c.add("and geometries stay");
      c.setHint("OK back  hold: delete");
      return;
    case ScreenId::About:
      // The firmware and the card, then the POST of this boot check by check, as /post.log has it.
      c.add("Firmware %s", f.firmware);
      c.add("Card %uGB %uKB/s", f.cardGb, f.cardKbps);
      c.add("POST %s", f.postSummary);
      if (f.post) {
        for (uint8_t id = 0; id < kPostChecks; ++id) {
          c.add("%2u %-7s %s", id, checkInfo(id).name, postResultText(f.post->checks[id], true));
        }
      }
      c.setHint("OK: back");
      return;
    case ScreenId::Restart:
      c.add("Restarts the board");
      c.add("and runs the POST");
      c.add("again");
      c.setHint("OK back hold: restart");
      return;
    default:
      return;
  }
}

// One list in three groups — laps, GNSS, inputs — that UP/DOWN turn a page at a time.
void session(const Board& b, const Facts& f, Content& c) {
  char a[16];
  c.add("Lap %u", f.laps);
  lapText(f.lastLapMs, a);
  c.add("Last   %s", a);
  lapText(f.bestLapMs, a);
  c.add("Best   %s", a);
  deltaText(f.deltaMs, a);
  c.add("Delta  %s", f.lastLapMs ? a : "--");
  c.add("Markers %u", b.markers());
  c.add("Fix %s  %u sats", fixName(f.fixType), f.sats);
  c.add("hAcc %u.%02u m", f.hAccCm / 100, f.hAccCm % 100);
  c.add("Speed %u km/h", f.speedKmh);
  c.add("GNSS loss %lu", (unsigned long)f.gnssLoss);
  c.add("Steering %+4d %%", f.steeringPct);
  c.add("Throttle %+4d %%", f.throttlePct);
  c.setHint("OK mark  hold: stop");
}

// Three lines per step, so the live widths show with either text size; DOWN is the aux step's skip.
void calibration(const Board& b, const Facts& f, Layout layout, Content& c) {
  static const char* const kSteps[kCalibrationSteps][2] = {
      {"Sticks at neutral", ""},           {"Steer full LEFT", "and hold it"},
      {"Steer full RIGHT", "and hold it"}, {"Full throttle", "and hold it"},
      {"Full brake", "and hold it"},       {"Aux: move through", "every position"},
  };
  uint8_t step = b.calibrationStep();
  if (step >= kCalibrationSteps) {
    c.add("Endpoints captured");
    c.add("for %s", profileName(f, f.menu.activeProfile));
    c.setHint("hold OK: save");
    return;
  }
  c.add("%s", kSteps[step][0]);
  c.add("%s", kSteps[step][1]);
  c.add("St %4uus  Th %4uus", f.steeringUs, f.throttleUs);
  bool aux = step == kCalibrationSteps - 1;
  if (layout == Layout::TwoButtons) {
    c.setHint(aux ? "OK capture  NAV skip" : "OK capture");
  } else {
    c.setHint(aux ? "OK capture  DOWN skip" : "OK capture  < back");
  }
}

void content(const Board& b, const Facts& f, Layout layout, TextSize size, Content& c) {
  if (b.notice() != Refusal::None) {
    notice(b, c);
    return;
  }
  if (b.dialog() != DialogKind::None) {
    dialog(b.dialog(), f, c);
    return;
  }
  switch (b.mode()) {
    case Mode::Idle:
      idle(b, f, layout, size, c);
      return;
    case Mode::Session:
      session(b, f, c);
      return;
    case Mode::Geometry:
      c.add("Recording track");
      c.add("Distance %lu m", (unsigned long)f.geometryM);
      c.add("Points %u", f.geometryPoints);
      c.setHint("hold OK: stop+save");
      return;
    case Mode::Calibration:
      calibration(b, f, layout, c);
      return;
    case Mode::Transfer:
      c.add("USB %s", f.usbHost ? "host attached" : "no host");
      c.add("BLE %s", f.bleConnected ? "phone connected" : "advertising");
      if (f.usbHost) c.add("Eject on host first");
      c.setHint(layout == Layout::TwoButtons ? "OK: exit" : "OK or <: exit");
      return;
  }
}

// --- Layout -------------------------------------------------------------------

// The window of the content the body shows: around the cursor in a list, else the scrolled page.
void layOut(const Content& c, TextSize size, int16_t scroll, ScreenText& out) {
  uint8_t rows = bodyRows(size);
  uint8_t first = 0;
  if (c.cursor >= 0) {
    first = c.cursor < rows ? 0 : c.cursor - rows + 1;
  } else if (c.count > rows) {
    int pages = (c.count + rows - 1) / rows;
    int page = ((scroll % pages) + pages) % pages;
    first = page * rows;
  }
  uint8_t base = statusRows(size);
  for (uint8_t i = 0; i < rows; ++i) {
    uint8_t line = first + i;
    snprintf(out.lines[base + i], sizeof out.lines[base + i], "%s",
             line < c.count ? c.lines[line] : "");
  }
  snprintf(out.lines[base + rows], sizeof out.lines[base + rows], "%s", c.hint);
  if (c.count > rows) {
    out.scrollTotal = c.count;
    out.scrollFirst = first;
  }
  out.overflow += c.overflow;
}

// --- The POST (§3.3) --------------------------------------------------------------

// "ok", "FAIL", "--", or the detail a check gave; in the list of failures the detail of a failure
// too, since that list is the only place it is seen before IDLE.
const char* postResultText(const CheckReport& c, bool failDetail) {
  switch (c.outcome) {
    case Outcome::Running:
      return "...";
    case Outcome::Pass:
      return c.detail[0] ? c.detail : "ok";
    case Outcome::Warn:
      return c.detail[0] ? c.detail : "warn";
    case Outcome::Fail:
      return failDetail && c.detail[0] ? c.detail : "FAIL";
    case Outcome::Skipped:
      return "--";
    default:
      return "";
  }
}

void postLine(ScreenText& out, uint8_t row, const PostResult& r, uint8_t id, bool failDetail) {
  put(out, row, "%2u %-7s %s", id, checkInfo(id).name, postResultText(r.checks[id], failDetail));
}

// What to do about each fatal check, two lines under its reason.
void fatalAdvice(uint8_t id, const char*& first, const char*& second) {
  if (id == 3) {
    first = "Insert a FAT32 card,";
    second = "then hold OK";
  } else {
    first = "The board's PSRAM";
    second = "did not answer";
  }
}

}  // namespace

void renderPost(const Post& post, TextSize size, ScreenText& out) {
  for (auto& line : out.lines) line[0] = '\0';
  out.count = screenLines(size);
  out.scrollTotal = out.scrollFirst = 0;
  out.overflow = 0;
  const uint8_t last = out.count - 1, body = out.count - 2;  // title, body rows, hint
  const PostResult& r = post.result();

  switch (post.phase()) {
    case Post::Phase::Running: {
      // The checks so far, the running one last.
      uint8_t upTo = post.current() < kPostChecks ? post.current() : kPostChecks - 1;
      uint8_t first = upTo + 1 > body ? upTo + 1 - body : 0;
      put(out, 0, "Self-test");
      for (uint8_t id = first, row = 1; id <= upTo; ++id, ++row) postLine(out, row, r, id, false);
      put(out, last, "check %u of %u", upTo + 1, kPostChecks);
      return;
    }
    case Post::Phase::Fatal: {
      uint8_t id = r.fatalCheck() >= 0 ? r.fatalCheck() : post.current();
      const char *first, *second;
      fatalAdvice(id, first, second);
      put(out, 0, "FATAL %u %s", id, checkInfo(id).name);
      put(out, 1, "%s", r.checks[id].detail[0] ? r.checks[id].detail : "failed");
      put(out, 2, "%s", first);
      put(out, 3, "%s", second);
      put(out, last, "hold OK: retry");
      return;
    }
    case Post::Phase::Listing:
    case Post::Phase::Done: {
      uint8_t ids[kPostChecks];
      uint8_t n = post.listed(ids);
      put(out, 0, "Self-test: %u issue%s", n, n == 1 ? "" : "s");
      for (uint8_t i = 0; i < n && i < body; ++i) {
        if (i == body - 1 && n > body) {
          put(out, 1 + i, "+%u more", n - i);
          break;
        }
        postLine(out, 1 + i, r, ids[i], true);
      }
      put(out, last, "starting...");
      return;
    }
  }
}

void render(const Board& board, const Facts& facts, Layout layout, TextSize size, uint32_t nowMs,
            ScreenText& out) {
  for (auto& line : out.lines) line[0] = '\0';
  out.count = screenLines(size);
  out.scrollTotal = 0;
  out.scrollFirst = 0;
  out.overflow = 0;
  statusBar(board, facts, size, nowMs, out);

  Content c;
  content(board, facts, layout, size, c);
  layOut(c, size, board.scroll(), out);
}

}  // namespace ui
