#!/usr/bin/env python3
"""trazar_islas.py - Seguimiento de las islas (sprites 26/27) durante N segundos.

Comprueba lo imposible con tiles: que la isla entra por arriba pixel a pixel,
que baja con el scroll y que sale por abajo (desaparece cuando y >= 240).

Lee tambien el valor de diagnostico publicado en el tilemap (fila 0, col 63)
para saber cuantas islas se han creado.

  Uso: python tools/trazar_islas.py [segundos]
"""
import json
import sys
import time
import urllib.request

URL = "http://127.0.0.1:8080/vram.json"
secs = float(sys.argv[1]) if len(sys.argv) > 1 else 20.0
ant = {}
t0 = time.time()
n = 0
saltos = 0
while time.time() - t0 < secs:
    d = json.load(urllib.request.urlopen(URL))
    tm = d["tilemap"]
    # valor de 16 bits publicado por el juego en fila 0, col 63
    dbg = tm[0][63] | (tm[1][63] << 8)
    ahora = {}
    for s in d["sprites"]:
        if s["n"] in (26, 27):
            ahora[s["n"]] = (s["x"], s["y"])
    for n_spr, (x, y) in ahora.items():
        prev = ant.get(n_spr)
        if prev is None:
            print("%6.2fs [nuevo] slot %d en x=%d y=%d  (dbg=%04X)"
                  % (time.time() - t0, n_spr, x, y, dbg))
        else:
            if prev[0] != x:
                print("%6.2fs [X CAMBIA] slot %d x %d -> %d"
                      % (time.time() - t0, n_spr, prev[0], x))
            dy = y - prev[1]
            if dy not in (0, 1):
                saltos += 1
                print("%6.2fs [SALTO Y] slot %d %d -> %d (%+d)"
                      % (time.time() - t0, n_spr, prev[1], y, dy))
    for n_spr in ant:
        if n_spr not in ahora:
            print("%6.2fs [fin] slot %d (estaba en y=%d)"
                  % (time.time() - t0, n_spr, ant[n_spr][1]))
    ant = ahora
    n += 1
    time.sleep(0.1)
print("muestras: %d  saltos de Y: %d" % (n, saltos))
