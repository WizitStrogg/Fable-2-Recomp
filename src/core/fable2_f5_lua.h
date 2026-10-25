// fable_2 - F5 host command: run an external Lua file in the in-game Lua
// state, using the game's own CScriptManager::RunScript (C++ core API).
//
// Pressing F5 (host keyboard) runs the external Lua file exactly the way the
// Lua "RunScript(path)" global does, but from the host side. The user supplies
// the .lua file (default data/scripts/recomp/F5.lua); this just triggers it.
//
// How it works (all hooks are recompiled-function probes so they have ctx+base):
//   A) poll_f5() is called per frame from the host input poll; it edge-detects
//      F5 and latches a flag (g_f5_pending).
//   B) run_pending() runs on the script VM thread only. Main point: right
//      after the engine's own outermost lua_pcall / lua_resume into a Lua VM
//      returns (overrides at the end of this file), i.e. in engine C++ code
//      with no Lua running. Also, mainly during loading:
//      once per frame from the FrameLimiterWait_82242628 hot function
//      when no Lua->C++ call is active, and from the dispatcher probe (D)
//      right after the game's own Lua->C++ call returns at the outermost
//      dispatch depth. If neither depth-0 point has been reached for 3 s (the
//      game's Lua can run entirely inside one long-lived dispatch), it also
//      drains at the shallowest dispatch depth seen in the last second
//      ("baseline"), after that call returns. When the flag is latched or a
//      script is queued it calls run_external().
//   C) run_external() builds a game (refcounted) string for the path in guest
//      memory and calls the captured CScriptManager::RunScript method.
//   D) The dispatcher probe (LuaBind_RunScript_82806168) captures the
//      (this, method) of the RunScript callable: the binding dispatches to a
//      member fn, so the wrapper holds (this=instance, method=fn). We pin the
//      (this, method) whose string argument ends in ".lua".
//
// The Lua VM is single-threaded: it must only be entered from the thread that
// runs the game's own Lua. MainRenderLoop runs on the "3D Engine" render
// thread, so it must NOT run scripts (doing so corrupted the VM and crashed
// GameThread). The probe records the thread of the game's boot-time RunScript
// call, and run_pending() refuses to run on any other thread.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "rex/cvar.h"
#include <rex/logging.h>
#include "rex/ppc/context.h"  // PPCContext

// Guest-memory helpers (defined in src/diagnostics/fable2_text_append.h, which
// is included later in main.cpp). We only need these five; forward-declare them
// here so this header can be included before fable2_text_append.h. The inline
// definitions in fable2_text_append.h satisfy these declarations.
namespace fable2::textappend {
inline uint64_t host_addr(const uint8_t* base, uint32_t a);
inline bool rdable2(const uint8_t* base, uint32_t a, size_t n);
inline bool wr2(const uint8_t* base, uint32_t a, size_t n);
inline uint32_t load_be(const uint8_t* base, uint32_t a);
inline uint32_t guest_alloc(PPCContext& ctx, uint8_t* base, size_t size);
}

#ifdef _WIN32
#include <windows.h>
#include <share.h>
#define FABLE2_F5_VK 0x74  // VK_F5
#endif

// External Lua file to run on F5. Path is resolved by CScriptManager::RunScript
// relative to the VFS root (data/). Default lands at
// <gamedataroot>/data/scripts/recomp/F5.lua.
REXCVAR_DEFINE_BOOL(coop_lua_runner, true, "Network",
                    "Run co-op and console Lua scripts in the game (camera, own hero, "
                    "story capture, the lua console). Untick if the game misbehaves; "
                    "plain co-op keeps working without it.");

REXCVAR_DEFINE_STRING(
    f5_lua_path,
    "scripts/recomp/F5.lua",
    "Input",
    "External Lua file run by pressing F5 (host keyboard). Path as the "
    "CScriptManager::RunScript / loadfile resolves it, relative to the VFS "
    "root (data/). Default scripts/recomp/F5.lua -> <gamedataroot>/data/scripts/recomp/F5.lua.");

// Current game-state name (defined in src/diagnostics/fable2_state_probe.h,
// which is included later in main.cpp). Logged before each RunScript call so
// a crash is self-explanatory (front-end vs. gameplay vs. cutscene). Must be
// declared at top level (NOT inside fable2::f5lua - nested namespace
// declarations nest, which would create fable2::f5lua::fable2::stateprobe).
namespace fable2::stateprobe {
const char* CurrentStateName();
}

namespace fable2::f5lua {

// Guest-memory helpers live in fable2::textappend (src/diagnostics/fable2_text_append.h).
namespace ta = fable2::textappend;

// The captured CScriptManager "run file" callable (guest addresses).
// The FIRST .lua dispatch (boot: 'miscellaneous/GeneralScriptManager.lua' via
// the canonical file-runner method) is pinned; later same-method dispatches
// refresh `this` in case the manager instance moves (a stale instance
// null-dereferences guest memory - the "read of guest 0x0" access violation).
// Dispatches of OTHER methods that also take a .lua path are ignored.
inline std::atomic<uint32_t> g_this{0};   // CScriptManager instance
inline std::atomic<uint32_t> g_method{0}; // member fn (CScriptManager::RunScript)
// this[0] as read at capture time (vtable-like; stable for the object's
// lifetime). A freed/reused block almost always differs, which catches the
// stale-`this` case even when the new first word happens to be a plausible
// pointer.
inline std::atomic<uint32_t> g_expected_head{0};

// Host thread id of the script VM, recorded by the dispatcher probe at the
// game's boot-time RunScript call (the first capture; 0 = not seen yet).
inline std::atomic<uint32_t> g_lua_thread{0};
// Bumped each time the game itself runs a script file through RunScript (boot,
// level loads); host code uses it as a level-load event. now-ms of the last.
inline std::atomic<uint32_t> g_load_epoch{0};
inline std::atomic<int64_t> g_load_at_ms{0};
inline uint32_t current_thread_id() {
#ifdef _WIN32
  return static_cast<uint32_t>(::GetCurrentThreadId());
#else
  return 0;
#endif
}

// Monotonic F5 run counter: the crash log (fable2_f5_lua.log is flushed per
// line) always shows the last run number before a hard crash.
inline std::atomic<uint32_t> g_run_count{0};

// Latched by the per-frame F5 poll; consumed by run_pending.
inline std::atomic<bool> g_f5_pending{false};

// Script paths queued by host code (the `lua` / `luafile` console commands),
// run one per frame by run_pending on the game's Lua thread.
inline std::mutex g_queue_mtx;
inline std::deque<std::string> g_queue;
// g_queue.size(), readable without the lock (the drain is polled per dispatch).
inline std::atomic<size_t> g_queued{0};
inline void request_run(std::string path) {
  std::lock_guard<std::mutex> l(g_queue_mtx);
  g_queue.push_back(std::move(path));
  g_queued.store(g_queue.size());
}
// True while an entry for `path` still waits in the queue.
inline bool is_queued(const std::string& path) {
  std::lock_guard<std::mutex> l(g_queue_mtx);
  return std::find(g_queue.begin(), g_queue.end(), path) != g_queue.end();
}

// Host directory that holds data/ (set from Fable2App::OnLoadXexImage), so
// host code can write script files the game VFS reads.
inline std::filesystem::path g_data_root;

// True once the RunScript callable has been captured (the game finished
// loading its own scripts), i.e. queued scripts can run.
inline bool ready() { return g_this.load() != 0 && g_method.load() != 0; }
inline bool g_f5_down_prev = false;

inline std::mutex& log_mtx() { static std::mutex m; return m; }
inline std::FILE* logf() {
  static std::FILE* f = [] {
    // Release build: don't create the F5-Lua log file. logline() is
    // null-guarded, so the F5 feature runs without logging.
    return nullptr;
#if defined(_WIN32)
    // // Open with shared read/write so the log can be read live while the game
    // // holds it open (plain fopen() takes an exclusive lock on Windows).
    // std::FILE* out = ::_fsopen("fable2_f5_lua.log", "a", _SH_DENYNO);
    // return out;
#else
    // return std::fopen("fable2_f5_lua.log", "a");
#endif
  }();
  return f;
}
inline void logline(const char* fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  std::lock_guard<std::mutex> l(log_mtx());
  if (std::FILE* f = logf()) {
    std::fputs(buf, f);
    std::fflush(f);
  }
}

// Per-frame F5 edge detector. Call from the host input poll (per frame).
inline void poll_f5() {
#ifdef _WIN32
  // GetAsyncKeyState sees F5 pressed in any application; only count it while
  // this game's window has focus.
  DWORD fg_pid = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &fg_pid);
  const bool focused = fg_pid == GetCurrentProcessId();
  const bool down = focused && (GetAsyncKeyState(FABLE2_F5_VK) & 0x8000) != 0;
  const bool edge = down && !g_f5_down_prev;
  g_f5_down_prev = down;
  if (edge) g_f5_pending.store(true);
#endif
}

// Run the external Lua file via the captured CScriptManager::RunScript.
// Returns true if invoked. Must be called from the game's Lua thread.
inline bool run_external(PPCContext& ctx, uint8_t* base, const std::string& path) {
  const uint32_t this_ptr = g_this.load();
  const uint32_t method = g_method.load();
  if (this_ptr == 0 || method == 0) {
    logline("[f5] requested but RunScript callable not captured yet\n");
    return false;
  }
  if (path.empty()) return false;

  // Safety: the captured method must resolve to a real recompiled guest
  // function, otherwise REX_CALL_INDIRECT_FUNC will jump to a bad pointer.
  if (method < 0x82000000 || method > 0x82FFFFFF ||
      !rex::runtime::ResolveIndirectFunction(method)) {
    logline("[f5] method=0x%08X does not resolve; not calling\n", method);
    return false;
  }

  // Safety: the captured CScriptManager instance must still be alive. RunScript
  // reads *(this) first and calls through it, so a freed/recreated manager
  // (head == 0, 0xFFFFFFFF, 0xCD.. free-fill, or an otherwise implausible
  // pointer) null-dereferences downstream. Skip this run; the next .lua
  // dispatch refreshes the capture. (Log line is flushed, so the run counter
  // below proves the guard fired before any crash.)
  if (!ta::rdable2(base, this_ptr, 8)) {
    logline("[f5] this=0x%08X no longer readable; skipping (capture refreshes)\n",
            this_ptr);
    return false;
  }
  const uint32_t head = ta::load_be(base, this_ptr);
  const bool head_plausible =
      (head >= 0x1000 && head < 0x82000000 && ta::rdable2(base, head, 4)) ||
      (head >= 0x82000000 && head <= 0x82FFFFFF);
  if (head == 0 || head == 0xFFFFFFFF || !head_plausible) {
    logline("[f5] this=0x%08X head=0x%08X not a live manager; skipping\n", this_ptr,
            head);
    return false;
  }
  const uint32_t expected_head = g_expected_head.load();
  if (expected_head != 0 && head != expected_head) {
    logline("[f5] this=0x%08X head=0x%08X != expected 0x%08X (manager moved or "
            "freed); skipping\n",
            this_ptr, head, expected_head);
    return false;
  }

  const uint32_t run_n = g_run_count.fetch_add(1) + 1;
  logline("[f5] run #%u state='%s'\n", run_n, fable2::stateprobe::CurrentStateName());

  // 1) Source C-string in guest memory.
  const uint32_t src = ta::guest_alloc(ctx, base, path.size() + 1);
  if (!src || !ta::wr2(base, src, path.size() + 1)) {
    logline("[f5] src alloc failed src=0x%08X\n", src);
    return false;
  }
  std::memcpy(reinterpret_cast<void*>(ta::host_addr(base, src)), path.data(), path.size());
  *reinterpret_cast<char*>(ta::host_addr(base, src + path.size())) = '\0';

  // 2) String object (the game's refcounted String).
  const uint32_t str_obj = ta::guest_alloc(ctx, base, 64);
  if (!str_obj || !ta::wr2(base, str_obj, 64)) {
    logline("[f5] str_obj alloc failed str_obj=0x%08X\n", str_obj);
    return false;
  }
  for (uint32_t i = 0; i < 64; i += 4) {
    uint32_t z = 0;
    std::memcpy(reinterpret_cast<void*>(ta::host_addr(base, str_obj + i)), &z, 4);
  }

  // DIAG: trace each guest step so fable2_f5_lua.log shows the last step before
  // a hard crash (the log is flushed per line).
  logline("[f5] step1 src=0x%08X str_obj=0x%08X this=0x%08X head=0x%08X method=0x%08X "
          "-> ConstructRefCounted\n",
          src, str_obj, this_ptr, head, method);
  // ConstructRefCounted_8222CF18(dest, cstring, len=-1): build the game string.
  ctx.r3.u32 = str_obj;
  ctx.r4.u32 = src;
  ctx.r5.u32 = 0xFFFFFFFF;
  REX_CALL_INDIRECT_FUNC(0x8222CF18);
  logline("[f5] step2 after ConstructRefCounted -> RunScript\n");

  // CScriptManager::RunScript(this, const String& path).
  ctx.r3.u32 = this_ptr;
  ctx.r4.u32 = str_obj;
  REX_CALL_INDIRECT_FUNC(method);
  logline("[f5] step3 after RunScript -> (keeping string alive) done\n");

  // NOTE: we deliberately do NOT call DestructRefCounted on str_obj here.
  // CScriptManager::RunScript may defer the actual file load/compile to a later
  // frame; freeing the path String synchronously would be a use-after-free the
  // moment the deferred load dereferences it (observed as a "read of guest 0x5"
  // access violation, intermittent, only when the menu is up). Leaving the
  // string's refcount at 1 keeps it valid for the deferred load. The small
  // per-press leak is acceptable for an on-demand utility.
  // (src C-string is likewise intentionally left allocated.)

  logline("[f5] ran external lua: %s (this=0x%08X method=0x%08X)\n", path.c_str(), this_ptr,
          method);
  return true;
}

// Set while a script is being started: the script's own Lua->C++ calls come
// back through the dispatcher probe and must not start another one.
inline std::atomic<bool> g_running{false};
// Dispatcher nesting depth on this thread (the probe is re-entered when a
// bound C++ method calls back into Lua and Lua calls another bound method).
inline thread_local int g_dispatch_depth = 0;
inline int64_t g_last_run_ms = 0;
// Diagnostics, logged once a minute by poll_mainloop and then reset.
inline std::atomic<uint32_t> g_probe_hits{0}, g_probe_depth0{0}, g_probe_vm{0};
inline std::atomic<uint32_t> g_frame_hits{0}, g_frame_vm{0};
inline std::atomic<int> g_max_depth{0};
// Nesting of lua_pcall / lua_resume (either VM) on this thread. Both catch Lua
// errors internally (setjmp) and always return normally, so this cannot be
// skipped by a longjmp and never leaks.
inline thread_local int g_vm_depth = 0;
std::string vm_report();
// Baseline tracking (script VM thread only): the shallowest dispatch depth
// seen in the current/last ~1 s window, and when a depth-0 point was last
// reached on the VM thread.
inline int g_window_min_depth = 1 << 30;
inline int g_baseline_depth = 0;  // 0 = not known yet
inline int64_t g_window_start_ms = 0;
inline std::atomic<int64_t> g_last_depth0_ms{0};
constexpr int64_t kBaselineAfterMs = 3000;
inline std::atomic<bool> g_runner_off_logged{false};
// At most one script per frame, so a burst never runs back to back.
constexpr int64_t kMinRunGapMs = 16;

inline int64_t steady_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

inline size_t queued_count() {
  std::lock_guard<std::mutex> l(g_queue_mtx);
  return g_queue.size() + (g_f5_pending.load() ? 1 : 0);
}

// Native tasks: host code that must call guest functions on the script VM
// thread at a safe point (same points and guards as queued scripts).
using NativeTask = std::function<void(PPCContext&, uint8_t*)>;
inline std::mutex g_native_mtx;
inline std::deque<NativeTask> g_native_tasks;
inline std::atomic<size_t> g_native_count{0};
inline void request_native(NativeTask task) {
  std::lock_guard<std::mutex> l(g_native_mtx);
  g_native_tasks.push_back(std::move(task));
  g_native_count.store(g_native_tasks.size());
}
inline void run_native(PPCContext& ctx, uint8_t* base) {
  if (g_native_count.load() == 0 || g_running.load()) return;
  if (g_lua_thread.load() != current_thread_id()) return;
  NativeTask task;
  {
    std::lock_guard<std::mutex> l(g_native_mtx);
    if (g_native_tasks.empty()) return;
    task = std::move(g_native_tasks.front());
    g_native_tasks.pop_front();
    g_native_count.store(g_native_tasks.size());
  }
  g_running = true;
  const PPCContext saved = ctx;
  try {
    task(ctx, base);
  } catch (...) {
  }
  ctx = saved;
  g_running = false;
}

// Called by the dispatcher probe after the game's own call returned, at
// dispatch depth 0. Runs the F5 file when latched, otherwise one queued script.
inline void run_pending(PPCContext& ctx, uint8_t* base, const std::string& via) {
  run_native(ctx, base);
  if (g_running.load() || (g_queued.load() == 0 && !g_f5_pending.load())) return;
  if (!REXCVAR_GET(coop_lua_runner)) {
    std::lock_guard<std::mutex> l(g_queue_mtx);
    if (!g_runner_off_logged.exchange(true)) {
      REXSYS_INFO("[lua] coop_lua_runner is off: dropping {} queued script(s)",
                  g_queue.size());
    }
    g_queue.clear();
    g_queued.store(0);
    g_f5_pending.store(false);
    return;
  }
  g_runner_off_logged.store(false);
  if (!ready()) return;
  const uint32_t lua_thread = g_lua_thread.load();
  if (lua_thread == 0 || lua_thread != current_thread_id()) return;
  const int64_t now = steady_ms();
  if (now - g_last_run_ms < kMinRunGapMs) return;
  std::string path;
  if (g_f5_pending.exchange(false)) {
    path = REXCVAR_GET(f5_lua_path);
  } else {
    std::lock_guard<std::mutex> l(g_queue_mtx);
    if (g_queue.empty()) return;
    path = std::move(g_queue.front());
    g_queue.pop_front();
    g_queued.store(g_queue.size());
  }
  // run_external calls guest functions (ConstructRefCounted, the RunScript
  // method) which clobber the PPC registers, including the game call's return
  // values. Save the whole context and restore it afterwards.
  g_running = true;
  g_last_run_ms = now;
  const PPCContext saved = ctx;
  bool ran = false;
  try {
    ran = run_external(ctx, base, path);
  } catch (...) {
  }
  ctx = saved;
  g_running = false;
  if (ran) {
    REXSYS_INFO("[lua] ran {} via {}", path, via);
  } else {
    REXSYS_WARN("[lua] could not run {} (via {})", path, via);
  }
}

// Optional per-frame callback, e.g. the co-op module deciding to queue a
// script. Runs on the render thread: must not call guest code itself.
inline void (*g_frame_hook)() = nullptr;
inline int64_t g_next_waiting_log_ms = 0;

// Per-frame render-thread entry point. Called from the MainRenderLoop_82B9CD68
// hook (in fps_meter.h). Runs the host-side frame hook and reports scripts
// left waiting; the scripts themselves run from run_pending().
// Guest memory base, for host-side readers (set every frame).
inline uint8_t* g_base = nullptr;

inline void poll_mainloop(PPCContext& /*ctx*/, uint8_t* base) {
  g_base = base;
  if (g_frame_hook) g_frame_hook();
  const int64_t now = steady_ms();
  if (now < g_next_waiting_log_ms) return;
  const bool first = g_next_waiting_log_ms == 0;
  g_next_waiting_log_ms = now + 60000;
  if (first) return;
  if (const size_t n = queued_count(); n != 0) {
    REXSYS_INFO("[lua] {} script(s) waiting (ready={} lua_thread={} runner={})", n, ready(),
                g_lua_thread.load(), REXCVAR_GET(coop_lua_runner));
  }
  REXSYS_INFO("[lua] last minute: probe hits {}, at depth 0 {}, on VM thread {}, max depth {}; "
              "frame hook hits {}, on VM thread {}",
              g_probe_hits.exchange(0), g_probe_depth0.exchange(0), g_probe_vm.exchange(0),
              g_max_depth.exchange(0), g_frame_hits.exchange(0), g_frame_vm.exchange(0));
  REXSYS_INFO("[lua] last minute: {}", vm_report());
}

// Per-frame game-loop entry point (FrameLimiterWait_82242628). Runs queued
// Lua only on the script VM thread and only when no Lua->C++ call is active
// on it, i.e. with no Lua on the stack, like the engine's own C++ code.
inline void frame_drain(PPCContext& ctx, uint8_t* base) {
  g_frame_hits.fetch_add(1, std::memory_order_relaxed);
  if (current_thread_id() != g_lua_thread.load()) return;
  g_frame_vm.fetch_add(1, std::memory_order_relaxed);
  if (g_dispatch_depth != 0 || g_vm_depth != 0) return;
  g_last_depth0_ms.store(steady_ms(), std::memory_order_relaxed);
  run_pending(ctx, base, "frame");
}

}  // namespace fable2::f5lua

// Probe on the shared LuaPlus bound-method dispatcher (named
// "LuaBind_RunScript" by the decompiler, but it is the general thunk every
// Lua->C++ method call goes through). r3 = the bound-object for the specific
// method. We replicate the thunk's wrapper build to read (this, method) and
// decode the string argument (element 1 of the bound-object, whose C-string
// sits at *(bound[12]) + 16). We pin the (this, method) whose argument ends in
// ".lua" — that is the real CScriptManager::RunScript callable.
extern "C" void LuaBind_RunScript_82806168(PPCContext& ctx, uint8_t* base) {
  using namespace fable2::f5lua;
  const uint32_t bound = ctx.r3.u32;  // bound-object for this method call

  if (bound != 0 && bound != 0xFFFFFFFF && ta::rdable2(base, bound, 16)) {
    // Replicate the dispatcher: ProcessAndProcessAndProcess206(-10003, bound)
    // -> wrapper, wrapper[0]=this, wrapper[4]=method.
    const uint64_t save_r3 = ctx.r3.u64;
    ctx.r4.s64 = -10003;
    ctx.r5.u32 = bound;
    REX_CALL_INDIRECT_FUNC(0x822281F8);
    const uint32_t wrapper = ctx.r3.u32;
    ctx.r3.u64 = save_r3;

    uint32_t this_ptr = 0, method = 0;
    if (wrapper != 0 && wrapper != 0xFFFFFFFF && ta::rdable2(base, wrapper, 8)) {
      this_ptr = ta::load_be(base, wrapper + 0);
      method = ta::load_be(base, wrapper + 4);
    }

    // Decode the string argument: element 1 address = bound[12]; the C-string
    // is at *(element1) + 16 (see ProcessAndProcessAndProcess263).
    char pathbuf[96] = {};
    if (ta::rdable2(base, bound + 12, 4)) {
      const uint32_t elem1_addr = ta::load_be(base, bound + 12);
      if (elem1_addr != 0 && elem1_addr != 0xFFFFFFFF && ta::rdable2(base, elem1_addr, 4)) {
        const uint32_t str_data = ta::load_be(base, elem1_addr);
        const uint32_t path_addr = str_data + 16;
        if (path_addr != 0 && ta::rdable2(base, path_addr, sizeof(pathbuf))) {
          std::memcpy(pathbuf, (void*)ta::host_addr(base, path_addr), sizeof(pathbuf) - 1);
        }
      }
    }
    pathbuf[95] = 0;

    if (this_ptr != 0 && method != 0 && pathbuf[0]) {
      const std::string p(pathbuf);
      const bool is_lua = p.size() > 4 && p.compare(p.size() - 4, 4, ".lua") == 0;
      // Capture the RunScript callable from .lua-arg dispatches. IMPORTANT:
      // the shared dispatcher serves MANY bound methods whose string argument
      // is a .lua path (the RunScript file runner AND register-style APIs -
      // e.g. 0x825B27D0 for 'CameraFunctions.lua', 0x825B26A8 for
      // 'AIHistory.lua'). Only the canonical file-runner method (first seen
      // at boot, 0x825ADB40, dispatched for
      // 'miscellaneous/GeneralScriptManager.lua' / 'Quests/QuestManager.lua')
      // actually runs a file - calling the others with a path does nothing.
      // So: the first capture establishes the canonical (method, this); later
      // captures refresh `this` ONLY when the method matches (the manager
      // instance can move over a session); all other methods are ignored.
      const uint32_t prev_this = g_this.load(std::memory_order_relaxed);
      const uint32_t prev_method = g_method.load(std::memory_order_relaxed);
      if (is_lua && prev_this == 0) {
        // The game's boot-time RunScript: this is the script VM's thread.
        g_lua_thread.store(current_thread_id(), std::memory_order_relaxed);
        REXSYS_INFO("[lua] script VM thread recorded: {} (first RunScript '{}')",
                    current_thread_id(), pathbuf);
      }
      if (is_lua && (prev_this == 0 || method == prev_method)) {
        g_load_epoch.fetch_add(1, std::memory_order_relaxed);
        g_load_at_ms.store(steady_ms(), std::memory_order_relaxed);
      }
      if (is_lua && (prev_this == 0 || (method == prev_method && this_ptr != prev_this))) {
        g_method.store(method, std::memory_order_relaxed);
        g_this.store(this_ptr, std::memory_order_relaxed);
        const uint32_t head_now =
            ta::rdable2(base, this_ptr, 4) ? ta::load_be(base, this_ptr) : 0;
        g_expected_head.store(head_now, std::memory_order_relaxed);
        logline("[f5] RunScript callable (re)captured this=0x%08X method=0x%08X "
                "head=0x%08X path='%s'\n",
                this_ptr, method, head_now, pathbuf);
      }
    }
  }
  const int depth = ++g_dispatch_depth;
  if (depth > g_max_depth.load(std::memory_order_relaxed)) {
    g_max_depth.store(depth, std::memory_order_relaxed);
  }
  __imp__LuaBind_RunScript_82806168(ctx, base);
  --g_dispatch_depth;
  g_probe_hits.fetch_add(1, std::memory_order_relaxed);
  const bool on_vm = current_thread_id() == g_lua_thread.load(std::memory_order_relaxed);
  if (on_vm) {
    g_probe_vm.fetch_add(1, std::memory_order_relaxed);
    // Baseline: shallowest depth seen per ~1 s window.
    const int64_t now = steady_ms();
    if (depth < g_window_min_depth) g_window_min_depth = depth;
    if (now - g_window_start_ms >= 1000) {
      g_baseline_depth = g_window_min_depth;
      g_window_min_depth = 1 << 30;
      g_window_start_ms = now;
    }
  }
  // The game's own call has finished; run queued Lua only from the outermost
  // dispatch (never while native->Lua->native calls are nested).
  if (g_dispatch_depth == 0) {
    g_probe_depth0.fetch_add(1, std::memory_order_relaxed);
    if (on_vm) g_last_depth0_ms.store(steady_ms(), std::memory_order_relaxed);
    static std::atomic<bool> s_logged{false};
    if (!s_logged.exchange(true)) {
      REXSYS_INFO("[lua] dispatcher first reached depth 0 on thread {} (script VM thread {})",
                  current_thread_id(), g_lua_thread.load());
    }
    if (g_vm_depth == 0) run_pending(ctx, base, "probe0");
  } else if (on_vm && depth == g_baseline_depth &&
             steady_ms() - g_last_depth0_ms.load(std::memory_order_relaxed) >= kBaselineAfterMs) {
    // No depth-0 point for a while: the game's Lua runs inside a long-lived
    // dispatch. Drain at the shallowest depth its calls reach every frame,
    // after this call returned (a C function re-entering the VM on the same
    // thread, as the game's own Lua RunScript binding does).
    run_pending(ctx, base, "baseline(depth " + std::to_string(depth) + ")");
  }
}

// Called from FrameLimiterWait_82242628 (src/core/hotfunc/vblank). Non-inline:
// this header is included only by main.cpp.
void fable2_frame_drain_lua(PPCContext& ctx, uint8_t* base) {
  fable2::f5lua::frame_drain(ctx, base);
}

// --- Lua VM entry points ------------------------------------------------------
// The game links two Lua 5.1 VMs. Their core functions were found from the
// error strings: the game VM (the one the LuaBind thunk and RunScript use) has
// lua_pcall sub_8219A9B0 (luaD_pcall 0x8219A8E8, f_call 0x82A1ED48, luaD_call
// 0x82228C70) and lua_resume sub_82277B88; the second VM has lua_pcall
// sub_821931E8 (luaD_pcall 0x822C05F8) and lua_resume sub_822DDF38.
//
// When the outermost call into either VM returns on the script VM thread, no
// Lua is running on it: drain one queued script there.

namespace fable2::f5lua {

struct VmEntry {
  const char* via;
  std::atomic<uint32_t> hits{0};
  std::atomic<uint32_t> on_vm_thread{0};
};
// [vm][0 = lua_pcall, 1 = lua_resume]
inline VmEntry g_vm_entries[2][2] = {{{"game-vm pcall"}, {"game-vm resume"}},
                                     {{"other-vm pcall"}, {"other-vm resume"}}};

// Per-thread totals of VM entries, so the log shows which thread owns the Lua
// traffic.
struct ThreadCount {
  uint32_t tid = 0;
  std::atomic<uint32_t> n{0};
};
inline std::mutex g_thread_counts_mtx;
inline std::vector<ThreadCount*> g_thread_counts;
inline ThreadCount& my_thread_count() {
  thread_local ThreadCount* tc = [] {
    auto* t = new ThreadCount;
    t->tid = current_thread_id();
    std::lock_guard<std::mutex> l(g_thread_counts_mtx);
    g_thread_counts.push_back(t);
    return t;
  }();
  return *tc;
}

// Entries into the game VM's luaD_call (any thread / outermost on the VM
// thread / on the VM thread). Counted at entry only, so longjmp-safe.
inline std::atomic<uint32_t> g_dcall_hits{0}, g_dcall_vm{0}, g_dcall_outer{0};

std::string vm_report() {
  std::string out = "luaD_call " + std::to_string(g_dcall_hits.exchange(0)) + " (VM thread " +
                    std::to_string(g_dcall_vm.exchange(0)) + ", outermost " +
                    std::to_string(g_dcall_outer.exchange(0)) + "), ";
  for (auto& vm : g_vm_entries) {
    for (auto& e : vm) {
      out += std::string(e.via) + " " + std::to_string(e.hits.exchange(0)) + " (VM thread " +
             std::to_string(e.on_vm_thread.exchange(0)) + "), ";
    }
  }
  std::vector<std::pair<uint32_t, uint32_t>> threads;
  {
    std::lock_guard<std::mutex> l(g_thread_counts_mtx);
    for (auto* t : g_thread_counts) threads.emplace_back(t->n.exchange(0), t->tid);
  }
  std::sort(threads.rbegin(), threads.rend());
  out += "top threads:";
  for (size_t i = 0; i < threads.size() && i < 3 && threads[i].first != 0; ++i) {
    out += " t" + std::to_string(threads[i].second) + "=" + std::to_string(threads[i].first);
  }
  return out;
}

template <int Vm, int Kind>
inline void vm_entry(PPCContext& ctx, uint8_t* base, void (*original)(PPCContext&, uint8_t*)) {
  VmEntry& e = g_vm_entries[Vm][Kind];
  ++g_vm_depth;
  original(ctx, base);
  --g_vm_depth;
  e.hits.fetch_add(1, std::memory_order_relaxed);
  my_thread_count().n.fetch_add(1, std::memory_order_relaxed);
  if (current_thread_id() != g_lua_thread.load(std::memory_order_relaxed)) return;
  e.on_vm_thread.fetch_add(1, std::memory_order_relaxed);
  if (g_vm_depth == 0 && g_dispatch_depth == 0) run_pending(ctx, base, e.via);
}

}  // namespace fable2::f5lua

extern "C" void sub_8219A9B0(PPCContext& ctx, uint8_t* base) {  // game VM lua_pcall
  fable2::f5lua::vm_entry<0, 0>(ctx, base, __imp__sub_8219A9B0);
}
extern "C" void sub_82277B88(PPCContext& ctx, uint8_t* base) {  // game VM lua_resume
  fable2::f5lua::vm_entry<0, 1>(ctx, base, __imp__sub_82277B88);
}
extern "C" void sub_821931E8(PPCContext& ctx, uint8_t* base) {  // other VM lua_pcall
  fable2::f5lua::vm_entry<1, 0>(ctx, base, __imp__sub_821931E8);
}
extern "C" void sub_822DDF38(PPCContext& ctx, uint8_t* base) {  // other VM lua_resume
  fable2::f5lua::vm_entry<1, 1>(ctx, base, __imp__sub_822DDF38);
}

// Game VM luaD_call (0x82228C70; L->nCcalls is the u16 at L+52, "C stack
// overflow" at 200). Every C->Lua call goes through it, including the
// engine's own unprotected lua_call. At ENTRY with nCcalls == 0 on the VM
// thread, outside any lua_pcall/lua_resume and LuaBind dispatch, no Lua is
// running: the engine is about to start an outermost call, so drain there,
// before it. Nothing is counted on the way out, so a longjmp cannot unbalance
// anything.
extern "C" void ProcessAndIncrementAndProcess3_82228C70(PPCContext& ctx, uint8_t* base) {
  using namespace fable2::f5lua;
  g_dcall_hits.fetch_add(1, std::memory_order_relaxed);
  if (current_thread_id() == g_lua_thread.load(std::memory_order_relaxed)) {
    g_dcall_vm.fetch_add(1, std::memory_order_relaxed);
    const uint32_t L = ctx.r3.u32;
    if (L != 0 && ta::rdable2(base, L + 52, 2)) {
      const uint8_t* p = reinterpret_cast<const uint8_t*>(ta::host_addr(base, L + 52));
      const uint16_t n_ccalls = static_cast<uint16_t>((p[0] << 8) | p[1]);
      if (n_ccalls == 0) {
        g_dcall_outer.fetch_add(1, std::memory_order_relaxed);
        if (g_vm_depth == 0 && g_dispatch_depth == 0) run_pending(ctx, base, "luaD_call entry");
      }
    }
  }
  __imp__ProcessAndIncrementAndProcess3_82228C70(ctx, base);
}
