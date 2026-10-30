#!/usr/bin/env python3
"""ciclos_por_frame.py - Coste real de cada frame, quieto vs moviendo el avion.

El emulador corre el CPU a 3,375 MHz SIEMPRE, asi que si el juego baja de fps es
porque hace mas trabajo por frame. Aqui se miden ciclos por frame:

    (ciclos_t1 - ciclos_t0) / (frames_t1 - frames_t0)

Con 60 Hz el presupuesto son 56250 ciclos por frame. Si al mover el avion sube
por encima, el juego pierde vblanks y el mundo avanza menos px por segundo.

  Uso: python tools/ciclos_por_frame.py
"""
import re
import time
import urllib.request

import uartsend

DBG = "http://127.0.0.1:8080"
CPF = 3375000 // 60          # ciclos por frame a 60 Hz


def ram():
    return urllib.request.urlopen(DBG + "/ram.bin", timeout=5).read()


def ciclos():
    t = urllib.request.urlopen(DBG + "/state.txt", timeout=5).read().decode()
    return int(re.search(r"ciclos: (\d+)", t).group(1))


def vivo():
    t = urllib.request.urlopen(DBG + "/vram.txt", timeout=5).read().decode()
    return "sprite  0:" in t


def frames():
    r = ram()
    return r[0x3E8] | (r[0x3E9] << 8), r[0x3EA] | (r[0x3EB] << 8)


def medida(seg, mover=None):
    f0, _s0 = frames()
    c0 = ciclos()
    t0 = time.time()
    while time.time() - t0 < seg:
        if mover:
            uartsend.enviar(mover, 0.01)
        time.sleep(0.06)
    f1, _s1 = frames()
    c1 = ciclos()
    df = (f1 - f0) & 0xFFFF
    dc = c1 - c0
    if df == 0:
        return None
    return dc / df


def main():
    for _ in range(8):
        uartsend.enviar(" ")
        time.sleep(0.4)
        if vivo():
            break

    for etiqueta, mover in (("QUIETO", None), ("MOVIENDO", "d")):
        vals = []
        for _ in range(3):
            v = medida(0.8, mover)
            if v:
                vals.append(v)
            if not vivo():
                uartsend.enviar(" ")
                time.sleep(0.3)
        if vals:
            media = sum(vals) / len(vals)
            print("%-9s ciclos/frame = %7d  (%.1f%% del presupuesto de %d)"
                  % (etiqueta, media, 100.0 * media / CPF, CPF))
        else:
            print(etiqueta, "sin datos")


if __name__ == "__main__":
    main()
