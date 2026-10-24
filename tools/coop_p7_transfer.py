#!/usr/bin/env python3
"""coop_p7_transfer.py - proof P7 (feature 001): zip a save slot, send it over
one TCP connection, verify the copy by SHA-256.

The slot is only read. The zip follows contracts/campaign-files.md rule 6: the
slot folder plus manifest.txt of SHA-256 hashes; the receiver checks every hash
before keeping the file.

  python tools/coop_p7_transfer.py <slot dir> <out dir> [--send-from 127.0.0.1 --recv-on 127.0.0.2]
"""

import argparse
import hashlib
import io
import os
import socket
import struct
import sys
import threading
import time
import zipfile


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def build_zip(slot: str):
    files = {}
    for root, _, names in os.walk(slot):
        for n in sorted(names):
            p = os.path.join(root, n)
            rel = os.path.relpath(p, slot).replace(os.sep, "/")
            with open(p, "rb") as f:
                files[rel] = f.read()
    manifest = "".join(f"{sha256(d)}  {rel}\n" for rel, d in sorted(files.items()))
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for rel, d in sorted(files.items()):
            z.writestr(rel, d)
        z.writestr("manifest.txt", manifest)
    return buf.getvalue(), files


def verify_zip(blob: bytes):
    """Returns (ok, n files, problems) checking every file against manifest.txt."""
    problems = []
    with zipfile.ZipFile(io.BytesIO(blob)) as z:
        want = {}
        for line in z.read("manifest.txt").decode().splitlines():
            h, _, rel = line.partition("  ")
            want[rel] = h
        for rel, h in want.items():
            try:
                got = sha256(z.read(rel))
            except KeyError:
                problems.append(f"missing {rel}")
                continue
            if got != h:
                problems.append(f"hash {rel}")
    return not problems, len(want), problems


def receiver(host: str, port_box: list, out_dir: str, result: dict):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.bind((host, 0))
    srv.listen(1)
    port_box.append(srv.getsockname()[1])
    conn, peer = srv.accept()
    with conn:
        size = struct.unpack("<Q", conn.recv(8, socket.MSG_WAITALL))[0]
        blob = b""
        while len(blob) < size:
            chunk = conn.recv(min(65536, size - len(blob)))
            if not chunk:
                break
            blob += chunk
        ok, n, problems = verify_zip(blob)
        if ok:
            os.makedirs(out_dir, exist_ok=True)
            path = os.path.join(out_dir, time.strftime("%Y%m%d-%H%M%S") + ".zip")
            with open(path, "wb") as f:
                f.write(blob)
            result["path"] = path
        conn.sendall(b"OK" if ok else b"NO")
        result.update(ok=ok, files=n, problems=problems, peer=peer[0], received=len(blob))
    srv.close()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("slot")
    ap.add_argument("out")
    ap.add_argument("--send-from", default="127.0.0.1")
    ap.add_argument("--recv-on", default="127.0.0.2")
    a = ap.parse_args()

    t0 = time.perf_counter()
    blob, files = build_zip(a.slot)
    t_zip = time.perf_counter() - t0
    raw = sum(len(d) for d in files.values())

    port_box, result = [], {}
    th = threading.Thread(target=receiver, args=(a.recv_on, port_box, a.out, result))
    th.start()
    while not port_box:
        time.sleep(0.01)
    t1 = time.perf_counter()
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind((a.send_from, 0))
        s.connect((a.recv_on, port_box[0]))
        s.sendall(struct.pack("<Q", len(blob)) + blob)
        reply = s.recv(2)
    th.join(30)
    t_send = time.perf_counter() - t1

    print(f"slot: {a.slot}")
    print(f"files: {len(files)}  raw: {raw} bytes  zip: {len(blob)} bytes ({100 * len(blob) / max(raw, 1):.0f}%)")
    print(f"zip time: {t_zip * 1000:.0f} ms  send+verify time: {t_send * 1000:.0f} ms")
    print(f"from {a.send_from} to {a.recv_on}: peer seen as {result.get('peer')}, received {result.get('received')} bytes")
    print(f"receiver verdict: {reply.decode()}  manifest files checked: {result.get('files')}  problems: {result.get('problems')}")
    if result.get("path"):
        print(f"kept: {result['path']}")
    return 0 if result.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
