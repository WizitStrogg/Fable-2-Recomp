// fable_2 - co-op campaign save slots: naming and content headers
// (specs/001-coop-campaign, starter-save path, research.md "P2 -> fallback A").
//
// No SDK or game dependencies (unit test: tools/coop_slot_test.cpp).
//
// A Fable II save slot is saves/<XUID>/4D5307F1/00000001/HeroNNN/ plus a
// content header saves/<XUID>/4D5307F1/Headers/00000001/HeroNNN.header that
// the SDK's save enumeration reads. Header layout (328 bytes, big-endian, as
// rexglue-sdk ContentManager::WriteContentHeaderFile writes it):
//   0x000 u32 device_id = 1        0x004 u32 content_type = 1 (saved game)
//   0x008 u16[128] display name    ("Hero 2" for Hero001: slot number + 1)
//   0x108 char[42] file name       ("Hero001")
//   0x138 u64 owner XUID           0x140 u32 title id = 0x4D5307F1
//   0x144 u32 0

#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace fable2::coopslot {

constexpr uint32_t kTitleId = 0x4D5307F1;
constexpr size_t kHeaderSize = 328;
constexpr int kMaxSlots = 1000;

// "Hero007" -> 7; anything else -> nothing.
inline std::optional<int> SlotNumber(const std::string& name) {
  if (name.size() != 7 || name.compare(0, 4, "Hero") != 0) return std::nullopt;
  int n = 0;
  for (size_t i = 4; i < 7; ++i) {
    if (name[i] < '0' || name[i] > '9') return std::nullopt;
    n = n * 10 + (name[i] - '0');
  }
  return n;
}

inline std::string SlotName(int n) {
  char buf[8];
  std::snprintf(buf, sizeof(buf), "Hero%03d", n);
  return buf;
}

// The lowest free slot, like a stock New Game (the game took Hero001 next to
// Hero000, Hero002 next to Hero000/Hero001). Names that are not slots are ignored.
inline std::optional<std::string> NextSlot(const std::vector<std::string>& existing) {
  std::set<int> used;
  for (const auto& e : existing)
    if (auto n = SlotNumber(e)) used.insert(*n);
  for (int n = 0; n < kMaxSlots; ++n)
    if (!used.count(n)) return SlotName(n);
  return std::nullopt;
}

inline void put_be32(std::array<uint8_t, kHeaderSize>& h, size_t at, uint32_t v) {
  for (int i = 0; i < 4; ++i) h[at + i] = static_cast<uint8_t>(v >> (24 - 8 * i));
}

// Header for slot `name` owned by `xuid` (the XUID folder the slot lives in).
inline std::array<uint8_t, kHeaderSize> MakeHeader(const std::string& name, uint64_t xuid) {
  std::array<uint8_t, kHeaderSize> h{};
  put_be32(h, 0x000, 1);
  put_be32(h, 0x004, 1);
  const auto n = SlotNumber(name);
  const std::string display = "Hero " + std::to_string(n ? *n + 1 : 0);
  for (size_t i = 0; i < display.size() && i < 127; ++i) {
    h[0x008 + 2 * i] = 0;
    h[0x008 + 2 * i + 1] = static_cast<uint8_t>(display[i]);
  }
  for (size_t i = 0; i < name.size() && i < 41; ++i) h[0x108 + i] = static_cast<uint8_t>(name[i]);
  put_be32(h, 0x138, static_cast<uint32_t>(xuid >> 32));
  put_be32(h, 0x13C, static_cast<uint32_t>(xuid));
  put_be32(h, 0x140, kTitleId);
  return h;
}

// "B13EBABEBABEBABE" -> its value; the XUID folder name of a profile's saves.
inline std::optional<uint64_t> ParseXuidFolder(const std::string& s) {
  if (s.size() != 16) return std::nullopt;
  uint64_t v = 0;
  for (char c : s) {
    int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'A' && c <= 'F') ? c - 'A' + 10
          : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
    if (d < 0) return std::nullopt;
    v = (v << 4) | static_cast<uint64_t>(d);
  }
  return v;
}

}  // namespace fable2::coopslot
