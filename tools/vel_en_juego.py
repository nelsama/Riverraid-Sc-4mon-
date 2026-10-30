#!/usr/bin/env python3
"""vel_en_juego.py - Velocidad del scroll px/frame medida DENTRO del juego.

Lee dos contadores del propio 6502: DBG_FRAMES ($03E8, frames del bucle) y
scroll_total (via el scroll del core), y calcula px de scroll por frame. Como
se toma en una sola instantanea, no le afecta el jitter de muestrear por HTTP:
lo que se mide es lo que el juego avanzo entre dos lecturas.

Si el avion afecta al scroll, la cifra cambia al moverlo a los lados.

  Uso: python tools/vel_en_juego.py
"""
import re
import time
import urllib.request

import uartsend

DBG = "http://127.0.0.1:8080"


def snap():
    r = urllib.request.urlopen(DBG + "/ram.bin", timeout=5).read()
    frames = r[0x3E8] | (r[0x3E9] << 8)
    speed = r[0x3E7]
    t = urllib.request.urlopen(DBG + "/state.txt", timeout=5).read().decode()
    sy = int(re.search(r"scroll: x=0 y=(\d+)", t).group(1))
    v = urllib.request.urlopen(DBG + "/vram.txt", timeout=5).read().decode()
    m = re.search(r"sprite  0: X=\s*(\d+)", v)
    return frames, speed, sy, (int(m.group(1)) if m else None)


def medir(seg=1.2):
    f0, sp0, s0, x0 = snap()
    time.sleep(seg)
    f1, sp1, s1, x1 = snap()
    df = (f1 - f0) & 0xFFFF
    ds = (s0 - s1) & 0xFF
    if ds > 128:
        ds -= 256
    return df, ds, sp0, sp1, x0, x1


def main():
    uartsend.enviar(" ")
    time.sleep(0.5)
    for i in range(8):
        df, ds, sp0, sp1, x0, x1 = medir()
        print("x %-5s frames %3d  scroll %3d px  = %.3f px/frame   speed %d->%d"
              % (x0, df, ds, (ds / df) if df else 0.0, sp0, sp1))
        if ds == 0:
            break


if __name__ == "__main__":
    main()
