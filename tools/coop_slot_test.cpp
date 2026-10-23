// Unit test for src/core/fable2_coop_slot.h, against a real header the game wrote.
//   clang++ -std=c++20 -I src/core tools/coop_slot_test.cpp -o out/coop_slot_test.exe
//   out\coop_slot_test.exe <path to a real HeroNNN.header> <its slot name>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
#include "fable2_coop_slot.h"

using namespace fable2::coopslot;

static int g_fail = 0;
#define CHECK(cond)                                              \
  do {                                                           \
    const bool ok_ = (cond);                                     \
    std::printf("%s  %s\n", ok_ ? "ok  " : "FAIL", #cond);       \
    if (!ok_) ++g_fail;                                          \
  } while (0)

int main(int argc, char** argv) {
  CHECK(SlotNumber("Hero000") == 0);
  CHECK(SlotNumber("Hero012") == 12);
  CHECK(!SlotNumber("Hero12"));
  CHECK(!SlotNumber("Heroabc"));
  CHECK(SlotName(7) == "Hero007");
  CHECK(NextSlot({}) == "Hero000");
  CHECK(NextSlot({"Hero000"}) == "Hero001");
  CHECK(NextSlot({"Hero000", "Hero001"}) == "Hero002");
  CHECK(NextSlot({"Hero000", "Hero002", "notes.txt"}) == "Hero001");  // lowest free, like the game
  CHECK(ParseXuidFolder("B13EBABEBABEBABE") == 0xB13EBABEBABEBABEull);
  CHECK(!ParseXuidFolder("B13EBABE"));

  if (argc >= 3) {
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<uint8_t> real((std::istreambuf_iterator<char>(in)), {});
    const auto made = MakeHeader(argv[2], 0xB13EBABEBABEBABEull);
    CHECK(real.size() == kHeaderSize);
    CHECK(real.size() == kHeaderSize && std::equal(made.begin(), made.end(), real.begin()));
    if (real.size() == kHeaderSize)
      for (size_t i = 0; i < kHeaderSize; ++i)
        if (real[i] != made[i]) std::printf("  differs at 0x%03zX: game %02X, made %02X\n", i, real[i], made[i]);
  }
  std::printf("%s (%d failed)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail);
  return g_fail ? 1 : 0;
}
