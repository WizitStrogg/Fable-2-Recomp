#pragma once
// fable2_gold_watch.h - testing only (coop_gold_watch): what pays player 2 its
// 1 gold every ~2 minutes in a co-op campaign (live session and rig,
// 2026-10-27; Debug.SetHenchmanGoldValue(0) does not stop it). Steps, each a
// value of the coop_gold_watch cvar (test input: "cvar coop_gold_watch find:9871"):
//   find:<n>    after Money.Set(player 2, n) from Lua: every 4-byte
//               big-endian n in the game heap (the shared purse merge is
//               held from the first step on)
//   pick:<n>    after Money.Set(player 2, n): keep the candidates now n
//   watch       arm a write watch on the one candidate left (or watch:<hex>)
// A write to it logs the thread and the host instruction (exe-relative;
// resolve with fable_2.pdb to the recompiled sub_<guest address>) and the
// value; the watch stays armed (other writes on the page are single-stepped).

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include <fmt/format.h>
#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_STRING(coop_gold_watch, "", "Network",
                      "Testing: find and watch player 2's gold (find:<n>, pick:<n>, watch[:<hex>])");

namespace fable2::goldwatch {

namespace ta = fable2::textappend;
constexpr uintptr_t kPage = 0x1000;
inline std::vector<uint32_t> g_candidates;  // guest addresses (watch thread only)
inline std::atomic<uint32_t> g_addr{0};     // watched guest address
inline std::atomic<uintptr_t> g_page{0};    // its host page while armed
inline std::atomic<uintptr_t> g_host{0};    // its host address
thread_local uintptr_t t_reprotect = 0;

struct Catch {
  uintptr_t rip;
  DWORD tid;
};
inline std::atomic<int> g_head{0}, g_tail{0};
inline Catch g_ring[64];

inline LONG CALLBACK handler(PEXCEPTION_POINTERS info) {
  const EXCEPTION_RECORD* r = info->ExceptionRecord;
  if (r->ExceptionCode == EXCEPTION_SINGLE_STEP && t_reprotect) {
    const uintptr_t page = t_reprotect;
    t_reprotect = 0;
    info->ContextRecord->EFlags &= ~0x100u;
    DWORD old = 0;
    if (g_page.load() == page) VirtualProtect(reinterpret_cast<void*>(page), kPage, PAGE_READONLY, &old);
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  if (r->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || r->NumberParameters < 2 || r->ExceptionInformation[0] != 1)
    return EXCEPTION_CONTINUE_SEARCH;
  const uintptr_t page = g_page.load();
  const uintptr_t addr = r->ExceptionInformation[1];
  if (!page || addr < page || addr >= page + kPage) return EXCEPTION_CONTINUE_SEARCH;
  DWORD old = 0;
  VirtualProtect(reinterpret_cast<void*>(page), kPage, PAGE_READWRITE, &old);
  const uintptr_t host = g_host.load();
  if (addr + 4 > host && addr < host + 4) {
    const int h = g_head.load();
    if (h - g_tail.load() < 64) {
      g_ring[h % 64] = {static_cast<uintptr_t>(info->ContextRecord->Rip) -
                            reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)),
                        GetCurrentThreadId()};
      g_head.store(h + 1);
    }
  }
  t_reprotect = page;  // this write goes through; the page is protected again after it
  info->ContextRecord->EFlags |= 0x100u;
  return EXCEPTION_CONTINUE_EXECUTION;
}

inline uint32_t rd(const uint8_t* base, uint32_t a) { return ta::rdable2(base, a, 4) ? ta::load_be(base, a) : 0; }

// [from, from + len) must be readable (checked once by the caller: one
// VirtualQuery per word made the heap scan take minutes).
inline void scan(const uint8_t* base, uint32_t from, uint32_t len, uint32_t value, std::vector<uint32_t>& out) {
  for (uint32_t a = from; a + 4 <= from + len; a += 4)
    if (ta::load_be(base, a) == value) out.push_back(a);
}

inline void step(const std::string& cmd) {
  const uint8_t* base = fable2::f5lua::g_base;
  if (!base) return;
  fable2::coophero::g_hold_purse.store(true);  // the purse merge would copy the value to the host
  const auto colon = cmd.find(':');
  const std::string verb = cmd.substr(0, colon);
  const std::string arg = colon == std::string::npos ? "" : cmd.substr(colon + 1);
  if (verb == "find") {
    // The game heap (0x40000000..0x50000000, as find_view): player 2's entity
    // is not known on the host (its CECPlayer setup is not ours to see there).
    const uint32_t value = static_cast<uint32_t>(std::strtoul(arg.c_str(), nullptr, 10));
    g_candidates.clear();
    for (uint64_t page = 0x40000000; page < 0x50000000 && g_candidates.size() < 4096; page += 0x10000) {
      const uint32_t p = static_cast<uint32_t>(page);
      if (ta::rdable2(base, p, 0x10000)) scan(base, p, 0x10000, value, g_candidates);
    }
    REXSYS_INFO("[goldwatch] find {}: {} candidate(s) in the heap", value, g_candidates.size());
  } else if (verb == "pick") {
    const uint32_t value = static_cast<uint32_t>(std::strtoul(arg.c_str(), nullptr, 10));
    std::vector<uint32_t> keep;
    for (uint32_t a : g_candidates)
      if (rd(base, a) == value) keep.push_back(a);
    g_candidates = keep;
    std::string list;
    for (uint32_t a : g_candidates) list += fmt::format(" {:08X}", a);
    REXSYS_INFO("[goldwatch] pick {}: {} left:{}", value, g_candidates.size(), list);
  } else if (verb == "watch") {
    const uint32_t a = !arg.empty() ? static_cast<uint32_t>(std::strtoul(arg.c_str(), nullptr, 16))
                       : g_candidates.size() == 1 ? g_candidates[0]
                                                  : 0;
    if (!a) {
      REXSYS_INFO("[goldwatch] watch: no single candidate ({} left)", g_candidates.size());
      return;
    }
    static const bool installed = AddVectoredExceptionHandler(1, &handler) != nullptr;
    const uintptr_t host = static_cast<uintptr_t>(ta::host_addr(base, a));
    const uintptr_t page = host & ~(kPage - 1);
    MEMORY_BASIC_INFORMATION mbi{};
    if (!installed || !VirtualQuery(reinterpret_cast<void*>(page), &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
        mbi.Protect != PAGE_READWRITE) {
      REXSYS_INFO("[goldwatch] watch 0x{:08X}: page not plain read-write, not armed", a);
      return;
    }
    g_addr.store(a);
    g_host.store(host);
    g_page.store(page);
    DWORD old = 0;
    VirtualProtect(reinterpret_cast<void*>(page), kPage, PAGE_READONLY, &old);
    REXSYS_INFO("[goldwatch] watching player 2's gold at 0x{:08X} (now {})", a, rd(base, a));
  }
}

inline void thread_main() {
  std::string last;
  for (;;) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const std::string cmd = REXCVAR_GET(coop_gold_watch);
    if (cmd != last) {
      last = cmd;
      if (!cmd.empty()) step(cmd);
    }
    while (g_tail.load() < g_head.load()) {
      const Catch c = g_ring[g_tail.load() % 64];
      g_tail.fetch_add(1);
      const uint8_t* base = fable2::f5lua::g_base;
      REXSYS_INFO("[goldwatch] gold 0x{:08X} written by thread {} at exe+0x{:X} (now {})", g_addr.load(), c.tid, c.rip,
                  base ? rd(base, g_addr.load()) : 0);
    }
  }
}

inline const bool g_started = [] {
  std::thread(thread_main).detach();
  return true;
}();

}  // namespace fable2::goldwatch
