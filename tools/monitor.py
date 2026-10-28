#!/usr/bin/env python3
"""monitor.py - Lee el HUD en sprites del emulador (puntos, combustible y vidas)
directamente de la OAM via /vram.json.

Debe coincidir con los defines de game.c:
  SPR_HUD_BASE=10, 4 digitos de puntos, HUD_FUEL_W celdas de barra,
  luego icono de vidas y 1 digito de vidas.

Uso: python tools/monitor.py [segundos]
"""
import json
import sys
import time
import urllib.request

BASE = "http://127.0.0.1:8080"
DUR = float(sys.argv[1]) if len(sys.argv) > 1 else 8.0

# Espejo de game.c: el HUD va a ESCALA 2x, asi que cada pieza usa DOS patrones
# (N y N+1). El slot apunta al primero del par.
PAT_DIGIT0 = 50      # 50,52,...,68 -> '0'..'9'
PAT_STEP = 2
PAT_LIVES = 46
PAT_BAR_ON = 42
PAT_BAR_OFF = 44

BASE_SLOT = 10
SCORE_SLOTS = 4
FUEL_SLOTS = 4


def read():
    d = json.load(urllib.request.urlopen(BASE + "/vram.json"))
    spr = {s["n"]: s for s in d["sprites"]}

    def tile(n):
        return spr[n]["tile"] if n in spr else None

    score = 0
    for i in range(SCORE_SLOTS):
        t = tile(BASE_SLOT + i)
        if t is not None and PAT_DIGIT0 <= t < PAT_DIGIT0 + 10 * PAT_STEP:
            score = score * 10 + (t - PAT_DIGIT0) // PAT_STEP

    fuel = sum(1 for i in range(FUEL_SLOTS)
               if tile(BASE_SLOT + SCORE_SLOTS + i) == PAT_BAR_ON)

    lives_slot = BASE_SLOT + SCORE_SLOTS + FUEL_SLOTS + 1
    lt = tile(lives_slot)
    lives = (lt - PAT_DIGIT0) // PAT_STEP \
        if lt is not None and PAT_DIGIT0 <= lt < PAT_DIGIT0 + 10 * PAT_STEP else None

    return score, fuel, lives


t0 = time.time()
print("t(s)  score  fuel/6  lives")
while time.time() - t0 < DUR:
    s, f, l = read()
    print("%5.1f   %04d     %d      %s" % (time.time() - t0, s, f, l))
    time.sleep(0.5)
