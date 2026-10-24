#!/usr/bin/env python3
"""coop_relay.py - a second network between the rig's host and partner.

Josh and Calvin share two networks (home LAN and ZeroTier), so each PC hears
the other at two addresses and every lobby record arrives twice. On one PC the
rig copies talk over loopback (host 127.0.0.1, partner 127.0.0.2); this relay
adds the second path:

  partner -> 127.0.0.4 (the host's second address)  ==> host    127.0.0.1, from 127.0.0.5
  host    -> 127.0.0.5 (the partner's second address) ==> partner 127.0.0.2, from 127.0.0.4

Host lan_peers = '127.0.0.2,127.0.0.5'; partner lan_peers = '127.0.0.1,127.0.0.4'.
UDP (lobby 3075, System Link 3074, the game's VDP/UDP 1000-1003) is forwarded
port to port; TCP (25002-25004, backup 3077) by connection.

  python coop_relay.py            runs until killed; logs counts every 10 s
"""

import socket
import sys
import threading
import time

HOST, PARTNER = "127.0.0.1", "127.0.0.2"
HOST_ALT, PARTNER_ALT = "127.0.0.4", "127.0.0.5"  # host's / partner's second address
UDP_PORTS = [1000, 1001, 1002, 1003, 3074, 3075]
TCP_PORTS = [3077, 25002, 25003, 25004]
counts = {}


def udp_pair(port):
    # a: what the partner sends to HOST_ALT; b: what the host sends to PARTNER_ALT.
    a = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    b = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    a.bind((HOST_ALT, port))
    b.bind((PARTNER_ALT, port))

    def pump(src, dst, to, key):
        while True:
            try:
                data, _ = src.recvfrom(65536)
                dst.sendto(data, to)
                counts[key] = counts.get(key, 0) + 1
            except OSError:
                time.sleep(0.05)

    threading.Thread(target=pump, args=(a, b, (HOST, port), f"udp{port} p->h"), daemon=True).start()
    threading.Thread(target=pump, args=(b, a, (PARTNER, port), f"udp{port} h->p"), daemon=True).start()


def tcp_listen(listen_ip, port, target_ip, from_ip, key):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((listen_ip, port))
    srv.listen(8)

    def pipe(x, y):
        try:
            while True:
                d = x.recv(65536)
                if not d:
                    break
                y.sendall(d)
        except OSError:
            pass
        for s in (x, y):
            try:
                s.close()
            except OSError:
                pass

    def serve():
        while True:
            c, _ = srv.accept()
            try:
                up = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                up.bind((from_ip, 0))
                up.connect((target_ip, port))
            except OSError:
                c.close()
                continue
            counts[key] = counts.get(key, 0) + 1
            threading.Thread(target=pipe, args=(c, up), daemon=True).start()
            threading.Thread(target=pipe, args=(up, c), daemon=True).start()

    threading.Thread(target=serve, daemon=True).start()


def main():
    for p in UDP_PORTS:
        udp_pair(p)
    for p in TCP_PORTS:
        tcp_listen(HOST_ALT, p, HOST, PARTNER_ALT, f"tcp{p} p->h")
        tcp_listen(PARTNER_ALT, p, PARTNER, HOST_ALT, f"tcp{p} h->p")
    print("relay up", flush=True)
    while True:
        time.sleep(10)
        print(time.strftime("%H:%M:%S"), dict(sorted(counts.items())), flush=True)


if __name__ == "__main__":
    sys.exit(main())
