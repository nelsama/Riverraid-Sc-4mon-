#!/usr/bin/env python3
"""captura30.py - Secuencia de capturas a ritmo fijo con una sola sesion UART.

  Uso: python tools/captura30.py <dir_salida> <n> [intervalo_s] [fire]

Arranca la partida con FIRE, mantiene la sesion UART viva (no reconectar por
comando) y guarda numeracion con 4 digitos: c0001.png, c0002.png...
Apunta en <dir>/log.txt el scroll_y y una huella del PNG de cada captura.
"""
import os
import re
import sys
import time
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sesion import Emu, state  # noqa: E402

DBG = "http://127.0.0.1:8080"


def screen(path):
    urllib.request.urlretrieve(DBG + "/screen.png?scale=1&t=%f" % time.time(), path)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "capturas"
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 30
    dt = float(sys.argv[3]) if len(sys.argv) > 3 else 0.25
    do_fire = (len(sys.argv) <= 4) or sys.argv[4] != "0"
    os.makedirs(out, exist_ok=True)
    log = open(os.path.join(out, "log.txt"), "w")

    e = Emu()
    e.drain()
    if do_fire:
        e.key("f")
        time.sleep(0.2)

    for i in range(1, n + 1):
        t0 = time.time()
        p = os.path.join(out, "c%04d.png" % i)
        screen(p)
        st = state()
        m = re.search(r"scroll: x=(\d+) y=(\d+)", st)
        sc = m.group(0) if m else "?"
        line = "%s %s\n" % (p, sc)
        log.write(line)
        print(line.strip(), flush=True)
        rest = dt - (time.time() - t0)
        if rest > 0:
            time.sleep(rest)
    log.close()
    e.close()


if __name__ == "__main__":
    main()
