// Host tests for lib/logbuf — run with `make test`.
//
// The logger's ring (PLAN-ESP32.md §4.4): whole records in, the same bytes out in order however
// the writer slices them, across the wrap, through a stall that fills it; and the write statistics
// of the session footer.
#include <string.h>
#include <unity.h>

#include <vector>

#include "logbuf.h"

using Bytes = std::vector<uint8_t>;

void setUp() {}
void tearDown() {}

// A record of `n` bytes whose every byte says where it is in the stream.
static Bytes record(size_t n, uint32_t& counter) {
  Bytes r(n);
  for (uint8_t& b : r) b = static_cast<uint8_t>(counter++ * 7 + 3);
  return r;
}

// Everything waiting, in the writer's slices of at most `chunk`.
static Bytes drain(logbuf::Ring& ring, size_t chunk) {
  Bytes out;
  const uint8_t* p;
  while (size_t n = ring.peek(p, chunk)) {
    out.insert(out.end(), p, p + n);
    ring.release(n);
  }
  return out;
}

static void records_come_out_in_order_across_the_wrap() {
  uint8_t storage[1000];
  logbuf::Ring ring(storage, sizeof storage);
  uint32_t in = 0;
  Bytes sent, got;
  // Records of every size from 1 to 120, the writer taking 256 at a time when half full: the
  // head and the tail wrap again and again, at every offset.
  for (int round = 0; round < 40; ++round) {
    for (size_t n = 1; n <= 120; n += 7) {
      Bytes r = record(n, in);
      TEST_ASSERT_TRUE(ring.append(r.data(), r.size()));
      sent.insert(sent.end(), r.begin(), r.end());
      if (ring.used() > 500) {
        const uint8_t* p;
        size_t k = ring.peek(p, 256);
        got.insert(got.end(), p, p + k);
        ring.release(k);
      }
    }
  }
  Bytes rest = drain(ring, 256);
  got.insert(got.end(), rest.begin(), rest.end());
  TEST_ASSERT_EQUAL_UINT(sent.size(), got.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(sent.data(), got.data(), sent.size());
  TEST_ASSERT_EQUAL_UINT32(0, ring.dropped());
  TEST_ASSERT_EQUAL_UINT(0, ring.used());
}

static void a_peek_stops_at_the_end_of_the_storage() {
  uint8_t storage[100];
  logbuf::Ring ring(storage, sizeof storage);
  uint32_t in = 0;
  Bytes a = record(80, in);
  ring.append(a.data(), a.size());
  const uint8_t* p;
  ring.release(ring.peek(p, 70));  // the tail at 70, the head at 80
  Bytes b = record(40, in);        // 20 to the end, 20 from the start
  TEST_ASSERT_TRUE(ring.append(b.data(), b.size()));
  TEST_ASSERT_EQUAL_UINT(50, ring.used());
  TEST_ASSERT_EQUAL_UINT(30, ring.peek(p, 1000));  // unbroken in memory: 70 to 100
  TEST_ASSERT_EQUAL_PTR(storage + 70, p);
  TEST_ASSERT_EQUAL_UINT(16, ring.peek(p, 16));  // or the writer's limit, if it is less
}

static void a_full_ring_drops_whole_records_and_counts_them() {
  uint8_t storage[256];
  logbuf::Ring ring(storage, sizeof storage);
  uint32_t in = 0;
  Bytes sent;
  // A stalled card: nothing drains, and records keep coming until they no longer fit.
  for (int i = 0; i < 10; ++i) {
    Bytes r = record(30, in);
    if (ring.append(r.data(), r.size())) sent.insert(sent.end(), r.begin(), r.end());
  }
  TEST_ASSERT_EQUAL_UINT(240, ring.used());  // eight of them
  TEST_ASSERT_EQUAL_UINT32(2, ring.dropped());
  TEST_ASSERT_EQUAL_UINT32(60, ring.droppedBytes());
  Bytes tiny = record(16, in);
  TEST_ASSERT_TRUE(ring.append(tiny.data(), tiny.size()));  // exactly the room left
  sent.insert(sent.end(), tiny.begin(), tiny.end());
  TEST_ASSERT_EQUAL_UINT(0, ring.free());
  TEST_ASSERT_EQUAL_UINT(256, ring.highWater());
  // The card comes back: what was kept comes out whole, and nothing of the dropped ones.
  Bytes got = drain(ring, 64);
  TEST_ASSERT_EQUAL_UINT(sent.size(), got.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(sent.data(), got.data(), sent.size());
  TEST_ASSERT_EQUAL_UINT(256, ring.highWater());  // the high water stays
}

static void clearing_forgets_the_waiting_bytes_not_the_counts() {
  uint8_t storage[64];
  logbuf::Ring ring(storage, sizeof storage);
  uint32_t in = 0;
  Bytes r = record(50, in);
  ring.append(r.data(), r.size());
  ring.append(r.data(), r.size());  // dropped
  ring.clear();
  TEST_ASSERT_EQUAL_UINT(0, ring.used());
  const uint8_t* p;
  TEST_ASSERT_EQUAL_UINT(0, ring.peek(p, 64));
  TEST_ASSERT_TRUE(ring.append(r.data(), r.size()));
  TEST_ASSERT_EQUAL_PTR(storage, (ring.peek(p, 64), p));  // from the start again
  TEST_ASSERT_EQUAL_UINT32(1, ring.dropped());
  TEST_ASSERT_EQUAL_UINT(50, ring.highWater());  // the first session's: one record of 50
  ring.clear();
  ring.resetHighWater();  // the next session's starts from nothing
  TEST_ASSERT_EQUAL_UINT(0, ring.highWater());
  ring.append(r.data(), 20);
  TEST_ASSERT_EQUAL_UINT(20, ring.highWater());
}

// --- Write statistics --------------------------------------------------------------

static void the_footer_numbers_of_a_healthy_card() {
  logbuf::LatencyStats s(250);
  TEST_ASSERT_EQUAL_UINT32(0, s.p99Ms());
  for (int i = 0; i < 990; ++i) s.add(9 + i % 5);  // 9 to 13 ms
  for (int i = 0; i < 10; ++i) s.add(40);
  TEST_ASSERT_EQUAL_UINT32(1000, s.count());
  TEST_ASSERT_EQUAL_UINT32(40, s.maxMs());
  TEST_ASSERT_EQUAL_UINT32(13, s.p99Ms());  // 990 of 1000 at 13 ms or less: exact below 32
  TEST_ASSERT_EQUAL_UINT32(0, s.slow());
}

static void a_stalling_card_shows_in_p99_and_in_the_slow_count() {
  logbuf::LatencyStats s(250);
  for (int i = 0; i < 95; ++i) s.add(12);
  for (int i = 0; i < 5; ++i) s.add(300 + i);  // 5 % over the limit
  TEST_ASSERT_EQUAL_UINT32(5, s.slow());
  TEST_ASSERT_EQUAL_UINT32(304, s.maxMs());
  TEST_ASSERT_EQUAL_UINT32(304, s.p99Ms());  // 300–319 ms is the bucket; never above the max
  s.add(250);                                // the limit itself is not over it
  TEST_ASSERT_EQUAL_UINT32(5, s.slow());
}

static void one_slow_write_in_a_hundred_is_the_one_percent() {
  logbuf::LatencyStats s(250);
  for (int i = 0; i < 99; ++i) s.add(20);
  s.add(1800);
  TEST_ASSERT_EQUAL_UINT32(20, s.p99Ms());
  TEST_ASSERT_EQUAL_UINT32(1800, s.maxMs());
  s.add(1800);  // two in 101: past the 1 %
  TEST_ASSERT_EQUAL_UINT32(1800, s.p99Ms());
}

static void the_buckets_are_as_coarse_as_documented() {
  struct Case {
    uint32_t ms, p99;
  } cases[] = {{0, 0}, {31, 31}, {32, 32}, {33, 33}, {100, 100}, {255, 255}, {256, 256},
               {2047, 2047}, {2048, 2048}, {60000, 60000}};
  for (const Case& c : cases) {
    logbuf::LatencyStats s(250);
    s.add(c.ms);  // one write: the bucket's upper edge, held down to the max
    TEST_ASSERT_EQUAL_UINT32(c.p99, s.p99Ms());
  }
  logbuf::LatencyStats s(250);
  s.add(33);
  s.add(1);
  for (int i = 0; i < 98; ++i) s.add(1);
  s.add(33);  // two of 101 in 32–39 ms: the bucket's top, 39, held to the max, 33
  TEST_ASSERT_EQUAL_UINT32(33, s.p99Ms());
  s.add(39);
  TEST_ASSERT_EQUAL_UINT32(39, s.p99Ms());
  s.reset();
  TEST_ASSERT_EQUAL_UINT32(0, s.count());
  TEST_ASSERT_EQUAL_UINT32(0, s.p99Ms());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(records_come_out_in_order_across_the_wrap);
  RUN_TEST(a_peek_stops_at_the_end_of_the_storage);
  RUN_TEST(a_full_ring_drops_whole_records_and_counts_them);
  RUN_TEST(clearing_forgets_the_waiting_bytes_not_the_counts);
  RUN_TEST(the_footer_numbers_of_a_healthy_card);
  RUN_TEST(a_stalling_card_shows_in_p99_and_in_the_slow_count);
  RUN_TEST(one_slow_write_in_a_hundred_is_the_one_percent);
  RUN_TEST(the_buckets_are_as_coarse_as_documented);
  return UNITY_END();
}
