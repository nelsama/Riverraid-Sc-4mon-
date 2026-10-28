#!/usr/bin/env python3
"""leer_mem.py - Lee bytes de la RAM del 6502 a traves de /vram.json.

El emulador solo publica RAM por HTTP como si fueran celdas del tilemap:
cualquier direccion de la 6502 se lee en RAM[a] -> tilemap[a//64][a%64].

  Uso: python tools/leer_mem.py 2531 8 [repeticiones]
"""
import json
import sys
import time
import urllib.request

addr = int(sys.argv[1], 16)
n = int(sys.argv[2]) if len(sys.argv) > 2 else 8
reps = int(sys.argv[3]) if len(sys.argv) > 3 else 1

for _ in range(reps):
    d = json.load(urllib.request.urlopen("http://127.0.0.1:8080/vram.json"))
    tm = d["tilemap"]
    out = []
    for a in range(addr, addr + n):
        r = a >> 6
        c = a & 63
        out.append(tm[r][c] if r < len(tm) else "NA")
    sp = [s for s in d["sprites"] if s["n"] in (26, 27)]
    print("%04X: %s   islas=%s" % (addr, out, sp))
    time.sleep(0.4)
