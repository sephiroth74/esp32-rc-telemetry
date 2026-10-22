// The logger's ring and its write statistics — PLAN-ESP32.md §4.4, ROADMAP.md step 5.3.
//
// Ring: the PSRAM buffer between the tasks that produce records and the one that writes them to the
// card. Every producer appends whole records — a record goes in entire or not at all, and one that
// finds no room is counted, never half-written — and the writer takes the bytes in order, as long a
// run as lies unbroken in memory, writes them, then releases them. Its 256 KB absorb a card that
// stalls for over a minute (§4.4). Nothing here locks: on the device each call is made under the
// logger's mutex, while the card write itself happens outside it, on bytes no producer touches
// until they are released.
//
// LatencyStats: the write times the session footer reports — the longest, the 99th percentile,
// and the writes over the slow limit (§4.4) — from a fixed histogram, so that a session of hours
// costs no more memory than one of seconds. The percentile is the upper edge of its bucket: exact
// to 1 ms below 32 ms, to 8 ms below 256, to 64 ms above.
//
// Framework-free (§6.1): host-tested as the device runs it.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace logbuf {

class Ring {
 public:
  Ring(uint8_t* storage, size_t size) : buf_(storage), size_(size) {}

  // Producers: the `n` bytes of a whole record, or nothing when they do not fit (counted).
  bool append(const uint8_t* data, size_t n);

  // The writer: the longest unbroken run of waiting bytes, up to `max`, from the oldest. Stays
  // valid, and untouched by append(), until release().
  size_t peek(const uint8_t*& data, size_t max) const;
  void release(size_t n);  // n ≤ what peek() gave
  void clear();            // a session's end: nothing kept for the next
  void resetHighWater() { highWater_ = used_; }  // a session's start: its own high water

  size_t used() const { return used_; }
  size_t free() const { return size_ - used_; }
  size_t highWater() const { return highWater_; }
  uint32_t dropped() const { return dropped_; }  // records that found no room
  uint32_t droppedBytes() const { return droppedBytes_; }

 private:
  uint8_t* buf_;
  size_t size_;
  size_t head_ = 0;  // where the next byte goes
  size_t tail_ = 0;  // the oldest waiting byte
  size_t used_ = 0;
  size_t highWater_ = 0;
  uint32_t dropped_ = 0, droppedBytes_ = 0;
};

class LatencyStats {
 public:
  explicit LatencyStats(uint32_t slowMs) : slowMs_(slowMs) {}

  void add(uint32_t ms);
  void reset();

  uint32_t count() const { return count_; }
  uint32_t slow() const { return slow_; }  // over slowMs
  uint32_t maxMs() const { return max_; }
  uint32_t p99Ms() const;  // 0 before the first write

 private:
  static constexpr uint8_t kBuckets = 32 + 28 + 28 + 1;  // 1 ms to 32, 8 ms to 256, 64 ms to 2048
  static uint8_t bucketOf(uint32_t ms);
  static uint32_t upperOf(uint8_t bucket);

  uint32_t slowMs_;
  uint32_t buckets_[kBuckets] = {};
  uint32_t count_ = 0, slow_ = 0, max_ = 0;
};

}  // namespace logbuf
