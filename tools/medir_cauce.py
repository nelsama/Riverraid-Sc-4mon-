#!/usr/bin/env python3
"""medir_cauce.py - Mide el perfil del cauce en una secuencia de capturas.

  Uso: python tools/medir_cauce.py <dir> [paso]

Para cada PNG saca, fila de celdas (8 px) y columna de celdas, el color de un
pixel central y lo resume en una cadena: `~` cesped, `A` orilla (arena),
`W` agua, `.` otro (sombra del cesped, sprite...). De cada fila imprime el
ANCHO de agua; comparando capturas consecutivas se ve si cada tramo de ancho
BAJA entero (mismo ancho, desplazado) o se acorta por abajo.
"""
import glob
import os
import sys

from PIL import Image

AGUA = (34, 102, 255)
CESPED = (0, 170, 0)
ORILLA = (221, 136, 51)


def celda(im, r, c):
    return im.getpixel((c * 8 + 4, r * 8 + 4))


def perfil(im, r, cols):
    """(izquierda, derecha) del agua en la fila r, o None."""
    w = [c for c in cols if celda(im, r, c) == AGUA]
    if not w:
        return None
    return w[0], w[-1]


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else "cap_fix"
    paso = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    pngs = sorted(glob.glob(os.path.join(d, "c*.png")))[::paso]
    print("captura        ancho de agua por fila (de arriba abajo)")
    for p in pngs:
        im = Image.open(p).convert("RGB")
        w, h = im.size
        anchos = []
        for r in range(h // 8):
            pf = perfil(im, r, range(w // 8))
            anchos.append("." if pf is None else str(pf[1] - pf[0] + 1))
        print("%-13s %s" % (os.path.basename(p), " ".join(anchos)))


if __name__ == "__main__":
    main()
