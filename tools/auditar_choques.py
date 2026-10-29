#!/usr/bin/env python3
"""auditar_choques.py - Comprueba que cada choque contra la orilla era justo.

No usa diagnosticos del programa: cuando muere por ORILLA/ISLA lee la posicion
del avion y el tilemap, y calcula por si mismo si alguna celda del sprite
tocaba tierra. Si TODAS eran agua, el choque fue injusto (el bug buscado).

  Uso: python tools/auditar_choques.py [segundos_por_lado] [max_ciclos]
"""
import json
import sys
import urllib.request

import juego

DBG = "http://127.0.0.1:8080"
AGUA = {0x11, 0x12}


def main():
    seg = float(sys.argv[1]) if len(sys.argv) > 1 else 0.7
    ciclos = int(sys.argv[2]) if len(sys.argv) > 2 else 30

    justos = injustos = otras = 0
    for i in range(ciclos):
        if juego.causa():
            juego.arrancar()
        d = "right" if i % 2 == 0 else "left"
        c = juego.mover(d, seg)
        if not c:
            continue
        nombre = juego.CAUSA.get(c, str(c))
        if nombre != "ORILLA/ISLA":
            otras += 1
            juego.arrancar()
            continue

        tm = None
        sy, pl = juego.estado()
        r = juego.ram()
        fila, col, l, rr = r[0x3E1], r[0x3E2], r[0x3E3], r[0x3E4]
        # celda_agua() del juego: agua si l < c < r (los bordes son arena)
        if l < col < rr:
            injustos += 1
            print("!! INJUSTO: cinta %d celda %d  agua %d..%d" % (fila, col, l, rr))
        else:
            justos += 1
            print("   justo:    cinta %d celda %d  agua %d..%d" % (fila, col, l, rr))
        juego.arrancar()

    print("\njustos: %d   INJUSTOS: %d   otras causas: %d"
          % (justos, injustos, otras))


if __name__ == "__main__":
    main()
