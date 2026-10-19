#include "post.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <stdio.h>

#include "card.h"
#include "config.h"
#include "display.h"
#include "gnss_link.h"
#include "i2c_bus.h"
#include "keys.h"
#include "log_ring.h"
#include "settings.h"
#include "ui_screens.h"

namespace {

// Until check 2 loads config.json (step 3.6), the POST is drawn in the default text size.
constexpr ui::TextSize kTextSize =
    DISPLAY_TEXT_SIZE_DEFAULT == 0 ? ui::TextSize::Small : ui::TextSize::Large;

// partitions.csv names it littlefs (§6.3); the Arduino default would look for "spiffs".
constexpr const char* kLittleFsLabel = "littlefs";

// 0: first, since the display is the only way to report anything on the device itself. The bus was
// recovered, if it needed to be, before the POST began (i2c_bus.h).
ui::CheckReport checkDisplay() {
  if (i2cBusState() == I2cBusState::Stuck) return ui::CheckReport::fail("SDA stuck");
  if (!displayBegin()) return ui::CheckReport::fail("absent");
  return ui::CheckReport::pass(i2cBusState() == I2cBusState::Freed ? "bus freed" : "");
}

// 1: the ring buffer of §4.4, without which a stalling card would drop samples.
ui::CheckReport checkPsram() {
  if (!psramFound()) return ui::CheckReport::fail("not found");
  if (!logRingBegin()) return ui::CheckReport::fail("no 256K");
  char detail[ui::kCheckDetailMax + 1];
  snprintf(detail, sizeof detail, "%luMB", (unsigned long)(ESP.getPsramSize() >> 20));
  return ui::CheckReport::pass(detail);
}

// 2: LittleFS, for the copy of config.json that step 3.6 loads here. The first boot finds the
// partition blank and formats it; nothing on it is the only copy of anything (§3.8, §6.3).
ui::CheckReport checkLittleFs() {
  // The settings come with it either way: from its copy of config.json, or from the defaults.
  struct Load {
    ~Load() { settingsLoadLocal(); }
  } load;
  if (LittleFS.begin(/*formatOnFail=*/false, "/littlefs", 10, kLittleFsLabel)) {
    char detail[ui::kCheckDetailMax + 1];
    snprintf(detail, sizeof detail, "%luK used", (unsigned long)(LittleFS.usedBytes() / 1024));
    return ui::CheckReport::pass(detail);
  }
  if (LittleFS.begin(/*formatOnFail=*/true, "/littlefs", 10, kLittleFsLabel)) {
    return ui::CheckReport::pass("formatted");
  }
  return ui::CheckReport::fail("no mount");
}

// 3: fatal, since a device that cannot log has nothing to do; the cause is named, as the fix
// differs: insert a card, or reformat it FAT32 on MBR (§2.3).
ui::CheckReport checkCard() {
  switch (cardMount()) {
    case CardMount::Ok:
      break;
    case CardMount::NoCard:
      return ui::CheckReport::fail("no card");
    case CardMount::Exfat:
      return ui::CheckReport::fail("exFAT");
    case CardMount::Gpt:
      return ui::CheckReport::fail("GPT");
    case CardMount::NoFat32:
      return ui::CheckReport::fail("no FAT32");
  }
  settingsLoadCard();  // the card's config.json, applied and mirrored (§3.8)
  char detail[ui::kCheckDetailMax + 1];
  snprintf(detail, sizeof detail, "%luGB", (unsigned long)(cardTotalMb() / 1024));
  return ui::CheckReport::pass(detail);
}

// 4: before the probe, so that a card with no room reports full rather than not writable.
ui::CheckReport checkFreeSpace() {
  uint32_t mb = cardFreeMb();
  char detail[ui::kCheckDetailMax + 1];
  if (mb < 1024) {
    snprintf(detail, sizeof detail, "%luMB", (unsigned long)mb);
  } else {
    snprintf(detail, sizeof detail, "%lu.%luGB", (unsigned long)(mb / 1024),
             (unsigned long)(mb % 1024 * 10 / 1024));
  }
  if (mb < settings().minFreeMb) return ui::CheckReport::fail(detail);  // sd.min_free_mb
  if (mb < settings().warnFreeMb) return ui::CheckReport::warn(detail);
  return ui::CheckReport::pass(detail);
}

// 5: the probe; skipped on a card too full to take it.
ui::CheckReport checkWritable() {
  if (cardFreeMb() < 1) return ui::CheckReport::skipped("full");
  CardProbe probe = cardProbe(POST_PROBE_BYTES);
  return probe.failure ? ui::CheckReport::fail(probe.failure) : ui::CheckReport::pass();
}

// 6: the same probe, timed — one write, two answers.
ui::CheckReport checkSpeed() {
  const CardProbe& probe = cardLastProbe();
  if (probe.failure || !probe.writeKbps) return ui::CheckReport::skipped();
  char detail[ui::kCheckDetailMax + 1];
  snprintf(detail, sizeof detail, "%luKB/s", (unsigned long)probe.writeKbps);
  if (probe.writeKbps < POST_SLOW_CARD_KBPS) return ui::CheckReport::fail(detail);
  return ui::CheckReport::pass(detail);
}

// 8: the auto-baud scan of §5.2 — this is that scan, not a second one. Without a link a session
// would hold no position, so logging is refused, while Transfer stays reachable.
ui::CheckReport checkGnssLink() {
  uint32_t baud = gnssLinkScan();
  if (!baud) return ui::CheckReport::fail("no reply");
  char detail[ui::kCheckDetailMax + 1];
  snprintf(detail, sizeof detail, "%lu bd", (unsigned long)baud);
  return ui::CheckReport::pass(detail);
}

ui::CheckReport runCheck(uint8_t id) {
  switch (id) {
    case 0:
      return checkDisplay();
    case 1:
      return checkPsram();
    case 2:
      return checkLittleFs();
    case 3:
      return checkCard();
    case 4:
      return checkFreeSpace();
    case 5:
      return checkWritable();
    case 6:
      return checkSpeed();
    case 8:
      return checkGnssLink();
    default:
      return ui::CheckReport::skipped();
  }
}

const char* outcomeName(ui::Outcome outcome) {
  switch (outcome) {
    case ui::Outcome::Pass:
      return "ok";
    case ui::Outcome::Warn:
      return "warn";
    case ui::Outcome::Fail:
      return "FAIL";
    case ui::Outcome::Skipped:
      return "--";
    default:
      return "?";
  }
}

// /post.log on LittleFS, rewritten every boot: the same lines as the POST screen, with the reasons,
// for a phone or a computer to read later (§3.3, "Where the result goes"). Nothing without check 2.
void writeLog(const ui::PostResult& r) {
  if (r.checks[2].outcome != ui::Outcome::Pass) return;
  File f = LittleFS.open("/post.log", FILE_WRITE);
  if (!f) return;
  char summary[17];
  ui::postSummary(r, summary);
  f.printf("POST of firmware %s, %lu ms after boot\n", FW_VERSION, (unsigned long)millis());
  f.printf("result: %s, failed mask 0x%03X\n", summary, r.failedMask());
  for (uint8_t id = 0; id < ui::kPostChecks; ++id) {
    const ui::CheckReport& c = r.checks[id];
    f.printf("%2u %-7s %-4s %s\n", id, ui::checkInfo(id).name, outcomeName(c.outcome), c.detail);
  }
  // The settings in force, and every key of the card's config.json that fell back to its default.
  const cfg::Problems& p = settingsProblems();
  f.printf("config: %s", settingsSourceName());
  if (p.count) f.printf(", %u %s at the default:", p.count, p.count == 1 ? "key" : "keys");
  for (uint8_t i = 0; i < p.count; ++i) f.printf(" %s", p.keys[i]);
  f.printf("\n");
  f.close();
}

void show(const ui::Post& post, uint32_t nowMs) {
  ui::ScreenText text;
  ui::renderPost(post, kTextSize, text);
  displayShow(text);
  displayFlush(nowMs);  // now: the next check may block for seconds
}

// The keys go on being sampled between checks, so that one held at the end is known as held and
// can be swallowed; the POST itself acts only on a hold of OK in the fatal latch.
void pollKeys(ui::Post& post, uint32_t nowMs) {
  keysTick(nowMs);
  ui::Event event;
  while (keysNext(event)) post.handle(event, nowMs);
}

}  // namespace

void postEchoLog() {
  File f = LittleFS.open("/post.log", FILE_READ);
  if (!f) {
    Serial.println("post.log: none");
    return;
  }
  while (f.available()) {
    String line = f.readStringUntil('\n');
    Serial.printf("post.log: %s\n", line.c_str());
  }
  f.close();
}

ui::PostResult postRun() {
  ui::Post post(DISPLAY_POST_DEGRADED_MS);
  displaySetTextSize(kTextSize);
  ui::Post::Phase shown = ui::Post::Phase::Done;

  for (;;) {
    uint32_t now = millis();
    pollKeys(post, now);
    switch (post.phase()) {
      case ui::Post::Phase::Running: {
        uint8_t id = post.current();
        post.start();
        show(post, now);  // nothing before check 0 has brought the display up
        ui::CheckReport report = runCheck(id);
        Serial.printf("POST %2u %-7s %-4s %s\n", id, ui::checkInfo(id).name,
                      outcomeName(report.outcome), report.detail);
        post.finish(report, millis());
        break;
      }
      case ui::Post::Phase::Fatal:
      case ui::Post::Phase::Listing:
        if (post.phase() != shown) {
          shown = post.phase();
          show(post, now);
          if (shown == ui::Post::Phase::Fatal) Serial.println("POST fatal: hold OK to retry");
        }
        post.tick(now);
        vTaskDelay(pdMS_TO_TICKS(1000 / KEY_SAMPLE_HZ));
        break;
      case ui::Post::Phase::Done:
        keysSwallow();
        writeLog(post.result());
        return post.result();
    }
    if (post.phase() == ui::Post::Phase::Running) shown = ui::Post::Phase::Done;  // a retry
  }
}
