#ifdef BENCH_SIM

#include "bench_sim.h"

#include <Arduino.h>
#include <esp_timer.h>
#include <math.h>
#include <string.h>

#include "gnss_task.h"
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

// The car of `drive`: laps of a 250 m stadium by the fixtures' sample point, east along the bottom
// straight from it, 75 m, round a 50 m bend, back along the top straight 31.8 m north, round the
// other; 15 m/s over 25 m of the bottom straight, 10 m/s elsewhere, each lap a few % faster or
// slower. A NAV-PVT every 100 ms, a 3D fix every time.
esp_timer_handle_t driveTimer = nullptr;
double driven = 0;
uint32_t driveItow = 0, driveLap = 0;
double lapPace = 1;

void stadium(double d, double& x, double& y, double& h) {
  const double r = 50 / M_PI;
  d = fmod(d, 250.0);
  if (d < 75) {
    x = d, y = 0, h = 90;
  } else if (d < 125) {
    double a = (d - 75) / r;
    x = 75 + r * sin(a), y = r - r * cos(a), h = 90 - a * 180 / M_PI;
  } else if (d < 200) {
    x = 75 - (d - 125), y = 2 * r, h = 270;
  } else {
    double a = (d - 200) / r;
    x = -r * sin(a), y = r + r * cos(a), h = 270 - a * 180 / M_PI;
  }
}

void driveEpoch(void*) {
  constexpr int32_t kLat0 = 451234560, kLon0 = 93456780;
  constexpr double kR = 6371008.8;
  double x, y, h;
  stadium(driven, x, y, h);
  double lap = fmod(driven, 250.0);
  double v = (lap >= 25 && lap < 50 ? 15.0 : 10.0) * lapPace;
  GnssEpoch e;
  e.atMs = millis();
  e.iTowMs = driveItow;
  e.latE7 = kLat0 + static_cast<int32_t>(lround(y / kR * 180 / M_PI * 1e7));
  e.lonE7 = kLon0 + static_cast<int32_t>(
                        lround(x / (kR * cos(kLat0 * 1e-7 * M_PI / 180)) * 180 / M_PI * 1e7));
  e.gSpeedMmS = static_cast<int32_t>(lround(v * 1000));
  e.headMotE5 = static_cast<int32_t>(lround(fmod(h + 360, 360) * 1e5));
  e.fix3d = true;
  gnssInjectEpoch(e);
  driveItow += 100;
  driven += v * 0.1;
  uint32_t lapNow = static_cast<uint32_t>(driven / 250.0);
  if (lapNow != driveLap) {  // a new lap, a new pace: within ±4 %
    driveLap = lapNow;
    lapPace = 0.96 + 0.08 * ((driveLap * 7919u) % 101) / 100.0;
  }
}

void drive(bool on) {
  if (!driveTimer) {
    esp_timer_create_args_t args = {};
    args.callback = driveEpoch;
    args.dispatch_method = ESP_TIMER_TASK;
    args.name = "sim-drive";
    if (esp_timer_create(&args, &driveTimer) != ESP_OK) return;
  }
  esp_timer_stop(driveTimer);
  if (on) {
    driven = 0;
    driveItow = 302400000;
    driveLap = 0;
    lapPace = 1;
    esp_timer_start_periodic(driveTimer, 100000);
  }
}

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
  } else if (sscanf(cmd, "drive %15s", word) == 1 &&
             (!strcmp(word, "on") || !strcmp(word, "off"))) {
    drive(strcmp(word, "on") == 0);
    Serial.printf("sim: car %s\n", strcmp(word, "on") == 0 ? "driving laps" : "stopped");
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
