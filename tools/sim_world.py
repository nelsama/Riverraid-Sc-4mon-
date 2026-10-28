#!/usr/bin/env python3
"""sim_world.py - Reproduce en Python la generacion de filas de game.c para
detectar islas sin pareja, sin depender del emulador.

  Uso: python tools/sim_world.py [filas]
"""
import sys

RIVER_LEFT, RIVER_RIGHT = 8, 31

rows = int(sys.argv[1]) if len(sys.argv) > 1 else 400

pending_l = 0xFF
islas = {}          # fila de mundo -> (l, r, top?)
for n in range(rows):
    il = ir = 0xFF
    top = True

    step = (n >> 3) & 3
    level = step if step <= 2 else 4 - step
    gl = RIVER_LEFT + 2 + level
    gr = RIVER_RIGHT - 2 - level

    h = (n * 37 + 11) & 0xFF
    scr_row = n & 31

    mitad_abajo = False
    if pending_l != 0xFF:
        il, ir = pending_l, pending_r
        top = False
        pending_l = 0xFF
        mitad_abajo = True

    if (not mitad_abajo) and (n & 1) == 0 and h < 30 and 8 <= scr_row <= 24:
        width = gr - gl + 1
        if width >= 14:
            bl = gl + width // 2 - 1
            if bl > gl + 3 and bl + 1 < gr - 3:
                il, ir = bl, bl + 1
                top = True
                pending_l, pending_r = il, ir

    if il != 0xFF:
        islas[n] = (il, ir, top)

# Comprobacion: toda isla debe tener su par en n+1 con top=False
print("islas generadas:", len(islas))
malas = []
for n, (l, r, top) in islas.items():
    par = islas.get(n + 1)
    if top:
        if par is None or par[2]:
            malas.append((n, "arriba sin abajo", par))
    else:
        prev = islas.get(n - 1)
        if prev is None or not prev[2]:
            malas.append((n, "abajo sin arriba", prev))
print("malas:", len(malas))
for m in malas[:10]:
    print("  ", m)
