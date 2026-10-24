// Unit test for src/core/fable2_coop_backup_fmt.h.
//   clang++ -std=c++20 -I src/core tools/coop_backup_test.cpp -o out/coop_backup_test.exe
//   out\coop_backup_test.exe [<slot dir> <zip out>]
// With a slot folder it also writes that slot's backup zip (read it back with
// Python's zipfile to prove other tools open it).
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include "fable2_coop_backup_fmt.h"

using namespace fable2::coopbackupfmt;

static int g_fail = 0;
#define CHECK(cond)                                              \
  do {                                                           \
    const bool ok_ = (cond);                                     \
    std::printf("%s  %s\n", ok_ ? "ok  " : "FAIL", #cond);       \
    if (!ok_) ++g_fail;                                          \
  } while (0)

int main(int argc, char** argv) {
  // FIPS 180-4 / NIST vectors.
  CHECK(Sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(Sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(Sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  CHECK(Crc32("123456789") == 0xCBF43926u);

  std::vector<Entry> files = {{"mainsave.bin", std::string(5000, '\x7f') + "end"},
                              {"saveuid.bin", std::string("\x00\x01\x02\x03\x04\x05\x06\x07", 8)},
                              {"coop_campaign.toml", "format = 1\n"}};
  const std::string zip = MakeBackup(files);
  std::string why;
  auto back = VerifyBackup(zip, &why);
  CHECK(back.has_value());
  CHECK(back && back->size() == 3);
  CHECK(back && (*back)[1].name == "mainsave.bin" && (*back)[1].data == files[0].data);

  std::string tampered = zip;
  tampered[60] ^= 1;  // inside the first file's data: CRC catches it
  CHECK(!VerifyBackup(tampered, &why));

  // A zip whose manifest does not match its files is rejected.
  auto entries = *UnzipStore(zip);
  for (auto& e : entries)
    if (e.name == "saveuid.bin") e.data[0] = 9;
  CHECK(!VerifyBackup(ZipStore(entries), &why) && why == "hash mismatch");
  CHECK(!VerifyBackup("not a zip", &why));

  CHECK(ToPrune({"20261024-010000.zip", "20261024-020000.zip"}, 10).empty());
  std::vector<std::string> many;
  for (int i = 0; i < 12; ++i) many.push_back("20261024-0" + std::to_string(10 + i) + "000.zip");
  const auto gone = ToPrune(many, 10);
  CHECK(gone.size() == 2 && gone[0] == "20261024-010000.zip" && gone[1] == "20261024-011000.zip");

  if (argc == 3) {
    std::vector<Entry> slot;
    for (const auto& f : std::filesystem::directory_iterator(argv[1])) {
      if (!f.is_regular_file()) continue;
      std::ifstream in(f.path(), std::ios::binary);
      slot.push_back({f.path().filename().string(), std::string(std::istreambuf_iterator<char>(in), {})});
    }
    const std::string z = MakeBackup(slot);
    std::ofstream(argv[2], std::ios::binary).write(z.data(), z.size());
    CHECK(VerifyBackup(z, &why).has_value());
    std::printf("wrote %s (%zu files, %zu bytes)\n", argv[2], slot.size(), z.size());
  }

  std::printf(g_fail ? "%d FAILED\n" : "all passed\n", g_fail);
  return g_fail ? 1 : 0;
}
