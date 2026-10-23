#!/usr/bin/env python3
"""coop_rig.py - drive the two-instance co-op test rig (mod-a, mod-b, mod-c).

Needs a rig build (tools\\build_coop.cmd, FABLE2_RIG_REMOTE_CONTROL=ON) installed in
the instance. Only ever starts and stops fable_2.exe processes it launched from
D:\\Fable2-LAN\\mod-a|mod-b|mod-c; never touches any other game process.

  python tools/coop_rig.py start c [--trace]      launch, print pid and control port
  python tools/coop_rig.py menu c                 press A until the main menu is up
  python tools/coop_rig.py press c A [--hold 150]
  python tools/coop_rig.py state c                game_state from the state probe
  python tools/coop_rig.py shot c out.png         frame from the renderer
  python tools/coop_rig.py snap c out.txt         copy fable2_func_summary.log (waits for a refresh)
  python tools/coop_rig.py diff before.txt after.txt [--top 60]
  python tools/coop_rig.py stop c

Library use: import coop_rig; rig = coop_rig.Instance("c"); rig.start(trace=True) ...
"""

import argparse
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import time

ROOT = r"D:\Fable2-LAN"
INSTANCES = {"a": "mod-a", "b": "mod-b", "c": "mod-c"}
PORT_RE = re.compile(r"\[remote\] control server listening on [\d.]+:(\d+)")


def instance_dir(name: str) -> str:
    return os.path.join(ROOT, INSTANCES[name])


def rig_pids(name: str) -> list:
    """pids of fable_2.exe running from this instance folder (exact path match)."""
    exe = os.path.join(instance_dir(name), "fable_2.exe").lower()
    out = subprocess.run(
        ["powershell", "-NoProfile", "-Command",
         "Get-CimInstance Win32_Process -Filter \"Name='fable_2.exe'\" | "
         "ForEach-Object { \"$($_.ProcessId)|$($_.ExecutablePath)\" }"],
        capture_output=True, text=True).stdout
    pids = []
    for line in out.splitlines():
        pid, _, path = line.partition("|")
        if path.strip().lower() == exe:
            pids.append(int(pid))
    return pids


def newest_log(name: str):
    d = os.path.join(instance_dir(name), "logs")
    logs = [os.path.join(d, f) for f in os.listdir(d) if f.endswith(".log")]
    return max(logs, key=os.path.getmtime) if logs else None


def read_shared(path: str) -> str:
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


class Instance:
    def __init__(self, name: str):
        self.name = name
        self.dir = instance_dir(name)
        self.port = None
        self.pid = None
        self.log = None

    # ---- process -----------------------------------------------------------
    def start(self, trace: bool = False, timeout: float = 120.0, env_extra=None):
        if rig_pids(self.name):
            raise RuntimeError(f"{self.name}: already running")
        env = dict(os.environ)
        if trace:
            env.update(FABLE2_FUNC_TRACE="1", FABLE2_FUNC_TRACE_LOG="0",
                       FABLE2_FUNC_TRACE_SUMMARY="1")
        env.update(env_extra or {})
        before = set(os.listdir(os.path.join(self.dir, "logs")))
        p = subprocess.Popen([os.path.join(self.dir, "fable_2.exe")], cwd=self.dir, env=env)
        self.pid = p.pid
        deadline = time.time() + timeout
        while time.time() < deadline:
            new = [f for f in os.listdir(os.path.join(self.dir, "logs"))
                   if f.endswith(".log") and f not in before]
            if new:
                self.log = os.path.join(self.dir, "logs", sorted(new)[-1])
                m = PORT_RE.search(read_shared(self.log))
                if m:
                    self.port = int(m.group(1))
                    return self
            time.sleep(1)
        raise RuntimeError(f"{self.name}: no control server line in the log (rig build installed?)")

    def attach(self):
        """Use an instance that is already running (port from its newest log)."""
        pids = rig_pids(self.name)
        if not pids:
            raise RuntimeError(f"{self.name}: not running")
        self.pid = pids[0]
        self.log = newest_log(self.name)
        m = PORT_RE.search(read_shared(self.log))
        if not m:
            raise RuntimeError(f"{self.name}: no control server line in {self.log}")
        self.port = int(m.group(1))
        return self

    def stop(self):
        for pid in rig_pids(self.name):
            subprocess.run(["taskkill", "/PID", str(pid), "/F"], capture_output=True)
        self.pid = None

    # ---- control server ----------------------------------------------------
    def cmd(self, request: dict, timeout: float = 10.0) -> dict:
        with socket.create_connection(("127.0.0.1", self.port), timeout=timeout) as s:
            s.sendall((json.dumps(request) + "\n").encode())
            buf = b""
            while not buf.endswith(b"\n"):
                chunk = s.recv(4096)
                if not chunk:
                    raise ConnectionError("server closed the connection")
                buf += chunk
        return json.loads(buf.decode().strip())

    def press(self, button: str, hold: int = 150):
        return self.cmd({"cmd": "press", "input": button, "hold_ms": hold})

    def state(self) -> str:
        r = self.cmd({"cmd": "game_state"})
        return r.get("state", {}).get("name", "?")

    def shot(self, path: str):
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        return self.cmd({"cmd": "screenshot", "path": os.path.abspath(path)}, timeout=30)

    def to_main_menu(self, timeout: float = 180.0) -> bool:
        """Press A on the title prompt until the state probe reports MainMenu."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            st = self.state()
            if st == "MainMenu":
                return True
            if st == "PressAScreen":
                self.press("A")
            time.sleep(2)
        return False

    def wait_log(self, pattern: str, timeout: float) -> bool:
        rx = re.compile(pattern)
        deadline = time.time() + timeout
        while time.time() < deadline:
            if rx.search(read_shared(self.log)):
                return True
            time.sleep(1)
        return False

    # ---- function-call summary ---------------------------------------------
    def snap(self, out: str, wait: float = 6.0) -> str:
        """Copies fable2_func_summary.log after its next 5 s refresh."""
        src = os.path.join(self.dir, "fable2_func_summary.log")
        t0 = os.path.getmtime(src) if os.path.exists(src) else 0
        deadline = time.time() + wait + 5
        while time.time() < deadline:
            if os.path.exists(src) and os.path.getmtime(src) > t0:
                break
            time.sleep(0.25)
        time.sleep(0.3)
        os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
        shutil.copyfile(src, out)
        return out


SUM_RE = re.compile(r"^\s*(\d+)\s+x\s+(\S+)")


def read_summary(path: str) -> dict:
    counts = {}
    for line in read_shared(path).splitlines():
        m = SUM_RE.match(line)
        if m:
            counts[m.group(2)] = int(m.group(1))
    return counts


def diff_summaries(before: str, after: str, top: int = 60) -> list:
    """Functions whose call count grew, new ones first, then by growth."""
    a, b = read_summary(before), read_summary(after)
    rows = []
    for name, n in b.items():
        d = n - a.get(name, 0)
        if d > 0:
            rows.append((name not in a, d, name))
    rows.sort(key=lambda r: (not r[0], -r[1]))
    return rows[:top]


def main() -> int:
    p = argparse.ArgumentParser(description="co-op test rig driver")
    sub = p.add_subparsers(dest="op", required=True)
    s = sub.add_parser("start"); s.add_argument("inst"); s.add_argument("--trace", action="store_true")
    for op in ("menu", "state", "stop"):
        sub.add_parser(op).add_argument("inst")
    s = sub.add_parser("press"); s.add_argument("inst"); s.add_argument("button"); s.add_argument("--hold", type=int, default=150)
    s = sub.add_parser("shot"); s.add_argument("inst"); s.add_argument("path")
    s = sub.add_parser("snap"); s.add_argument("inst"); s.add_argument("path")
    s = sub.add_parser("diff"); s.add_argument("before"); s.add_argument("after"); s.add_argument("--top", type=int, default=60)
    a = p.parse_args()

    if a.op == "diff":
        for new, d, name in diff_summaries(a.before, a.after, a.top):
            print(f"{'NEW ' if new else '    '}{d:>10}  {name}")
        return 0
    inst = Instance(a.inst)
    if a.op == "start":
        inst.start(trace=a.trace)
        print(f"pid={inst.pid} port={inst.port} log={inst.log}")
        return 0
    if a.op == "stop":
        inst.stop()
        return 0
    inst.attach()
    if a.op == "menu":
        ok = inst.to_main_menu()
        print("MainMenu" if ok else "timeout: " + inst.state())
        return 0 if ok else 1
    if a.op == "state":
        print(inst.state())
    elif a.op == "press":
        print(json.dumps(inst.press(a.button, a.hold)))
    elif a.op == "shot":
        print(json.dumps(inst.shot(a.path)))
    elif a.op == "snap":
        print(inst.snap(a.path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
