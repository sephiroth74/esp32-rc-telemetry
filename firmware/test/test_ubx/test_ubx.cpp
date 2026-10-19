// Host tests for lib/ubx — run with `make test`.
//
// The scanner against bytes the GM10 really sent (docs/fixtures/gm10-indoor-115200.hex: NMEA and
// UBX interleaved, as delivered), and against what a UART can make of them: a flipped byte, a frame
// cut short, a false sync, a length out of range, a frame arriving in pieces (PLAN-ESP32.md §5.4).
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <initializer_list>
#include <vector>

#include "link_probe.h"
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
  return UNITY_END();
}
