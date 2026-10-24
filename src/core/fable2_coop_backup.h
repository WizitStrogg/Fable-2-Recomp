// fable_2 - co-op campaign backup to the partner (feature 001 US5, T039; proof P7).
//
// After each campaign save made with player 2 in the world (fable2_coop_frontend.h,
// write_p2_file) the host zips the whole slot folder with a manifest of SHA-256
// hashes (fable2_coop_backup_fmt.h) and sends it over one TCP connection to the
// partner. The partner keeps a listener on its game address, port kPort, while
// co-op campaigns are on; it takes a backup only from the host it is joined
// to, checks every hash, writes
//   <exe dir>\coop-backups\<campaign id>\<yyyymmdd-hhmmss>.zip
// by temp file + rename, keeps the newest kKeep and answers OK. F6 shows the
// latest one (coop_backup_last). Co-op code writes nothing else outside the
// campaign slot (contracts/campaign-files.md rule 5).
//
// Wire format: "F2CB1 <campaign id> <zip bytes>\n" then the zip; reply "OK\n"
// or "ERR <why>\n".

#pragma once

#ifdef _WIN32
// Same as src/input/remote_control_server.h: WIN32_LEAN_AND_MEAN is set for
// this target, so windows.h never pulled in the old winsock.h.
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/kernel/xam/lan_lobby.h>
#include <rex/logging.h>

#include "fable2_coop_backup_fmt.h"
#include "fable2_coop_template.h"

#if defined(_WIN32) && (defined(_MSC_VER) || defined(__clang__))
#pragma comment(lib, "Ws2_32.lib")
#endif

REXCVAR_DEFINE_STRING(coop_backup_last, "", "Network",
                      "Read only: the newest co-op campaign backup this PC holds (shown in F6)");

namespace fable2::coopbackup {

namespace fs = std::filesystem;
namespace fmt_ = fable2::coopbackupfmt;
namespace lobby = rex::kernel::xam::lobby;

constexpr uint16_t kPort = 3077;
constexpr size_t kKeep = 10, kMaxZip = 16u << 20;
constexpr int kTimeoutMs = 15000;

inline fs::path backups_dir() { return fable2::cooptemplate::exe_dir() / "coop-backups"; }

inline bool valid_cid(const std::string& cid) {
  if (cid.size() != 16) return false;
  for (char c : cid)
    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) return false;
  return true;
}

inline std::string stamp_now() {
  const std::time_t t = std::time(nullptr);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm);
  return buf;
}

#ifdef _WIN32
inline bool wsa_ready() {
  static const bool ok = [] {
    WSADATA d;
    return WSAStartup(MAKEWORD(2, 2), &d) == 0;
  }();
  return ok;
}

inline void set_timeouts(SOCKET s) {
  DWORD ms = kTimeoutMs;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
}

inline bool send_all(SOCKET s, const std::string& data) {
  size_t off = 0;
  while (off < data.size()) {
    const int n = ::send(s, data.data() + off, static_cast<int>(std::min<size_t>(data.size() - off, 1 << 16)), 0);
    if (n <= 0) return false;
    off += static_cast<size_t>(n);
  }
  return true;
}

inline bool recv_line(SOCKET s, std::string& line, size_t max = 256) {
  line.clear();
  char c;
  while (line.size() < max) {
    if (::recv(s, &c, 1, 0) != 1) return false;
    if (c == '\n') return true;
    line.push_back(c);
  }
  return false;
}

inline bool recv_exact(SOCKET s, std::string& out, size_t n) {
  out.resize(n);
  size_t off = 0;
  while (off < n) {
    const int r = ::recv(s, out.data() + off, static_cast<int>(std::min<size_t>(n - off, 1 << 16)), 0);
    if (r <= 0) return false;
    off += static_cast<size_t>(r);
  }
  return true;
}

// --- host: send ----------------------------------------------------------------------

inline std::atomic<bool> g_sending{false};

// Queues a backup of `slot` to the partner at `ip` (host byte order). Runs on
// its own thread; one at a time (a save while one is still sending is skipped,
// the next save sends again).
inline void SendBackup(const fs::path& slot, const std::string& cid, uint32_t ip) {
  if (!valid_cid(cid) || !ip || g_sending.exchange(true)) return;
  std::thread([slot, cid, ip] {
    struct Done {
      ~Done() { g_sending.store(false); }
    } done;
    std::vector<fmt_::Entry> files;
    std::error_code ec;
    for (const auto& f : fs::directory_iterator(slot, ec)) {
      if (!f.is_regular_file() || f.path().extension() == ".tmp") continue;
      std::ifstream in(f.path(), std::ios::binary);
      files.push_back({f.path().filename().string(), std::string(std::istreambuf_iterator<char>(in), {})});
    }
    if (files.empty() || !wsa_ready()) return;
    const std::string zip = fmt_::MakeBackup(files);
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return;
    set_timeouts(s);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(kPort);
    a.sin_addr.s_addr = htonl(ip);
    std::string reply = "no connection";
    if (::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0 &&
        send_all(s, "F2CB1 " + cid + " " + std::to_string(zip.size()) + "\n") && send_all(s, zip)) {
      if (!recv_line(s, reply)) reply = "no reply";
    }
    closesocket(s);
    REXSYS_INFO("[coop] backup of campaign {} ({} files, {} bytes) to {}.{}.{}.{}: {}", cid, files.size(),
                zip.size(), ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255, reply);
  }).detach();
}

// --- partner: receive ----------------------------------------------------------------

inline bool keep_backup(const std::string& cid, const std::string& zip, std::string* why) {
  if (!fmt_::VerifyBackup(zip, why)) return false;
  std::error_code ec;
  const fs::path dir = backups_dir() / cid;
  fs::create_directories(dir, ec);
  const std::string name = stamp_now() + ".zip";
  const fs::path tmp = dir / (name + ".tmp");
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out.write(zip.data(), static_cast<std::streamsize>(zip.size()));
    if (!out) {
      *why = "cannot write";
      return false;
    }
  }
  fs::rename(tmp, dir / name, ec);
  if (ec) {
    fs::remove(tmp, ec);
    *why = "cannot rename";
    return false;
  }
  std::vector<std::string> names;
  for (const auto& f : fs::directory_iterator(dir, ec))
    if (f.path().extension() == ".zip") names.push_back(f.path().filename().string());
  for (const auto& old : fmt_::ToPrune(names, kKeep)) fs::remove(dir / old, ec);
  rex::cvar::SetFlagByName("coop_backup_last", "Campaign backup received " + name.substr(0, 15) +
                                                  " (" + std::to_string(std::min(names.size(), kKeep)) +
                                                  " kept in coop-backups)");
  REXSYS_INFO("[coop] backup of campaign {} kept: {} ({} bytes, hashes ok)", cid, (dir / name).string(),
              zip.size());
  return true;
}

inline void serve(SOCKET c, uint32_t from) {
  set_timeouts(c);
  std::string line, why = "bad header";
  bool ok = false;
  const uint32_t host = lobby::GetLobbyStatus().joined_host_ip;
  if (from != host) {
    why = "not the host this PC is joined to";
  } else if (recv_line(c, line) && line.rfind("F2CB1 ", 0) == 0) {
    const std::string cid = line.substr(6, 16);
    const unsigned long long size =
        line.size() >= 24 && line[22] == ' ' ? std::strtoull(line.c_str() + 23, nullptr, 10) : 0;
    std::string zip;
    if (!valid_cid(cid) || line.size() < 24 || line[22] != ' ') why = "bad campaign id";
    else if (!size || size > kMaxZip) why = "bad size";
    else if (!recv_exact(c, zip, static_cast<size_t>(size))) why = "short read";
    else ok = keep_backup(cid, zip, &why);
  }
  send_all(c, ok ? std::string("OK\n") : "ERR " + why + "\n");
  if (!ok) REXSYS_WARN("[coop] backup refused: {}", why);
  closesocket(c);
}

inline std::atomic<bool> g_listening{false};

// Starts the listener once (on the game address) while co-op campaigns are on.
inline void EnsureListener() {
  if (g_listening.load() || !wsa_ready()) return;
  const auto status = lobby::GetLobbyStatus();
  if (!status.running) return;
  SOCKET l = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (l == INVALID_SOCKET) return;
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(kPort);
  if (status.game_address.empty() ||
      inet_pton(AF_INET, status.game_address.c_str(), &a.sin_addr) != 1)
    a.sin_addr.s_addr = htonl(INADDR_ANY);
  if (::bind(l, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || ::listen(l, 2) != 0) {
    REXSYS_WARN("[coop] backup listener: cannot listen on {}:{}", status.game_address, kPort);
    closesocket(l);
    g_listening.store(true);  // do not retry every frame
    return;
  }
  g_listening.store(true);
  REXSYS_INFO("[coop] backup listener on {}:{}", status.game_address, kPort);
  std::thread([l] {
    for (;;) {
      sockaddr_in from{};
      int len = sizeof(from);
      SOCKET c = ::accept(l, reinterpret_cast<sockaddr*>(&from), &len);
      if (c == INVALID_SOCKET) continue;
      serve(c, ntohl(from.sin_addr.s_addr));
    }
  }).detach();
}

// Shows the newest backup this PC holds (F6) at start.
inline void LoadLast() {
  std::error_code ec;
  std::string newest;
  for (const auto& d : fs::directory_iterator(backups_dir(), ec))
    for (const auto& f : fs::directory_iterator(d.path(), ec))
      if (f.path().extension() == ".zip" && f.path().filename().string() > newest)
        newest = f.path().filename().string();
  if (!newest.empty())
    rex::cvar::SetFlagByName("coop_backup_last", "Newest campaign backup " + newest.substr(0, 15));
}
#else
inline void SendBackup(const fs::path&, const std::string&, uint32_t) {}
inline void EnsureListener() {}
inline void LoadLast() {}
#endif

}  // namespace fable2::coopbackup
