#include "gnss_task.h"

#include <Arduino.h>
#include <esp_timer.h>
#include <freertos/idf_additions.h>
#include <string.h>

#include <atomic>

#include "config.h"
#include "gnss_config.h"
#include "gnss_link.h"
#include "ui_fsm.h"

namespace {

constexpr uint32_t kStackBytes = 4096;

TaskHandle_t task = nullptr;
QueueHandle_t frames = nullptr;
std::atomic<bool> logging{false};

// The UART's own errors arrive on HardwareSerial's event task: counted there, read here.
std::atomic<uint32_t> uartOverruns{0}, uartErrors{0};

portMUX_TYPE liveLock = portMUX_INITIALIZER_UNLOCKED;
GnssLive live;  // under liveLock

ubx::Scanner scanner;
GnssFrame staged;  // the frame on its way into the queue: too big for the stack

void onUartError(hardwareSerial_error_t error) {
  if (error == UART_FIFO_OVF_ERROR || error == UART_BUFFER_FULL_ERROR) {
    uartOverruns.fetch_add(1, std::memory_order_relaxed);
  } else if (error != UART_NO_ERROR) {
    uartErrors.fetch_add(1, std::memory_order_relaxed);
  }
}

void enqueue(const ubx::Frame& f, int64_t rxUs, uint32_t& queueFull) {
  if (!logging.load(std::memory_order_relaxed)) return;
  staged.rxUs = rxUs;
  staged.size = static_cast<uint16_t>(f.size);
  memcpy(staged.bytes, f.bytes, f.size);
  if (xQueueSend(frames, &staged, 0) != pdTRUE) ++queueFull;
}

void handle(const ubx::Frame& f, int64_t rxUs, uint32_t nowMs, uint32_t& queueFull) {
  enqueue(f, rxUs, queueFull);
  ubx::NavPvt pvt;
  ubx::NavSat sat;
  if (ubx::decodeNavPvt(f, pvt)) {
    taskENTER_CRITICAL(&liveLock);
    live.pvt = pvt;
    live.havePvt = true;
    live.pvtAtMs = nowMs;
    ++live.epochs;
    if (!live.firstFixMs && pvt.fix3d()) live.firstFixMs = nowMs;
    taskEXIT_CRITICAL(&liveLock);
  } else if (ubx::decodeNavSat(f, sat)) {
    uint8_t tracked = 0, used = 0;
    for (uint8_t i = 0; i < sat.numSvs; ++i) {
      ubx::NavSatSv sv = sat.sv(i);
      if (sv.cnoDbHz) ++tracked;
      if (sv.used()) ++used;
    }
    taskENTER_CRITICAL(&liveLock);
    live.haveSat = true;
    live.satsTracked = tracked;
    live.satsUsed = used;
    taskEXIT_CRITICAL(&liveLock);
  }
}

// Check 9 unanswered at the POST: tried again from here, between reads. The exchange reads the
// UART itself, so whatever this task's scanner held is stale afterwards.
void retryConfig(uint32_t& lastTryMs) {
  uint32_t now = millis();
  if (!gnssConfigPending() || now - lastTryMs < GNSS_CFG_RETRY_MS) return;
  lastTryMs = now;
  ui::CheckReport report = gnssConfigure();
  scanner.reset();
  Serial.printf("GNSS config retried: %s\n", report.detail);
  gnssConfigLog(Serial);
  uiPostUpdate(9, report);
}

// A line on the console every GNSS_BENCH_LOG_MS: the rate, the fix and the losses, for the bench.
void benchLog(uint32_t nowMs, uint32_t& lastMs, uint32_t& lastEpochs) {
  if (nowMs - lastMs < GNSS_BENCH_LOG_MS) return;
  GnssLive l;
  gnssLiveRead(l);
  Serial.printf("[%7lu] gnss: %lu epochs in %lu s, fix %u%s, %u sv used, %u seen, hAcc %lu mm, "
                "lost %lu\n",
                (unsigned long)nowMs, (unsigned long)(l.epochs - lastEpochs),
                (unsigned long)((nowMs - lastMs) / 1000), static_cast<unsigned>(l.pvt.fixType),
                l.pvt.fix3d() ? " ok" : "", l.pvt.numSv, l.satsTracked,
                (unsigned long)l.pvt.hAccMm, (unsigned long)l.loss.total());
  lastMs = nowMs;
  lastEpochs = l.epochs;
}

void gnssTask(void*) {
  HardwareSerial& port = gnssSerial();
  port.setTimeout(GNSS_READ_TIMEOUT_MS);
  uint8_t buf[256];
  uint32_t lastTryMs = millis(), lastLogMs = millis(), lastEpochs = 0;
  uint32_t queueFull = 0;
  for (;;) {
    retryConfig(lastTryMs);
    // Back with a full buffer, or with what came within the timeout: about 11 ms of bytes at
    // 230400 baud, so a frame waits no longer than that.
    size_t n = port.readBytes(buf, sizeof buf);
    uint32_t nowMs = millis();
    int64_t rxUs = esp_timer_get_time();
    for (size_t at = 0; at < n;) {
      at += scanner.push(buf + at, n - at);
      ubx::Frame f;
      while (scanner.next(f)) handle(f, rxUs, nowMs, queueFull);
    }
    const ubx::Stats& s = scanner.stats();
    taskENTER_CRITICAL(&liveLock);
    if (n) live.lastByteMs = nowMs;
    live.loss.uartOverruns = uartOverruns.load(std::memory_order_relaxed);
    live.loss.uartErrors = uartErrors.load(std::memory_order_relaxed);
    live.loss.badChecksum = s.badChecksum;
    live.loss.oversize = s.oversize;
    live.loss.queueFull = queueFull;
    taskEXIT_CRITICAL(&liveLock);
    benchLog(nowMs, lastLogMs, lastEpochs);
  }
}

}  // namespace

void gnssTaskStart() {
  if (task || !gnssLinkBaud()) return;
  // In PSRAM: 32 frames of up to 1 KB each, read and written by tasks only, never from an ISR.
  frames = xQueueCreateWithCaps(GNSS_FRAME_QUEUE_DEPTH, sizeof(GnssFrame), MALLOC_CAP_SPIRAM);
  if (!frames) {
    Serial.println("gnss: no memory for the frame queue");
    return;
  }
  live = GnssLive();
  live.startMs = millis();
  gnssSerial().onReceiveError(onUartError);
  xTaskCreatePinnedToCore(gnssTask, "gnss", kStackBytes, nullptr, TASK_GNSS_PRIO, &task,
                          TASK_GNSS_CORE);
}

bool gnssTaskRunning() { return task != nullptr; }

void gnssLiveRead(GnssLive& out) {
  taskENTER_CRITICAL(&liveLock);
  out = live;
  taskEXIT_CRITICAL(&liveLock);
}

void gnssLogFrames(bool on) {
  if (!on && frames) xQueueReset(frames);  // what nobody will read is not kept
  logging.store(on, std::memory_order_relaxed);
}

bool gnssNextFrame(GnssFrame& out, uint32_t waitMs) {
  return frames && xQueueReceive(frames, &out, pdMS_TO_TICKS(waitMs)) == pdTRUE;
}
