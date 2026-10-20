#include "config_db.h"

namespace ubx {
namespace {

void put32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = (v >> (8 * i)) & 0xFF;
}

uint32_t get32(const uint8_t* p) {
  return p[0] | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

// The payload of either message: a 4-byte head, then each key, followed by its value for VALSET.
size_t encodeConfig(uint8_t id, const uint8_t (&head)[4], bool values, const KeyValue* items,
                    size_t n, uint8_t* out, size_t capacity) {
  if (n > kMaxKeys) return 0;
  uint8_t payload[4 + kMaxKeys * 8];
  size_t at = 0;
  for (uint8_t b : head) payload[at++] = b;
  for (size_t i = 0; i < n; ++i) {
    uint8_t size = valueSize(items[i].key);
    if (!size || size > 4) return 0;
    put32(payload + at, items[i].key);
    at += 4;
    for (uint8_t k = 0; values && k < size; ++k) payload[at++] = (items[i].value >> (8 * k)) & 0xFF;
  }
  return encode(kClsCfg, id, payload, static_cast<uint16_t>(at), out, capacity);
}

}  // namespace

uint8_t valueSize(uint32_t key) {
  switch ((key >> 28) & 0x07) {
    case 1:  // one bit, in a byte
    case 2:
      return 1;
    case 3:
      return 2;
    case 4:
      return 4;
    case 5:
      return 8;
    default:
      return 0;
  }
}

size_t encodeValset(uint8_t layers, const KeyValue* items, size_t n, uint8_t* out,
                    size_t capacity) {
  const uint8_t head[4] = {0x00, layers, 0x00, 0x00};  // version 0: applied at once, no transaction
  return encodeConfig(kIdValset, head, true, items, n, out, capacity);
}

size_t encodeValget(Layer layer, const KeyValue* items, size_t n, uint8_t* out, size_t capacity) {
  const uint8_t head[4] = {0x00, static_cast<uint8_t>(layer), 0x00, 0x00};  // v0, from the first key
  return encodeConfig(kIdValget, head, false, items, n, out, capacity);
}

bool decodeValget(const uint8_t* payload, size_t length, KeyValue* items, size_t n,
                  uint64_t& found) {
  found = 0;
  if (length < 4 || payload[0] != 0x01) return false;  // a response is version 1
  size_t at = 4;
  while (at < length) {
    if (length - at < 4) return false;
    uint32_t key = get32(payload + at);
    uint8_t size = valueSize(key);
    at += 4;
    if (!size || length - at < size) return false;
    uint32_t value = 0;
    for (uint8_t k = 0; k < size && k < 4; ++k) value |= uint32_t(payload[at + k]) << (8 * k);
    at += size;
    for (size_t i = 0; i < n && i < 64; ++i) {
      if (items[i].key != key) continue;
      items[i].value = value;
      found |= uint64_t(1) << i;
    }
  }
  return true;
}

int8_t ackOf(const Frame& frame, uint8_t cls, uint8_t id) {
  if (frame.cls != kClsAck || frame.length != 2) return 0;
  if (frame.payload[0] != cls || frame.payload[1] != id) return 0;
  if (frame.id == kIdAck) return 1;
  if (frame.id == kIdNak) return -1;
  return 0;
}

}  // namespace ubx
