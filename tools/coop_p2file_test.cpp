// Unit test for src/core/fable2_coop_p2file.h.
//   clang++ -std=c++20 -I src/core tools/coop_p2file_test.cpp -o out/coop_p2file_test.exe
#include <cstdio>
#include "fable2_coop_p2file.h"

using namespace fable2::coopp2file;

static int g_fail = 0;
#define CHECK(cond)                                              \
  do {                                                           \
    const bool ok_ = (cond);                                     \
    std::printf("%s  %s\n", ok_ ? "ok  " : "FAIL", #cond);       \
    if (!ok_) ++g_fail;                                          \
  } while (0)

int main() {
  // FNV-1a 64 reference values.
  CHECK(Fnv1a64("") == 0xCBF29CE484222325ull);
  CHECK(Fnv1a64("a") == 0xAF63DC4C8601EC8Cull);
  CHECK(Hex64(0xAF63DC4C8601EC8Cull) == "AF63DC4C8601EC8C");
  CHECK(HexBytes(std::string("\x01\xAB\x00\xFF", 4)) == "01AB00FF");

  P2File f;
  f.saveuid = "0011223344556677";
  f.stamp = Hex64(Fnv1a64("mainsave"));
  f.written = "2026-10-24T03:00:00Z";
  f.source = "cache";
  f.profile = "COOPHERO:v2|g=F|mor=0.0000|worn=BD511D47|weap=98F97077,3A153787|rev=0";
  const std::string text = Format(f);
  auto back = Parse(text);
  CHECK(back.has_value());
  CHECK(back && back->saveuid == f.saveuid && back->stamp == f.stamp);
  CHECK(back && back->written == f.written && back->source == "cache");
  CHECK(back && back->profile == f.profile);
  CHECK(back && Format(*back) == text);

  // CRLF (edited on Windows) still parses.
  std::string crlf;
  for (char c : text) {
    if (c == '\n') crlf += '\r';
    crlf += c;
  }
  CHECK(Parse(crlf) && Parse(crlf)->profile == f.profile);

  // Incomplete files are rejected.
  CHECK(!Parse(""));
  CHECK(!Parse("saveuid=00\nstamp=11\n"));                      // no profile
  CHECK(!Parse("stamp=11\nCOOPHERO:v2|g=M\n"));                 // no saveuid
  CHECK(!Parse("saveuid=00\nstamp=11\nCOOPHERO:v1|g=M\n"));     // v1 is not a v2 profile

  CHECK(Check(f, f.saveuid, f.stamp) == Match::kCurrent);
  CHECK(Check(f, f.saveuid, "0000000000000000") == Match::kOlder);
  CHECK(Check(f, "FFFFFFFFFFFFFFFF", f.stamp) == Match::kOtherSlot);

  std::printf(g_fail ? "%d FAILED\n" : "all passed\n", g_fail);
  return g_fail ? 1 : 0;
}
