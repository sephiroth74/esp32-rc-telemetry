#include "ubx.h"

#include <string.h>

namespace ubx {

void checksum(const uint8_t* data, size_t n, uint8_t& a, uint8_t& b) {
  a = b = 0;
  for (size_t i = 0; i < n; ++i) {
    a += data[i];
    b += a;
  }
}

size_t encode(uint8_t cls, uint8_t id, const uint8_t* payload, uint16_t length, uint8_t* out,
              size_t capacity) {
  size_t size = kHeaderBytes + length + kChecksumBytes;
  if (size > capacity) return 0;
  out[0] = kSync1;
  out[1] = kSync2;
  out[2] = cls;
  out[3] = id;
  out[4] = length & 0xFF;
  out[5] = length >> 8;
  if (length) memcpy(out + kHeaderBytes, payload, length);
  checksum(out + 2, 4 + length, out[size - 2], out[size - 1]);
  return size;
}

void Scanner::drop(size_t n) {
  memmove(buf_, buf_ + n, count_ - n);
  count_ -= n;
}

void Scanner::skip(size_t n) {
  stats_.skipped += n;
  drop(n);
}

size_t Scanner::push(const uint8_t* data, size_t n) {
  drop(consumed_);
  consumed_ = 0;
  size_t room = sizeof buf_ - count_;
  size_t take = n < room ? n : room;
  memcpy(buf_ + count_, data, take);
  count_ += take;
  return take;
}

bool Scanner::next(Frame& frame) {
  drop(consumed_);
  consumed_ = 0;
  for (;;) {
    // Nothing before a possible sync can belong to a frame.
    size_t i = 0;
    while (i < count_ && buf_[i] != kSync1) ++i;
    if (i) skip(i);
    if (count_ < 2) return false;
    if (buf_[1] != kSync2) {
      skip(1);
      continue;
    }
    if (count_ < kHeaderBytes) return false;
    uint16_t length = buf_[4] | buf_[5] << 8;
    if (length > kMaxPayload) {
      ++stats_.oversize;
      skip(1);  // the search goes on from the byte after this sync
      continue;
    }
    size_t size = kHeaderBytes + length + kChecksumBytes;
    if (count_ < size) return false;  // below kMaxFrame, so push() has room for the rest
    uint8_t a, b;
    checksum(buf_ + 2, 4 + length, a, b);
    if (a != buf_[size - 2] || b != buf_[size - 1]) {
      ++stats_.badChecksum;
      skip(1);
      continue;
    }
    frame.cls = buf_[2];
    frame.id = buf_[3];
    frame.length = length;
    frame.payload = buf_ + kHeaderBytes;
    frame.bytes = buf_;
    frame.size = size;
    consumed_ = size;
    ++stats_.frames;
    return true;
  }
}

}  // namespace ubx
