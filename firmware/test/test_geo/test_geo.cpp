// Host tests for lib/geo — run with `make test`.
//
// Placeholder. Cell ids come first (PLAN-ESP32.md §7.1); gate crossing and lap
// detection follow in phase 8, against a recorded lap with known crossing times
// in docs/fixtures/.
#include <unity.h>

void setUp() {}
void tearDown() {}

static void pending() { TEST_IGNORE_MESSAGE("lib/geo is a skeleton — see PLAN-ESP32.md §7.1"); }

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(pending);
  return UNITY_END();
}
