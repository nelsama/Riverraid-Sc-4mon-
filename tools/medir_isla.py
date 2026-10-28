#!/usr/bin/env python3
"""medir_isla.py - Mide el ancho de la isla en la pantalla actual.

  Uso: python tools/medir_isla.py [png]

Cuenta, fila a fila visible, el cauce (agua) y la tierra central que lo parte.
Sirve para comprobar que la isla tiene el ancho pedido y que los dos brazos son
navegables, sin depender de capturas a ojo.
"""
import sys
import time
import urllib.request
from io import BytesIO

from PIL import Image

DBG = "http://127.0.0.1:8080"
AGUA = (34, 102, 255)
ROJO = (221, 17, 17)


def carga(arg):
    if arg:
        return Image.open(arg).convert("RGB")
    png = urllib.request.urlopen(DBG + "/screen.png?t=%f" % time.time(), timeout=5).read()
    return Image.open(BytesIO(png)).convert("RGB")


def main():
    im = carga(sys.argv[1] if len(sys.argv) > 1 else None)
    im = im.resize((320, 240), Image.NEAREST)
    print("fila  cauce     ancho  brazo_izq  tierra       brazo_der")
    for r in range(4, 30):
        vals = [im.getpixel((c * 8 + 4, r * 8 + 4)) for c in range(40)]
        w = [c for c, v in enumerate(vals) if v == AGUA]
        if not w:
            continue
        h = [c for c in range(w[0], w[-1] + 1) if vals[c] not in (AGUA, ROJO)]
        if not h:
            print("%4d  %2d..%-2d   %2d" % (r, w[0], w[-1], w[-1] - w[0] + 1))
            continue
        print("%4d  %2d..%-2d   %2d     %2d         %2d..%-2d (%2d) %2d+"
              % (r, w[0], w[-1], w[-1] - w[0] + 1, h[0] - w[0],
                 h[0], h[-1], len(h), w[-1] - h[-1]))


if __name__ == "__main__":
    main()
