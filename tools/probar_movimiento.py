#!/usr/bin/env python3
"""probar_movimiento.py - Mueve el avion por barrido y registra por que muere.

  Uso: python tools/probar_movimiento.py [segundos]

Mantiene una direccion pulsada y, cada ~120 ms, guarda:
  - la columna del avion (por su color cian)
  - si el scroll se ha detenido (senal de muerte)
  - el perfil de agua de la fila del avion, pixel a pixel, en las columnas
    que ocupa el sprite

Sirve para detectar "choques invisibles": muertes en las que el avion no
tocaba nada visible.
"""
import io
import ctypes
import re
import sys
import time
import urllib.request

from PIL import Image

TITULO = "Emulador FPGA 6502 - Tang Nano 9K"
AGUA = {(34, 102, 255), (17, 68, 238)}
CIAN = (0, 255, 255)

u = ctypes.windll.user32


def ventana():
    h = u.FindWindowW(None, TITULO)
    if not h:
        print("No se encontro la ventana del emulador")
        sys.exit(1)
    return h


def scroll():
    t = urllib.request.urlopen("http://127.0.0.1:8080/state.txt", timeout=5).read().decode()
    m = re.search(r"scroll: x=0 y=(\d+)", t)
    return int(m.group(1)) if m else -1


def pantalla():
    png = urllib.request.urlopen("http://127.0.0.1:8080/screen.png?t=%f" % time.time(),
                                 timeout=5).read()
    return Image.open(io.BytesIO(png)).convert("RGB")


def avion(im):
    pts = [(x, y) for y in range(160, 220) for x in range(320)
           if im.getpixel((x, y)) == CIAN]
    if not pts:
        return None
    return min(p[0] for p in pts), max(p[0] for p in pts), min(p[1] for p in pts)


def perfil(im, x0, x1, y):
    """Devuelve '~' agua / '.' no agua para cada pixel de x0..x1."""
    return "".join("~" if im.getpixel((x, y)) in AGUA else "." for x in range(x0, x1 + 1))


def mantener(h, vk, sc, seg):
    lp = 0x00010001 | (sc << 16)
    u.PostMessageW(h, 0x0100, vk, lp)
    try:
        fin = time.time() + seg
        while time.time() < fin:
            yield
            time.sleep(1 / 60)
    finally:
        u.PostMessageW(h, 0x0101, vk, 0xC0010001 | (sc << 16))


def main():
    seg = float(sys.argv[1]) if len(sys.argv) > 1 else 6.0
    h = ventana()
    u.SetForegroundWindow(h)
    time.sleep(0.3)

    teclas = [("izquierda", 0x25, 0x4B), ("derecha", 0x27, 0x4D)]
    for nombre, vk, sc in teclas:
        print("=== %s %g s ===" % (nombre, seg))
        prev = None
        muertes = 0
        for _ in mantener(h, vk, sc, seg):
            a = avion(pantalla())
            s = scroll()
            if a is None:
                continue
            if prev is not None and s == prev:
                muertes += 1
                im = pantalla()
                x0, x1, y = a
                py = y + 8
                print("  MUERTE  avion x=%d..%d  y=%d" % (x0, x1, y))
                print("    fila y=%d : %s" % (py, perfil(im, max(0, x0 - 8), x1 + 8, py)))
            prev = s
        print("  muertes detectadas: %d" % muertes)
        time.sleep(0.3)


if __name__ == "__main__":
    main()
