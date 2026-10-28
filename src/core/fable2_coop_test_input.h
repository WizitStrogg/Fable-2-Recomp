// fable_2 - the test input source's command language (no game or SDK
// dependencies; unit-tested by tools/coop_test_input_test.cpp). Only read when
// FABLE2_TEST_INPUT=1 is set by tools/coop_ship_e2e.py (fable2_coop_frontend.h,
// test_input_tick); play-coop.cmd, the friend package and the installer never
// set it.
//
//   press <A|B|X|Y|Up|Down|Left|Right|Start|Back> [ms]   one pad press (default 120 ms)
//   stick <L|R> <x> <y> [ms]       a stick held at x, y (-1..1; y up) (default 500 ms)
//   trigger <L|R> <value> [ms]     a trigger held at value (0..1) (default 500 ms)
//   lua <chunk>                    run Lua in the game
//   cvar <name> <value>            set a setting (e.g. coop_cam_trace true)

#pragma once

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>

namespace fable2::cooptestinput {

enum class Kind { kNone, kPress, kStick, kTrigger, kLua, kCvar };

struct Command {
  Kind kind = Kind::kNone;
  uint16_t mask = 0;     // press
  char side = 0;         // stick / trigger: 'L' or 'R'
  float x = 0, y = 0;    // stick
  float value = 0;       // trigger
  int ms = 0;
  std::string text;      // lua chunk, or cvar name
  std::string value_text;  // cvar value
};

inline uint16_t ButtonMask(std::string_view name) {
  static const std::pair<const char*, uint16_t> kNames[] = {
      {"Up", 0x0001},    {"Down", 0x0002}, {"Left", 0x0004}, {"Right", 0x0008}, {"Start", 0x0010},
      {"Back", 0x0020},  {"LS", 0x0040},   {"RS", 0x0080},   {"A", 0x1000},     {"B", 0x2000},
      {"X", 0x4000},     {"Y", 0x8000}};
  for (const auto& [n, m] : kNames)
    if (name == n) return m;
  return 0;
}

// -1..1 -> the pad's int16 range.
inline int16_t StickValue(float v) {
  v = std::clamp(v, -1.0f, 1.0f);
  return static_cast<int16_t>(v * 32767.0f);
}
// 0..1 -> the pad's uint8 range.
inline uint8_t TriggerValue(float v) {
  v = std::clamp(v, 0.0f, 1.0f);
  return static_cast<uint8_t>(v * 255.0f + 0.5f);
}

inline Command Parse(std::string line) {
  while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
  Command c;
  if (line.rfind("lua ", 0) == 0) {
    c.kind = Kind::kLua;
    c.text = line.substr(4);
    return c;
  }
  std::istringstream w(line);
  std::string cmd;
  w >> cmd;
  if (cmd == "cvar") {
    w >> c.text >> std::ws;
    std::getline(w, c.value_text);  // the rest: a command's arguments may have spaces
    while (!c.value_text.empty() && (c.value_text.back() == '\r' || c.value_text.back() == ' ')) c.value_text.pop_back();
    if (!c.text.empty()) c.kind = Kind::kCvar;
  } else if (cmd == "press") {
    std::string name;
    c.ms = 120;
    w >> name >> c.ms;
    c.mask = ButtonMask(name);
    if (c.mask) c.kind = Kind::kPress;
  } else if (cmd == "stick") {
    std::string side;
    c.ms = 500;
    w >> side >> c.x >> c.y;
    if (!(w >> c.ms)) c.ms = 500;
    if ((side == "L" || side == "R")) {
      c.side = side[0];
      c.kind = Kind::kStick;
    }
  } else if (cmd == "trigger") {
    std::string side;
    w >> side >> c.value;
    if (!(w >> c.ms)) c.ms = 500;
    if ((side == "L" || side == "R")) {
      c.side = side[0];
      c.kind = Kind::kTrigger;
    }
  }
  return c;
}

}  // namespace fable2::cooptestinput
