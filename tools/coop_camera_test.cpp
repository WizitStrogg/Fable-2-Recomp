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

  std::printf("%s (%d failed)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail);
  return g_fail ? 1 : 0;
}
