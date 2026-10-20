#include "gnss_config.h"

#include <Arduino.h>
#include <stdio.h>

#include "config.h"
#include "config_db.h"
#include "gnss_link.h"
#include "ubx.h"

namespace {

struct Setting {
  const char* name;  // as the interface description spells it, for /post.log
  uint32_t key;
  uint32_t value;
};

// §5.2, key by key. The baud rate last: it is switched on its own, after the rest.
const Setting kSettings[] = {
    {"CFG-RATE-MEAS", 0x30210001, GNSS_NAV_PERIOD_MS},  // ms between measurements: 10 Hz
    {"CFG-RATE-NAV", 0x30210002, 1},                     // a solution for every measurement
    {"CFG-MSGOUT-UBX_NAV_PVT_UART1", 0x20910007, 1},     // with every solution
    {"CFG-MSGOUT-UBX_NAV_SAT_UART1", 0x20910016, GNSS_SAT_RATE_DIVISOR},
    {"CFG-UART1OUTPROT-UBX", 0x10740001, 1},
    {"CFG-UART1OUTPROT-NMEA", 0x10740002, 0},
    {"CFG-NAVSPG-DYNMODEL", 0x20110021, GNSS_DYNMODEL},
    {"CFG-SIGNAL-SBAS_ENA", 0x10310020, 1},  // EGNOS, in Europe
    {"CFG-SIGNAL-SBAS_L1CA_ENA", 0x10310005, 1},
    {"CFG-SBAS-USE_DIFFCORR", 0x10360004, 1},
    {"CFG-UART1-BAUDRATE", 0x40520001, GNSS_BAUD},
};
constexpr size_t kCount = sizeof kSettings / sizeof kSettings[0];
constexpr size_t kBaud = kCount - 1;
constexpr uint16_t kAll = (1u << kCount) - 1;
static_assert(kCount <= 16, "a bit per setting in a uint16_t");

enum class Flash : uint8_t { NotTried, Ack, Nak, NoAnswer };

// What a layer other than RAM holds of the settings, read back once they are in force: whether the
// battery-backed RAM keeps them, and whether there is any flash to keep them in.
struct Held {
  int8_t answer = 0;  // as valget(): 1, -1 refused, 0 nothing
  uint8_t keys = 0;   // the settings the layer holds a value for
  uint8_t right = 0;  // ...and the value is the one wanted
  uint16_t mask = 0;  // which, a bit per setting
};

// The last attempt, for gnssConfigLog().
struct Attempt {
  const char* outcome = "not run";
  uint32_t was[kCount] = {};  // what the module ran on, for the settings it answered for
  uint16_t changed = 0;       // a bit per setting
  uint16_t unknown = 0;       // keys the module refused to read
  Flash flash = Flash::NotTried;
  Held bbr, flashHeld;
  bool pending = false;  // unanswered: try again
} last;

ubx::Scanner scanner;
uint8_t frame[ubx::kHeaderBytes + 4 + kCount * 8 + ubx::kChecksumBytes];

// Sends the `size` bytes of `frame`, then reads frames until `answer` returns other than 0: its
// value, or 0 after GNSS_CFG_ANSWER_MS. What came before the request is no answer to it.
template <typename Answer>
int8_t exchange(size_t size, Answer answer) {
  HardwareSerial& port = gnssSerial();
  while (port.available()) port.read();
  scanner.reset();
  port.write(frame, size);
  uint8_t buf[256];
  uint32_t start = millis();
  while (millis() - start < GNSS_CFG_ANSWER_MS) {
    size_t n = port.available();
    if (!n) {
      delay(2);
      continue;
    }
    n = port.readBytes(buf, n < sizeof buf ? n : sizeof buf);
    for (size_t at = 0; at < n;) {
      at += scanner.push(buf + at, n - at);
      ubx::Frame f;
      while (scanner.next(f)) {
        if (int8_t r = answer(f)) return r;
      }
    }
  }
  return 0;
}

// The settings of `mask`, in order, as key-value pairs; how many, and which setting each one is.
size_t select(uint16_t mask, ubx::KeyValue (&items)[kCount], uint8_t (&index)[kCount]) {
  size_t n = 0;
  for (uint8_t i = 0; i < kCount; ++i) {
    if (!(mask >> i & 1)) continue;
    items[n] = {kSettings[i].key, kSettings[i].value};
    index[n++] = i;
  }
  return n;
}

// The values `layer` holds for the settings of `mask`, into `values`; `answered` gets the settings
// the response held. 1 answered, -1 refused, 0 nothing.
int8_t valget(ubx::Layer layer, uint16_t mask, uint32_t (&values)[kCount], uint16_t& answered) {
  ubx::KeyValue items[kCount];
  uint8_t index[kCount];
  size_t n = select(mask, items, index);
  size_t size = ubx::encodeValget(layer, items, n, frame, sizeof frame);
  uint64_t found = 0;
  int8_t r = exchange(size, [&](const ubx::Frame& f) -> int8_t {
    if (f.cls == ubx::kClsCfg && f.id == ubx::kIdValget) {
      return ubx::decodeValget(f.payload, f.length, items, n, found) ? 1 : 0;
    }
    return ubx::ackOf(f, ubx::kClsCfg, ubx::kIdValget) < 0 ? -1 : 0;
  });
  answered = 0;
  for (size_t k = 0; r > 0 && k < n; ++k) {
    if (!(found >> k & 1)) continue;
    values[index[k]] = items[k].value;
    answered |= 1u << index[k];
  }
  return r;
}

// The settings of `mask` into `layers`: 1 ACK, -1 NAK, 0 nothing.
int8_t valset(uint8_t layers, uint16_t mask) {
  ubx::KeyValue items[kCount];
  uint8_t index[kCount];
  size_t n = select(mask, items, index);
  size_t size = ubx::encodeValset(layers, items, n, frame, sizeof frame);
  return exchange(size, [](const ubx::Frame& f) {
    return ubx::ackOf(f, ubx::kClsCfg, ubx::kIdValset);
  });
}

Held held(ubx::Layer layer, uint16_t known) {
  Held h;
  uint32_t values[kCount] = {};
  uint16_t answered = 0;
  h.answer = valget(layer, known, values, answered);
  for (uint8_t i = 0; i < kCount; ++i) {
    if (!(answered >> i & 1)) continue;
    ++h.keys;
    if (values[i] == kSettings[i].value) ++h.right;
    h.mask |= 1u << i;
  }
  return h;
}

// A layer that holds only some of the settings names them: in BBR, the ones a boot had to change.
void logHeld(Print& out, const char* layer, const Held& h) {
  if (h.answer > 0) {
    out.printf("gnss:   %s holds %u of %u keys, %u as set", layer, h.keys, (unsigned)kCount,
               h.right);
    for (uint8_t i = 0; h.keys < kCount && i < kCount; ++i) {
      if (h.mask >> i & 1) out.printf("%s %s", i == __builtin_ctz(h.mask) ? ":" : ",",
                                      kSettings[i].name);
    }
    out.printf("\n");
  } else {
    out.printf("gnss:   %s %s\n", layer, h.answer < 0 ? "NAK" : "no answer");
  }
}

ui::CheckReport unanswered() {
  last.outcome = "no answer";
  last.pending = true;
  return ui::CheckReport::fail("no answer");
}

const char* flashName(Flash f) {
  switch (f) {
    case Flash::Ack:
      return "ACK";
    case Flash::Nak:
      return "NAK";
    case Flash::NoAnswer:
      return "no answer";
    default:
      return "not tried";
  }
}

}  // namespace

ui::CheckReport gnssConfigure() {
  last = Attempt();
  if (!gnssLinkBaud()) {
    last.outcome = "no link";
    return ui::CheckReport::skipped("no link");
  }

  uint16_t answered = 0;
  int8_t r = valget(ubx::Layer::Ram, kAll, last.was, answered);
  if (r == 0) return unanswered();
  if (r < 0) {  // one key the module does not know refuses them all: find it, a key at a time
    answered = 0;
    for (uint8_t i = 0; i < kCount; ++i) {
      uint16_t one = 0;
      if (valget(ubx::Layer::Ram, 1u << i, last.was, one) == 0) return unanswered();
      answered |= one;
    }
  }
  last.unknown = kAll & ~answered;
  for (uint8_t i = 0; i < kCount; ++i) {
    if (answered >> i & 1 && last.was[i] != kSettings[i].value) last.changed |= 1u << i;
  }

  uint16_t rest = last.changed & ~(1u << kBaud);
  if (rest) {
    r = valset(ubx::kLayerRam | ubx::kLayerBbr, rest);
    if (r == 0) return unanswered();
    if (r < 0) {
      last.outcome = "VALSET NAK";
      return ui::CheckReport::fail("NAK");
    }
  }
  // Opportunistic (§5.2): a module without config flash refuses, and nothing depends on it.
  if (last.changed) {
    r = valset(ubx::kLayerFlash, last.changed);
    last.flash = r > 0 ? Flash::Ack : r < 0 ? Flash::Nak : Flash::NoAnswer;
  }
  if (last.changed >> kBaud & 1) {
    // No ACK to wait for: it may come at either rate. The poll at the new one is the proof.
    ubx::KeyValue baud = {kSettings[kBaud].key, kSettings[kBaud].value};
    gnssSerial().write(frame, ubx::encodeValset(ubx::kLayerRam | ubx::kLayerBbr, &baud, 1, frame,
                                                sizeof frame));
    if (!gnssLinkSwitch(GNSS_BAUD)) {
      last.outcome = gnssLinkBaud() ? "baud kept" : "lost at the switch";
      return ui::CheckReport::fail(gnssLinkBaud() ? "no 230400" : "lost");
    }
  }

  last.bbr = held(ubx::Layer::Bbr, answered);
  last.flashHeld = held(ubx::Layer::Flash, answered);

  char detail[ui::kCheckDetailMax + 1];
  if (last.unknown) {
    last.outcome = "keys unknown";
    snprintf(detail, sizeof detail, "%d unknown", __builtin_popcount(last.unknown));
    return ui::CheckReport::warn(detail);
  }
  if (!last.changed) {
    last.outcome = "as set";
    return ui::CheckReport::pass("as set");
  }
  last.outcome = "set";
  snprintf(detail, sizeof detail, "set %d", __builtin_popcount(last.changed));
  return ui::CheckReport::pass(detail);
}

bool gnssConfigPending() { return last.pending && gnssLinkBaud(); }

void gnssConfigLog(Print& out) {
  out.printf("gnss: %s", last.outcome);
  if (last.changed) {
    out.printf(", %d of %u keys changed", __builtin_popcount(last.changed), (unsigned)kCount);
  }
  if (last.flash != Flash::NotTried) out.printf(", flash %s", flashName(last.flash));
  out.printf(", %lu bd\n", (unsigned long)gnssLinkBaud());
  for (uint8_t i = 0; i < kCount; ++i) {
    if (last.changed >> i & 1) {
      out.printf("gnss:   %s %lu > %lu\n", kSettings[i].name, (unsigned long)last.was[i],
                 (unsigned long)kSettings[i].value);
    }
    if (last.unknown >> i & 1) out.printf("gnss:   %s unknown to the module\n", kSettings[i].name);
  }
  if (last.bbr.answer || last.flashHeld.answer) {
    logHeld(out, "BBR", last.bbr);
    logHeld(out, "flash", last.flashHeld);
  }
}
