// Host tests for lib/ubx — run with `make test`.
//
// The scanner against bytes the GM10 really sent (docs/fixtures/gm10-indoor-115200.hex: NMEA and
// UBX interleaved, as delivered), and against what a UART can make of them: a flipped byte, a frame
// cut short, a false sync, a length out of range, a frame arriving in pieces (PLAN-ESP32.md §5.4).
// Then the configuration messages of check 9 (§5.2): VALSET, VALGET and its response, the ACKs.
// Then the decode of NAV-PVT and NAV-SAT (step 4.2): the capture's epochs, indoors without a fix,
// frames built at the interface description's offsets for the signs of the signed fields, and a
// second capture, at a window with a fix (docs/fixtures/gm10-fix-230400.hex), checked against the
// values a Python decode of the same bytes gave.
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <initializer_list>
#include <vector>

#include "config_db.h"
#include "link_probe.h"
#include "nav.h"
#include "ubx.h"

#ifndef FIXTURES_DIR
#error "FIXTURES_DIR comes from the native_test environment of platformio.ini"
#endif

using Bytes = std::vector<uint8_t>;

// The fixture's hex bytes; '#' starts a comment.
static Bytes loadHex(const char* name) {
  char path[256];
  snprintf(path, sizeof path, "%s/%s", FIXTURES_DIR, name);
  FILE* f = fopen(path, "r");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
  Bytes out;
  char line[256];
  while (fgets(line, sizeof line, f)) {
    if (line[0] == '#') continue;
    unsigned value;
    int used;
    for (const char* p = line; sscanf(p, "%2x%n", &value, &used) == 1; p += used) {
      out.push_back(static_cast<uint8_t>(value));
      while (p[used] == ' ') ++used;
    }
  }
  fclose(f);
  return out;
}

static const Bytes& capture() {
  static Bytes bytes = loadHex("gm10-indoor-115200.hex");
  return bytes;
}

struct Seen {
  uint8_t cls, id;
  uint16_t length;
  Bytes bytes;
};

// Pushes `data` `chunk` bytes at a time, draining after each push, as the GNSS task will.
static std::vector<Seen> scan(ubx::Scanner& s, const Bytes& data, size_t chunk = 64) {
  std::vector<Seen> seen;
  size_t at = 0;
  while (at < data.size()) {
    size_t n = data.size() - at < chunk ? data.size() - at : chunk;
    at += s.push(data.data() + at, n);
    ubx::Frame f;
    while (s.next(f)) seen.push_back({f.cls, f.id, f.length, Bytes(f.bytes, f.bytes + f.size)});
  }
  return seen;
}

static Bytes slice(const Bytes& b, size_t from, size_t size) {
  return Bytes(b.begin() + from, b.begin() + from + size);
}

static Bytes join(std::initializer_list<Bytes> parts) {
  Bytes out;
  for (const Bytes& p : parts) out.insert(out.end(), p.begin(), p.end());
  return out;
}

// The six frames of the fixture, as its header lists them.
struct Expected {
  size_t offset;
  uint8_t cls, id;
  uint16_t length;
};
static const Expected kFixture[] = {
    {0, 0x0A, 0x04, 160},  {168, 0x01, 0x07, 92},  {637, 0x01, 0x35, 8},
    {653, 0x01, 0x07, 92}, {1122, 0x01, 0x07, 92}, {1591, 0x01, 0x07, 92},
};

static Bytes fixtureFrame(int i) {
  return slice(capture(), kFixture[i].offset, ubx::kHeaderBytes + kFixture[i].length + 2);
}

void setUp() {}
void tearDown() {}

// --- The capture ----------------------------------------------------------------

static void the_capture_yields_its_six_frames_unchanged() {
  ubx::Scanner s;
  std::vector<Seen> seen = scan(s, capture());
  TEST_ASSERT_EQUAL_UINT(6, seen.size());
  size_t inFrames = 0;
  for (int i = 0; i < 6; ++i) {
    TEST_ASSERT_EQUAL_HEX8(kFixture[i].cls, seen[i].cls);
    TEST_ASSERT_EQUAL_HEX8(kFixture[i].id, seen[i].id);
    TEST_ASSERT_EQUAL_UINT16(kFixture[i].length, seen[i].length);
    Bytes original = fixtureFrame(i);
    TEST_ASSERT_EQUAL_UINT(original.size(), seen[i].bytes.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(original.data(), seen[i].bytes.data(), original.size());
    inFrames += original.size();
  }
  TEST_ASSERT_EQUAL_UINT32(0, s.stats().badChecksum);
  TEST_ASSERT_EQUAL_UINT32(capture().size() - inFrames, s.stats().skipped);  // the NMEA, all of it
  TEST_ASSERT_EQUAL_UINT(0, s.buffered());
}

static void byte_by_byte_finds_the_same_frames() {
  ubx::Scanner whole, single;
  std::vector<Seen> a = scan(whole, capture(), capture().size());
  std::vector<Seen> b = scan(single, capture(), 1);
  TEST_ASSERT_EQUAL_UINT(a.size(), b.size());
  for (size_t i = 0; i < a.size(); ++i) {
    TEST_ASSERT_EQUAL_UINT(a[i].bytes.size(), b[i].bytes.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(a[i].bytes.data(), b[i].bytes.data(), a[i].bytes.size());
  }
}

// --- What a UART does to it ---------------------------------------------------

static void a_flipped_byte_costs_only_its_frame() {
  Bytes data = capture();
  data[kFixture[2].offset + ubx::kHeaderBytes + 3] ^= 0x40;  // inside NAV-SAT's payload
  ubx::Scanner s;
  std::vector<Seen> seen = scan(s, data);
  TEST_ASSERT_EQUAL_UINT(5, seen.size());
  TEST_ASSERT_EQUAL_UINT32(1, s.stats().badChecksum);
  for (const Seen& f : seen) TEST_ASSERT_FALSE(f.cls == 0x01 && f.id == 0x35);
}

static void a_frame_cut_short_does_not_hide_the_next() {
  // MON-VER loses its tail; its length then reaches into the NAV-PVT behind it.
  Bytes data = join({slice(fixtureFrame(0), 0, 100), fixtureFrame(1)});
  ubx::Scanner s;
  std::vector<Seen> seen = scan(s, data);
  TEST_ASSERT_EQUAL_UINT(1, seen.size());
  TEST_ASSERT_EQUAL_HEX8(0x07, seen[0].id);
  TEST_ASSERT_EQUAL_UINT32(1, s.stats().badChecksum);
}

static void a_false_sync_is_left_behind() {
  // A sync and a header out of nowhere, its length running into a real frame.
  Bytes bogus = {ubx::kSync1, ubx::kSync2, 0x01, 0x07, 0x10, 0x00, 0x11, 0x22};
  ubx::Scanner s;
  std::vector<Seen> seen = scan(s, join({bogus, fixtureFrame(2), fixtureFrame(3)}));
  TEST_ASSERT_EQUAL_UINT(2, seen.size());
  TEST_ASSERT_EQUAL_HEX8(0x35, seen[0].id);
  TEST_ASSERT_EQUAL_HEX8(0x07, seen[1].id);
}

static void a_length_out_of_range_is_skipped_at_once() {
  Bytes bogus = {ubx::kSync1, ubx::kSync2, 0x01, 0x07, 0xFF, 0xFF};
  ubx::Scanner s;
  std::vector<Seen> seen = scan(s, join({bogus, fixtureFrame(1)}));
  TEST_ASSERT_EQUAL_UINT(1, seen.size());
  TEST_ASSERT_EQUAL_UINT32(1, s.stats().oversize);
  TEST_ASSERT_EQUAL_UINT32(0, s.stats().badChecksum);  // not waited for, not checked
}

static void a_frame_in_pieces_waits_for_the_rest() {
  Bytes frame = fixtureFrame(1);
  ubx::Scanner s;
  ubx::Frame f;
  s.push(frame.data(), 50);
  TEST_ASSERT_FALSE(s.next(f));
  TEST_ASSERT_EQUAL_UINT(50, s.buffered());
  s.push(frame.data() + 50, frame.size() - 50);
  TEST_ASSERT_TRUE(s.next(f));
  TEST_ASSERT_EQUAL_UINT(frame.size(), f.size);
  TEST_ASSERT_FALSE(s.next(f));
}

static void a_stream_of_false_syncs_never_fills_it_up() {
  Bytes data(10000, ubx::kSync1);
  data = join({data, fixtureFrame(1)});
  ubx::Scanner s;
  std::vector<Seen> seen = scan(s, data, 512);  // scan() loops until every byte was taken
  TEST_ASSERT_EQUAL_UINT(1, seen.size());
  TEST_ASSERT_EQUAL_UINT32(10000, s.stats().skipped);
}

// --- Encoding -------------------------------------------------------------------

static void encode_builds_the_polls_of_gps_test() {
  uint8_t out[8];
  const uint8_t kNavPvt[] = {0xB5, 0x62, 0x01, 0x07, 0x00, 0x00, 0x08, 0x19};
  const uint8_t kNavSat[] = {0xB5, 0x62, 0x01, 0x35, 0x00, 0x00, 0x36, 0xA3};
  const uint8_t kMonVer[] = {0xB5, 0x62, 0x0A, 0x04, 0x00, 0x00, 0x0E, 0x34};
  TEST_ASSERT_EQUAL_UINT(8, ubx::encode(0x01, 0x07, nullptr, 0, out, sizeof out));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kNavPvt, out, 8);
  ubx::encode(0x01, 0x35, nullptr, 0, out, sizeof out);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kNavSat, out, 8);
  ubx::encode(0x0A, 0x04, nullptr, 0, out, sizeof out);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kMonVer, out, 8);
  TEST_ASSERT_EQUAL_UINT(0, ubx::encode(0x01, 0x07, nullptr, 0, out, 7));  // does not fit
}

static void an_encoded_frame_scans_back_the_same() {
  Bytes original = fixtureFrame(1);
  uint8_t out[ubx::kMaxFrame];
  size_t size = ubx::encode(original[2], original[3], original.data() + ubx::kHeaderBytes, 92, out,
                            sizeof out);
  TEST_ASSERT_EQUAL_UINT(original.size(), size);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(original.data(), out, size);  // the module's own checksum
}

// --- The link probe of POST check 8 ----------------------------------------------

static ubx::LinkProbe probeOf(const Bytes& data) {
  ubx::LinkProbe probe;
  probe.push(data.data(), data.size());
  return probe;
}

// The first NMEA sentence of the capture, "$" to the line ending. A "$" alone could be a byte of a
// UBX payload, so it takes "$G" and four capitals: "$GNRMC".
static Bytes firstSentence() {
  const Bytes& b = capture();
  for (size_t i = 0; i + 6 < b.size(); ++i) {
    bool start = b[i] == '$' && b[i + 1] == 'G';
    for (size_t k = 2; k < 6 && start; ++k) start = b[i + k] >= 'A' && b[i + k] <= 'Z';
    if (!start) continue;
    size_t end = i;
    while (end < b.size() && b[end] != '\n') ++end;
    return Bytes(b.begin() + i, b.begin() + end + 1);
  }
  return {};
}

static void the_capture_shows_a_link_both_ways() {
  ubx::LinkProbe probe = probeOf(capture());
  TEST_ASSERT_TRUE(probe.found());
  TEST_ASSERT_TRUE(probe.sawUbx());
  TEST_ASSERT_TRUE(probe.sawNmea());
}

static void one_frame_of_either_kind_is_enough() {
  TEST_ASSERT_TRUE(probeOf(fixtureFrame(1)).sawUbx());
  Bytes sentence = firstSentence();
  TEST_ASSERT_EQUAL_CHAR('$', sentence[0]);
  ubx::LinkProbe nmea = probeOf(sentence);
  TEST_ASSERT_TRUE(nmea.sawNmea());
  TEST_ASSERT_FALSE(nmea.sawUbx());
}

static void a_sentence_with_a_wrong_checksum_is_not() {
  Bytes sentence = firstSentence();
  sentence[3] ^= 0x01;  // a letter of the talker's sentence id
  TEST_ASSERT_FALSE(probeOf(sentence).found());
}

// What a rate that is not the module's makes of its bytes: noise, with a sync or a "$G" in it now
// and then — not enough without the frame and its checksum.
static void noise_with_syncs_in_it_is_not_a_link() {
  Bytes noise;
  uint32_t state = 12345;
  for (int i = 0; i < 20000; ++i) {
    state = state * 1103515245u + 12345u;
    noise.push_back(static_cast<uint8_t>(state >> 16));
    if (i % 997 == 0) noise.insert(noise.end(), {ubx::kSync1, ubx::kSync2, 0x01, 0x07});
    if (i % 1009 == 0) noise.insert(noise.end(), {'$', 'G', 'N', 'R', 'M', 'C', ','});
  }
  TEST_ASSERT_FALSE(probeOf(noise).found());
}

// --- The configuration database (check 9) -----------------------------------------

constexpr uint32_t kRateMeas = 0x30210001;  // U2
constexpr uint32_t kNmeaOut = 0x10740002;   // L
constexpr uint32_t kDynModel = 0x20110021;  // E1
constexpr uint32_t kBaud = 0x40520001;      // U4
constexpr uint32_t kPrnMask = 0x50360006;   // X8, CFG-SBAS-PRNSCANMASK

static void a_key_says_how_long_its_value_is() {
  TEST_ASSERT_EQUAL_UINT8(1, ubx::valueSize(kNmeaOut));  // a bit, sent as a byte
  TEST_ASSERT_EQUAL_UINT8(1, ubx::valueSize(kDynModel));
  TEST_ASSERT_EQUAL_UINT8(2, ubx::valueSize(kRateMeas));
  TEST_ASSERT_EQUAL_UINT8(4, ubx::valueSize(kBaud));
  TEST_ASSERT_EQUAL_UINT8(8, ubx::valueSize(kPrnMask));
  TEST_ASSERT_EQUAL_UINT8(0, ubx::valueSize(0x00000001));
  TEST_ASSERT_EQUAL_UINT8(0, ubx::valueSize(0x60000001));
}

// The frame comes back out of the scanner: its checksum holds, and so does its length.
static ubx::Frame scanned(ubx::Scanner& s, const uint8_t* bytes, size_t size) {
  s.push(bytes, size);
  ubx::Frame f;
  TEST_ASSERT_TRUE(s.next(f));
  TEST_ASSERT_EQUAL_UINT(size, f.size);
  return f;
}

static void valset_lays_out_each_value_after_its_key() {
  const ubx::KeyValue items[] = {{kRateMeas, 100}, {kNmeaOut, 0}, {kBaud, 230400}};
  uint8_t out[64];
  size_t size =
      ubx::encodeValset(ubx::kLayerRam | ubx::kLayerBbr, items, 3, out, sizeof out);
  const uint8_t payload[] = {0x00, 0x03, 0x00, 0x00,                          // v0, RAM+BBR
                             0x01, 0x00, 0x21, 0x30, 0x64, 0x00,              // 100 ms
                             0x02, 0x00, 0x74, 0x10, 0x00,                    // NMEA off
                             0x01, 0x00, 0x52, 0x40, 0x00, 0x84, 0x03, 0x00};  // 230400
  TEST_ASSERT_EQUAL_UINT(ubx::kHeaderBytes + sizeof payload + ubx::kChecksumBytes, size);
  ubx::Scanner s;
  ubx::Frame f = scanned(s, out, size);
  TEST_ASSERT_EQUAL_HEX8(ubx::kClsCfg, f.cls);
  TEST_ASSERT_EQUAL_HEX8(ubx::kIdValset, f.id);
  TEST_ASSERT_EQUAL_UINT16(sizeof payload, f.length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, f.payload, sizeof payload);
}

static void valset_refuses_what_it_cannot_send() {
  uint8_t out[64];
  const ubx::KeyValue wide[] = {{kPrnMask, 0}};  // 8 bytes: more than a KeyValue holds
  TEST_ASSERT_EQUAL_UINT(0, ubx::encodeValset(ubx::kLayerRam, wide, 1, out, sizeof out));
  const ubx::KeyValue one[] = {{kBaud, 230400}};
  // 6 of header, 4 of head, 4 of key, 4 of value, 2 of checksum.
  TEST_ASSERT_EQUAL_UINT(0, ubx::encodeValset(ubx::kLayerRam, one, 1, out, 19));
  TEST_ASSERT_EQUAL_UINT(20, ubx::encodeValset(ubx::kLayerRam, one, 1, out, 20));
  std::vector<ubx::KeyValue> many(ubx::kMaxKeys + 1, ubx::KeyValue{kNmeaOut, 0});
  uint8_t big[1024];
  TEST_ASSERT_EQUAL_UINT(0, ubx::encodeValset(ubx::kLayerRam, many.data(), many.size(), big,
                                              sizeof big));
}

static void valget_asks_one_layer_for_keys_only() {
  const ubx::KeyValue items[] = {{kNmeaOut, 1}, {kBaud, 230400}};
  uint8_t out[32];
  size_t size = ubx::encodeValget(ubx::Layer::Ram, items, 2, out, sizeof out);
  const uint8_t payload[] = {0x00, 0x00, 0x00, 0x00,  // v0, RAM, from the first key
                             0x02, 0x00, 0x74, 0x10, 0x01, 0x00, 0x52, 0x40};
  ubx::Scanner s;
  ubx::Frame f = scanned(s, out, size);
  TEST_ASSERT_EQUAL_HEX8(ubx::kIdValget, f.id);
  TEST_ASSERT_EQUAL_UINT16(sizeof payload, f.length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, f.payload, sizeof payload);
  ubx::encodeValget(ubx::Layer::Flash, items, 2, out, sizeof out);
  TEST_ASSERT_EQUAL_HEX8(0x02, out[ubx::kHeaderBytes + 1]);
}

// A response in its own order, with a key nobody asked for in the middle, 8 bytes wide.
static const uint8_t kResponse[] = {
    0x01, 0x00, 0x00, 0x00,                                      // v1, RAM
    0x01, 0x00, 0x52, 0x40, 0x00, 0xC2, 0x01, 0x00,              // 115200
    0x06, 0x00, 0x36, 0x50, 1, 2, 3, 4, 5, 6, 7, 8,              // the PRN mask
    0x02, 0x00, 0x74, 0x10, 0x01,                                // NMEA on
};

static void a_valget_response_fills_what_it_holds() {
  ubx::KeyValue items[] = {{kRateMeas, 7}, {kNmeaOut, 0}, {kBaud, 0}, {kDynModel, 9}};
  uint64_t found = 0;
  TEST_ASSERT_TRUE(ubx::decodeValget(kResponse, sizeof kResponse, items, 4, found));
  TEST_ASSERT_EQUAL_HEX64(0x6, found);
  TEST_ASSERT_EQUAL_UINT32(1, items[1].value);
  TEST_ASSERT_EQUAL_UINT32(115200, items[2].value);
  TEST_ASSERT_EQUAL_UINT32(7, items[0].value);  // not in the response: left alone
  TEST_ASSERT_EQUAL_UINT32(9, items[3].value);
}

static void a_response_cut_short_is_no_response() {
  ubx::KeyValue items[] = {{kNmeaOut, 0}};
  uint64_t found = 0;
  TEST_ASSERT_FALSE(ubx::decodeValget(kResponse, sizeof kResponse - 1, items, 1, found));
  TEST_ASSERT_FALSE(ubx::decodeValget(kResponse, 10, items, 1, found));  // inside a key
  uint8_t request[sizeof kResponse];
  memcpy(request, kResponse, sizeof request);
  request[0] = 0x00;  // version 0 is the poll, not its answer
  TEST_ASSERT_FALSE(ubx::decodeValget(request, sizeof request, items, 1, found));
  TEST_ASSERT_TRUE(ubx::decodeValget(kResponse, 4, items, 1, found));  // no keys at all
  TEST_ASSERT_EQUAL_HEX64(0, found);
}

static void an_ack_answers_only_its_own_message() {
  uint8_t out[16];
  const uint8_t valset[] = {ubx::kClsCfg, ubx::kIdValset};
  ubx::Scanner s;
  ubx::Frame ack = scanned(s, out, ubx::encode(ubx::kClsAck, ubx::kIdAck, valset, 2, out, 16));
  TEST_ASSERT_EQUAL_INT8(1, ubx::ackOf(ack, ubx::kClsCfg, ubx::kIdValset));
  TEST_ASSERT_EQUAL_INT8(0, ubx::ackOf(ack, ubx::kClsCfg, ubx::kIdValget));
  ubx::Frame nak = scanned(s, out, ubx::encode(ubx::kClsAck, ubx::kIdNak, valset, 2, out, 16));
  TEST_ASSERT_EQUAL_INT8(-1, ubx::ackOf(nak, ubx::kClsCfg, ubx::kIdValset));
  ubx::Frame poll = scanned(s, out, ubx::encode(ubx::kClsMon, ubx::kIdMonVer, nullptr, 0, out, 16));
  TEST_ASSERT_EQUAL_INT8(0, ubx::ackOf(poll, ubx::kClsCfg, ubx::kIdValset));
}

// --- NAV-PVT and NAV-SAT (step 4.2) ------------------------------------------------

static void put16(Bytes& b, size_t at, uint16_t v) {
  for (int i = 0; i < 2; ++i) b[at + i] = (v >> (8 * i)) & 0xFF;
}

static void put32(Bytes& b, size_t at, uint32_t v) {
  for (int i = 0; i < 4; ++i) b[at + i] = (v >> (8 * i)) & 0xFF;
}

// A frame built around `payload`, then taken back out of a scanner, as the GNSS task gets it.
static ubx::Frame framed(ubx::Scanner& s, uint8_t id, const Bytes& payload) {
  static uint8_t out[ubx::kMaxFrame];
  size_t size = ubx::encode(ubx::kClsNav, id, payload.data(), payload.size(), out, sizeof out);
  return scanned(s, out, size);
}

static void the_capture_holds_four_epochs_without_a_fix() {
  const int pvts[] = {1, 3, 4, 5};
  for (int k = 0; k < 4; ++k) {
    Bytes bytes = fixtureFrame(pvts[k]);
    ubx::Scanner s;
    ubx::NavPvt pvt;
    TEST_ASSERT_TRUE(ubx::decodeNavPvt(scanned(s, bytes.data(), bytes.size()), pvt));
    TEST_ASSERT_EQUAL_UINT32(31500 + 100 * k, pvt.iTowMs);  // 10 Hz
    TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(pvt.fixType));
    TEST_ASSERT_EQUAL_UINT8(0, pvt.numSv);
    TEST_ASSERT_FALSE(pvt.gnssFixOk());
    TEST_ASSERT_FALSE(pvt.fix3d());
    TEST_ASSERT_FALSE(pvt.validDate());  // 2021-03-07 is the receiver's default, not a date
    TEST_ASSERT_FALSE(pvt.validTime());
    TEST_ASSERT_EQUAL_UINT16(2021, pvt.year);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFF, pvt.hAccMm);  // no estimate at all
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFF, pvt.tAccNs);
    TEST_ASSERT_EQUAL_INT32(-17000, pvt.hMslMm);  // the geoid at 0°N 0°E
    TEST_ASSERT_EQUAL_UINT16(9999, pvt.pDopE2);
    TEST_ASSERT_EQUAL_UINT32(20000, pvt.sAccMmS);
    TEST_ASSERT_EQUAL_UINT32(18000000, pvt.headAccE5);  // 180°
  }
}

static void the_capture_holds_a_sky_without_satellites() {
  Bytes bytes = fixtureFrame(2);
  ubx::Scanner s;
  ubx::NavSat sat;
  TEST_ASSERT_TRUE(ubx::decodeNavSat(scanned(s, bytes.data(), bytes.size()), sat));
  TEST_ASSERT_EQUAL_UINT32(31500, sat.iTowMs);  // the epoch of the PVT before it
  TEST_ASSERT_EQUAL_UINT8(1, sat.version);
  TEST_ASSERT_EQUAL_UINT8(0, sat.numSvs);
}

// The capture's first epoch, given a 3D fix west of Greenwich, moving and descending.
static Bytes pvtWithFix() {
  Bytes p = slice(fixtureFrame(1), ubx::kHeaderBytes, ubx::kNavPvtLength);
  p[11] = 0x07;  // date, time, fully resolved
  p[20] = 3;
  p[21] = 0x03;  // gnssFixOK, diffSoln
  p[23] = 14;
  put32(p, 24, static_cast<uint32_t>(-93456780));  // 9.345678° W
  put32(p, 28, 451234560);
  put32(p, 40, 1200);
  put32(p, 56, static_cast<uint32_t>(-250));  // velD: climbing 0.25 m/s
  put32(p, 60, 12500);                        // 45 km/h
  put32(p, 64, 27000000);                     // 270°
  put16(p, 76, 132);
  put16(p, 88, static_cast<uint16_t>(-150));  // magDec -1.5°
  return p;
}

static void a_fix_reads_back_field_by_field() {
  ubx::Scanner s;
  ubx::NavPvt pvt;
  TEST_ASSERT_TRUE(ubx::decodeNavPvt(framed(s, ubx::kIdNavPvt, pvtWithFix()), pvt));
  TEST_ASSERT_TRUE(pvt.validDate());
  TEST_ASSERT_TRUE(pvt.validTime());
  TEST_ASSERT_EQUAL_UINT8(3, static_cast<uint8_t>(pvt.fixType));
  TEST_ASSERT_TRUE(pvt.gnssFixOk());
  TEST_ASSERT_TRUE(pvt.diffSoln());
  TEST_ASSERT_EQUAL_UINT8(14, pvt.numSv);
  TEST_ASSERT_EQUAL_INT32(-93456780, pvt.lonE7);
  TEST_ASSERT_EQUAL_INT32(451234560, pvt.latE7);
  TEST_ASSERT_EQUAL_UINT32(1200, pvt.hAccMm);
  TEST_ASSERT_EQUAL_INT32(-250, pvt.velDMmS);
  TEST_ASSERT_EQUAL_INT32(12500, pvt.gSpeedMmS);
  TEST_ASSERT_EQUAL_INT32(27000000, pvt.headMotE5);
  TEST_ASSERT_EQUAL_UINT16(132, pvt.pDopE2);
  TEST_ASSERT_EQUAL_INT16(-150, pvt.magDecE2);
  TEST_ASSERT_EQUAL_INT32(-17000, pvt.hMslMm);  // untouched fields stay the capture's
  TEST_ASSERT_TRUE(pvt.fix3d());
}

static void only_a_vouched_3d_fix_is_a_fix() {
  struct Case {
    size_t at;
    uint8_t value;
  } cases[] = {
      {20, 2},     // 2D
      {20, 5},     // time only
      {21, 0x02},  // diffSoln without gnssFixOK
      {78, 0x01},  // invalidLlh
  };
  for (const Case& c : cases) {
    Bytes p = pvtWithFix();
    p[c.at] = c.value;
    ubx::Scanner s;
    ubx::NavPvt pvt;
    TEST_ASSERT_TRUE(ubx::decodeNavPvt(framed(s, ubx::kIdNavPvt, p), pvt));
    TEST_ASSERT_FALSE(pvt.fix3d());
  }
  Bytes p = pvtWithFix();
  p[20] = 4;  // GNSS and dead reckoning: still 3D
  ubx::Scanner s;
  ubx::NavPvt pvt;
  TEST_ASSERT_TRUE(ubx::decodeNavPvt(framed(s, ubx::kIdNavPvt, p), pvt));
  TEST_ASSERT_TRUE(pvt.fix3d());
}

// Three satellites: GPS in the solution with SBAS, Galileo in it, BeiDou not tracked.
static const uint8_t kSatPayload[] = {
    0x0C, 0x7B, 0x00, 0x00, 0x01, 0x03, 0x00, 0x00,                          // 31500, v1, 3
    0x00, 12, 42, 0xFB, 0x2C, 0x01, 0xF1, 0xFF, 0x5F, 0x00, 0x01, 0x00,      // GPS 12
    0x02, 7, 38, 61, 45, 0x00, 0x03, 0x00, 0x1C, 0x00, 0x00, 0x00,           // Galileo 7
    0x03, 30, 0, 12, 0xBE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,         // BeiDou 30
};

static void each_satellite_reads_back_in_place() {
  ubx::Scanner s;
  ubx::NavSat sat;
  TEST_ASSERT_TRUE(ubx::decodeNavSat(
      framed(s, ubx::kIdNavSat, Bytes(kSatPayload, kSatPayload + sizeof kSatPayload)), sat));
  TEST_ASSERT_EQUAL_UINT8(3, sat.numSvs);

  ubx::NavSatSv gps = sat.sv(0);
  TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(gps.gnss));
  TEST_ASSERT_EQUAL_UINT8(12, gps.svId);
  TEST_ASSERT_EQUAL_UINT8(42, gps.cnoDbHz);
  TEST_ASSERT_EQUAL_INT8(-5, gps.elevDeg);
  TEST_ASSERT_EQUAL_INT16(300, gps.azimDeg);
  TEST_ASSERT_EQUAL_INT16(-15, gps.prResDm);
  TEST_ASSERT_EQUAL_UINT8(7, gps.quality());
  TEST_ASSERT_TRUE(gps.used());
  TEST_ASSERT_EQUAL_UINT8(1, gps.health());
  TEST_ASSERT_TRUE(gps.diffCorr());
  TEST_ASSERT_TRUE(gps.sbasCorrUsed());

  ubx::NavSatSv gal = sat.sv(1);
  TEST_ASSERT_EQUAL_UINT8(2, static_cast<uint8_t>(gal.gnss));
  TEST_ASSERT_EQUAL_INT16(45, gal.azimDeg);
  TEST_ASSERT_EQUAL_UINT8(4, gal.quality());
  TEST_ASSERT_TRUE(gal.used());
  TEST_ASSERT_FALSE(gal.sbasCorrUsed());

  ubx::NavSatSv bds = sat.sv(2);
  TEST_ASSERT_EQUAL_UINT8(3, static_cast<uint8_t>(bds.gnss));
  TEST_ASSERT_EQUAL_UINT8(0, bds.cnoDbHz);
  TEST_ASSERT_EQUAL_INT16(190, bds.azimDeg);
  TEST_ASSERT_FALSE(bds.used());
}

static void a_frame_of_the_wrong_shape_does_not_decode() {
  Bytes sat(kSatPayload, kSatPayload + sizeof kSatPayload);
  ubx::NavSat navSat;
  ubx::NavPvt pvt;
  {
    Bytes p = sat;
    p[5] = 4;  // one satellite more than the bytes hold
    ubx::Scanner s;
    TEST_ASSERT_FALSE(ubx::decodeNavSat(framed(s, ubx::kIdNavSat, p), navSat));
  }
  {
    Bytes p = sat;
    p[4] = 2;  // a version this layout is not for
    ubx::Scanner s;
    TEST_ASSERT_FALSE(ubx::decodeNavSat(framed(s, ubx::kIdNavSat, p), navSat));
  }
  {
    Bytes p = pvtWithFix();
    p.pop_back();  // 91 bytes
    ubx::Scanner s;
    TEST_ASSERT_FALSE(ubx::decodeNavPvt(framed(s, ubx::kIdNavPvt, p), pvt));
  }
  ubx::Scanner s;
  TEST_ASSERT_FALSE(ubx::decodeNavPvt(framed(s, ubx::kIdNavSat, sat), pvt));  // each to its own
  ubx::Scanner s2;
  TEST_ASSERT_FALSE(ubx::decodeNavSat(framed(s2, ubx::kIdNavPvt, pvtWithFix()), navSat));
  Bytes monVer = fixtureFrame(0);
  ubx::Scanner s3;
  TEST_ASSERT_FALSE(ubx::decodeNavPvt(scanned(s3, monVer.data(), monVer.size()), pvt));
}

// --- The capture with a fix ---------------------------------------------------------

static std::vector<Seen> fixFrames() {
  static Bytes bytes = loadHex("gm10-fix-230400.hex");
  ubx::Scanner s;
  std::vector<Seen> seen = scan(s, bytes);
  TEST_ASSERT_EQUAL_UINT32(0, s.stats().badChecksum);  // the moved positions' checksums hold
  TEST_ASSERT_EQUAL_UINT32(0, s.stats().skipped);      // no NMEA: check 9's configuration
  return seen;
}

static ubx::Frame asFrame(const Seen& seen) {
  ubx::Frame f;
  f.cls = seen.cls;
  f.id = seen.id;
  f.length = seen.length;
  f.bytes = seen.bytes.data();
  f.payload = seen.bytes.data() + ubx::kHeaderBytes;
  f.size = seen.bytes.size();
  return f;
}

static void a_second_at_a_window_is_nine_fixes_at_10_hz() {
  std::vector<Seen> seen = fixFrames();
  TEST_ASSERT_EQUAL_UINT(10, seen.size());
  uint32_t pvts = 0;
  for (const Seen& s : seen) {
    ubx::NavPvt pvt;
    if (!ubx::decodeNavPvt(asFrame(s), pvt)) continue;
    TEST_ASSERT_EQUAL_UINT32(375549100 + 100 * pvts, pvt.iTowMs);
    TEST_ASSERT_TRUE(pvt.fix3d());
    TEST_ASSERT_FALSE(pvt.diffSoln());  // SBAS tracked, its corrections not applied yet
    TEST_ASSERT_EQUAL_UINT8(10, pvt.numSv);
    TEST_ASSERT_TRUE(pvt.validDate());
    TEST_ASSERT_TRUE(pvt.validTime());
    TEST_ASSERT_EQUAL_UINT16(2026, pvt.year);
    TEST_ASSERT_EQUAL_UINT8(10, pvt.month);
    TEST_ASSERT_EQUAL_UINT8(22, pvt.day);
    TEST_ASSERT_TRUE(pvt.hAccMm >= 629 && pvt.hAccMm <= 635);
    TEST_ASSERT_EQUAL_UINT16(316, pvt.pDopE2);
    TEST_ASSERT_TRUE(pvt.gSpeedMmS < 100);  // standing on a sill
    if (pvts == 0) {  // moved onto the sample point (the fixture's header)
      TEST_ASSERT_EQUAL_INT32(451234560, pvt.latE7);
      TEST_ASSERT_EQUAL_INT32(93456780, pvt.lonE7);
    }
    ++pvts;
  }
  TEST_ASSERT_EQUAL_UINT32(9, pvts);
}

static void the_sky_of_that_second() {
  std::vector<Seen> seen = fixFrames();
  ubx::NavSat sat;
  TEST_ASSERT_TRUE(ubx::decodeNavSat(asFrame(seen[6]), sat));
  TEST_ASSERT_EQUAL_UINT32(375549600, sat.iTowMs);  // the epoch of the PVT before it
  TEST_ASSERT_EQUAL_UINT8(24, sat.numSvs);
  uint8_t tracked = 0, used = 0, usedBy[7] = {}, listed[7] = {};
  for (uint8_t i = 0; i < sat.numSvs; ++i) {
    ubx::NavSatSv sv = sat.sv(i);
    uint8_t g = static_cast<uint8_t>(sv.gnss);
    TEST_ASSERT_TRUE(g < 7);
    ++listed[g];
    if (sv.cnoDbHz) ++tracked;
    if (sv.used()) {
      ++used;
      ++usedBy[g];
      TEST_ASSERT_TRUE(sv.cnoDbHz > 0);  // nothing in the solution without a signal
    }
    TEST_ASSERT_FALSE(sv.sbasCorrUsed());
  }
  TEST_ASSERT_EQUAL_UINT8(17, tracked);
  TEST_ASSERT_EQUAL_UINT8(10, used);  // NAV-PVT's numSV for the same epoch
  TEST_ASSERT_EQUAL_UINT8(4, usedBy[static_cast<uint8_t>(ubx::Gnss::Gps)]);
  TEST_ASSERT_EQUAL_UINT8(3, usedBy[static_cast<uint8_t>(ubx::Gnss::Galileo)]);
  TEST_ASSERT_EQUAL_UINT8(3, usedBy[static_cast<uint8_t>(ubx::Gnss::Beidou)]);
  TEST_ASSERT_EQUAL_UINT8(4, listed[static_cast<uint8_t>(ubx::Gnss::Sbas)]);
  TEST_ASSERT_EQUAL_UINT8(0, listed[static_cast<uint8_t>(ubx::Gnss::Glonass)]);  // off on the M10
  ubx::NavSatSv first = sat.sv(0);
  TEST_ASSERT_EQUAL_UINT8(4, first.svId);  // GPS 4: listed, not heard
  TEST_ASSERT_EQUAL_UINT8(0, first.cnoDbHz);
  TEST_ASSERT_EQUAL_INT8(19, first.elevDeg);
  TEST_ASSERT_EQUAL_INT16(74, first.azimDeg);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(the_capture_yields_its_six_frames_unchanged);
  RUN_TEST(byte_by_byte_finds_the_same_frames);
  RUN_TEST(a_flipped_byte_costs_only_its_frame);
  RUN_TEST(a_frame_cut_short_does_not_hide_the_next);
  RUN_TEST(a_false_sync_is_left_behind);
  RUN_TEST(a_length_out_of_range_is_skipped_at_once);
  RUN_TEST(a_frame_in_pieces_waits_for_the_rest);
  RUN_TEST(a_stream_of_false_syncs_never_fills_it_up);
  RUN_TEST(encode_builds_the_polls_of_gps_test);
  RUN_TEST(an_encoded_frame_scans_back_the_same);
  RUN_TEST(the_capture_shows_a_link_both_ways);
  RUN_TEST(one_frame_of_either_kind_is_enough);
  RUN_TEST(a_sentence_with_a_wrong_checksum_is_not);
  RUN_TEST(noise_with_syncs_in_it_is_not_a_link);
  RUN_TEST(a_key_says_how_long_its_value_is);
  RUN_TEST(valset_lays_out_each_value_after_its_key);
  RUN_TEST(valset_refuses_what_it_cannot_send);
  RUN_TEST(valget_asks_one_layer_for_keys_only);
  RUN_TEST(a_valget_response_fills_what_it_holds);
  RUN_TEST(a_response_cut_short_is_no_response);
  RUN_TEST(an_ack_answers_only_its_own_message);
  RUN_TEST(the_capture_holds_four_epochs_without_a_fix);
  RUN_TEST(the_capture_holds_a_sky_without_satellites);
  RUN_TEST(a_fix_reads_back_field_by_field);
  RUN_TEST(only_a_vouched_3d_fix_is_a_fix);
  RUN_TEST(each_satellite_reads_back_in_place);
  RUN_TEST(a_frame_of_the_wrong_shape_does_not_decode);
  RUN_TEST(a_second_at_a_window_is_nine_fixes_at_10_hz);
  RUN_TEST(the_sky_of_that_second);
  return UNITY_END();
}
