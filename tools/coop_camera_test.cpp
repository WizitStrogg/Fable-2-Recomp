// Unit test for src/core/fable2_coop_camera_math.h (no game, no SDK).
//   clang++ -std=c++20 -I src/core tools/coop_camera_test.cpp -o out/coop_camera_test.exe
#include <cmath>
#include <cstdio>
#include "fable2_coop_camera_math.h"

using namespace fable2::coopcam;

static int g_fail = 0;
#define CHECK(cond)                                        \
  do {                                                     \
    const bool ok_ = (cond);                               \
    std::printf("%s  %s\n", ok_ ? "ok  " : "FAIL", #cond); \
    if (!ok_) ++g_fail;                                    \
  } while (0)

static bool near(float a, float b, float e) { return std::fabs(a - b) <= e; }

int main() {
  // Cause 1: the playtest's hand-backs. 1.571 (90 deg) is a special camera;
  // 1.187 / 1.201 / 1.255 / 1.259 (running, aiming) and the normal FOV are not.
  CHECK(HostCameraIsSpecial(1.571f));
  CHECK(!HostCameraIsSpecial(1.187f) && !HostCameraIsSpecial(1.201f));
  CHECK(!HostCameraIsSpecial(1.255f) && !HostCameraIsSpecial(1.259f));
  CHECK(!HostCameraIsSpecial(kNormalFov));
  CHECK(HostCameraIsSpecial(0.6f));

  // Smoothing: the same elapsed time gives the same result at 20 and 60 fps.
  Follow a, b;
  a.Step(0, 0, 0, 0);
  b.Step(0, 0, 0, 0);
  for (int i = 0; i < 20; ++i) a.Step(1, 0, 0, 1.0f / 20);  // 1 s at 20 fps
  for (int i = 0; i < 60; ++i) b.Step(1, 0, 0, 1.0f / 60);  // 1 s at 60 fps
  CHECK(near(a.x, b.x, 1e-3f));
  CHECK(a.x > 0.99f && a.x <= 1.0f);  // settled within 1 s (tau 0.12 s)
  // A network step of 0.5 m becomes a smooth approach, not a jump in one frame.
  Follow c;
  c.Step(0, 0, 0, 0);
  c.Step(0.5f, 0, 0, 1.0f / 60);
  CHECK(c.x > 0 && c.x < 0.1f);
  // A teleport snaps.
  Follow d;
  d.Step(0, 0, 0, 0);
  d.Step(50, 0, 0, 1.0f / 60);
  CHECK(d.x == 50);

  // Cause 5: the chase turns by elapsed time, not by frame count.
  float y20 = 0, y60 = 0;
  for (int i = 0; i < 10; ++i) y20 = ChaseYaw(y20, 1.0f, 1.0f / 20);  // 0.5 s
  for (int i = 0; i < 30; ++i) y60 = ChaseYaw(y60, 1.0f, 1.0f / 60);  // 0.5 s
  CHECK(near(y20, y60, 1e-3f));
  CHECK(near(AngleDiff(3.0f, -3.0f), 0.2831853f, 1e-4f));  // the short way round

  // Camera proof 2026-10-25: noisy per-frame steps (network) while walking at
  // ~3 m/s along +x. The heading stays close to +x; tiny jitter alone (standing)
  // stays under the chase speed.
  {
    Heading h;
    unsigned seed = 1;
    auto rnd = [&]() { seed = seed * 1103515245u + 12345u; return ((seed >> 8) % 1000) / 1000.0f - 0.5f; };
    for (int i = 0; i < 120; ++i) {  // 3 s at 40 fps: steps of 0 or 0.3 m (packets) plus noise
      const float dx = (i % 4 == 0 ? 0.3f : 0.0f) + rnd() * 0.02f;
      const float dy = rnd() * 0.02f;
      h.Update(dx, dy, 1.0f / 40);
    }
    CHECK(h.Speed() > 2.0f && h.Speed() < 4.0f);
    CHECK(std::fabs(std::atan2(h.vy, h.vx)) < 0.15f);  // direction within ~8 deg of +x
    Heading still;
    for (int i = 0; i < 120; ++i) still.Update(rnd() * 0.01f, rnd() * 0.01f, 1.0f / 40);
    CHECK(still.Speed() < kChaseMinSpeed);  // standing: no chase
  }
  // The swing-rate cap: a half-turn target moves the camera at most 2 rad/s.
  {
    const float dt = 1.0f / 40;
    const float y1 = ChaseYawLimited(0.0f, 3.0f, dt);
    CHECK(y1 > 0 && y1 <= kMaxSwingRate * dt + 1e-6f);
    // At 5.5 m radius the capped swing is <= 0.28 m a frame at 40 fps.
    CHECK(5.5f * kMaxSwingRate * dt < 0.3f);
  }

  // The tracker against the proof's input: reports in 0.3 m steps every 4th
  // frame at 40 fps (3 m/s), then one 2.5 m step. The estimate moves smoothly:
  // no frame moves more than the walk would (~0.075 m) plus a bounded
  // correction, and the big step is spread over several frames.
  {
    Tracker t;
    const float dt = 1.0f / 40;
    float rep = 0;
    t.Step(0, 0, 0, dt);
    float worst = 0, prev = 0;
    for (int i = 1; i <= 200; ++i) {
      if (i % 4 == 0) rep += 0.3f;
      t.Step(rep, 0, 0, dt);
      if (i > 80) worst = std::fmax(worst, std::fabs(t.ex - prev));  // after it settled
      prev = t.ex;
    }
    CHECK(worst < 0.15f);                 // smooth: no 0.3 m jumps per report
    CHECK(t.Speed() > 2.5f && t.Speed() < 3.5f);
    CHECK(std::fabs(t.ex - rep) < 1.0f);  // keeps up with the walk
    rep += 2.5f;                          // one big network step
    float big = 0;
    prev = t.ex;
    for (int i = 0; i < 20; ++i) {
      t.Step(rep, 0, 0, dt);
      big = std::fmax(big, std::fabs(t.ex - prev));
      prev = t.ex;
    }
    CHECK(big <= kMaxCorrection * dt + 1e-4f);  // at most 0.25 m a frame
    CHECK(std::fabs(t.ex - rep) < 0.9f);         // caught up within half a second
    Tracker standing;
    standing.Step(0, 0, 0, dt);
    for (int i = 0; i < 80; ++i) standing.Step(0, 0, 0, dt);
    CHECK(standing.Speed() == 0 && standing.ex == 0);  // nothing moves: nothing drifts
  }

  std::printf("%s (%d failed)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail);
  return g_fail ? 1 : 0;
}
