// Host tests for lib/ubx — run with `make test`.
//
// Placeholder. The real tests arrive in phase 3 with the frame scanner, checked
// against captured NAV-PVT / NAV-SAT frames in docs/fixtures/ (PLAN-ESP32.md §5.4).
#include <unity.h>

void setUp() {}
void tearDown() {}

static void pending() {
  TEST_IGNORE_MESSAGE("lib/ubx is a skeleton — see PLAN-ESP32.md §5.4");
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(pending);
  return UNITY_END();
}
