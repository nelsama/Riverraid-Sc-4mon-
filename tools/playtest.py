#!/usr/bin/env python3
"""playtest.py - Prueba de juego automatizada: dispara, esquiva y recoge.

Lee la OAM para saber donde estan avion/islas/barco/deposito y actua en
consecuencia. Comprueba que disparar sube el SCORE y que recoger fuel sube
la barra. Uso: python tools/playtest.py [segundos]
"""
import json
import socket
import sys
import time
import urllib.request

BASE = "http://127.0.0.1:8080"
DUR = float(sys.argv[1]) if len(sys.argv) > 1 else 20.0

PAT_DIGIT0, PAT_LIVES, PAT_BAR_ON = 50, 46, 42   # HUD a 2x: paso 2
BASE_SLOT, SCORE_SLOTS, FUEL_SLOTS = 10, 4, 4

# Columnas de isla ocupadas en pantalla: se detectan por tilemap.
WATER = {2, 3}
ISLAND = {4, 5, 6, 7}

s = socket.create_connection(("127.0.0.1", 4000), timeout=10)
s.settimeout(0.02)


def drain():
    try:
        while True:
            if not s.recv(512):
                break
    except socket.timeout:
        pass


def tap(k):
    s.sendall(k.encode())


def read():
    d = json.load(urllib.request.urlopen(BASE + "/vram.json"))
    spr = {x["n"]: x for x in d["sprites"]}
    tm = d["tilemap"]

    def tile(n):
        return spr[n]["tile"] if n in spr else None

    score = 0
    for i in range(SCORE_SLOTS):
        t = tile(BASE_SLOT + i)
        if t is not None and PAT_DIGIT0 <= t < PAT_DIGIT0 + 20:
            score = score * 10 + (t - PAT_DIGIT0) // 2
    fuel = sum(1 for i in range(FUEL_SLOTS)
               if tile(BASE_SLOT + SCORE_SLOTS + i) == PAT_BAR_ON)
    lt = tile(BASE_SLOT + SCORE_SLOTS + FUEL_SLOTS + 1)
    lives = (lt - PAT_DIGIT0) // 2 if lt and PAT_DIGIT0 <= lt < PAT_DIGIT0 + 20 else -1
    return spr.get(0), spr.get(1), spr.get(4), tm, score, fuel, lives


def blocked(tm, plane):
    """True si hay isla justo encima del avion (fila de pantalla del avion)."""
    px, py = plane["x"], plane["y"]
    row = py // 8
    c0, c1 = px // 8, (px + 7) // 8
    for r in range(row - 1, row + 2):
        if 0 <= r < 32:
            for c in range(c0, c1 + 1):
                if tm[r][c] in ISLAND:
                    return True
    return False


drain()
t0 = time.time()
last_fire = 0.0
first = read()
print("inicio: score=%d fuel=%d lives=%d" % (first[4], first[5], first[6]))
while time.time() - t0 < DUR:
    plane, boat, fuel, tm, score, fs, lives = read()
    if not plane:
        break
    # Disparar siempre (cooldown interno del juego).
    if time.time() - last_fire > 0.15:
        tap(" ")
        last_fire = time.time()
    # Esquivar islas: si la hay encima, moverse de lado.
    if blocked(tm, plane):
        tap("a" if plane["x"] > 160 else "d")
    # Perseguir el deposito si existe y esta arriba.
    if fuel and fuel["y"] < plane["y"]:
        if plane["x"] + 4 < fuel["x"]:
            tap("d")
        elif plane["x"] + 4 > fuel["x"] + 8:
            tap("a")
    drain()
    time.sleep(0.04)

plane, boat, fuel, tm, score, fs, lives = read()
print("final : score=%d fuel=%d lives=%d" % (score, fs, lives))
s.close()
