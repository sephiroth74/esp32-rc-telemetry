// RC receiver signal scope (ROADMAP.md step 1.5) on a Seeed XIAO ESP32-S3 Sense.
//
// What a receiver puts on a channel's signal wire: its level (3.3 V logic, 5 V logic, or the BEC
// rail itself), the pulse width and the frame period. They go into the per-car table of
// docs/hardware.md, and the level decides the front end of PLAN-ESP32.md §2.5.
//
// Wiring. The level is what is being measured and a GPIO takes 3.6 V at most, so through a divider:
//   signal (white) — 10 kΩ — D3 — 10 kΩ — GND
//   receiver − (black) — GND, connected first and disconnected last
//   receiver + (red): NOT connected. The XIAO runs from USB.
//   OLED as in tools/oled-sd-test (VCC 3V3, SDA D4, SCL D5), optional
// D3 sees half the wire voltage and the ADC reads up to about 3.1 V: 6.2 V on the wire, and 7.2 V
// before D3 passes 3.6 V. On a BEC above 7 V use 22 kΩ on top and set kTopK. The divider is for
// this measurement only (§2.5, rule 3); it also holds D3 at 0 V with the wire unplugged.
//
// The ADC is the scope. D3 is read as fast as analogReadMilliVolts() goes, about every 50 µs on
// core 3.3.11 (46 µs of it is the one-shot conversion), and that interval is the resolution of
// every width and period here; it is shown with them. Enough for the level and the shape; the
// firmware times pulses digitally, behind the buffer of §2.5. Edges are found with hysteresis on
// the wire voltage, which works at either logic level.
//
// Output for the Arduino IDE's Serial Plotter, 115200 baud, whose window is 50 points. Send a
// letter from its input box to switch:
//   v  values, the default: width_us, period_us and level_mV of a frame, every 50 ms. Untick
//      period_us and level_mV in the legend to see the width at full scale.
//   z  zoom: one pulse, from 0.5 ms before its rising edge to 2.5 ms after, as 50 points of wire_V
//   f  frame: one whole frame, from just before a rising edge, as 50 points of wire_V
//   t  text, for the Serial Monitor: a summary every second, with min and max since the last r
//   r  reset the min and max
// In the plot modes every line is data: the plotter would turn any other text into data sets.
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "pins.h"

constexpr uint8_t kPin = PIN_RC_STEERING;      // D3, the firmware's steering input
constexpr uint32_t kTopK = 10, kBottomK = 10;  // the divider, in kΩ: wire to D3, D3 to GND

// The hysteresis, on the wire: above any logic low and below any logic high, at either level.
constexpr uint32_t kRiseMv = 1500, kFallMv = 1000;
// The ADC tops out at about 3,120 mV on D3 (docs/hardware.md, key ladder): a high there is clipped.
constexpr uint32_t kClippedMv = 3050 * (kTopK + kBottomK) / kBottomK;
// A longer wait between two readings (the OLED, the serial port) loses the frame in progress.
constexpr uint32_t kGapUs = 200;
constexpr uint32_t kNoSignalUs = 100000;

constexpr uint32_t kPlotEveryMs = 50;  // 2.5 s in the plotter's 50 points
constexpr uint32_t kTraceEveryMs = 500;
constexpr uint32_t kSecondMs = 1000;
constexpr uint32_t kDrawEveryMs = 500;
constexpr uint32_t kCommandEveryMs = 20;
constexpr uint8_t kTracePoints = 50;  // one trace fills the plotter's window
constexpr uint32_t kZoomBeforeUs = 500, kZoomSpanUs = 3000;
constexpr uint32_t kIdleSpanUs = 20000;  // a trace with no edge to start from: the last 20 ms

constexpr uint8_t kOledAddr = 0x3C;

Adafruit_SSD1306 oled(128, 64, &Wire, -1);  // 4-pin modules have no reset line
bool oledOk = false;

enum class Mode : char { Values = 'v', Zoom = 'z', Frame = 'f', Text = 't' };
Mode mode = Mode::Values;

// The readings of the last few frames, which the traces are cut from.
constexpr uint16_t kRingSize = 4096;  // a 22 ms frame, the longest, down to 5 µs a reading
uint32_t ringUs[kRingSize];
uint16_t ringMv[kRingSize];
uint16_t ringHead = 0, ringCount = 0;

struct Range {
  uint32_t min = UINT32_MAX, max = 0, count = 0;
  uint64_t sum = 0;
  void add(uint32_t v) {
    if (v < min) min = v;
    if (v > max) max = v;
    sum += v;
    ++count;
  }
  uint32_t avg() const { return count ? sum / count : 0; }
};

// The averages come from the last second, the min and max from everything since the last r.
struct Stats {
  Range width, period, high, low, interval;
  uint32_t frames = 0;
  bool clipped = false;
};
Stats second, shown, total;  // shown: the last complete second

uint32_t lastUs = 0;  // the previous reading
bool high = false;
bool synced = false;       // a rising edge since the last gap: riseUs is real
bool periodKnown = false;  // and one before it: periodUs is real
uint32_t riseUs = 0, periodUs = 0;
uint32_t lastEdgeUs = 0;

// The pulse in progress: its high readings less the first and the last, which may catch an edge.
uint32_t pulseSum = 0, pulseCount = 0, pulsePending = 0;
bool pulseHasPending = false;

struct Frame {
  uint32_t widthUs, periodUs, levelMv;
} lastFrame;
bool frameReady = false;  // completed since the last plot line

bool traceArmed = false;    // waiting for a rising edge to start from
bool tracePending = false;  // started, waiting for the end of its window
uint32_t traceStartUs = 0, traceSpanUs = 0;
uint32_t lastTraceMs = 0;

bool signalPresent() { return micros() - lastEdgeUs < kNoSignalUs; }

// The average of the newest readings, for a wire that does not move.
uint32_t recentMv() {
  uint32_t sum = 0, n = 0;
  for (uint16_t i = ringHead; n < 64 && n < ringCount; ++n) {
    i = (i + kRingSize - 1) % kRingSize;
    sum += ringMv[i];
  }
  return n ? sum / n : 0;
}

void addPulseReading(uint32_t mv) {
  if (pulseHasPending) {
    pulseSum += pulsePending;
    ++pulseCount;
  }
  pulsePending = mv;
  pulseHasPending = true;
}

void onRise(uint32_t edgeUs) {
  bool wasSynced = synced;
  if (synced) {
    periodUs = edgeUs - riseUs;
    periodKnown = true;
  }
  riseUs = edgeUs;
  synced = true;
  pulseSum = pulseCount = 0;
  pulseHasPending = false;

  // Only after an unbroken frame, so that the ring holds the readings before the edge too.
  if (!traceArmed || !wasSynced || (mode == Mode::Frame && !periodKnown)) return;
  traceArmed = false;
  tracePending = true;
  if (mode == Mode::Zoom) {
    traceStartUs = edgeUs - kZoomBeforeUs;
    traceSpanUs = kZoomSpanUs;
  } else {
    traceStartUs = edgeUs - periodUs / 20;
    traceSpanUs = periodUs;
  }
}

void onFall(uint32_t edgeUs) {
  if (!synced) return;
  uint32_t width = edgeUs - riseUs;
  uint32_t level = pulseCount ? pulseSum / pulseCount : pulseHasPending ? pulsePending : 0;
  for (Stats* s : {&second, &total}) {
    s->width.add(width);
    if (level) s->high.add(level);
    if (periodKnown) {
      s->period.add(periodUs);
      ++s->frames;
    }
  }
  if (periodKnown) {
    lastFrame = {width, periodUs, level};
    frameReady = true;
  }
}

void onReading(uint32_t nowUs, uint32_t mv) {
  ringUs[ringHead] = nowUs;
  ringMv[ringHead] = mv;
  ringHead = (ringHead + 1) % kRingSize;
  if (ringCount < kRingSize) ++ringCount;

  uint32_t prevUs = lastUs;
  lastUs = nowUs;
  if (nowUs - prevUs > kGapUs) {
    // A pulse in progress is not timed: everything waits for the next rising edge.
    synced = periodKnown = false;
    high = mv >= kRiseMv;
    return;
  }
  second.interval.add(nowUs - prevUs);
  total.interval.add(nowUs - prevUs);
  if (mv >= kClippedMv) second.clipped = total.clipped = true;

  uint32_t edgeUs = prevUs + (nowUs - prevUs) / 2;  // somewhere between the two readings
  if (!high) {
    if (mv >= kRiseMv) {
      high = true;
      lastEdgeUs = nowUs;
      onRise(edgeUs);
    } else {
      second.low.add(mv);
      total.low.add(mv);
    }
  } else if (mv < kFallMv) {
    high = false;
    lastEdgeUs = nowUs;
    onFall(edgeUs);
  } else {
    addPulseReading(mv);
  }
}

// One trace: the window cut into kTracePoints bins, each the average of its readings. A bin across
// an edge shows as a step in between, and an empty one repeats the bin before.
void emitTrace(uint32_t startUs, uint32_t spanUs) {
  uint16_t i = ringHead, n = 0;
  while (n < ringCount) {  // back from the newest reading to the oldest in the window
    uint16_t before = (i + kRingSize - 1) % kRingSize;
    if (static_cast<int32_t>(ringUs[before] - startUs) < 0) break;
    i = before;
    ++n;
  }
  uint32_t mv = n ? ringMv[i] : 0;
  for (uint8_t p = 0; p < kTracePoints; ++p) {
    uint32_t endUs = startUs + spanUs * (p + 1) / kTracePoints;
    uint32_t sum = 0, count = 0;
    while (n && static_cast<int32_t>(ringUs[i] - endUs) < 0) {
      sum += ringMv[i];
      ++count;
      i = (i + 1) % kRingSize;
      --n;
    }
    if (count) mv = sum / count;
    Serial.printf("wire_V:%lu.%02lu\n", (unsigned long)(mv / 1000),
                  (unsigned long)(mv % 1000 / 10));
  }
}

void runValues() {
  static uint32_t lastPlotMs = 0;
  if (mode != Mode::Values || millis() - lastPlotMs < kPlotEveryMs) return;
  if (!signalPresent()) {
    Serial.printf("width_us:0 period_us:0 level_mV:%lu\n", (unsigned long)recentMv());
  } else if (frameReady) {  // right after a falling edge, so the next pulse is still far off
    Serial.printf("width_us:%lu period_us:%lu level_mV:%lu\n", (unsigned long)lastFrame.widthUs,
                  (unsigned long)lastFrame.periodUs, (unsigned long)lastFrame.levelMv);
    frameReady = false;
  } else {
    return;
  }
  lastPlotMs = millis();
}

void runTrace() {
  if (mode != Mode::Zoom && mode != Mode::Frame) return;
  if (tracePending) {
    if (static_cast<int32_t>(lastUs - (traceStartUs + traceSpanUs)) < 0) return;
    emitTrace(traceStartUs, traceSpanUs);
    tracePending = false;
    lastTraceMs = millis();
    return;
  }
  if (millis() - lastTraceMs < kTraceEveryMs) return;
  if (signalPresent()) {
    traceArmed = true;  // the next rising edge starts it
    return;
  }
  traceArmed = false;
  emitTrace(lastUs - kIdleSpanUs, kIdleSpanUs);
  lastTraceMs = millis();
}

const char* levelName(uint32_t mv, bool clipped) {
  if (clipped) return "clipped, 6.2 V or more";
  if (mv <= 3600) return "3.3 V logic";
  if (mv <= 5500) return "5 V logic";
  return "over 5.5 V";
}

bool inside(const Range& r, uint32_t lo, uint32_t hi) {
  return r.count && r.min >= lo && r.max <= hi;
}

void printText(const Stats& s) {
  Serial.printf("-- %lu frames/s, a reading every %lu us (longest %lu)\n", (unsigned long)s.frames,
                (unsigned long)s.interval.avg(), (unsigned long)s.interval.max);
  if (!signalPresent()) {
    Serial.printf("no edges: the wire stays at %.2f V\n", recentMv() / 1000.0f);
    return;
  }
  Serial.printf("high   %.2f V  min %.2f  max %.2f  %s\n", s.high.avg() / 1000.0f,
                total.high.min / 1000.0f, total.high.max / 1000.0f,
                levelName(s.high.avg(), total.clipped));
  Serial.printf("low    %.2f V\n", s.low.avg() / 1000.0f);
  Serial.printf("width  %5lu us  min %5lu  max %5lu  %s config.h's %u-%u\n",
                (unsigned long)s.width.avg(), (unsigned long)total.width.min,
                (unsigned long)total.width.max,
                inside(total.width, RC_PULSE_MIN_US, RC_PULSE_MAX_US) ? "inside" : "OUTSIDE",
                RC_PULSE_MIN_US, RC_PULSE_MAX_US);
  uint32_t period = s.period.avg();
  Serial.printf("period %5lu us  min %5lu  max %5lu  %lu Hz  %s config.h's %u-%u\n",
                (unsigned long)period, (unsigned long)total.period.min,
                (unsigned long)total.period.max, (unsigned long)(period ? 1000000 / period : 0),
                inside(total.period, RC_FRAME_MIN_US, RC_FRAME_MAX_US) ? "inside" : "OUTSIDE",
                RC_FRAME_MIN_US, RC_FRAME_MAX_US);
}

void printHelp() {
  Serial.println("RC scope on D3: v values, z zoom, f frame (Serial Plotter), t text, r reset");
}

void readCommands() {
  while (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case 'v':
      case 'z':
      case 'f':
      case 't':
        mode = static_cast<Mode>(c);
        traceArmed = tracePending = false;
        if (mode == Mode::Text) printHelp();
        break;
      case 'r':
        total = Stats();
        break;
      default:  // the line ending the plotter's input box adds
        break;
    }
  }
}

void draw() {
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextWrap(false);
  oled.setCursor(0, 0);
  oled.printf("RC scope D3  mode %c", static_cast<char>(mode));
  if (!signalPresent()) {
    oled.setCursor(0, 8);
    oled.print("NO EDGES");
    oled.setCursor(0, 16);
    oled.printf("wire %.2f V", recentMv() / 1000.0f);
  } else {
    oled.setCursor(0, 8);
    oled.printf("high %.2f V", shown.high.avg() / 1000.0f);
    oled.setCursor(0, 16);
    oled.print(levelName(shown.high.avg(), total.clipped));
    oled.setCursor(0, 24);
    oled.printf("width  %5lu us", (unsigned long)shown.width.avg());
    oled.setCursor(0, 32);
    oled.printf("  %lu..%lu us", (unsigned long)total.width.min, (unsigned long)total.width.max);
    oled.setCursor(0, 40);
    oled.printf("period %5lu us", (unsigned long)shown.period.avg());
  }
  oled.setCursor(0, 48);
  oled.printf("reading every %lu us", (unsigned long)shown.interval.avg());
  oled.setCursor(0, 56);
  oled.print("send v z f t r");
  oled.display();
}

void setup() {
  Serial.begin(115200);  // no banner: in the plot modes every line is data

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.beginTransmission(kOledAddr);  // begin() succeeds whether or not anything answers
  oledOk = Wire.endTransmission() == 0 &&
           oled.begin(SSD1306_SWITCHCAPVCC, kOledAddr, /*reset=*/true, /*periphBegin=*/false);

  lastUs = micros();
  lastEdgeUs = lastUs - kNoSignalUs;
}

void loop() {
  uint32_t mv = analogReadMilliVolts(kPin) * (kTopK + kBottomK) / kBottomK;
  onReading(micros(), mv);
  runValues();
  runTrace();

  uint32_t nowMs = millis();
  static uint32_t lastCommandMs = 0, lastSecondMs = 0, lastDrawMs = 0;
  if (nowMs - lastCommandMs >= kCommandEveryMs) {
    lastCommandMs = nowMs;
    readCommands();
  }
  if (nowMs - lastSecondMs >= kSecondMs) {
    lastSecondMs = nowMs;
    shown = second;
    second = Stats();
    if (mode == Mode::Text) printText(shown);
  }
  // The panel takes ~25 ms of bus time, a frame or two lost at every draw: never during a trace.
  if (oledOk && !tracePending && nowMs - lastDrawMs >= kDrawEveryMs) {
    lastDrawMs = nowMs;
    draw();
  }
}
