// fable_2 - co-op joiner camera, step 1: instrumentation.
//
// research/05-client-camera.md: the joiner runs no camera Lua while visiting
// and shows the camera computed by the host for the host's hero. The camera
// object keeps its position at +144 (Camera.MoveTo, sub_82174C08: stvx v1 ->
// this+144) and its orientation at +160 (Camera.SetDirection, sub_82182920 ->
// 0x8223B988 on this+160). These probes count who calls the camera setters
// on each PC (thread, camera object, last position) so we can see how the
// joiner's camera is written.

#pragma once

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>

#include <rex/cvar.h>
#include <rex/input/input.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

#include "fable2_coop_camera_math.h"

namespace fable2::coopcam {

namespace ta = fable2::textappend;

struct SetterStats {
  const char* name;
  std::atomic<uint32_t> calls{0};
  std::atomic<uint32_t> last_obj{0};
  std::atomic<uint32_t> last_thread{0};
};

inline SetterStats g_move_to{"MoveTo"}, g_move_to_cut{"MoveToCut"}, g_set_direction{"SetDirection"},
    g_set_angles{"SetAngles"}, g_set_target{"SetTarget"};
inline std::atomic<int64_t> g_next_report_ms{0};

inline float load_be_float(const uint8_t* base, uint32_t a) {
  const uint32_t v = ta::load_be(base, a);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}

inline void report(const uint8_t* base) {
  const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();
  int64_t next = g_next_report_ms.load();
  if (now < next || !g_next_report_ms.compare_exchange_strong(next, now + 10000)) return;
  for (SetterStats* s : {&g_move_to, &g_move_to_cut, &g_set_direction, &g_set_angles, &g_set_target}) {
    const uint32_t calls = s->calls.exchange(0);
    if (calls == 0) continue;
    const uint32_t obj = s->last_obj.load();
    float px = 0, py = 0, pz = 0;
    if (obj && ta::rdable2(base, obj + 144, 12)) {
      px = load_be_float(base, obj + 144);
      py = load_be_float(base, obj + 148);
      pz = load_be_float(base, obj + 152);
    }
    REXSYS_INFO("[coopcam] {} calls in 10 s: {} (thread {}, camera 0x{:08X}, pos {:.2f} {:.2f} {:.2f})",
                s->name, calls, s->last_thread.load(), obj, px, py, pz);
  }
}

inline void note(SetterStats& s, const PPCContext& ctx, const uint8_t* base) {
  s.calls.fetch_add(1, std::memory_order_relaxed);
  s.last_obj.store(ctx.r3.u32, std::memory_order_relaxed);
  s.last_thread.store(fable2::f5lua::current_thread_id(), std::memory_order_relaxed);
  report(base);
}

// Logs the position/orientation stored in the last camera object seen by a
// setter, every 2 s, whether or not a setter ran (render thread).
inline int64_t g_next_watch_ms = 0;
inline void watch() {
  const uint8_t* base = fable2::f5lua::g_base;
  const uint32_t obj = g_move_to.last_obj.load();
  if (!base || !obj || !ta::rdable2(base, obj + 144, 32)) return;
  const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();
  if (now < g_next_watch_ms) return;
  g_next_watch_ms = now + 2000;
  REXSYS_INFO("[coopcam] watch camera 0x{:08X} pos {:.2f} {:.2f} {:.2f} rot {:.3f} {:.3f} {:.3f} {:.3f}",
              obj, load_be_float(base, obj + 144), load_be_float(base, obj + 148),
              load_be_float(base, obj + 152), load_be_float(base, obj + 160),
              load_be_float(base, obj + 164), load_be_float(base, obj + 168),
              load_be_float(base, obj + 172));
}

}  // namespace fable2::coopcam

// --- joiner follow camera (step 2) -------------------------------------------
//
// On a joiner the game sets up player 2 (its own creature) with CECPlayer
// setup sub_8238F530(component, entity, slot = 1) and creates its camera with
// sub_82422EB0. The view it renders comes from a per-player view struct that
// holds the component pointer at +0x00 and the camera pointer at +0x4C, the
// current view at +0xE0 and the target view at +0x130 (each: position xyz,
// up xyz at +0x10, forward xyz at +0x20, FOV at +0x30). Network updates write
// the host's camera into it. The creature's position is at
// (*(entity + 124)) + 0x70 (what the Lua GetPosition binding reads through
// the component's virtual). Verified on the two-copy rig
// (research/05-client-camera.md, research/v12-evidence).

REXCVAR_DEFINE_BOOL(coop_cam_trace, false, "Network",
                    "Testing: log the joiner camera every frame for 30 s (frame time, creature, "
                    "smoothed target, camera, the host's camera, FOV); turns itself off");
REXCVAR_DEFINE_BOOL(coop_cam_chase, true, "Network",
                    "Joiner camera: swing behind your hero while walking (when you're not "
                    "turning the camera yourself)");

namespace fable2::coopcam {

inline std::atomic<uint32_t> g_last_camera{0};  // last camera created (r3 out)
inline std::atomic<uint32_t> g_p2_entity{0}, g_p2_component{0}, g_p2_camera{0};
inline std::atomic<uint32_t> g_view{0};        // per-player view struct (found by scan)
inline std::atomic<bool> g_joined{false};      // in someone else's world (set by the co-op module)

constexpr uint32_t kViewCurrent = 0xE0, kViewTarget = 0x130;

struct Vec3 {
  float x = 0, y = 0, z = 0;
};
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 normalized(Vec3 a) {
  const float l = std::sqrt(dot(a, a));
  return l > 1e-6f ? a * (1.0f / l) : Vec3{0, 0, 1};
}

inline Vec3 read_vec(const uint8_t* base, uint32_t a) {
  return {load_be_float(base, a), load_be_float(base, a + 4), load_be_float(base, a + 8)};
}
inline void store_be_float(uint8_t* base, uint32_t a, float f) {
  uint32_t v;
  std::memcpy(&v, &f, 4);
  v = (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
  std::memcpy(reinterpret_cast<void*>(ta::host_addr(base, a)), &v, 4);
}
inline void write_vec(uint8_t* base, uint32_t a, Vec3 v) {
  store_be_float(base, a, v.x);
  store_be_float(base, a + 4, v.y);
  store_be_float(base, a + 8, v.z);
}

// Finds the view struct: the word pair (component, camera) 0x4C bytes apart.
inline uint32_t find_view(const uint8_t* base, uint32_t component, uint32_t camera) {
  for (uint64_t page = 0x40000000; page < 0x50000000; page += 0x10000) {
    const uint32_t p = static_cast<uint32_t>(page);
    if (!ta::rdable2(base, p, 0x10000)) continue;
    for (uint32_t a = p; a + 0x50 < p + 0x10000; a += 4) {
      if (ta::load_be(base, a) == component && ta::load_be(base, a + 0x4C) == camera) return a;
    }
  }
  return 0;
}

// Shared with the input filter (input thread).
inline std::atomic<bool> g_active{false};       // our camera is driving the view
inline std::atomic<float> g_look_x{0};          // right stick / mouse look, -1..1
inline std::atomic<float> g_look_y{0};          // right stick up/down, -1..1 (pitch)
inline std::atomic<int64_t> g_look_ms{0};       // last time look input was non-zero
inline std::atomic<bool> g_recenter{false};     // R3 pressed: camera back behind the hero
inline bool g_r3_down = false;                  // input thread only
constexpr uint16_t kButtonR3 = 0x0080;          // XINPUT_GAMEPAD_RIGHT_THUMB

inline int64_t clock_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// Pad filter (every XInput poll, all input devices). Records the look input
// for the orbit; while our camera is active the look input turns only our
// camera, so it is not passed on to the game. The left stick needs no change:
// the joiner's movement is relative to the view it renders, i.e. our camera
// (verified on the rig: with the stick held forward the hero walks the way our
// camera faces, research/v12-evidence).
inline void input_filter(uint32_t /*user_index*/, rex::input::X_INPUT_STATE* state) {
  if (!state) return;
  auto norm = [](int16_t v) {
    const float f = v / 32767.0f;
    return std::fabs(f) < 0.03f ? 0.0f : f;  // mouse look gives small values
  };
  const float look = norm(static_cast<int16_t>(state->gamepad.thumb_rx));
  const float look_y = norm(static_cast<int16_t>(state->gamepad.thumb_ry));
  g_look_x.store(look);
  g_look_y.store(look_y);
  if (look != 0 || look_y != 0) g_look_ms.store(clock_ms());
  const bool r3 = (static_cast<uint16_t>(state->gamepad.buttons) & kButtonR3) != 0;
  if (r3 && !g_r3_down) g_recenter.store(true);
  g_r3_down = r3;
  if (!g_active.load()) return;
  // Our camera owns the right stick and R3 (the game's own camera would act
  // on the host's hero): yaw from X, pitch from Y, R3 recenters.
  state->gamepad.thumb_rx.set(0);
  state->gamepad.thumb_ry.set(0);
  state->gamepad.buttons.set(static_cast<uint16_t>(state->gamepad.buttons) & ~kButtonR3);
}

// Follow camera state (render thread only).
inline float g_yaw = 0;          // camera heading around the creature (radians)
inline bool g_have_yaw = false;
inline Vec3 g_last_creature;
inline Vec3 g_written_pos;       // what we last wrote (to tell network updates apart)
inline Vec3 g_host_pos, g_host_fwd;  // last camera received from the host
inline int64_t g_next_find_ms = 0;
inline bool g_logged_active = false;

constexpr float kDistance = 5.5f, kHeight = 2.3f, kLookHeight = 1.5f, kTurnRate = 0.04f;
constexpr float kOrbitSpeed = 4.0f;          // radians per second at full look input
// Pitch (right stick up/down): the camera's height above its normal one, in
// metres; the distance shrinks as it rises so it looks down over the hero.
constexpr float kPitchSpeed = 3.0f, kPitchMin = -1.6f, kPitchMax = 3.5f;
inline float g_pitch = 0;
constexpr float kDefaultFov = 1.309f;        // the game's normal field of view
inline int64_t g_last_tick_ms = 0;
inline Tracker g_follow;                // the creature, extrapolated between network steps (camera_math)
inline int64_t g_handback_ms = 0;       // when the view went to the host's special camera
inline int64_t g_trace_until_ms = 0;
// Testing (coop_cam_watch, src/diagnostics/fable2_cam_watch.h): called after
// each write with the view's and its target block's host addresses.
inline void (*g_after_write)(uintptr_t view, uintptr_t target, int64_t now_ms) = nullptr;
inline void (*g_before_write)() = nullptr;

inline void follow_tick(bool enabled) {
  uint8_t* base = fable2::f5lua::g_base;
  const uint32_t entity = g_p2_entity.load(), component = g_p2_component.load(),
                 camera = g_p2_camera.load();
  if (!enabled || !base || !g_joined.load() || !entity || !component || !camera) {
    g_logged_active = false;
    g_active.store(false);
    return;
  }
  uint32_t view = g_view.load();
  if (view && (!ta::rdable2(base, view, 0x180) || ta::load_be(base, view) != component)) {
    view = 0;
    g_view.store(0);
  }
  if (!view) {
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
    if (now < g_next_find_ms) return;
    g_next_find_ms = now + 2000;
    view = find_view(base, component, camera);
    g_view.store(view);
    REXSYS_INFO("[coopcam] joiner view struct {} 0x{:08X} (component 0x{:08X}, camera 0x{:08X})",
                view ? "found at" : "not found", view, component, camera);
    if (!view) return;
    g_have_yaw = false;
  }
  if (!ta::rdable2(base, entity + 124, 4)) return;
  const uint32_t placement = ta::load_be(base, entity + 124);
  if (!placement || !ta::rdable2(base, placement + 0x70, 12)) return;
  const Vec3 creature = read_vec(base, placement + 0x70);

  // Anything other than what we wrote came from the host (network update).
  const Vec3 seen = read_vec(base, view + kViewTarget);
  const Vec3 d = seen - g_written_pos;
  if (dot(d, d) > 1e-4f) {
    g_host_pos = seen;
    g_host_fwd = read_vec(base, view + kViewTarget + 0x20);
  }
  if (!g_have_yaw) {
    g_yaw = std::atan2(-g_host_fwd.y, -g_host_fwd.x);  // start where the host camera is
    g_last_creature = creature;
    g_follow = Tracker{};
    g_have_yaw = true;
  }
  const int64_t now = clock_ms();
  // Cutscenes / special cameras (a FOV far from the normal one): show the
  // host's camera (we stop writing, so the next network update shows through)
  // and resume afterwards. The host's small FOV changes (running, aiming) do
  // not hand the view over (2026-10-25 playtest: 5 of 7 hand-backs were
  // those); our camera keeps the host's FOV, which we never overwrite.
  const float host_fov = load_be_float(base, view + kViewTarget + 0x30);
  if (HostCameraIsSpecial(host_fov)) {
    if (g_active.exchange(false)) {
      g_handback_ms = now;
      REXSYS_INFO("[coopcam] host camera FOV {:.3f}: showing the host's camera", host_fov);
    }
    g_written_pos = Vec3{1e9f, 1e9f, 1e9f};
    g_last_tick_ms = now;
    return;
  }
  if (g_handback_ms) {
    REXSYS_INFO("[coopcam] own camera again after {} ms of the host's camera (FOV {:.3f})", now - g_handback_ms,
                host_fov);
    g_handback_ms = 0;
    g_follow = Tracker{};  // the creature may have moved a lot: start from it
  }
  const float dt = g_last_tick_ms ? std::fmin(0.1f, (now - g_last_tick_ms) / 1000.0f) : 0.0f;
  g_last_tick_ms = now;
  // Orbit with the mouse / right stick; pitch with the stick's Y.
  g_yaw -= g_look_x.load() * kOrbitSpeed * dt;
  g_pitch = std::fmin(kPitchMax, std::fmax(kPitchMin, g_pitch - g_look_y.load() * kPitchSpeed * dt));
  // The creature's position arrives in network steps: follow it damped, by
  // elapsed time, so the steps do not shake the camera.
  g_follow.Step(creature.x, creature.y, creature.z, dt);
  const Vec3 target{g_follow.ex, g_follow.ey, g_follow.ez};
  const Vec3 moved = target - g_last_creature;
  g_last_creature = target;
  // Chase: ease the camera behind the direction the creature is walking,
  // unless the player turned the camera in the last 1.5 s; per second, not per
  // frame (the same feel at 20 and 60 fps).
  // The walk direction comes from a low-passed velocity, and the chase turns
  // the camera at most kMaxSwingRate (camera proof: the per-frame direction was
  // noise and swung the camera up to 1 m a frame).
  if (REXCVAR_GET(coop_cam_chase) && now - g_look_ms.load() > 1500 && dt > 0 &&
      g_follow.Speed() > kChaseMinSpeed) {
    g_yaw = ChaseYawLimited(g_yaw, std::atan2(-g_follow.vy, -g_follow.vx), dt);
  }
  (void)moved;
  // R3: back to the normal height, behind the way the hero walks (or faces
  // the host's camera's way when standing).
  if (g_recenter.exchange(false)) {
    g_pitch = 0;
    g_yaw = g_follow.Speed() > kChaseMinSpeed ? std::atan2(-g_follow.vy, -g_follow.vx)
                                              : std::atan2(-g_host_fwd.y, -g_host_fwd.x);
    REXSYS_INFO("[coopcam] R3: camera recentered");
  }
  const float dist = kDistance * (1.0f - 0.12f * std::fmax(0.0f, g_pitch));
  const Vec3 pos = target + Vec3{std::cos(g_yaw) * dist, std::sin(g_yaw) * dist, kHeight + g_pitch};
  const Vec3 fwd = normalized(target + Vec3{0, 0, kLookHeight} - pos);
  // Testing (coop_cam_trace): one line per frame for 30 s.
  if (REXCVAR_GET(coop_cam_trace)) {
    if (!g_trace_until_ms) g_trace_until_ms = now + 30000;
    if (now > g_trace_until_ms) {
      g_trace_until_ms = 0;
      rex::cvar::SetFlagByName("coop_cam_trace", "false");
    } else {
      REXSYS_INFO("[coopcam] trace dt {:.1f} ms creature {:.3f} {:.3f} {:.3f} target {:.3f} {:.3f} {:.3f} "
                  "cam {:.3f} {:.3f} {:.3f} host {:.3f} {:.3f} {:.3f} fov {:.3f} net {}",
                  dt * 1000, creature.x, creature.y, creature.z, target.x, target.y, target.z, pos.x, pos.y,
                  pos.z, seen.x, seen.y, seen.z, host_fov, dot(d, d) > 1e-4f ? 1 : 0);
    }
  }
  const Vec3 up = normalized(Vec3{0, 0, 1} - fwd * fwd.z);
  if (g_before_write) g_before_write();  // testing: the watched page writable for our own write
  for (uint32_t block : {kViewCurrent, kViewTarget}) {
    if (!ta::wr2(base, view + block, 0x30)) return;
    write_vec(base, view + block, pos);
    write_vec(base, view + block + 0x10, up);
    write_vec(base, view + block + 0x20, fwd);
  }
  g_written_pos = pos;
  g_active.store(true);
  if (g_after_write) {
    g_after_write(static_cast<uintptr_t>(ta::host_addr(base, view)),
                  static_cast<uintptr_t>(ta::host_addr(base, view + kViewTarget)), now);
  }
  if (!g_logged_active) {
    g_logged_active = true;
    REXSYS_INFO("[coopcam] joiner follow camera active: creature {:.2f} {:.2f} {:.2f}", creature.x,
                creature.y, creature.z);
  }
}

}  // namespace fable2::coopcam

// CECPlayer setup (one per player creature, also on a joiner): r3 = player
// component, r4 = creature entity, r5 = player slot (1 = player 2). Camera
// creation: r3 out = the player's camera (created just before the setup's
// camera link).
extern "C" void sub_8238F530(PPCContext& ctx, uint8_t* base) {
  const uint32_t comp = ctx.r3.u32, entity = ctx.r4.u32, slot = ctx.r5.u32;
  __imp__sub_8238F530(ctx, base);
  REXSYS_INFO("[coopcam] CECPlayer setup: component 0x{:08X} entity 0x{:08X} slot {} (thread {})",
              comp, entity, slot, fable2::f5lua::current_thread_id());
  if (slot == 1) {
    using namespace fable2::coopcam;
    g_p2_entity.store(entity);
    g_p2_component.store(comp);
    g_p2_camera.store(g_last_camera.load());
    g_view.store(0);
  }
}
extern "C" void sub_82422EB0(PPCContext& ctx, uint8_t* base) {
  const uint32_t a3 = ctx.r3.u32, a4 = ctx.r4.u32, a5 = ctx.r5.u32;
  __imp__sub_82422EB0(ctx, base);
  fable2::coopcam::g_last_camera.store(ctx.r3.u32);
  REXSYS_INFO("[coopcam] camera created: r3 0x{:08X} r4 0x{:08X} r5 0x{:08X} -> 0x{:08X}", a3, a4,
              a5, ctx.r3.u32);
}

// Registered with the SDK input system (src/input/input_system.cpp).
extern "C" void rex_input_set_state_filter(void (*filter)(uint32_t, rex::input::X_INPUT_STATE*));
namespace fable2::coopcam {
inline const bool g_filter_registered = [] {
  rex_input_set_state_filter(&input_filter);
  return true;
}();
}  // namespace fable2::coopcam
