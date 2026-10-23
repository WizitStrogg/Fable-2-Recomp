// fable_2 - Lua console: run Lua in the game from the backtick console and
// see its print() output in the log / console.
//
//   lua <chunk>        run a Lua chunk in the in-game Lua state, e.g.
//                        lua print(Stats.GetMorality(GetPlayerHero()))
//                      "lua =<expr>" prints the value of an expression.
//                      Errors are caught (loadstring + pcall) and printed.
//   luafile <name>     run data/scripts/recomp/<name>.lua (the file must
//                      exist when the game starts; the VFS indexes data/ from
//                      dir.manifest at launch).
//
// How it works:
//   * Chunks are wrapped (loadstring/pcall, error -> print) and written to
//     one of kSlots fixed files data/scripts/recomp/console_<n>.lua that ship
//     with the build (so they are in dir.manifest from launch), round robin.
//     CScriptManager::RunScript may load on a later frame, so a slot is not
//     reused until kSlots more chunks were queued.
//   * The path is queued to the F5 runner (fable2_f5_lua.h), which runs it on
//     the guest main thread from the MainRenderLoop hook.
//   * Lua's print() (both of the game's Lua runtimes) ends in CRT
//     fputs(str, stdout), guest sub_82CA89A0 with r4 = &_iob[1] (0x8382ED10).
//     A strong override of that function collects stdout text into lines and
//     logs each as "[lua] <line>". Lines can also be routed to handlers by
//     prefix (AddLineHandler), e.g. tagged data lines from capture scripts.

#pragma once

#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

#include "fable2_f5_lua.h"

namespace fable2::luaconsole {

namespace ta = fable2::textappend;

constexpr uint32_t kGuestStdout = 0x8382ED10;  // &_iob[1], see header comment
constexpr int kSlots = 8;

inline std::mutex& mtx() {
  static std::mutex m;
  return m;
}

// --- print() capture -------------------------------------------------------

using LineHandler = std::function<void(std::string_view line)>;

inline std::vector<std::pair<std::string, LineHandler>>& handlers() {
  static std::vector<std::pair<std::string, LineHandler>> h;
  return h;
}

// Routes every captured stdout line starting with `prefix` to `fn` (the line
// is still logged). Call during startup.
inline void AddLineHandler(std::string prefix, LineHandler fn) {
  std::lock_guard<std::mutex> l(mtx());
  handlers().emplace_back(std::move(prefix), std::move(fn));
}

inline std::string& pending_line() {
  static std::string s;
  return s;
}

inline void emit_line(std::string line) {
  while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
  if (line.empty()) return;
  // Our own scripts' output and the co-op lines go to the console; anything
  // else the game's scripts print only to the log file.
  const bool ours = fable2::f5lua::g_running.load() || line.rfind("COOP", 0) == 0 ||
                    line.rfind("[coop", 0) == 0 || line.rfind("[lua error]", 0) == 0;
  if (ours) {
    REXSYS_INFO("[lua] {}", line);
  } else {
    REXSYS_DEBUG("[lua] {}", line);
  }
  std::vector<LineHandler> matched;
  {
    std::lock_guard<std::mutex> l(mtx());
    for (const auto& [prefix, fn] : handlers()) {
      if (line.compare(0, prefix.size(), prefix) == 0) matched.push_back(fn);
    }
  }
  for (const auto& fn : matched) fn(line);
}

// Appends guest stdout text; complete lines are emitted.
inline void on_stdout_text(const char* text, size_t len) {
  std::vector<std::string> done;
  {
    std::lock_guard<std::mutex> l(mtx());
    std::string& cur = pending_line();
    for (size_t i = 0; i < len; ++i) {
      if (text[i] == '\n') {
        done.push_back(std::move(cur));
        cur.clear();
      } else if (cur.size() < 16384) {
        cur.push_back(text[i]);
      }
    }
  }
  for (auto& line : done) emit_line(std::move(line));
}

// --- commands --------------------------------------------------------------

inline std::atomic<int> g_next_slot{0};

inline std::filesystem::path recomp_dir() {
  return fable2::f5lua::g_data_root / "data" / "scripts" / "recomp";
}

// Long-bracket level that does not occur in `text` ("]==]" style).
inline std::string long_bracket_level(const std::string& text) {
  std::string eq;
  while (text.find("]" + eq + "]") != std::string::npos) eq += "=";
  return eq;
}

inline std::string wrap_chunk(std::string chunk) {
  if (!chunk.empty() && chunk.front() == '=') chunk = "return " + chunk.substr(1);
  const std::string eq = long_bracket_level(chunk);
  std::string out;
  out += "-- written by the `lua` console command\n";
  out += "local __src = [" + eq + "[\n" + chunk + "\n]" + eq + "]\n";
  out += "local __f, __err = loadstring(__src, \"=console\")\n";
  out += "if not __f then print(\"[lua error] \" .. tostring(__err)) return end\n";
  out += "local __ok, __res = pcall(__f)\n";
  out += "if not __ok then print(\"[lua error] \" .. tostring(__res))\n";
  out += "elseif __res ~= nil then print(tostring(__res)) end\n";
  return out;
}

inline std::string trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  return std::string(s);
}

inline bool not_ready_warning() {
  if (fable2::f5lua::ready()) return false;
  REXSYS_WARN("[lua] the game has not finished loading its scripts yet; the command "
              "will run as soon as it has");
  return true;
}

// Queues a Lua chunk to run in the game (same path as the `lua` command).
// Callable from any thread. Returns false if it could not be queued.
inline bool RunChunk(const std::string& chunk) {
  if (fable2::f5lua::g_data_root.empty()) {
    REXSYS_ERROR("lua: game data folder not known yet");
    return false;
  }
  const int slot = g_next_slot.fetch_add(1) % kSlots;
  const std::string name = "console_" + std::to_string(slot) + ".lua";
  const auto file = recomp_dir() / name;
  {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
      REXSYS_ERROR("lua: cannot write {}", file.string());
      return false;
    }
    out << wrap_chunk(chunk);
  }
  not_ready_warning();
  fable2::f5lua::request_run("scripts/recomp/" + name);
  return true;
}

// Queues a script file from data/scripts/recomp. Its text runs through the
// same loadstring + pcall wrapper as the `lua` command, so an error inside it
// is printed instead of unwinding into the game's own Lua.
inline bool RunFile(const std::string& name) {
  std::ifstream in(recomp_dir() / name, std::ios::binary);
  if (!in) {
    REXSYS_ERROR("luafile: cannot read {}", (recomp_dir() / name).string());
    return false;
  }
  std::ostringstream text;
  text << in.rdbuf();
  return RunChunk(text.str());
}

inline void CmdLua(std::string_view args) {
  const std::string chunk = trim(args);
  if (chunk.empty()) {
    REXSYS_INFO("lua: usage: lua <chunk>   (lua =<expr> prints a value)");
    return;
  }
  RunChunk(chunk);
}

inline void CmdLuaFile(std::string_view args) {
  std::string name = trim(args);
  if (name.empty()) {
    REXSYS_INFO("luafile: usage: luafile <name>   (runs data/scripts/recomp/<name>.lua)");
    return;
  }
  for (char c : name) {
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.')) {
      REXSYS_ERROR("luafile: '{}' is not a plain file name", name);
      return;
    }
  }
  if (name.size() < 4 || name.compare(name.size() - 4, 4, ".lua") != 0) name += ".lua";
  if (!fable2::f5lua::g_data_root.empty() && !std::filesystem::exists(recomp_dir() / name)) {
    REXSYS_ERROR("luafile: {} not found in {}", name, recomp_dir().string());
    return;
  }
  RunFile(name);
}

}  // namespace fable2::luaconsole

REXCVAR_DEFINE_COMMAND_ARGS(lua, fable2::luaconsole::CmdLua, "Lua",
                            "Run a Lua chunk in the game, e.g. lua print(1+1); "
                            "lua =<expr> prints a value");
REXCVAR_DEFINE_COMMAND_ARGS(luafile, fable2::luaconsole::CmdLuaFile, "Lua",
                            "Run data/scripts/recomp/<name>.lua in the game");

namespace fable2::luaconsole {
// Set while the stock Lua print runs on this thread: all its fputs output is
// print text, whatever FILE* it passes.
inline thread_local bool g_in_print = false;
}  // namespace fable2::luaconsole

// The engine replaces the Lua globals print / cprint / _ALERT with its own C
// function (registered by sub_82A19EB0 via lua_pushcclosure 0x8219AB28 and
// lua_setfield 0x82A1EC08). In this retail build it formats the text and then
// discards it, so nothing printed by Lua ever reached stdout. Route it to the
// game VM's stock luaB_print (sub_82A20768, "'tostring' must return a string to
// 'print'"), which writes through fputs below. Same lua_CFunction contract.
extern "C" void sub_82A20768(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82A19CD8(PPCContext& ctx, uint8_t* base) {
  fable2::luaconsole::g_in_print = true;
  sub_82A20768(ctx, base);
  fable2::luaconsole::g_in_print = false;
}

// CRT fputs(const char* str, FILE* stream). Lua print() writes through it to
// stdout; capture that text, then let the original run.
extern "C" void __imp__sub_82CA89A0(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82CA89A0(PPCContext& ctx, uint8_t* base) {
  namespace ta = fable2::textappend;
  if (ctx.r4.u32 == fable2::luaconsole::kGuestStdout || fable2::luaconsole::g_in_print) {
    const uint32_t str = ctx.r3.u32;
    if (str != 0 && ta::rdable2(base, str, 1)) {
      const char* p = reinterpret_cast<const char*>(ta::host_addr(base, str));
      size_t n = 0;
      while (n < 4096 && ta::rdable2(base, str + uint32_t(n), 1) && p[n] != '\0') ++n;
      fable2::luaconsole::on_stdout_text(p, n);
    }
  }
  __imp__sub_82CA89A0(ctx, base);
}
