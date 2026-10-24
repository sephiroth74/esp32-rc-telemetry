#include "rc_capture.h"

#include <Arduino.h>
#include <driver/mcpwm_cap.h>
#include <esp_timer.h>
#include <string.h>

#include "config.h"
#include "logger.h"
#include "pins.h"
#include "rcl.h"

namespace {

constexpr uint8_t kSamplesPerRecord = 5;  // an epoch's, at 50 Hz

// One input: the interrupt's edge state, then what the rest reads, under `lock`. No internal
// pull-down: the car's 100 kΩ is outside (§2.5), and with it in parallel a 3.3 V receiver's high,
// through the 10 kΩ in series, would sink to the pin's threshold.
struct Channel {
  uint8_t gpio;
  mcpwm_cap_channel_handle_t handle = nullptr;
  uint32_t riseTicks = 0, periodTicks = 0;
  bool seenRise = false, risen = false, periodKnown = false;
  uint16_t minUs = RC_PULSE_MIN_US, maxUs = RC_PULSE_MAX_US;
  uint16_t widthUs = 0;
  int64_t lastValidUs = 0;  // esp_timer_get_time(); 0 for never
  uint32_t pulses = 0, rejects = 0;
};

Channel channels[2] = {{PIN_RC_STEERING}, {PIN_RC_THROTTLE}};
mcpwm_cap_timer_handle_t timer = nullptr;
uint32_t ticksPerUs = 80;
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
bool started = false;

// A pulse is the time from a rising edge to the next falling one; the frame period, from one rising
// edge to the next. The first pulse after a gap has no period yet, and goes with the rejects.
bool IRAM_ATTR onEdge(mcpwm_cap_channel_handle_t, const mcpwm_capture_event_data_t* e, void* arg) {
  Channel& c = *static_cast<Channel*>(arg);
  if (e->cap_edge == MCPWM_CAP_EDGE_POS) {
    if (c.seenRise) {
      c.periodTicks = e->cap_value - c.riseTicks;
      c.periodKnown = true;
    }
    c.riseTicks = e->cap_value;
    c.seenRise = c.risen = true;
    return false;
  }
  if (!c.risen) return false;
  c.risen = false;
  uint32_t width = (e->cap_value - c.riseTicks) / ticksPerUs;
  uint32_t period = c.periodKnown ? c.periodTicks / ticksPerUs : 0;
  portENTER_CRITICAL_ISR(&lock);
  if (width >= c.minUs && width <= c.maxUs && period >= RC_FRAME_MIN_US && period <= RC_FRAME_MAX_US) {
    c.widthUs = width;
    c.lastValidUs = esp_timer_get_time();
    ++c.pulses;
  } else {
    ++c.rejects;
  }
  portEXIT_CRITICAL_ISR(&lock);
  return false;
}

// The 50 Hz sampler, on the esp_timer task: the latest widths, five to a record, while a session is
// open; outside one it only starts the next record afresh.
uint16_t batch[kSamplesPerRecord][3];
uint8_t batched = 0;
uint32_t batchStartMs = 0;

void sample(void*) {
  if (!logActive()) {
    batched = 0;
    return;
  }
  RcLive live;
  rcRead(live);
  if (batched == 0) batchStartMs = millis();
  for (uint8_t i = 0; i < 3; ++i) batch[batched][i] = live.widthUs[i];
  if (++batched < kSamplesPerRecord) return;
  batched = 0;
  rcl::RcSamples r;
  r.count = kSamplesPerRecord;
  memcpy(r.samples, batch, sizeof batch);
  uint8_t payload[2 + kSamplesPerRecord * 6];
  size_t n = rcl::encode(r, payload, sizeof payload);
  if (n) logAppend(rcl::Type::RcSamples, batchStartMs, payload, n);
}

bool channelBegin(Channel& c) {
  mcpwm_capture_channel_config_t config = {};
  config.gpio_num = c.gpio;
  config.prescale = 1;
  config.flags.pos_edge = true;
  config.flags.neg_edge = true;
  if (mcpwm_new_capture_channel(timer, &config, &c.handle) != ESP_OK) return false;
  mcpwm_capture_event_callbacks_t callbacks = {};
  callbacks.on_cap = onEdge;
  return mcpwm_capture_channel_register_event_callbacks(c.handle, &callbacks, &c) == ESP_OK &&
         mcpwm_capture_channel_enable(c.handle) == ESP_OK;
}

}  // namespace

bool rcBegin() {
  if (started) return true;
  mcpwm_capture_timer_config_t timerConfig = {};
  timerConfig.group_id = 0;
  timerConfig.clk_src = MCPWM_CAPTURE_CLK_SRC_DEFAULT;
  if (mcpwm_new_capture_timer(&timerConfig, &timer) != ESP_OK) return false;
  uint32_t hz = 0;
  if (mcpwm_capture_timer_get_resolution(timer, &hz) != ESP_OK || hz < 1000000) return false;
  ticksPerUs = hz / 1000000;
  for (Channel& c : channels) {
    if (!channelBegin(c)) return false;
  }
  if (mcpwm_capture_timer_enable(timer) != ESP_OK || mcpwm_capture_timer_start(timer) != ESP_OK) {
    return false;
  }
  esp_timer_create_args_t args = {};
  args.callback = sample;
  args.dispatch_method = ESP_TIMER_TASK;
  args.name = "rc";
  esp_timer_handle_t sampler = nullptr;
  if (esp_timer_create(&args, &sampler) != ESP_OK ||
      esp_timer_start_periodic(sampler, RC_SAMPLE_PERIOD_US) != ESP_OK) {
    return false;
  }
  started = true;
  return true;
}

void rcRead(RcLive& out) {
  out = RcLive();
  int64_t now = esp_timer_get_time();
  int64_t newest = 0;
  portENTER_CRITICAL(&lock);
  for (uint8_t i = 0; i < 2; ++i) {
    const Channel& c = channels[i];
    out.present[i] = c.pulses > 0;
    out.pulses[i] = c.pulses;
    out.rejects[i] = c.rejects;
    bool fresh = c.lastValidUs && now - c.lastValidUs < int64_t(RC_STALE_MS) * 1000;
    out.widthUs[i] = fresh ? c.widthUs : 0;
    if (c.lastValidUs > newest) newest = c.lastValidUs;
  }
  portEXIT_CRITICAL(&lock);
  out.lastPulseMs = uint32_t(newest / 1000);  // millis() runs on the same clock
}

void rcSetWindow(RcInput input, uint16_t minUs, uint16_t maxUs) {
  if (input == RcInput::Aux) return;
  portENTER_CRITICAL(&lock);
  channels[static_cast<uint8_t>(input)].minUs = minUs;
  channels[static_cast<uint8_t>(input)].maxUs = maxUs;
  portEXIT_CRITICAL(&lock);
}
