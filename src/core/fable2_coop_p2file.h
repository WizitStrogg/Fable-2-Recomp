// fable_2 - co-op campaign player 2 hero file (data-model.md, T030/T031).
//
// coop_player2.txt sits in the host's campaign slot folder next to the game's
// own files and holds player 2's hero as a COOPHERO:v2 line:
//   # Fable II co-op campaign player 2 (feature 001). Written by the game; do not edit.
//   saveuid=<16 hex digits>   the slot's saveuid.bin: this file belongs to this slot
//   stamp=<16 hex digits>     FNV-1a 64 of the slot's mainsave.bin right after the save
//   written=<UTC time>
//   source=capture|cache      fresh capture at the save, or the last one taken
//                             (player 2 had already left, e.g. the partner-loss save)
//   COOPHERO:v2|...
// saveuid only changes when a slot is made, so the stamp tells whether the
// file matches the save the game holds now (research.md: per-save stamp,
// decided with T030).
//
// Dependency-free (unit test: tools/coop_p2file_test.cpp).

#pragma once

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace fable2::coopp2file {

struct P2File {
  std::string saveuid;  // 16 hex digits
  std::string stamp;    // 16 hex digits
  std::string written;
  std::string source = "capture";
  std::string profile;  // COOPHERO:v2|...
};

inline std::string Hex64(uint64_t v) {
  char buf[17];
  std::snprintf(buf, sizeof(buf), "%016llX", static_cast<unsigned long long>(v));
  return buf;
}

inline std::string HexBytes(std::string_view bytes) {
  std::string s;
  char buf[3];
  for (unsigned char c : bytes) {
    std::snprintf(buf, sizeof(buf), "%02X", c);
    s += buf;
  }
  return s;
}

// FNV-1a 64 over a byte string.
inline uint64_t Fnv1a64(std::string_view bytes) {
  uint64_t h = 0xCBF29CE484222325ull;
  for (unsigned char c : bytes) {
    h ^= c;
    h *= 0x100000001B3ull;
  }
  return h;
}

inline std::string Format(const P2File& f) {
  std::string s = "# Fable II co-op campaign player 2 (feature 001). Written by the game; do not edit.\n";
  s += "saveuid=" + f.saveuid + "\n";
  s += "stamp=" + f.stamp + "\n";
  s += "written=" + f.written + "\n";
  s += "source=" + f.source + "\n";
  s += f.profile + "\n";
  return s;
}

// Nothing unless saveuid, stamp and a COOPHERO:v2 line are present.
inline std::optional<P2File> Parse(std::string_view text) {
  P2File f;
  while (!text.empty()) {
    const size_t nl = text.find('\n');
    std::string_view line = text.substr(0, nl);
    text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    auto value = [&](std::string_view key) -> std::optional<std::string> {
      if (line.substr(0, key.size()) != key) return std::nullopt;
      return std::string(line.substr(key.size()));
    };
    if (auto v = value("saveuid=")) f.saveuid = *v;
    else if (auto v2 = value("stamp=")) f.stamp = *v2;
    else if (auto v3 = value("written=")) f.written = *v3;
    else if (auto v4 = value("source=")) f.source = *v4;
    else if (line.substr(0, 12) == "COOPHERO:v2|") f.profile = std::string(line);
  }
  if (f.saveuid.empty() || f.stamp.empty() || f.profile.empty()) return std::nullopt;
  return f;
}

enum class Match {
  kCurrent,   // same slot, same save: use it
  kOlder,     // same slot, the game saved again since (no capture then): use it, say so
  kOtherSlot  // from another slot (copied folder): do not use
};

inline Match Check(const P2File& f, std::string_view saveuid_now, std::string_view stamp_now) {
  if (f.saveuid != saveuid_now) return Match::kOtherSlot;
  return f.stamp == stamp_now ? Match::kCurrent : Match::kOlder;
}

}  // namespace fable2::coopp2file
