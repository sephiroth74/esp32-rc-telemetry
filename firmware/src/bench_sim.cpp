#ifdef BENCH_SIM

#include "bench_sim.h"

#include <Arduino.h>
#include <string.h>

#include "rc_capture.h"

namespace {

// The bench car's KR-331, timed by the MCPWM capture (docs/hardware.md, bench session 5).
constexpr uint16_t kSteerLeft = 1054, kSteerNeutral = 1500, kSteerRight = 1936;
constexpr uint16_t kBrake = 1002, kThrottleNeutral = 1498, kFullThrottle = 1956;

char line[48];
uint8_t used = 0;
constexpr uint8_t kQueue = 16;
ui::Event events[kQueue];
uint8_t head = 0, count = 0;
bool screenWanted = false;
bool hostHidden = false;

// A key as the keys give it: a Press, then the gesture's own event.
void queue(ui::Event e) {
  if (count + 2 > kQueue) return;
  events[(head + count++) % kQueue] = ui::Event::Press;
  events[(head + count++) % kQueue] = e;
}

void run(const char* cmd) {
  unsigned steer = 0, throttle = 0;
  char word[16] = "";
  if (sscanf(cmd, "rc %u %u", &steer, &throttle) == 2) {
    rcSimulate(steer, throttle);
  } else if (sscanf(cmd, "pos %15s", word) == 1) {
    struct Pos {
      const char* name;
      uint16_t steer, throttle;
    };
    static const Pos kPos[] = {{"neutral", kSteerNeutral, kThrottleNeutral},
                               {"left", kSteerLeft, kThrottleNeutral},
                               {"right", kSteerRight, kThrottleNeutral},
                               {"throttle", kSteerNeutral, kFullThrottle},
                               {"brake", kSteerNeutral, kBrake},
                               {"off", 0, 0}};
    const Pos* p = nullptr;
    for (const Pos& k : kPos) {
      if (strcmp(word, k.name) == 0) p = &k;
    }
    if (!p) {
      Serial.printf("sim: no position %s\n", word);
      return;
    }
    rcSimulate(p->steer, p->throttle);
    steer = p->steer;
    throttle = p->throttle;
  } else if (sscanf(cmd, "key %15s", word) == 1) {
    if (strcmp(word, "ok") == 0) {
      queue(ui::Event::Open);
    } else if (strcmp(word, "hold") == 0) {
      queue(ui::Event::Hold);
    } else if (strcmp(word, "nav") == 0) {
      queue(ui::Event::Down);
    } else if (strcmp(word, "back") == 0) {
      queue(ui::Event::Back);
    } else {
      Serial.printf("sim: no key %s\n", word);
    }
    return;
  } else if (strcmp(cmd, "screen") == 0) {
    screenWanted = true;
    return;
  } else if (strcmp(cmd, "reset") == 0) {
    Serial.println("sim: reset, as the car switched off");
    Serial.flush();
    esp_restart();
  } else if (sscanf(cmd, "usb %15s", word) == 1 && (!strcmp(word, "on") || !strcmp(word, "off"))) {
    hostHidden = strcmp(word, "off") == 0;
    Serial.printf("sim: USB host %s for sleep\n", hostHidden ? "hidden" : "seen");
    return;
  } else {
    Serial.printf("sim: what is \"%s\"?\n", cmd);
    return;
  }
  Serial.printf("sim: receiver at steering %u us, throttle %u us\n", steer, throttle);
}

}  // namespace

void benchSimPoll() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (used < sizeof line - 1) line[used++] = c;
      continue;
    }
    line[used] = '\0';
    used = 0;
    if (line[0]) run(line);
  }
}

bool benchSimNextEvent(ui::Event& event) {
  if (!count) return false;
  event = events[head];
  head = (head + 1) % kQueue;
  --count;
  return true;
}

void benchSimSwallow() {
  while (count && events[head] != ui::Event::Press) {
    head = (head + 1) % kQueue;
    --count;
  }
}

bool benchSimHostHidden() { return hostHidden; }

bool benchSimTakeScreenRequest() {
  bool wanted = screenWanted;
  screenWanted = false;
  return wanted;
}

#endif  // BENCH_SIM
