#!/usr/bin/env python3
"""prioridad_hud.py - Comprueba que el HUD nunca lo tape un sprite del juego.

El core dibuja el sprite de MENOR indice por encima, y tiene un line buffer de
8 entradas por linea (el 9.o sprite de una misma linea no se dibuja). Este
script juega unos segundos con el piloto automatico y vigila, frame a frame:

  1. Que los sprites del HUD ocupen los indices MAS BAJOS (prioridad).
  2. Que como maximo 8 sprites crucen cada linea.
  3. Que ningun objeto del juego invada la banda del HUD (y < HUD_Y2 + 8).

Uso: python tools/prioridad_hud.py [segundos]
Requiere el emulador con --debug=8080 y el juego ya arrancado (FIRE).
"""
import json
import socket
import sys
import time
import urllib.request
from collections import Counter

BASE = "http://127.0.0.1:8080"
DUR = float(sys.argv[1]) if len(sys.argv) > 1 else 30.0

# Mismo reparto que game.c (SPR_HUD_BASE / SPR_HUD_FUEL / SPR_HUD_LIVES).
HUD_SLOTS = set(range(0, 4)) | set(range(4, 11)) | {11, 12}
HUD_BOTTOM = 19 + 8          # ultima linea que pinta el HUD
OBJETOS = {13: "avion", 14: "explosion", 15: "enemigo",
           16: "bala", 17: "bala", 18: "bidon"}


def lectura():
    return json.load(urllib.request.urlopen(BASE + "/vram.json"))


def main():
    s = socket.create_connection(("127.0.0.1", 4000), timeout=10)
    s.settimeout(0.05)

    def drain():
        try:
            while True:
                if not s.recv(1024):
                    break
        except socket.timeout:
            pass

    peor_linea = 0
    fallos = []
    t0 = time.time()
    while time.time() - t0 < DUR:
        d = lectura()
        sp = d["sprites"]
        if not sp:
            fallos.append("sin sprites: el juego esta en GAME OVER")
            break

        # 1. El HUD tiene los indices mas bajos.
        objetos = [x["n"] for x in sp if x["n"] not in HUD_SLOTS]
        if objetos and min(objetos) <= max(HUD_SLOTS):
            fallos.append(f"objeto {min(objetos)} por debajo del HUD")

        # 2. Densidad por linea.
        por_y = Counter()
        for x in sp:
            for k in range(8):
                por_y[x["y"] + k] += 1
        m = max(por_y.values())
        peor_linea = max(peor_linea, m)
        if m > 8:
            fallos.append(f"{m} sprites en una linea (max 8)")

        # 3. Ningun objeto en la banda del HUD.
        for x in sp:
            if x["n"] in OBJETOS and x["y"] < HUD_BOTTOM:
                fallos.append(f"{OBJETOS[x['n']]} en y={x['y']} dentro del HUD")

        # Piloto: persigue el centro del cauce.
        avion = [x for x in sp if x["n"] == 13]
        if avion:
            px = avion[0]["x"]
            if px < 150:
                s.sendall(b"d")
            elif px > 170:
                s.sendall(b"a")
        drain()
        time.sleep(0.03)

    s.close()
    print(f"frames duracion: {DUR:.0f} s   peor densidad por linea: {peor_linea}")
    if fallos:
        uniq = sorted(set(fallos))
        print(f"FALLOS ({len(fallos)}):")
        for f in uniq[:20]:
            print("  " + f)
        sys.exit(1)
    print("OK: HUD con prioridad, densidad <= 8 y banda del HUD despejada")


if __name__ == "__main__":
    main()
