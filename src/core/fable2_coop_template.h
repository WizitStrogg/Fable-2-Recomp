// fable_2 - co-op campaign starter save (research.md "P2 -> fallback A").
//
// A new co-op campaign starts from a template slot made once from a normal
// playthrough, saved as soon as the adult hero wakes at the Gypsy camp
// (D:\Fable2-LAN\PLAY-CHILDHOOD.txt). The game's own gameflow skip from a
// fresh New Game never reached Bower Lake on the rig (launches 4-11), so the
// skip stays behind coop_childhood_skip (off) for a later "play the
// childhood" option.
//
// Template layout, next to the exe:
//   coop_template\template.txt   gender=M|F (the template hero), source=...
//   coop_template\slot\          the slot files (mainsave.bin, herosave.bin, ...)
// CreateCampaignSlot copies it into the next free saves\<XUID>\4D5307F1\00000001\HeroNNN
// with a fresh saveuid.bin and a content header owned by that XUID, so the
// game lists it and Continue (newest save) loads it.

#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include <rex/cvar.h>
#include <rex/logging.h>

#include "fable2_coop_slot.h"

REXCVAR_DEFINE_BOOL(coop_childhood_skip, false, "Network",
                    "Co-op campaign: start with the game's own gameflow skip from a fresh New "
                    "Game instead of the starter save (experimental, does not work yet)");

namespace fable2::cooptemplate {

namespace fs = std::filesystem;

inline fs::path exe_dir() {
#ifdef _WIN32
  wchar_t buf[MAX_PATH] = {};
  ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
  return fs::path(buf).parent_path();
#else
  return fs::current_path();
#endif
}

inline fs::path template_dir() { return exe_dir() / "coop_template"; }

struct TemplateInfo {
  bool female = false;
  std::string source;
};

inline std::optional<TemplateInfo> ReadTemplate() {
  std::ifstream in(template_dir() / "template.txt");
  if (!in || !fs::exists(template_dir() / "slot" / "mainsave.bin")) return std::nullopt;
  TemplateInfo t;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.rfind("gender=", 0) == 0) t.female = line.substr(7) == "F";
    if (line.rfind("source=", 0) == 0) t.source = line.substr(7);
  }
  return t;
}

// The profile's save folder: saves\<16 hex digit XUID>\4D5307F1. With more
// than one, the most recently written wins (one profile per PC in practice).
inline std::optional<fs::path> ProfileTitleDir(uint64_t* xuid_out) {
  std::error_code ec;
  std::optional<fs::path> best;
  fs::file_time_type best_time{};
  for (const auto& e : fs::directory_iterator(exe_dir() / "saves", ec)) {
    if (!e.is_directory()) continue;
    const auto xuid = fable2::coopslot::ParseXuidFolder(e.path().filename().string());
    const fs::path title = e.path() / "4D5307F1";
    if (!xuid || !fs::is_directory(title)) continue;
    const auto t = fs::last_write_time(title, ec);
    if (!best || t > best_time) {
      best = title;
      best_time = t;
      if (xuid_out) *xuid_out = *xuid;
    }
  }
  return best;
}

struct NewSlot {
  std::string name;  // HeroNNN
  fs::path path;     // the slot folder
};

// The profile's save slots (folders under 00000001 that hold a mainsave.bin).
inline std::vector<NewSlot> Slots() {
  std::vector<NewSlot> out;
  const auto title = ProfileTitleDir(nullptr);
  if (!title) return out;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(*title / "00000001", ec))
    if (e.is_directory() && fs::exists(e.path() / "mainsave.bin"))
      out.push_back({e.path().filename().string(), e.path()});
  return out;
}

// The slot the game's Continue loads: the most recently saved one.
inline std::optional<NewSlot> NewestSlot() {
  std::optional<NewSlot> best;
  fs::file_time_type best_time{};
  std::error_code ec;
  for (auto& s : Slots()) {
    const auto t = fs::last_write_time(s.path / "mainsave.bin", ec);
    if (ec) continue;
    if (!best || t > best_time) {
      best = s;
      best_time = t;
    }
  }
  return best;
}

// Copies the template into the next free slot. Returns nothing (and leaves
// nothing behind) on any failure.
inline std::optional<NewSlot> CreateCampaignSlot(std::string* error) {
  auto fail = [&](const std::string& why) -> std::optional<NewSlot> {
    if (error) *error = why;
    REXSYS_ERROR("[coop] starter save: {}", why);
    return std::nullopt;
  };
  if (!ReadTemplate()) return fail("no template in " + template_dir().string());
  uint64_t xuid = 0;
  const auto title = ProfileTitleDir(&xuid);
  if (!title) return fail("no profile save folder under " + (exe_dir() / "saves").string());

  std::error_code ec;
  const fs::path slots = *title / "00000001";
  const fs::path headers = *title / "Headers" / "00000001";
  std::vector<std::string> existing;
  for (const auto& e : fs::directory_iterator(slots, ec)) existing.push_back(e.path().filename().string());
  const auto name = fable2::coopslot::NextSlot(existing);
  if (!name) return fail("no free slot");
  const fs::path dst = slots / *name;
  const fs::path header = headers / (*name + ".header");
  if (fs::exists(dst) || fs::exists(header)) return fail(*name + " already exists");

  auto undo = [&] {
    std::error_code ec2;
    fs::remove_all(dst, ec2);
    fs::remove(header, ec2);
  };
  fs::create_directories(dst, ec);
  fs::create_directories(headers, ec);
  for (const auto& e : fs::directory_iterator(template_dir() / "slot", ec)) {
    if (!e.is_regular_file() || e.path().filename() == "saveuid.bin") continue;
    fs::copy_file(e.path(), dst / e.path().filename(), fs::copy_options::overwrite_existing, ec);
    if (ec) {
      undo();
      return fail("copy " + e.path().filename().string() + ": " + ec.message());
    }
  }
  // A fresh saveuid: the slot is a new save of its own, not the template's.
  std::random_device rd;
  std::array<uint8_t, 8> uid{};
  for (auto& b : uid) b = static_cast<uint8_t>(rd());
  const auto head = fable2::coopslot::MakeHeader(*name, xuid);
  {
    std::ofstream u(dst / "saveuid.bin", std::ios::binary);
    u.write(reinterpret_cast<const char*>(uid.data()), uid.size());
    std::ofstream h(header, std::ios::binary);
    h.write(reinterpret_cast<const char*>(head.data()), head.size());
    if (!u || !h) {
      undo();
      return fail("writing saveuid/header failed");
    }
  }
  // copy_file keeps the template's times: make the slot the newest save.
  fs::last_write_time(dst / "mainsave.bin", fs::file_time_type::clock::now(), ec);
  REXSYS_INFO("[coop] starter save: created {} for XUID {:016X} from {}", dst.string(), xuid,
              template_dir().string());
  return NewSlot{*name, dst};
}

}  // namespace fable2::cooptemplate
