// Host tests for lib/rcl — run with `make test`.
//
// Placeholder. The record round-trips arrive with step 5.2 and are run against
// the golden `.rcl` in docs/fixtures/, the same one packages/rct_core is tested
// against (PLAN-ESP32.md §7.2, docs/protocol.md).
#include <unity.h>

void setUp() {}
void tearDown() {}

static void pending() {
  TEST_IGNORE_MESSAGE("lib/rcl arrives with ROADMAP.md step 5.2, on docs/protocol.md");
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(pending);
  return UNITY_END();
}
