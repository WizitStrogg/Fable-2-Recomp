// fable_2 - joiner follow camera: the frame-rate-independent math (no game or
// SDK dependencies, unit-tested by tools/coop_camera_test.cpp).
//
// Josh's playtest (2026-10-25) found the joiner camera glitchy;
// research/001-evidence/camera-glitch-notes.md lists the causes. This file
// holds the fixes that are pure math:
//   - HostCameraIsSpecial: hand the view to the host's camera only for a real
//     cutscene / special camera, not for the small FOV changes of running,
//     aiming or zooming (cause 1: 5 of 7 hand-backs were FOV 1.19-1.26).
//   - Follow: damped following of the creature (its position arrives in
//     network steps; cause 3), and a chase turn per second, not per frame
//     (cause 5).

#pragma once

#include <cmath>

namespace fable2::coopcam {

constexpr float kNormalFov = 1.309f;   // the game's normal field of view
constexpr float kSpecialFovHigh = 1.5f, kSpecialFovLow = 0.9f;

// True for the host cameras the joiner should show as they are (cutscenes,
// conversations, special views): a FOV far from the normal one. The host's
// small FOV changes (running, aiming) are not.
inline bool HostCameraIsSpecial(float host_fov) {
  return host_fov >= kSpecialFovHigh || host_fov <= kSpecialFovLow;
}

// Weight of the new value in an exponential smoothing step: 0 at dt 0, -> 1
// for dt >> tau; the same result at any frame rate for the same elapsed time.
inline float SmoothWeight(float dt, float tau) {
  if (dt <= 0) return 0;
  if (tau <= 0) return 1;
  return 1.0f - std::exp(-dt / tau);
}

// Shortest signed angle from a to b (radians, -pi..pi).
inline float AngleDiff(float from, float to) {
  float d = to - from;
  while (d > 3.14159265f) d -= 6.2831853f;
  while (d < -3.14159265f) d += 6.2831853f;
  return d;
}

constexpr float kFollowTau = 0.12f;   // creature smoothing (s)
constexpr float kChaseTau = 0.35f;    // swing-behind time constant (s)
constexpr float kSnapDistance = 8.0f; // a jump this large (teleport, load) snaps

struct Follow {
  float x = 0, y = 0, z = 0;  // smoothed creature position
  bool have = false;

  // Moves the smoothed position toward the creature; returns how far it moved
  // in xy this step (squared), for the chase.
  float Step(float cx, float cy, float cz, float dt) {
    if (!have) {
      x = cx, y = cy, z = cz;
      have = true;
      return 0;
    }
    const float dx = cx - x, dy = cy - y, dz = cz - z;
    if (dx * dx + dy * dy + dz * dz > kSnapDistance * kSnapDistance) {
      x = cx, y = cy, z = cz;
      return 0;
    }
    const float w = SmoothWeight(dt, kFollowTau);
    const float ox = x, oy = y;
    x += dx * w;
    y += dy * w;
    z += dz * w;
    return (x - ox) * (x - ox) + (y - oy) * (y - oy);
  }
};

// The chase: yaw eased toward "behind the walk direction" at a rate per second.
inline float ChaseYaw(float yaw, float behind, float dt) {
  return yaw + AngleDiff(yaw, behind) * SmoothWeight(dt, kChaseTau);
}

// The walk direction for the chase, from a low-passed velocity (camera proof
// 2026-10-25: the per-frame move of the smoothed target is tiny and noisy, so
// its direction swung the camera up to 1 m a frame while walking circles).
constexpr float kHeadingTau = 0.3f;     // velocity smoothing (s)
constexpr float kChaseMinSpeed = 1.0f;  // chase only while walking at least this (m/s)
constexpr float kMaxSwingRate = 2.0f;   // the chase turns the camera at most this (rad/s)

struct Heading {
  float vx = 0, vy = 0;  // smoothed velocity (m/s)
  void Update(float dx, float dy, float dt) {
    if (dt <= 0) return;
    const float w = SmoothWeight(dt, kHeadingTau);
    vx += (dx / dt - vx) * w;
    vy += (dy / dt - vy) * w;
  }
  float Speed() const { return std::sqrt(vx * vx + vy * vy); }
};

// The camera's target: the partner's creature, whose position arrives in
// network steps (camera proof: per-frame move p50 0, steps up to 2.5 m).
// Between steps the target is extrapolated from the low-passed velocity
// (capped at kMaxLead s), and the estimate moves toward it smoothly, never
// faster than kMaxCorrection m/s, so a big step is spread over several frames.
constexpr float kMaxLead = 0.25f;        // longest extrapolation (s)
constexpr float kMaxCorrection = 10.0f;  // fastest the target may move (m/s)
constexpr float kMaxSpeed = 8.0f;        // fastest a hero really moves (m/s)
constexpr float kStopTau = 0.15f;        // velocity decay once reports stop (s)

struct Tracker {
  float ax = 0, ay = 0, az = 0;  // last reported position
  float ex = 0, ey = 0, ez = 0;  // the estimate the camera follows
  float vx = 0, vy = 0;          // low-passed velocity (m/s)
  float since = 0;               // seconds since the last reported change
  bool have = false;

  void Step(float cx, float cy, float cz, float dt) {
    if (!have) {
      ax = ex = cx, ay = ey = cy, az = ez = cz;
      have = true;
      return;
    }
    since += dt;
    const float dx = cx - ax, dy = cy - ay, dz = cz - az;
    if (dx != 0 || dy != 0 || dz != 0) {  // a new report
      if (dx * dx + dy * dy + dz * dz > kSnapDistance * kSnapDistance) {  // teleport / load
        ax = ex = cx, ay = ey = cy, az = ez = cz;
        vx = vy = 0;
        since = 0;
        return;
      }
      const float span = std::fmax(since, 1e-3f);
      float mx = dx / span, my = dy / span;
      const float ms = std::sqrt(mx * mx + my * my);
      if (ms > kMaxSpeed) mx *= kMaxSpeed / ms, my *= kMaxSpeed / ms;  // a burst is not a speed
      const float w = SmoothWeight(span, kHeadingTau);
      vx += (mx - vx) * w;
      vy += (my - vy) * w;
      ax = cx, ay = cy, az = cz;
      since = 0;
    } else if (since > kMaxLead) {  // no report for a while: it stopped
      const float d = 1.0f - SmoothWeight(dt, kStopTau);
      vx *= d, vy *= d;
    }
    const float lead = std::fmin(since, kMaxLead);
    const float px = ax + vx * lead, py = ay + vy * lead, pz = az;
    const float w = SmoothWeight(dt, kFollowTau);
    float mx = (px - ex) * w, my = (py - ey) * w, mz = (pz - ez) * w;
    const float len = std::sqrt(mx * mx + my * my + mz * mz), cap = kMaxCorrection * dt;
    if (len > cap && len > 0) {
      const float k = cap / len;
      mx *= k, my *= k, mz *= k;
    }
    ex += mx, ey += my, ez += mz;
  }
  float Speed() const { return std::sqrt(vx * vx + vy * vy); }
};

// The chase with a swing-rate cap: never more than kMaxSwingRate * dt per step.
inline float ChaseYawLimited(float yaw, float behind, float dt) {
  float step = AngleDiff(yaw, behind) * SmoothWeight(dt, kChaseTau);
  const float cap = kMaxSwingRate * dt;
  if (step > cap) step = cap;
  if (step < -cap) step = -cap;
  return yaw + step;
}

}  // namespace fable2::coopcam
