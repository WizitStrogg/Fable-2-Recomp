// fable_2 - co-op join gate (feature 001, starter save inside QC060).
//
// In QC060 "A New Beginning" every join was refused (launches 7, 12, 14b). The
// partner's join request (message type 12, 304 bytes on the wire) reaches the host's
// ProcessJoinSession() (sub_822EE2D8, XboxLive.cpp; r3 = session manager, r4 = the
// message, r5 = the sender), which answers with a type 1 reply carrying a result:
//   0 JOINRESPONSE_APPROVED, 1 SESSIONFULL, 2 DIFFERENTVERSION, 3 NOTHOSTING,
//   4 DIFFERENTVERSIONORDLCS.
// NOTHOSTING is returned unless all of these hold (static read of the handler):
//   ptr [0x83496920] set and byte 0x834968C5 set           (gate "g")
//   if the profile at [[[[0x83496920]+12]+140]+184] has +412 == 1 and the request's
//     private flag (+3+125) is 0: a privilege check passes  (logged only)
//   manager +456 in 4096..4101                              (gate "456", session state)
//   manager +236 non-zero                                   (logged only, may be a pointer)
//   byte [0x834971D0]+243 set                               (gate "243")
// and SESSIONFULL unless the open slots cover the request and [0x834971D0]+245 is set
// (gate "245"; the host clears it itself when it approves a join).
//
// coop_join_gate_log logs those values on each request; coop_join_gate_force opens
// the listed gates for that request while a co-op campaign is on. A forced 456 is put
// back after the call. With coop_campaign off nothing is touched.
//
// The older probe on the in-session message handler sub_8230DFF0 (gates 226, 52, 53,
// 54) stays: that handler runs only after a join is approved.

#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

REXCVAR_DEFINE_BOOL(coop_join_gate_log, false, "Network",
                    "Testing: log the co-op join gates on each host join request and message");
REXCVAR_DEFINE_STRING(coop_join_gate_force, "", "Network",
                      "During a co-op campaign, force these join gates open: comma list of "
                      "g, 243, 245, 456 (join request) and 226, 52, 53, 54 (session messages)");

namespace fable2::coopjoingate {

constexpr uint32_t kGlobalGate = 0x834968C5;
constexpr uint32_t kLiveGlobal = 0x83496920;
constexpr uint32_t kSessionFlags = 0x834971D0;

inline uint8_t rd8(uint8_t* base, uint32_t a) { return base[a]; }
inline void wr8(uint8_t* base, uint32_t a, uint8_t v) { base[a] = v; }
inline uint32_t rd32(uint8_t* base, uint32_t a) {
  return (uint32_t(base[a]) << 24) | (uint32_t(base[a + 1]) << 16) | (uint32_t(base[a + 2]) << 8) |
         base[a + 3];
}
inline void wr32(uint8_t* base, uint32_t a, uint32_t v) {
  base[a] = uint8_t(v >> 24);
  base[a + 1] = uint8_t(v >> 16);
  base[a + 2] = uint8_t(v >> 8);
  base[a + 3] = uint8_t(v);
}
// Guest pointers below 0x10000 are never valid.
inline bool ok(uint32_t p) { return p >= 0x10000; }

inline bool listed(const std::string& list, const char* item) {
  const std::string want = std::string(",") + item + ",";
  return ("," + list + ",").find(want) != std::string::npos;
}

inline std::string forced() {
  return REXCVAR_GET(coop_campaign) ? std::string(REXCVAR_GET(coop_join_gate_force)) : std::string();
}

inline std::atomic<int> g_logged{0};
inline std::atomic<int> g_join_logged{0};

// --- ProcessJoinSession (sub_822EE2D8) ---------------------------------------------------

struct JoinRestore {
  uint32_t mgr = 0;
  bool state = false;
  uint32_t old_state = 0;
};

inline void log_join(uint8_t* base, uint32_t mgr, uint32_t req, const char* when) {
  const uint32_t live = rd32(base, kLiveGlobal);
  uint32_t prof = 0, prof412 = 0;
  if (ok(live)) {
    const uint32_t a = rd32(base, live + 12);
    const uint32_t b = ok(a) ? rd32(base, a + 140) : 0;
    prof = ok(b) ? rd32(base, b + 184) : 0;
    prof412 = ok(prof) ? rd32(base, prof + 412) : 0;
  }
  const uint32_t flags = rd32(base, kSessionFlags);
  REXSYS_INFO(
      "[joingate] join request {}: live={:08X} g={} prof412={} private={} state={} +236={:08X} "
      "flags={:08X} +243={} +245={} players={} mgr={:08X}",
      when, live, rd8(base, kGlobalGate), prof412, rd32(base, req + 3 + 125), rd32(base, mgr + 456),
      rd32(base, mgr + 236), flags, ok(flags) ? rd8(base, flags + 243) : 0,
      ok(flags) ? rd8(base, flags + 245) : 0, rd8(base, req + 3 + 8), mgr);
}

inline JoinRestore before_join(PPCContext& ctx, uint8_t* base) {
  JoinRestore r;
  const uint32_t mgr = ctx.r3.u32, req = ctx.r4.u32;
  if (!ok(mgr) || !ok(req)) return r;
  const bool log = REXCVAR_GET(coop_join_gate_log) && g_join_logged.fetch_add(1) < 40;
  if (log) log_join(base, mgr, req, "seen");
  const std::string force = forced();
  if (force.empty()) return r;
  if (listed(force, "g") && ok(rd32(base, kLiveGlobal))) wr8(base, kGlobalGate, 1);
  const uint32_t flags = rd32(base, kSessionFlags);
  if (ok(flags)) {
    if (listed(force, "243")) wr8(base, flags + 243, 1);
    if (listed(force, "245")) wr8(base, flags + 245, 1);
  }
  if (listed(force, "456")) {
    const uint32_t s = rd32(base, mgr + 456);
    if (s < 4096 || s > 4101) {
      r.mgr = mgr;
      r.state = true;
      r.old_state = s;
      wr32(base, mgr + 456, 4096);
    }
  }
  if (log) log_join(base, mgr, req, "forced");
  return r;
}

inline void after_join(uint8_t* base, const JoinRestore& r) {
  if (r.state) wr32(base, r.mgr + 456, r.old_state);
}

// --- in-session messages (sub_8230DFF0) --------------------------------------------------

inline void before(PPCContext& ctx, uint8_t* base) {
  const uint32_t mgr = ctx.r3.u32, msg = ctx.r5.u32;
  if (!ok(mgr) || !ok(msg)) return;
  const uint16_t type = static_cast<uint16_t>((base[msg] << 8) | base[msg + 1]);
  const std::string force = forced();
  if (!force.empty()) {
    if (listed(force, "226")) wr8(base, mgr + 226, 1);
    if (listed(force, "52")) wr8(base, mgr + 52, 1);
    if (listed(force, "53")) wr8(base, mgr + 53, 1);
    if (listed(force, "54")) wr8(base, mgr + 54, 0);  // +54 set = refuse in case 1
  }
  if (REXCVAR_GET(coop_join_gate_log) && g_logged.fetch_add(1) < 60) {
    REXSYS_INFO("[joingate] msg {} (case {}): g={} +52={} +53={} +54={} +226={} mgr={:08X}", type,
                type - 275, rd8(base, kGlobalGate), rd8(base, mgr + 52), rd8(base, mgr + 53),
                rd8(base, mgr + 54), rd8(base, mgr + 226), mgr);
  }
}

}  // namespace fable2::coopjoingate

extern "C" void __imp__sub_822EE2D8(PPCContext& ctx, uint8_t* base);
extern "C" void sub_822EE2D8(PPCContext& ctx, uint8_t* base) {
  const auto restore = fable2::coopjoingate::before_join(ctx, base);
  __imp__sub_822EE2D8(ctx, base);
  fable2::coopjoingate::after_join(base, restore);
}

extern "C" void __imp__sub_8230DFF0(PPCContext& ctx, uint8_t* base);
extern "C" void sub_8230DFF0(PPCContext& ctx, uint8_t* base) {
  fable2::coopjoingate::before(ctx, base);
  __imp__sub_8230DFF0(ctx, base);
}
