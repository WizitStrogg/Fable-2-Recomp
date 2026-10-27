#pragma once
// fable2_cam_watch.h - testing only (coop_cam_watch): which game function
// writes the joiner's view struct (fable2_coop_camera.h, the host's camera
// arriving over the network). The joiner's follow camera writes that struct
// from the render thread; the network writes it from the game's threads in
// between, so the host's camera shows through for a frame while the host
// moves (jitter, live session 2026-10-27). Overriding the writer needs its name.
//
// After each follow camera write, the host page holding the view's target
// block is made read-only. The next write to that page faults into the
// handler below, which makes the page writable again, notes the writing
// thread, the host instruction (exe-relative, resolve it with fable_2.pdb to
// the recompiled sub_<guest address>) and the offset in the view, and lets
// the write go ahead. One fault per arm; re-armed at most every 250 ms, 60
// samples per session. Faults on any other page go to the next handler.

#include <Windows.h>

#include <atomic>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(coop_cam_watch, false, "Network",
                    "Testing: log which game function writes the joiner camera's view (60 samples)");

namespace fable2::camwatch {

constexpr uintptr_t kPage = 0x1000;
inline std::atomic<uintptr_t> g_page{0};      // armed page (host address), 0 = not armed
inline std::atomic<uintptr_t> g_view_host{0};  // the view struct (host address)
inline std::atomic<DWORD> g_own_tid{0};       // the follow camera's thread (its writes are ours)
inline std::atomic<int> g_samples{0};
inline int64_t g_next_arm_ms = 0;

struct Sample {
  std::atomic<bool> ready{false};
  uintptr_t rip = 0;
  DWORD tid = 0;
  int64_t offset = 0;
};
inline Sample g_last;

inline LONG CALLBACK handler(PEXCEPTION_POINTERS info) {
  const EXCEPTION_RECORD* r = info->ExceptionRecord;
  if (r->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || r->NumberParameters < 2 || r->ExceptionInformation[0] != 1)
    return EXCEPTION_CONTINUE_SEARCH;
  const uintptr_t page = g_page.load();
  const uintptr_t addr = r->ExceptionInformation[1];
  if (!page || addr < page || addr >= page + kPage) return EXCEPTION_CONTINUE_SEARCH;
  DWORD old = 0;
  VirtualProtect(reinterpret_cast<void*>(page), kPage, PAGE_READWRITE, &old);
  g_page.store(0);
  const DWORD tid = GetCurrentThreadId();
  if (tid != g_own_tid.load() && !g_last.ready.load()) {
    g_last.rip = static_cast<uintptr_t>(info->ContextRecord->Rip) -
                 reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    g_last.tid = tid;
    g_last.offset = static_cast<int64_t>(addr) - static_cast<int64_t>(g_view_host.load());
    g_last.ready.store(true);
  }
  return EXCEPTION_CONTINUE_EXECUTION;
}

// From the follow camera, right after it wrote the view (render thread).
inline void after_write(uintptr_t view_host, uintptr_t target_host, int64_t now_ms) {
  if (g_last.ready.load()) {
    REXSYS_INFO("[camwatch] view written by thread {} at exe+0x{:X}, view+0x{:X}", g_last.tid, g_last.rip,
                g_last.offset);
    g_last.ready.store(false);
  }
  if (!REXCVAR_GET(coop_cam_watch) || g_page.load() || now_ms < g_next_arm_ms || g_samples.load() >= 60) return;
  static const bool installed = AddVectoredExceptionHandler(1, &handler) != nullptr;
  if (!installed) return;
  g_next_arm_ms = now_ms + 250;
  g_own_tid.store(GetCurrentThreadId());
  g_view_host.store(view_host);
  const uintptr_t page = target_host & ~(kPage - 1);
  MEMORY_BASIC_INFORMATION mbi{};
  if (!VirtualQuery(reinterpret_cast<void*>(page), &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
      mbi.Protect != PAGE_READWRITE) {
    return;  // not a plain read-write page (the SDK may be watching it): leave it alone
  }
  g_page.store(page);
  DWORD old = 0;
  if (!VirtualProtect(reinterpret_cast<void*>(page), kPage, PAGE_READONLY, &old)) {
    g_page.store(0);
    return;
  }
  g_samples.fetch_add(1);
}

inline const bool g_hooked = [] {
  fable2::coopcam::g_after_write = &after_write;
  return true;
}();

}  // namespace fable2::camwatch
