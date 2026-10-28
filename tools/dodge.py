#!/usr/bin/env python3
"""dodge.py - Piloto que esquiva islas leyendo el tilemap, para comprobar que
la colisión es justa (se puede sobrevivir alejandose de los obstaculos).

Uso: python tools/dodge.py [segundos]
"""
import json
import socket
import sys
import time
import urllib.request

BASE = "http://127.0.0.1:8080"
DUR = float(sys.argv[1]) if len(sys.argv) > 1 else 15.0
ISLAND = {4, 5, 6, 7, 12}   # tiles de isla/vegetacion

s = socket.create_connection(("127.0.0.1", 4000), timeout=10)
s.settimeout(0.02)


def drain():
    try:
        while s.recv(512):
            pass
    except socket.timeout:
        pass


drain()
t0 = time.time()
last_hit = 0
while time.time() - t0 < DUR:
    d = json.load(urllib.request.urlopen(BASE + "/vram.json"))
    spr = {x["n"]: x for x in d["sprites"]}
    tm = d["tilemap"]
    p = spr.get(0)
    if not p:
        print("%.1fs avion oculto (game over o parpadeo)" % (time.time() - t0))
        drain()
        time.sleep(0.05)
        continue
    px, py = p["x"], p["y"]
    col = (px + 8) // 8
    # fila de pantalla del avion -> fila de cinta (aproximada por el propio tilemap:
    # se busca en las filas cercanas a py/8 la que tenga isla en la columna)
    row = (py // 8) % 32
    danger = False
    for r in range(max(0, row - 3), min(32, row + 4)):
        if tm[r][col] in ISLAND or tm[r][col - 1] in ISLAND:
            danger = True
            break
    if danger:
        # apartarse hacia el lado con mas agua
        left = sum(1 for c in range(col - 6, col) if 0 <= c < 64 and tm[row][c] in (2, 3))
        s.sendall(b"d" if left < 3 else b"a")
    drain()
    time.sleep(0.03)
s.close()
print("fin dodge")
