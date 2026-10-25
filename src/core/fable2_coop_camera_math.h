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

}  // namespace fable2::coopcam
