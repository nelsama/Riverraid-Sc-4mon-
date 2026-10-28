#!/usr/bin/env python3
"""uart.py - Envia un comando al monitor por el UART emulado (TCP) y muestra la
respuesta. Uso: python tools/uart.py "comando" [segundos_espera]"""
import socket
import sys
import time

HOST = "127.0.0.1"
PORT = 4000

cmd = sys.argv[1] if len(sys.argv) > 1 else "h"
wait = float(sys.argv[2]) if len(sys.argv) > 2 else 2.0

s = socket.create_connection((HOST, PORT), timeout=10)
s.settimeout(0.2)
s.sendall((cmd + "\r").encode())
time.sleep(wait)
try:
    while True:
        d = s.recv(4096)
        if not d:
            break
        sys.stdout.write(d.decode("latin-1", errors="replace"))
        sys.stdout.flush()
except socket.timeout:
    pass
s.close()
