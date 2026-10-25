// Unit test for src/core/fable2_coop_test_input.h (no game, no SDK).
//   clang++ -std=c++20 -I src/core tools/coop_test_input_test.cpp -o out/coop_test_input_test.exe
#include <cstdio>
#include "fable2_coop_test_input.h"

using namespace fable2::cooptestinput;

static int g_fail = 0;
#define CHECK(cond)                                        \
  do {                                                     \
    const bool ok_ = (cond);                               \
    std::printf("%s  %s\n", ok_ ? "ok  " : "FAIL", #cond); \
    if (!ok_) ++g_fail;                                    \
  } while (0)

int main() {
  auto p = Parse("press A 120");
  CHECK(p.kind == Kind::kPress && p.mask == 0x1000 && p.ms == 120);
  CHECK(Parse("press Down").ms == 120 && Parse("press Down").mask == 0x0002);
  CHECK(Parse("press E 120").kind == Kind::kNone);  // only pad names
  auto s = Parse("stick L 0.5 -1 750\r");
  CHECK(s.kind == Kind::kStick && s.side == 'L' && s.x == 0.5f && s.y == -1.0f && s.ms == 750);
  CHECK(Parse("stick R 1 0").ms == 500);
  CHECK(Parse("stick Q 1 0").kind == Kind::kNone);
  auto t = Parse("trigger L 1 2000");
  CHECK(t.kind == Kind::kTrigger && t.side == 'L' && t.value == 1.0f && t.ms == 2000);
  CHECK(Parse("trigger R 0.5").ms == 500);
  auto l = Parse("lua print('x y')");
  CHECK(l.kind == Kind::kLua && l.text == "print('x y')");
  auto c = Parse("cvar coop_cam_trace true");
  CHECK(c.kind == Kind::kCvar && c.text == "coop_cam_trace" && c.value_text == "true");
  CHECK(StickValue(1.0f) == 32767 && StickValue(-1.0f) == -32767 && StickValue(5.0f) == 32767);
  CHECK(TriggerValue(1.0f) == 255 && TriggerValue(0.0f) == 0 && TriggerValue(0.5f) == 128);
  std::printf("%s (%d failed)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail);
  return g_fail ? 1 : 0;
}
