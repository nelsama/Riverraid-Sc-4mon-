#!/usr/bin/env python3
"""vel_vs_x.py - Velocidad REAL del scroll segun la posicion horizontal.

Mide, para cada posicion del avion, cuantos frames ejecuta el juego y cuantos
pixeles avanza el scroll en un intervalo de tiempo de RELOJ. Asi se ve la
velocidad efectiva en px/segundo, que es lo que percibe el jugador, y no solo
px/frame (que puede ser constante mientras el frame se alarga).

Mueve el avion con la UART y va midiendo en cada posición. Si el avión muere,
reinicia. Evita posiciones donde muere seguido.

  Uso: python tools/vel_vs_x.py [segundos_por_punto]
"""
import re
import sys
import time
import urllib.request

import uartsend
from vel_en_juego import snap

DBG = "http://127.0.0.1:8080"


def muestra(seg):
    f0, _s, sy0, x0 = snap()
    t0 = time.time()
    time.sleep(seg)
    f1, _s2, sy1, x1 = snap()
    dt = time.time() - t0
    df = (f1 - f0) & 0xFFFF
    ds = (sy0 - sy1) & 0xFF
    if ds > 128:
        ds -= 256
    return x0, df / dt, ds / dt, df, ds


def main():
    seg = float(sys.argv[1]) if len(sys.argv) > 1 else 0.8
    uartsend.enviar(" ")
    time.sleep(0.4)

    print("%-6s %-10s %-10s %s" % ("x", "fps", "px/s", "px/frame"))
    for k in range(14):
        x, fps, pxs, df, ds = muestra(seg)
        pf = (ds / df) if df else 0
        print("%-6s %-10.1f %-10.1f %.3f" % (x, fps, pxs, pf))
        if df == 0 or x is None:
            uartsend.enviar(" ")
            time.sleep(0.3)
            continue
        # dar un paso a la derecha y volver al centro si se va del cauce
        uartsend.enviar("d", 0.02)
        time.sleep(0.08)


if __name__ == "__main__":
    main()
