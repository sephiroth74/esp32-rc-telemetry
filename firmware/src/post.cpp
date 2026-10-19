#include "post.h"

#include <Arduino.h>

#include "config.h"
#include "display.h"
#include "keys.h"
#include "ui_screens.h"

namespace {

// Until check 2 loads config.json (step 3.6), the POST is drawn in the default text size.
constexpr ui::TextSize kTextSize =
    DISPLAY_TEXT_SIZE_DEFAULT == 0 ? ui::TextSize::Small : ui::TextSize::Large;

ui::CheckReport runCheck(uint8_t id) {
  switch (id) {
    case 0:  // first, since it is the only way to report anything on the device itself
      return displayBegin() ? ui::CheckReport::pass() : ui::CheckReport::fail("absent");
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
        return post.result();
    }
    if (post.phase() == ui::Post::Phase::Running) shown = ui::Post::Phase::Done;  // a retry
  }
}
