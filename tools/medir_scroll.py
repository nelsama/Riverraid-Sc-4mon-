#!/usr/bin/env python3
"""medir_scroll.py - Mide la velocidad del scroll segun la posicion del avion.

El usuario reporta que al moverse a los lados, en ciertas posiciones el scroll
va mas rapido y en otras mas lento, SIN tocar arriba/abajo (y sin que cambie el
sonido del motor). Aqui se mueve el avion a una direccion, se deja quieto y se
mide cuantos pixeles avanza el scroll por segundo en esa posicion.

  Uso: python tools/medir_scroll.py [lado] [segundos_por_posicion]
"""
import re
import sys
import time
import urllib.request

import juego
import mandos

DBG = "http://127.0.0.1:8080"


def estado():
    t = urllib.request.urlopen(DBG + "/state.txt", timeout=5).read().decode()
    sy = int(re.search(r"scroll: x=0 y=(\d+)", t).group(1))
    m = re.search(r"sprite\s+0: X=\s*(\d+) Y=\s*(\d+)", t)
    return sy, (int(m.group(1)), int(m.group(2))) if m else None


def velocidad(seg):
    s0, _ = estado()
    t0 = time.time()
    time.sleep(seg)
    s1, pl = estado()
    # el scroll decrece al avanzar el mundo: medir el avance real
    d = (s0 - s1) & 0xFF
    if d > 128:
        d -= 256
    return d / (time.time() - t0), pl


def main():
    lado = sys.argv[1] if len(sys.argv) > 1 else "right"
    seg = float(sys.argv[2]) if len(sys.argv) > 2 else 1.2

    if not juego.arrancar(verbose=True):
        print("no arranco")
        return

    print("%-8s %-8s %s" % ("paso", "avion_x", "px/seg"))
    mandos.abajo(lado)
    prev_x = None
    for paso in range(14):
        v, pl = velocidad(seg)
        x = pl[0] if pl else None
        print("%-8d %-8s %.2f" % (paso, x, v))
        if x == prev_x and paso > 2:
            break
        prev_x = x
        if juego.causa():
            print("  (murio, reinicio)")
            juego.arrancar()
            mandos.abajo(lado)
    mandos.arriba(lado)


if __name__ == "__main__":
    main()
