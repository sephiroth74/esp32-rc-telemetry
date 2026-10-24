#include "logger.h"

#include <Arduino.h>
#include <esp_random.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <new>

#include "config.h"
#include "gnss_task.h"
#include "log_ring.h"
#include "logbuf.h"
#include "rc_capture.h"

namespace {

constexpr uint32_t kStackBytes = 6144;
constexpr uint32_t kTickMs = 20;  // how often the task looks at the ring when nothing wakes it
constexpr size_t kScratch = rcl::kRecordOverhead + 2048;  // the longest record a producer sends

enum class State : uint8_t { Idle, Opening, Open, Closing, Failed };

SemaphoreHandle_t lock = nullptr;  // the ring, the writer and the counters below
SemaphoreHandle_t closed = nullptr;
TaskHandle_t task = nullptr;
logbuf::Ring* ring = nullptr;
volatile State state = State::Idle;

// Under `lock`.
rcl::Writer writer(0);
uint32_t startMs = 0;
uint32_t records = 0, ubxFrames = 0;
uint32_t droppedAtOpen = 0;
uint32_t rcRejectsAtOpen[3] = {};
uint8_t scratch[kScratch];

// The logger task's own.
char path[96];
int fd = -1;
uint64_t written = 0, allocated = 0;
uint32_t lastFlushMs = 0;
logbuf::LatencyStats latency(LOG_SLOW_WRITE_MS);
rcl::Close closeReason = rcl::Close::ByHand;
GnssLoss lossAtOpen;
rcl::Footer lastFooter;
bool haveFooter = false;

void fail(const char* what) {
  Serial.printf("log: %s failed (errno %d), %s closed\n", what, errno, path);
  if (fd >= 0) close(fd);
  fd = -1;
  state = State::Failed;
  xSemaphoreGive(closed);  // a close waiting on it has nothing more to wait for
}

// The next extent, ahead of need: seeking past the end and writing its last byte makes FAT
// allocate the clusters now, without writing what lies between.
bool extend() {
  uint32_t t0 = millis();
  uint64_t to = allocated + LOG_EXTENT_BYTES;
  if (lseek(fd, to - 1, SEEK_SET) < 0 || ::write(fd, "", 1) != 1 ||
      lseek(fd, written, SEEK_SET) < 0) {
    fail("extend");
    return false;
  }
  allocated = to;
  Serial.printf("log: extent to %lu MB in %lu ms\n", (unsigned long)(allocated >> 20),
                (unsigned long)(millis() - t0));
  return true;
}

void openFile() {
  fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    fail("open");
    return;
  }
  written = allocated = 0;
  latency.reset();
  if (!extend()) return;
  lastFlushMs = millis();
  state = State::Open;
  Serial.printf("log: %s open\n", path);
}

// Writes what waits: whenever a whole chunk is ready, and all of it when the flush is due or
// `all`; then the fsync that makes it survive a power cut.
void drain(bool all) {
  uint32_t now = millis();
  bool flushDue = all || now - lastFlushMs >= LOG_FLUSH_INTERVAL_MS;
  for (;;) {
    const uint8_t* p;
    xSemaphoreTake(lock, portMAX_DELAY);
    size_t used = ring->used();
    size_t n = ring->peek(p, LOG_WRITE_CHUNK);
    xSemaphoreGive(lock);
    if (!n || (!flushDue && used < LOG_WRITE_CHUNK)) break;
    if (allocated - written < LOG_EXTENT_REFILL_BYTES + n && !extend()) return;
    uint32_t t0 = millis();
    ssize_t w = ::write(fd, p, n);  // outside the lock: no producer touches these bytes
    latency.add(millis() - t0);
    if (w != static_cast<ssize_t>(n)) {
      fail("write");
      return;
    }
    written += n;
    xSemaphoreTake(lock, portMAX_DELAY);
    ring->release(n);
    xSemaphoreGive(lock);
  }
  if (flushDue) {
    if (fsync(fd) != 0) {
      fail("fsync");
      return;
    }
    lastFlushMs = now;
  }
}

void finish() {
  if (fd < 0) openFile();  // closed before the task had opened it: a short file, but a whole one
  if (state == State::Failed) return;
  drain(/*all=*/true);
  if (state == State::Failed) return;
  GnssLive gnss;
  gnssLiveRead(gnss);
  rcl::Footer f;
  f.close = static_cast<uint8_t>(closeReason);
  f.uartOverruns = gnss.loss.uartOverruns - lossAtOpen.uartOverruns;
  f.uartErrors = gnss.loss.uartErrors - lossAtOpen.uartErrors;
  f.ubxBadChecksums = gnss.loss.badChecksum - lossAtOpen.badChecksum;
  f.ubxOversize = gnss.loss.oversize - lossAtOpen.oversize;
  RcLive rc;
  rcRead(rc);
  for (uint8_t i = 0; i < 3; ++i) f.rcRejects[i] = rc.rejects[i] - rcRejectsAtOpen[i];
  f.writes = latency.count();
  f.slowWrites = latency.slow();
  f.writeMaxMs = latency.maxMs();
  f.writeP99Ms = latency.p99Ms();
  uint8_t payload[rcl::kFooterBytes];
  xSemaphoreTake(lock, portMAX_DELAY);
  f.ubxFrames = ubxFrames;
  f.records = records;
  f.ringOverflow = ring->dropped() - droppedAtOpen;
  f.ringHighWater = ring->highWater();
  size_t n = rcl::encode(f, payload, sizeof payload);
  size_t r = writer.record(rcl::Type::Footer, millis() - startMs, payload, n, scratch, sizeof scratch);
  ring->append(scratch, r);
  xSemaphoreGive(lock);
  drain(/*all=*/true);
  if (state == State::Failed) return;
  if (ftruncate(fd, written) != 0) {
    fail("truncate");
    return;
  }
  close(fd);
  fd = -1;
  lastFooter = f;
  haveFooter = true;
  Serial.printf("log: %s closed, %lu records, %llu bytes, writes %lu (max %lu ms, p99 %lu, "
                "slow %lu), dropped %lu\n",
                path, (unsigned long)f.records + 1, (unsigned long long)written,
                (unsigned long)f.writes, (unsigned long)f.writeMaxMs, (unsigned long)f.writeP99Ms,
                (unsigned long)f.slowWrites, (unsigned long)f.ringOverflow);
  state = State::Idle;
  xSemaphoreGive(closed);
}

void loggerTask(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kTickMs));
    switch (state) {
      case State::Opening:
        openFile();
        break;
      case State::Open:
        drain(/*all=*/false);
        break;
      case State::Closing:
        finish();
        break;
      default:
        break;
    }
  }
}

}  // namespace

bool loggerBegin() {
  if (task) return true;
  if (!logRing()) return false;
  lock = xSemaphoreCreateMutex();
  closed = xSemaphoreCreateBinary();
  ring = new (std::nothrow) logbuf::Ring(logRing(), logRingBytes());
  if (!lock || !closed || !ring) return false;
  return xTaskCreatePinnedToCore(loggerTask, "logger", kStackBytes, nullptr, TASK_LOGGER_PRIO,
                                 &task, TASK_LOGGER_CORE) == pdPASS;
}

bool logOpen(const char* filePath, const rcl::SessionHeader& header) {
  if (!task || (state != State::Idle && state != State::Failed)) return false;
  haveFooter = false;
  GnssLive gnss;
  gnssLiveRead(gnss);
  lossAtOpen = gnss.loss;
  RcLive rc;
  rcRead(rc);
  for (uint8_t i = 0; i < 3; ++i) rcRejectsAtOpen[i] = rc.rejects[i];
  snprintf(path, sizeof path, "%s", filePath);
  uint64_t nonce = uint64_t(esp_random()) << 32 | esp_random();
  uint8_t head[rcl::kFileHeaderBytes];
  rcl::writeFileHeader(nonce, head, sizeof head);
  uint8_t payload[rcl::kSessionHeaderBytes];
  size_t n = rcl::encode(header, payload, sizeof payload);

  xSemaphoreTake(lock, portMAX_DELAY);
  ring->clear();
  ring->resetHighWater();
  droppedAtOpen = ring->dropped();
  writer = rcl::Writer(nonce);
  startMs = millis();
  ring->append(head, sizeof head);
  size_t r = writer.record(rcl::Type::SessionHeader, 0, payload, n, scratch, sizeof scratch);
  ring->append(scratch, r);
  records = 1;
  ubxFrames = 0;
  xSemaphoreTake(closed, 0);  // a stale give from the last session
  state = State::Opening;
  xSemaphoreGive(lock);
  xTaskNotifyGive(task);
  return true;
}

bool logClose(rcl::Close reason, uint32_t waitMs) {
  if (state == State::Idle) return true;
  if (state == State::Failed) {
    state = State::Idle;
    return true;
  }
  closeReason = reason;
  state = State::Closing;
  xTaskNotifyGive(task);
  return xSemaphoreTake(closed, pdMS_TO_TICKS(waitMs)) == pdTRUE;
}

bool logActive() { return state == State::Open || state == State::Opening; }

bool logFailed() { return state == State::Failed; }

bool logAppend(rcl::Type type, uint32_t eventMs, const void* payload, size_t length) {
  if (!logActive()) return false;  // without the lock: the common case, outside a session
  bool ok = false;
  xSemaphoreTake(lock, portMAX_DELAY);
  if (logActive()) {
    int32_t t = static_cast<int32_t>(eventMs - startMs);  // received just before the start: 0
    size_t n = writer.record(type, t < 0 ? 0 : t, static_cast<const uint8_t*>(payload), length,
                             scratch, sizeof scratch);
    ok = n && ring->append(scratch, n);
    if (ok) {
      ++records;
      if (type == rcl::Type::Ubx) ++ubxFrames;
    }
  }
  xSemaphoreGive(lock);
  return ok;
}

bool logLastFooter(rcl::Footer& out) {
  if (!haveFooter || state != State::Idle) return false;
  out = lastFooter;
  return true;
}

void logStats(LogStats& out) {
  out = LogStats();
  if (!task) return;
  out.open = logActive();
  xSemaphoreTake(lock, portMAX_DELAY);
  out.records = records;
  out.dropped = ring->dropped() - droppedAtOpen;
  out.ringUsed = ring->used();
  xSemaphoreGive(lock);
  out.written = written;
  out.allocated = allocated;
  out.writeMaxMs = latency.maxMs();
  out.slowWrites = latency.slow();
}
