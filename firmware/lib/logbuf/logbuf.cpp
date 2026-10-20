#include "logbuf.h"

#include <string.h>

namespace logbuf {

bool Ring::append(const uint8_t* data, size_t n) {
  if (n > size_ - used_) {
    ++dropped_;
    droppedBytes_ += n;
    return false;
  }
  size_t first = size_ - head_ < n ? size_ - head_ : n;  // up to the end of the storage
  memcpy(buf_ + head_, data, first);
  memcpy(buf_, data + first, n - first);  // and the rest from its start
  head_ = (head_ + n) % size_;
  used_ += n;
  if (used_ > highWater_) highWater_ = used_;
  return true;
}

size_t Ring::peek(const uint8_t*& data, size_t max) const {
  size_t run = size_ - tail_ < used_ ? size_ - tail_ : used_;
  data = buf_ + tail_;
  return run < max ? run : max;
}

void Ring::release(size_t n) {
  if (n > used_) n = used_;
  tail_ = (tail_ + n) % size_;
  used_ -= n;
}

void Ring::clear() {
  head_ = tail_ = used_ = 0;
}

uint8_t LatencyStats::bucketOf(uint32_t ms) {
  if (ms < 32) return static_cast<uint8_t>(ms);
  if (ms < 256) return static_cast<uint8_t>(32 + (ms - 32) / 8);
  if (ms < 2048) return static_cast<uint8_t>(60 + (ms - 256) / 64);
  return kBuckets - 1;
}

uint32_t LatencyStats::upperOf(uint8_t bucket) {
  if (bucket < 32) return bucket;
  if (bucket < 60) return 32 + (bucket - 32 + 1) * 8 - 1;
  if (bucket < 88) return 256 + (bucket - 60 + 1) * 64 - 1;
  return 0;  // the open bucket: the caller uses the maximum
}

void LatencyStats::add(uint32_t ms) {
  ++buckets_[bucketOf(ms)];
  ++count_;
  if (ms > slowMs_) ++slow_;
  if (ms > max_) max_ = ms;
}

void LatencyStats::reset() {
  memset(buckets_, 0, sizeof buckets_);
  count_ = slow_ = max_ = 0;
}

uint32_t LatencyStats::p99Ms() const {
  if (!count_) return 0;
  // The smallest bucket with 99 % of the writes at or below it; rounded up, so that 100 writes
  // need 99 and a single slow one among them is the 1 % left out.
  uint64_t need = (uint64_t(count_) * 99 + 99) / 100;
  uint64_t seen = 0;
  for (uint8_t b = 0; b < kBuckets; ++b) {
    seen += buckets_[b];
    if (seen >= need) {
      uint32_t upper = b == kBuckets - 1 ? max_ : upperOf(b);
      return upper < max_ ? upper : max_;  // never above the longest write
    }
  }
  return max_;
}

}  // namespace logbuf
