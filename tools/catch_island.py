#!/usr/bin/env python3
"""catch_island.py - Espera a que aparezca una fila de isla aislada (posible
isla fantasma) y guarda la captura y las filas implicadas.

  Uso: python tools/catch_island.py [segundos]
"""
import http.client
import io
import json
import socket
import sys
import time

from PIL import Image

secs = float(sys.argv[1]) if len(sys.argv) > 1 else 120.0
conn = http.client.HTTPConnection("127.0.0.1", 8080, timeout=15)


def uart(cmd):
    try:
        s = socket.create_connection(("127.0.0.1", 4000), timeout=5)
        s.sendall((cmd + "\r").encode())
        time.sleep(0.05)
        s.close()
    except OSError:
        pass


t0 = time.time()
n = 0
while time.time() - t0 < secs:
    # pilotar: zigzag lento y disparar
    t = int(time.time() - t0)
    uart("a" if (t // 3) % 2 == 0 else "d")
    if t % 4 == 0:
        uart(" ")

    conn.request("GET", "/vram.json")
    d = json.loads(conn.getresponse().read())
    tm = d["tilemap"]
    filas = [r for r in range(32) if any(v in (4, 5, 6, 7) for v in tm[r])]
    borde = {0, 1, 30, 31}
    sueltas = [r for r in filas
               if r not in borde and (r - 1 not in filas and r + 1 not in filas)]
    if sueltas:
        conn.request("GET", "/screen.png")
        png = conn.getresponse().read()
        Image.open(io.BytesIO(png)).convert("RGB").save(
            "docs/img/seq/fantasma_%d.png" % n)
        print("CAPTURA %d  filas con isla=%s  sueltas=%s" % (n, filas, sueltas))
        for r in range(32):
            isl = [k for k, v in enumerate(tm[r]) if v in (4, 5, 6, 7)]
            if isl:
                print("   fila %2d cols=%s tiles=%s" %
                      (r, isl, [tm[r][k] for k in isl]))
        n += 1
        if n >= 3:
            break
    time.sleep(0.2)
if n == 0:
    print("sin islas sueltas en %.0f s" % secs)
