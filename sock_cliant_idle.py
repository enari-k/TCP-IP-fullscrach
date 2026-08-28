#!/usr/bin/env python3

import socket
import sys
import time


HOST = "192.0.2.2"
PORT = 7

if len(sys.argv) < 2:
    print(f"usage: {sys.argv[0]} <connections> [hold_seconds]")
    sys.exit(1)

N = int(sys.argv[1])
HOLD_SECONDS = int(sys.argv[2]) if len(sys.argv) >= 3 else 30

socks = []

try:
    for i in range(N):
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(5)

        try:
            s.connect((HOST, PORT))
        except Exception as e:
            print(f"[CONNECT FAIL] {i}: {e}")
            s.close()
            break

        socks.append(s)

    print(f"{len(socks)} / {N} connections established")
    print(f"holding for {HOLD_SECONDS} seconds...")

    time.sleep(HOLD_SECONDS)

finally:
    print("closing connections...")

    for s in socks:
        try:
            s.close()
        except Exception:
            pass

    print("closed")