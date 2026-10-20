// The M10's configuration database over UBX — PLAN-ESP32.md §5.2, ROADMAP.md step 4.1.
//
// The M10 generation is configured key by key: UBX-CFG-VALSET writes values into the layers asked
// for (RAM, the battery-backed RAM, flash), UBX-CFG-VALGET reads them from one, and UBX-ACK-ACK or
// ACK-NAK answers. A key is 32 bits; bits 28–30 give the size of its value, so a response is read
// without knowing the keys in advance. A key the module does not know refuses the whole message.
//
// Framework-free, like the rest of lib/ubx (§6.1); the exchange itself is the firmware's.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ubx.h"

namespace ubx {

constexpr uint8_t kClsAck = 0x05, kIdNak = 0x00, kIdAck = 0x01;
constexpr uint8_t kClsCfg = 0x06, kIdValset = 0x8A, kIdValget = 0x8B;
constexpr uint8_t kClsMon = 0x0A, kIdMonVer = 0x04;

// VALSET's layers, a bitmask.
constexpr uint8_t kLayerRam = 0x01, kLayerBbr = 0x02, kLayerFlash = 0x04;
// VALGET's layer, one at a time: RAM is what the module runs on now.
enum class Layer : uint8_t { Ram = 0, Bbr = 1, Flash = 2, Default = 7 };

// Up to 64 keys a message, by the interface description.
constexpr size_t kMaxKeys = 64;

struct KeyValue {
  uint32_t key;
  uint32_t value;  // every key the firmware sets fits in 4 bytes
};

// The bytes of a key's value: 1 for a bit (sent as a byte) or a byte, then 2, 4, 8; 0 for a size
// the interface reserves.
uint8_t valueSize(uint32_t key);

// Whole frames, sync to checksum, into `out`; their size, or 0 when they do not fit or a key is of a
// reserved size.
size_t encodeValset(uint8_t layers, const KeyValue* items, size_t n, uint8_t* out, size_t capacity);
size_t encodeValget(Layer layer, const KeyValue* items, size_t n, uint8_t* out, size_t capacity);

// A VALGET response's payload: the value of each of `items`' keys it holds goes into that item, and
// `found` gets a bit per item filled (item 0 in bit 0). A layer other than RAM may hold only some
// of the keys asked for. False for a payload that is not a response, or whose last value runs past
// its end.
bool decodeValget(const uint8_t* payload, size_t length, KeyValue* items, size_t n,
                  uint64_t& found);

// What `frame` says about a message of class `cls`, id `id`: 1 ACK, -1 NAK, 0 nothing.
int8_t ackOf(const Frame& frame, uint8_t cls, uint8_t id);

}  // namespace ubx
