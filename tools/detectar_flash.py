"""Detecta el DESTELLO del agua comparando el histograma de un frame.

Un pixel suelto no sirve: el cauce se mueve, hay cesped, sprites y BG_COLOR.
Lo robusto es contar cuantos pixeles tienen EXACTAMENTE el color del agua
normal (#2266FF). Si el destello esta activo, ese color desaparece del frame.

Uso:
    python tools/detectar_flash.py [segundos]
"""
import sys
import time

sys.path.insert(0, "tools")
from probar_flash_orilla import get, png_pixel  # noqa: E402

AGUA = (34, 102, 255)


def cuenta_agua(img):
    n = 0
    for y in range(60, 230, 6):        # por debajo del HUD, sin sprites
        for x in range(40, 280, 6):
            if png_pixel(img, x, y) == AGUA:
                n += 1
    return n


def main():
    segs = float(sys.argv[1]) if len(sys.argv) > 1 else 6.0
    t0 = time.time()
    vals = []
    while time.time() - t0 < segs:
        vals.append(cuenta_agua(get("/screen.png")))
        time.sleep(0.02)
    print("muestras=%d  min=%d max=%d" % (len(vals), min(vals), max(vals)))
    print("(un destello hace CAER el conteo de agua normal)")


if __name__ == "__main__":
    main()
