#!/usr/bin/env python3
"""trazar_choque.py - Captura el frame exacto en que muere el avion.

  Uso: python tools/trazar_choque.py [segundos]

Pulsa la flecha derecha y vigila el scroll. En cuanto se detiene (muerte),
guarda el volcado de VRAM del momento y lo compara con lo que se ve en
pantalla, fila a fila, en las columnas del avion.

Es la herramienta para "choques invisibles": muestra si el perfil que usa la
colision (VRAM) coincide con lo pintado, y que celda concreta la dispara.
"""
import io
import json
import re
import sys
import time
import urllib.request

from PIL import Image

DBG = "http://127.0.0.1:8080"
AGUA = {(34, 102, 255), (17, 68, 238)}
CIAN = (0, 255, 255)
TILE = {0x10: 'g', 0x11: '~', 0x12: '~', 0x17: 'a', 0x19: 'a', 0x1D: 'B'}


def estado():
    t = urllib.request.urlopen(DBG + "/state.txt", timeout=5).read().decode()
    s = int(re.search(r"scroll: x=0 y=(\d+)", t).group(1))
    pc = re.search(r"PC=([0-9A-F]+)", t).group(1)
    return s, pc


def pantalla():
    return Image.open(io.BytesIO(urllib.request.urlopen(DBG + "/screen.png?t=%f" % time.time(),
                                                         timeout=5).read())).convert("RGB")


def vram():
    return json.load(urllib.request.urlopen(DBG + "/vram.json", timeout=5))["tilemap"]


def main():
    seg = float(sys.argv[1]) if len(sys.argv) > 1 else 8.0
    import ctypes
    u = ctypes.windll.user32
    h = u.FindWindowW(None, "Emulador FPGA 6502 - Tang Nano 9K")
    u.SetForegroundWindow(h)
    time.sleep(0.3)

    VK, SC = 0x27, 0x4D          # flecha derecha
    lp = 0x00010001 | (SC << 16)
    u.PostMessageW(h, 0x0100, VK, lp)

    prev, prev_v, prev_tm = None, None, None
    try:
        fin = time.time() + seg
        while time.time() < fin:
            s, pc = estado()
            im = pantalla()
            tm = vram()

            if prev is not None and s == prev:
                # MUERTE: guardamos el estado del frame ANTERIOR (previo al paro)
                if prev_v is not None:
                    analiza(prev_v, prev_tm, prev_pc)
                return

            prev, prev_v, prev_tm, prev_pc = s, im, tm, pc
            time.sleep(0.1)
        print("No hubo muerte en %g s" % seg)
    finally:
        u.PostMessageW(h, 0x0101, VK, 0xC0010001 | (SC << 16))


def analiza(im, tm, pc):
    pts = [(x, y) for y in range(160, 220) for x in range(320) if im.getpixel((x, y)) == CIAN]
    if not pts:
        print("murio sin avion visible (parpadeo)")
        return
    x0, x1 = min(p[0] for p in pts), max(p[0] for p in pts)
    y0 = min(p[1] for p in pts)
    print("PC=%s  avion x=%d..%d y=%d  celdas %d..%d" % (pc, x0, x1, y0, x0 // 8, x1 // 8))
    print("columnas %d..%d, pixel a pixel:" % (x0 // 8, x1 // 8))
    for dy in range(0, 16, 2):
        y = y0 + dy
        fila = "".join("~" if im.getpixel((x, y)) in AGUA else "." for x in range(max(0, x0 - 16), x1 + 16))
        print("  y=%3d %s" % (y, fila))
    # que dice la VRAM en la fila del centro del avion
    print("\nVRAM (cinta, columnas 8..32) de la fila central del sprite:")
    yc = y0 + 8
    # no conocemos scroll_total exacto aqui; se prueban las 32 cintas
    for r in range(32):
        print("  cinta %2d %s" % (r, "".join(TILE.get(tm[r][c], '?') for c in range(8, 33))))


if __name__ == "__main__":
    main()
