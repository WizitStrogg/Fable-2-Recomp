// fable_2 - research commands for the co-op joiner camera
// (research/05-client-camera.md).
//   camscan <x> <y> <z> [tol]   find guest addresses holding the float triple
//                               (big-endian), e.g. the host camera position
//   camdump <addr> [count]      log <count> floats from a guest address

#pragma once

#include <cmath>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include <rex/cvar.h>
#include <rex/logging.h>

namespace fable2::camscan {

namespace ta = fable2::textappend;

inline float rd(const uint8_t* base, uint32_t a) {
  const uint32_t v = ta::load_be(base, a);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}

inline void CmdCamScan(std::string_view args) {
  const uint8_t* base = fable2::f5lua::g_base;
  float x = 0, y = 0, z = 0, tol = 0.05f;
  const std::string s(args);
  if (!base || std::sscanf(s.c_str(), "%f %f %f %f", &x, &y, &z, &tol) < 3) {
    REXSYS_INFO("camscan: usage: camscan <x> <y> <z> [tol]");
    return;
  }
  int hits = 0;
  // Guest heap and physical ranges in 64 KiB steps; unreadable pages are skipped.
  for (uint64_t page = 0x40000000; page < 0xA0000000 && hits < 40; page += 0x10000) {
    const uint32_t p = static_cast<uint32_t>(page);
    if (!ta::rdable2(base, p, 0x10000)) continue;
    for (uint32_t a = p; a < p + 0x10000 - 12 && hits < 40; a += 4) {
      if (std::fabs(rd(base, a) - x) <= tol && std::fabs(rd(base, a + 4) - y) <= tol &&
          std::fabs(rd(base, a + 8) - z) <= tol) {
        REXSYS_INFO("camscan: hit at 0x{:08X}", a);
        ++hits;
      }
    }
  }
  REXSYS_INFO("camscan: {} hit(s) for {:.2f} {:.2f} {:.2f}", hits, x, y, z);
}

inline void CmdCamDump(std::string_view args) {
  const uint8_t* base = fable2::f5lua::g_base;
  const std::string s(args);
  char* end = nullptr;
  const uint32_t addr = static_cast<uint32_t>(std::strtoul(s.c_str(), &end, 16));
  const int count = end && *end ? std::atoi(end) : 16;
  if (!base || !addr || !ta::rdable2(base, addr, count * 4)) {
    REXSYS_INFO("camdump: usage: camdump <hexaddr> [count]");
    return;
  }
  for (int i = 0; i < count; i += 4) {
    REXSYS_INFO("camdump 0x{:08X}: {:.3f} {:.3f} {:.3f} {:.3f}  ({:08X} {:08X} {:08X} {:08X})",
                addr + i * 4, rd(base, addr + i * 4), rd(base, addr + i * 4 + 4),
                rd(base, addr + i * 4 + 8), rd(base, addr + i * 4 + 12),
                ta::load_be(base, addr + i * 4), ta::load_be(base, addr + i * 4 + 4),
                ta::load_be(base, addr + i * 4 + 8), ta::load_be(base, addr + i * 4 + 12));
  }
}

// campoke <hexaddr> <x> <y> <z> [seconds]: write the float triple to the
// address every frame for a few seconds (render thread), to see which copy of
// the camera position the renderer uses.
inline std::atomic<uint32_t> g_poke_addr{0};
inline float g_poke[3] = {0, 0, 0};
inline std::atomic<int64_t> g_poke_until_ms{0};

inline void CmdCamPoke(std::string_view args) {
  const std::string s(args);
  unsigned addr = 0;
  float x = 0, y = 0, z = 0, secs = 3;
  if (std::sscanf(s.c_str(), "%x %f %f %f %f", &addr, &x, &y, &z, &secs) < 4) {
    REXSYS_INFO("campoke: usage: campoke <hexaddr> <x> <y> <z> [seconds]");
    return;
  }
  g_poke[0] = x; g_poke[1] = y; g_poke[2] = z;
  g_poke_addr.store(addr);
  g_poke_until_ms.store(std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count() +
                        int64_t(secs * 1000));
  REXSYS_INFO("campoke: writing {:.2f} {:.2f} {:.2f} to 0x{:08X} for {:.1f} s", x, y, z, addr, secs);
}

inline void poke_tick() {
  uint8_t* base = fable2::f5lua::g_base;
  const uint32_t addr = g_poke_addr.load();
  if (!base || !addr) return;
  const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();
  if (now > g_poke_until_ms.load()) {
    g_poke_addr.store(0);
    return;
  }
  if (!ta::wr2(base, addr, 12)) return;
  for (int i = 0; i < 3; ++i) {
    uint32_t v;
    std::memcpy(&v, &g_poke[i], 4);
    v = (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
    std::memcpy(reinterpret_cast<void*>(ta::host_addr(base, addr + i * 4)), &v, 4);
  }
}

}  // namespace fable2::camscan

REXCVAR_DEFINE_COMMAND_ARGS(camscan, fable2::camscan::CmdCamScan, "Network",
                            "Research: find a float triple in guest memory");
REXCVAR_DEFINE_COMMAND_ARGS(campoke, fable2::camscan::CmdCamPoke, "Network",
                            "Research: hold a float triple at a guest address for a few seconds");
REXCVAR_DEFINE_COMMAND_ARGS(camdump, fable2::camscan::CmdCamDump, "Network",
                            "Research: dump floats at a guest address");
