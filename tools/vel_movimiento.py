#!/usr/bin/env python3
"""vel_movimiento.py - Mide fps/velocidad MIENTRAS el avion se mueve.

Compara dos regimenes:
  - QUIETO: el avion no recibe ordenes.
  - MOVIENDOSE: se le manda un paso lateral cada cierto numero de frames.

Si moverse baja los fps, la velocidad del mundo (px/seg) cae, que es lo que el
jugador percibe como "va mas lento".

  Uso: python tools/vel_movimiento.py
"""
import time
import urllib.request

import uartsend
from vel_vs_x import muestra

DBG = "http://127.0.0.1:8080"


def vivo():
    """Hay avion en pantalla? dbg_muerte NO vale: se queda a 1 tras la muerte y
    solo se limpia al reiniciar la partida. Se mira el OAM."""
    t = urllib.request.urlopen(DBG + "/vram.txt", timeout=5).read().decode()
    return "sprite  0:" in t


def reintentar(fn):
    for _ in range(8):
        uartsend.enviar(" ")
        time.sleep(0.4)
        if vivo():
            return fn()
    return None


def quieto():
    out = []
    for _ in range(5):
        x, fps, pxs, df, ds = muestra(0.6)
        if x is None:
            return None
        out.append((x, fps, pxs))
        time.sleep(0.03)
    return out


def moviendo(direccion="d"):
    out = []
    for _ in range(8):
        uartsend.enviar(direccion, 0.02)
        x, fps, pxs, df, ds = muestra(0.45)
        if x is None:
            return out if out else None
        out.append((x, fps, pxs))
    return out


def main():
    r = reintentar(quieto)
    print("QUIETO:", [(x, round(f, 1)) for x, f, p in r] if r else "no se pudo")
    r2 = reintentar(lambda: moviendo("d"))
    print("DERECHA:", [(x, round(f, 1)) for x, f, p in r2] if r2 else "no se pudo")
    r3 = reintentar(lambda: moviendo("a"))
    print("IZQUIERDA:", [(x, round(f, 1)) for x, f, p in r3] if r3 else "no se pudo")


if __name__ == "__main__":
    main()
