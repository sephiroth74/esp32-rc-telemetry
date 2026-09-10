// Host tests for lib/rcl — run with `make test`.
//
// Placeholder. The record round-trips arrive with docs/protocol.md and are run
// against the golden `.rcl` in docs/fixtures/, the same one packages/rct_core is
// tested against (PLAN-ESP32.md §7.2).
#include <unity.h>

void setUp() {}
void tearDown() {}

static void pending() {
  TEST_IGNORE_MESSAGE("docs/protocol.md not written yet — see PLAN-ESP32.md §7.2");
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(pending);
  return UNITY_END();
}
