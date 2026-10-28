#!/usr/bin/env python3
"""volcado.py - Vuelca la pantalla de celdas de un PNG o del emulador.

  Uso: python tools/volcado.py [png] [primera_col] [ultima_col]

Leyenda: W agua, A orilla (arena), ~ cesped, # sombra del cesped, . otro.
Sirve para ver de un vistazo la FORMA del cauce fila a fila.
"""
import sys
import time
import urllib.request
from io import BytesIO

from PIL import Image

DBG = "http://127.0.0.1:8080"
AGUA = (34, 102, 255)
ORILLA = (221, 136, 51)
CESPED = (0, 170, 0)
SOMBRA = (0, 102, 0)


def carga(arg):
    if arg and arg.endswith(".png"):
        return Image.open(arg).convert("RGB")
    png = urllib.request.urlopen(DBG + "/screen.png?t=%f" % time.time(), timeout=5).read()
    return Image.open(BytesIO(png)).convert("RGB")


def simbolo(p):
    if p == AGUA:
        return "W"
    if p == ORILLA:
        return "A"
    if p == CESPED:
        return "~"
    if p == SOMBRA:
        return "#"
    return "."


def main():
    arg = sys.argv[1] if len(sys.argv) > 1 else None
    c0 = int(sys.argv[2]) if len(sys.argv) > 2 else 6
    c1 = int(sys.argv[3]) if len(sys.argv) > 3 else 36
    im = carga(arg)
    if im.size != (320, 240):
        im = im.resize((320, 240), Image.NEAREST)
    for r in range(30):
        print("%2d " % r + "".join(simbolo(im.getpixel((c * 8 + 4, r * 8 + 4)))
                                   for c in range(c0, c1)))


if __name__ == "__main__":
    main()
