// Host tests for lib/rcl — run with `make test`.
//
// Against the golden file of docs/protocol.md (docs/fixtures/rcl-golden-v1), the one rct_core is
// held to as well: the JSON's values must encode to the .hex's bytes, written by a Python script
// from the document rather than from this code, and the bytes must decode to the values. Then what
// a power cut and a card can make of a file (§4.5): a cut at every byte, a tail of zeros or of an
// older session's records, one corrupted record, a length mangled mid-file. And where a file left
// open ends, read in pieces as the device reads a long one.
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <algorithm>
#include <string>
#include <vector>

#include "rcl.h"

#ifndef FIXTURES_DIR
#error "FIXTURES_DIR comes from the native_test environment of platformio.ini"
#endif

using Bytes = std::vector<uint8_t>;

void setUp() {}
void tearDown() {}

static std::string readText(const char* name) {
  char path[256];
  snprintf(path, sizeof path, "%s/%s", FIXTURES_DIR, name);
  FILE* f = fopen(path, "r");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
  std::string text;
  char buf[1024];
  for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
  fclose(f);
  return text;
}

// The fixture's hex bytes; '#' starts a comment. One copy, which the records read point into.
static const Bytes& golden() {
  static Bytes bytes = [] {
    Bytes out;
    std::string text = readText("rcl-golden-v1.hex");
    size_t at = 0;
    while (at < text.size()) {
      size_t end = text.find('\n', at);
      if (end == std::string::npos) end = text.size();
      std::string line = text.substr(at, end - at);
      at = end + 1;
      if (line.empty() || line[0] == '#') continue;
      unsigned value;
      int used;
      for (const char* p = line.c_str(); sscanf(p, "%2x%n", &value, &used) == 1; p += used) {
        out.push_back(static_cast<uint8_t>(value));
      }
    }
    return out;
  }();
  return bytes;
}

static JsonDocument& expected() {
  static JsonDocument doc;
  static bool loaded = false;
  if (!loaded) {
    std::string text = readText("rcl-golden-v1.json");
    TEST_ASSERT_FALSE(deserializeJson(doc, text));
    loaded = true;
  }
  return doc;
}

static uint64_t goldenNonce() {
  return strtoull(expected()["nonce"].as<const char*>(), nullptr, 16);
}

static Bytes hexBytes(const char* hex) {
  Bytes out;
  for (size_t i = 0; hex[i] && hex[i + 1]; i += 2) {
    unsigned v;
    sscanf(hex + i, "%2x", &v);
    out.push_back(static_cast<uint8_t>(v));
  }
  return out;
}

// --- The JSON's records, through lib/rcl's structs -------------------------------------

struct Kind {
  const char* name;
  rcl::Type type;
};
static const Kind kKinds[] = {
    {"session_header", rcl::Type::SessionHeader}, {"ubx", rcl::Type::Ubx},
    {"imu_aggregate", rcl::Type::ImuAggregate},   {"imu_raw", rcl::Type::ImuRaw},
    {"rc_samples", rcl::Type::RcSamples},         {"marker", rcl::Type::Marker},
    {"lap", rcl::Type::Lap},                      {"footer", rcl::Type::Footer},
};

static rcl::Type typeOf(JsonObjectConst r) {
  for (const Kind& k : kKinds) {
    if (strcmp(r["type"], k.name) == 0) return k.type;
  }
  TEST_FAIL_MESSAGE("a type the golden JSON should not have");
  return rcl::Type::Ubx;
}

static rcl::SessionHeader sessionHeaderOf(JsonObjectConst r) {
  rcl::SessionHeader h;
  h.sessionId = r["session_id"];
  unsigned mac[6];
  sscanf(r["device"], "%x:%x:%x:%x:%x:%x", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
  for (int i = 0; i < 6; ++i) h.device[i] = static_cast<uint8_t>(mac[i]);
  h.post = r["post"];
  strncpy(h.firmware, r["firmware"], sizeof h.firmware);
  h.epochMs = r["epoch_ms"];
  h.rcPeriodUs = r["rc_period_us"];
  h.imuRawPeriodUs = r["imu_raw_period_us"];
  h.accelRangeG = r["accel_range_g"];
  h.gyroRangeDps = r["gyro_range_dps"];
  h.carProfile = r["car_profile"];
  return h;
}

static rcl::Footer footerOf(JsonObjectConst r) {
  rcl::Footer f;
  f.close = r["close"];
  f.ubxFrames = r["ubx_frames"];
  f.records = r["records"];
  f.uartOverruns = r["uart_overruns"];
  f.uartErrors = r["uart_errors"];
  f.ubxBadChecksums = r["ubx_bad_checksums"];
  f.ubxOversize = r["ubx_oversize"];
  f.gnssQueueFull = r["gnss_queue_full"];
  f.ringOverflow = r["ring_overflow"];
  for (int i = 0; i < 3; ++i) f.rcRejects[i] = r["rc_rejects"][i];
  f.writes = r["writes"];
  f.slowWrites = r["slow_writes"];
  f.writeMaxMs = r["write_max_ms"];
  f.writeP99Ms = r["write_p99_ms"];
  f.ringHighWater = r["ring_high_water"];
  return f;
}

// The payload of a golden record, encoded by lib/rcl from the JSON's values.
static Bytes payloadOf(JsonObjectConst r) {
  uint8_t buf[2048];
  size_t n = 0;
  switch (typeOf(r)) {
    case rcl::Type::SessionHeader:
      n = rcl::encode(sessionHeaderOf(r), buf, sizeof buf);
      break;
    case rcl::Type::Ubx:
      return hexBytes(r["frame"]);
    case rcl::Type::ImuAggregate: {
      rcl::ImuAggregate a;
      a.count = r["count"];
      for (int k = 0; k < 6; ++k) {
        a.axes[k] = {r["axes"][k][0], r["axes"][k][1], r["axes"][k][2]};
      }
      n = rcl::encode(a, buf, sizeof buf);
      break;
    }
    case rcl::Type::ImuRaw: {
      rcl::ImuRaw raw;
      JsonArrayConst s = r["samples"];
      raw.count = s.size();
      for (size_t i = 0; i < s.size(); ++i) {
        for (int k = 0; k < 6; ++k) raw.samples[i][k] = s[i][k];
      }
      n = rcl::encode(raw, buf, sizeof buf);
      break;
    }
    case rcl::Type::RcSamples: {
      rcl::RcSamples rc;
      JsonArrayConst s = r["samples"];
      rc.count = s.size();
      for (size_t i = 0; i < s.size(); ++i) {
        for (int k = 0; k < 3; ++k) rc.samples[i][k] = s[i][k];
      }
      n = rcl::encode(rc, buf, sizeof buf);
      break;
    }
    case rcl::Type::Marker: {
      rcl::Marker m;
      m.number = r["number"];
      m.source = r["source"];
      n = rcl::encode(m, buf, sizeof buf);
      break;
    }
    case rcl::Type::Lap: {
      rcl::Lap l;
      l.lap = r["lap"];
      l.flags = r["flags"];
      l.line = r["line"];
      l.crossingMs = r["crossing_ms"];
      l.crossingItowMs = r["crossing_itow_ms"];
      l.lapTimeMs = r["lap_time_ms"];
      n = rcl::encode(l, buf, sizeof buf);
      break;
    }
    case rcl::Type::Footer:
      n = rcl::encode(footerOf(r), buf, sizeof buf);
      break;
  }
  TEST_ASSERT_NOT_EQUAL_UINT(0, n);
  return Bytes(buf, buf + n);
}

// The golden records as lib/rcl writes them, with `nonce`; the file header first if asked.
static Bytes encodeGolden(uint64_t nonce, bool withHeader = true) {
  Bytes out(rcl::kFileHeaderBytes);
  if (withHeader) {
    TEST_ASSERT_EQUAL_UINT(rcl::kFileHeaderBytes,
                           rcl::writeFileHeader(nonce, out.data(), out.size()));
  } else {
    out.clear();
  }
  rcl::Writer w(nonce);
  for (JsonObjectConst r : expected()["records"].as<JsonArrayConst>()) {
    Bytes p = payloadOf(r);
    Bytes rec(rcl::kRecordOverhead + p.size());
    TEST_ASSERT_EQUAL_UINT(rec.size(), w.record(typeOf(r), r["t_ms"], p.data(), p.size(),
                                                rec.data(), rec.size()));
    out.insert(out.end(), rec.begin(), rec.end());
  }
  return out;
}

// Each decoded record against its golden values, field by field.
static void expectRecord(const rcl::Record& rec, JsonObjectConst r) {
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(typeOf(r)), rec.type);
  TEST_ASSERT_EQUAL_UINT32(r["t_ms"].as<uint32_t>(), rec.tMs);
  switch (typeOf(r)) {
    case rcl::Type::SessionHeader: {
      rcl::SessionHeader h, want = sessionHeaderOf(r);
      TEST_ASSERT_TRUE(rcl::decode(rec.payload, rec.length, h));
      TEST_ASSERT_EQUAL_UINT32(want.sessionId, h.sessionId);
      TEST_ASSERT_EQUAL_UINT8_ARRAY(want.device, h.device, 6);
      TEST_ASSERT_EQUAL_UINT16(want.post, h.post);
      TEST_ASSERT_EQUAL_STRING_LEN(want.firmware, h.firmware, 16);
      TEST_ASSERT_EQUAL_UINT16(want.epochMs, h.epochMs);
      TEST_ASSERT_EQUAL_UINT16(want.rcPeriodUs, h.rcPeriodUs);
      TEST_ASSERT_EQUAL_UINT16(want.imuRawPeriodUs, h.imuRawPeriodUs);
      TEST_ASSERT_EQUAL_UINT8(want.accelRangeG, h.accelRangeG);
      TEST_ASSERT_EQUAL_UINT16(want.gyroRangeDps, h.gyroRangeDps);
      TEST_ASSERT_EQUAL_UINT8(want.carProfile, h.carProfile);
      break;
    }
    case rcl::Type::Ubx: {
      Bytes frame = hexBytes(r["frame"]);
      TEST_ASSERT_EQUAL_UINT(frame.size(), rec.length);
      TEST_ASSERT_EQUAL_UINT8_ARRAY(frame.data(), rec.payload, frame.size());
      break;
    }
    case rcl::Type::ImuAggregate: {
      rcl::ImuAggregate a;
      TEST_ASSERT_TRUE(rcl::decode(rec.payload, rec.length, a));
      TEST_ASSERT_EQUAL_UINT16(r["count"].as<uint16_t>(), a.count);
      for (int k = 0; k < 6; ++k) {
        TEST_ASSERT_EQUAL_INT16(r["axes"][k][0].as<int16_t>(), a.axes[k].min);
        TEST_ASSERT_EQUAL_INT16(r["axes"][k][1].as<int16_t>(), a.axes[k].max);
        TEST_ASSERT_EQUAL_INT16(r["axes"][k][2].as<int16_t>(), a.axes[k].mean);
      }
      break;
    }
    case rcl::Type::ImuRaw: {
      rcl::ImuRaw raw;
      TEST_ASSERT_TRUE(rcl::decode(rec.payload, rec.length, raw));
      JsonArrayConst s = r["samples"];
      TEST_ASSERT_EQUAL_UINT8(s.size(), raw.count);
      for (size_t i = 0; i < s.size(); ++i) {
        for (int k = 0; k < 6; ++k) TEST_ASSERT_EQUAL_INT16(s[i][k].as<int16_t>(), raw.samples[i][k]);
      }
      break;
    }
    case rcl::Type::RcSamples: {
      rcl::RcSamples rc;
      TEST_ASSERT_TRUE(rcl::decode(rec.payload, rec.length, rc));
      JsonArrayConst s = r["samples"];
      TEST_ASSERT_EQUAL_UINT8(s.size(), rc.count);
      for (size_t i = 0; i < s.size(); ++i) {
        for (int k = 0; k < 3; ++k) TEST_ASSERT_EQUAL_UINT16(s[i][k].as<uint16_t>(), rc.samples[i][k]);
      }
      break;
    }
    case rcl::Type::Marker: {
      rcl::Marker m;
      TEST_ASSERT_TRUE(rcl::decode(rec.payload, rec.length, m));
      TEST_ASSERT_EQUAL_UINT16(r["number"].as<uint16_t>(), m.number);
      TEST_ASSERT_EQUAL_UINT8(r["source"].as<uint8_t>(), m.source);
      break;
    }
    case rcl::Type::Lap: {
      rcl::Lap l;
      TEST_ASSERT_TRUE(rcl::decode(rec.payload, rec.length, l));
      TEST_ASSERT_EQUAL_UINT16(r["lap"].as<uint16_t>(), l.lap);
      TEST_ASSERT_EQUAL_UINT8(r["flags"].as<uint8_t>(), l.flags);
      TEST_ASSERT_EQUAL_UINT8(r["line"].as<uint8_t>(), l.line);
      TEST_ASSERT_EQUAL_UINT32(r["crossing_ms"].as<uint32_t>(), l.crossingMs);
      TEST_ASSERT_EQUAL_UINT32(r["crossing_itow_ms"].as<uint32_t>(), l.crossingItowMs);
      TEST_ASSERT_EQUAL_UINT32(r["lap_time_ms"].as<uint32_t>(), l.lapTimeMs);
      break;
    }
    case rcl::Type::Footer: {
      rcl::Footer f, want = footerOf(r);
      TEST_ASSERT_TRUE(rcl::decode(rec.payload, rec.length, f));
      TEST_ASSERT_EQUAL_UINT8(want.close, f.close);
      TEST_ASSERT_EQUAL_UINT32(want.ubxFrames, f.ubxFrames);
      TEST_ASSERT_EQUAL_UINT32(want.records, f.records);
      TEST_ASSERT_EQUAL_UINT32(want.uartErrors, f.uartErrors);
      TEST_ASSERT_EQUAL_UINT32_ARRAY(want.rcRejects, f.rcRejects, 3);
      TEST_ASSERT_EQUAL_UINT32(want.writes, f.writes);
      TEST_ASSERT_EQUAL_UINT32(want.writeMaxMs, f.writeMaxMs);
      TEST_ASSERT_EQUAL_UINT32(want.writeP99Ms, f.writeP99Ms);
      TEST_ASSERT_EQUAL_UINT32(want.ringHighWater, f.ringHighWater);
      break;
    }
  }
}

// Every record a reader finds in `data`.
static std::vector<rcl::Record> readAll(const Bytes& data, rcl::Reader& reader) {
  std::vector<rcl::Record> out;
  TEST_ASSERT_TRUE(reader.begin(data.data(), data.size()));
  rcl::Record rec;
  while (reader.next(rec)) out.push_back(rec);
  return out;
}

static size_t goldenCount() { return expected()["records"].size(); }

// --- The format's pieces ---------------------------------------------------------------

static void the_crc_is_zlibs() {
  const uint8_t check[] = "123456789";
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926, rcl::crc32(check, 9));
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926, rcl::crc32(check + 5, 4, rcl::crc32(check, 5)));  // continued
  TEST_ASSERT_EQUAL_HEX32(0, rcl::crc32(check, 0));
}

static void the_file_header_is_as_documented() {
  uint8_t out[rcl::kFileHeaderBytes];
  TEST_ASSERT_EQUAL_UINT(0, rcl::writeFileHeader(goldenNonce(), out, sizeof out - 1));
  TEST_ASSERT_EQUAL_UINT(24, rcl::writeFileHeader(goldenNonce(), out, sizeof out));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(golden().data(), out, sizeof out);
  const uint8_t magic[] = {0x89, 'R', 'C', 'L', '\r', '\n', 0x1A, '\n'};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(magic, out, 8);
}

// --- The golden file -----------------------------------------------------------------

static void the_golden_values_encode_to_the_golden_bytes() {
  Bytes mine = encodeGolden(goldenNonce());
  TEST_ASSERT_EQUAL_UINT(golden().size(), mine.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(golden().data(), mine.data(), mine.size());
}

static void the_golden_bytes_decode_to_the_golden_values() {
  rcl::Reader reader;
  std::vector<rcl::Record> recs = readAll(golden(), reader);
  TEST_ASSERT_EQUAL_UINT16(1, reader.version());
  TEST_ASSERT_EQUAL_HEX64(goldenNonce(), reader.nonce());
  TEST_ASSERT_EQUAL_UINT(goldenCount(), recs.size());
  TEST_ASSERT_EQUAL_UINT(0, reader.skipped());
  JsonArrayConst want = expected()["records"];
  for (size_t i = 0; i < recs.size(); ++i) expectRecord(recs[i], want[i]);
}

// --- What a power cut and a card make of a file ----------------------------------------

static void a_file_cut_anywhere_keeps_every_whole_record() {
  rcl::Reader reader;
  std::vector<rcl::Record> whole = readAll(golden(), reader);
  for (size_t cut = rcl::kFileHeaderBytes; cut <= golden().size(); ++cut) {
    Bytes part(golden().begin(), golden().begin() + cut);
    std::vector<rcl::Record> recs = readAll(part, reader);
    size_t fit = 0;
    while (fit < whole.size() && whole[fit].offset + rcl::kRecordOverhead + whole[fit].length <= cut) {
      ++fit;
    }
    TEST_ASSERT_EQUAL_UINT_MESSAGE(fit, recs.size(), "records in a file cut short");
    for (size_t i = 0; i < recs.size(); ++i) {
      TEST_ASSERT_EQUAL_UINT(whole[i].offset, recs[i].offset);
      TEST_ASSERT_EQUAL_UINT(whole[i].length, recs[i].length);
    }
    size_t used = fit ? whole[fit - 1].offset + rcl::kRecordOverhead + whole[fit - 1].length
                      : rcl::kFileHeaderBytes;
    TEST_ASSERT_EQUAL_UINT(cut - used, reader.skipped());  // the cut record, and nothing else
  }
}

static void a_tail_of_zeros_or_of_an_older_session_is_skipped() {
  rcl::Reader reader;
  Bytes zeros = golden();
  zeros.insert(zeros.end(), 4096, 0x00);
  TEST_ASSERT_EQUAL_UINT(goldenCount(), readAll(zeros, reader).size());
  TEST_ASSERT_EQUAL_UINT(4096, reader.skipped());

  // The same records, valid in every way, but for another session's nonce: in the pre-allocated
  // space an earlier session left behind.
  Bytes stale = encodeGolden(goldenNonce() ^ 1, /*withHeader=*/false);
  Bytes file = golden();
  file.insert(file.end(), stale.begin(), stale.end());
  TEST_ASSERT_EQUAL_UINT(goldenCount(), readAll(file, reader).size());
  TEST_ASSERT_EQUAL_UINT(stale.size(), reader.skipped());
}

static void a_corrupted_record_costs_only_itself() {
  rcl::Reader reader;
  std::vector<rcl::Record> whole = readAll(golden(), reader);
  const rcl::Record& hit = whole[3];  // the IMU aggregate
  size_t hitSize = rcl::kRecordOverhead + hit.length;
  for (size_t at : {hit.offset + 10, hit.offset + 2}) {  // a payload byte; the length itself
    Bytes file = golden();
    file[at] ^= 0x40;
    std::vector<rcl::Record> recs = readAll(file, reader);
    TEST_ASSERT_EQUAL_UINT(whole.size() - 1, recs.size());
    TEST_ASSERT_EQUAL_UINT(hitSize, reader.skipped());
    TEST_ASSERT_EQUAL_UINT(whole[4].offset, recs[3].offset);  // the one after it, untouched
    TEST_ASSERT_EQUAL_UINT(whole.back().offset, recs.back().offset);
  }
}

// --- A file left open --------------------------------------------------------------

static size_t readFrom(void* context, size_t offset, uint8_t* buf, size_t capacity) {
  const Bytes& file = *static_cast<const Bytes*>(context);
  if (offset >= file.size()) return 0;
  size_t n = std::min(capacity, file.size() - offset);
  memcpy(buf, file.data() + offset, n);
  return n;
}

// Pieces just over the largest record, so that a long file takes many.
constexpr size_t kPiece = rcl::kRecordOverhead + rcl::kMaxPayload + 4096;
constexpr size_t kGap = 65536;

static rcl::Tail tailOf(const Bytes& file) {
  Bytes buf(kPiece);
  rcl::Tail t;
  TEST_ASSERT_TRUE(rcl::findTail(readFrom, const_cast<Bytes*>(&file), buf.data(), buf.size(),
                                 kGap, t));
  return t;
}

// `count` records of `bytes` payload each, of the golden session, a pattern in each payload.
static Bytes bigRecords(size_t count, size_t bytes) {
  rcl::Writer w(goldenNonce());
  Bytes out, payload(bytes), rec(rcl::kRecordOverhead + bytes);
  for (size_t i = 0; i < count; ++i) {
    for (size_t j = 0; j < bytes; ++j) payload[j] = static_cast<uint8_t>(i * 7 + j);
    TEST_ASSERT_EQUAL_UINT(rec.size(), w.record(rcl::Type::Ubx, 1000 + i, payload.data(), bytes,
                                                rec.data(), rec.size()));
    out.insert(out.end(), rec.begin(), rec.end());
  }
  return out;
}

// The rest of a 4 MB extent, zeros and an older session's records: the file ends at its own last
// record, and the session is the header's.
static void a_file_left_open_ends_at_its_last_record() {
  Bytes file = golden();
  file.insert(file.end(), 200000, 0x00);
  Bytes stale = encodeGolden(goldenNonce() ^ 1, /*withHeader=*/false);
  file.insert(file.end(), stale.begin(), stale.end());
  rcl::Tail t = tailOf(file);
  TEST_ASSERT_EQUAL_UINT(golden().size(), t.end);
  TEST_ASSERT_EQUAL_UINT(goldenCount(), t.records);
  TEST_ASSERT_EQUAL_UINT32(expected()["records"][0]["session_id"].as<uint32_t>(), t.sessionId);
}

// Many pieces, records of up to 60 KB across their edges, the last one cut by the power: every
// whole record is found, as reading the file at once finds them, and the cut one is not.
static void a_long_file_read_in_pieces_finds_every_record() {
  Bytes file = golden();
  for (size_t bytes : {60000u, 100u, 40000u, 65535u, 7u}) {
    Bytes recs = bigRecords(3, bytes);
    file.insert(file.end(), recs.begin(), recs.end());
  }
  Bytes cut = bigRecords(1, 30000);
  file.insert(file.end(), cut.begin(), cut.begin() + 20000);
  size_t end = file.size() - 20000;
  file.insert(file.end(), 100000, 0x00);
  rcl::Reader reader;
  std::vector<rcl::Record> whole = readAll(file, reader);
  rcl::Tail t = tailOf(file);
  TEST_ASSERT_EQUAL_UINT(whole.size(), t.records);
  TEST_ASSERT_EQUAL_UINT(goldenCount() + 15, t.records);
  TEST_ASSERT_EQUAL_UINT(end, t.end);
  // Nothing after the last record, as in a file already cut: the same.
  Bytes exact(file.begin(), file.begin() + end);
  rcl::Tail e = tailOf(exact);
  TEST_ASSERT_EQUAL_UINT(end, e.end);
  TEST_ASSERT_EQUAL_UINT(t.records, e.records);
}

// Past `gap` bytes with no record the search stops: what the card held there is not this session's.
static void the_search_gives_up_after_the_gap() {
  Bytes file = golden();
  file.insert(file.end(), kGap + rcl::kRecordOverhead + rcl::kMaxPayload + kPiece, 0x00);
  Bytes more = bigRecords(2, 100);
  file.insert(file.end(), more.begin(), more.end());
  TEST_ASSERT_EQUAL_UINT(golden().size(), tailOf(file).end);
}

static void only_an_rcl_has_a_tail() {
  Bytes zeros(1000, 0x00), buf(kPiece);
  rcl::Tail t;
  TEST_ASSERT_FALSE(rcl::findTail(readFrom, &zeros, buf.data(), buf.size(), kGap, t));
  Bytes file = golden();  // and a piece too small for the largest record
  TEST_ASSERT_FALSE(rcl::findTail(readFrom, &file, buf.data(), rcl::kMaxPayload, kGap, t));
}

static void a_header_that_is_not_ours_is_refused() {
  rcl::Reader reader;
  Bytes file = golden();
  TEST_ASSERT_FALSE(reader.begin(file.data(), 23));
  Bytes magic = file;
  magic[0] = 'R';
  TEST_ASSERT_FALSE(reader.begin(magic.data(), magic.size()));
  Bytes crc = file;
  crc[12] ^= 1;  // the nonce, under the header's CRC
  TEST_ASSERT_FALSE(reader.begin(crc.data(), crc.size()));
  // A version this reader does not know, with a CRC that holds.
  Bytes v2 = file;
  v2[8] = 2;
  uint32_t c = rcl::crc32(v2.data(), 20);
  for (int i = 0; i < 4; ++i) v2[20 + i] = (c >> (8 * i)) & 0xFF;
  TEST_ASSERT_FALSE(reader.begin(v2.data(), v2.size()));
  TEST_ASSERT_TRUE(reader.begin(file.data(), file.size()));
}

// --- Writing, and the rules of payloads ------------------------------------------------

static void a_type_the_reader_does_not_know_is_still_a_record() {
  uint8_t file[64];
  size_t n = rcl::writeFileHeader(7, file, sizeof file);
  rcl::Writer w(7);
  const uint8_t payload[] = {1, 2, 3};
  n += w.record(static_cast<rcl::Type>(0x42), 5, payload, 3, file + n, sizeof file - n);
  rcl::Reader reader;
  TEST_ASSERT_TRUE(reader.begin(file, n));
  rcl::Record rec;
  TEST_ASSERT_TRUE(reader.next(rec));
  TEST_ASSERT_EQUAL_HEX8(0x42, rec.type);  // for the caller to skip
  TEST_ASSERT_EQUAL_UINT16(3, rec.length);
  TEST_ASSERT_FALSE(reader.next(rec));
}

static void a_record_that_does_not_fit_is_not_written() {
  rcl::Writer w(7);
  uint8_t out[32];
  const uint8_t payload[21] = {};
  TEST_ASSERT_EQUAL_UINT(0, w.record(rcl::Type::Marker, 0, payload, 21, out, sizeof out));  // 33
  TEST_ASSERT_EQUAL_UINT(32, w.record(rcl::Type::Marker, 0, payload, 20, out, sizeof out));
  rcl::Footer f;
  TEST_ASSERT_EQUAL_UINT(0, rcl::encode(f, out, sizeof out));  // 68 bytes
}

static void payloads_grow_by_appending_and_never_shrink() {
  rcl::SessionHeader h;
  h.sessionId = 9;
  uint8_t buf[64] = {};
  TEST_ASSERT_EQUAL_UINT(rcl::kSessionHeaderBytes, rcl::encode(h, buf, sizeof buf));
  rcl::SessionHeader back;
  TEST_ASSERT_TRUE(rcl::decode(buf, rcl::kSessionHeaderBytes + 8, back));  // a later version's
  TEST_ASSERT_EQUAL_UINT32(9, back.sessionId);
  TEST_ASSERT_FALSE(rcl::decode(buf, rcl::kSessionHeaderBytes - 1, back));

  rcl::RcSamples rc;
  rc.count = 5;
  size_t n = rcl::encode(rc, buf, sizeof buf);
  TEST_ASSERT_EQUAL_UINT(32, n);
  TEST_ASSERT_FALSE(rcl::decode(buf, n - 1, rc));  // a sample cut short
  buf[0] = rcl::kMaxSamples + 1;
  TEST_ASSERT_FALSE(rcl::decode(buf, sizeof buf, rc));
  rc.count = rcl::kMaxSamples + 1;
  TEST_ASSERT_EQUAL_UINT(0, rcl::encode(rc, buf, sizeof buf));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(the_crc_is_zlibs);
  RUN_TEST(the_file_header_is_as_documented);
  RUN_TEST(the_golden_values_encode_to_the_golden_bytes);
  RUN_TEST(the_golden_bytes_decode_to_the_golden_values);
  RUN_TEST(a_file_cut_anywhere_keeps_every_whole_record);
  RUN_TEST(a_tail_of_zeros_or_of_an_older_session_is_skipped);
  RUN_TEST(a_file_left_open_ends_at_its_last_record);
  RUN_TEST(a_long_file_read_in_pieces_finds_every_record);
  RUN_TEST(the_search_gives_up_after_the_gap);
  RUN_TEST(only_an_rcl_has_a_tail);
  RUN_TEST(a_corrupted_record_costs_only_itself);
  RUN_TEST(a_header_that_is_not_ours_is_refused);
  RUN_TEST(a_type_the_reader_does_not_know_is_still_a_record);
  RUN_TEST(a_record_that_does_not_fit_is_not_written);
  RUN_TEST(payloads_grow_by_appending_and_never_shrink);
  return UNITY_END();
}
