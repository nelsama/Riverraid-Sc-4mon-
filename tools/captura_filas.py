#!/usr/bin/env python3
"""captura_filas.py - Capturas EN EL FRAME de entrada de cada fila nueva.

  Uso: python tools/captura_filas.py <dir> <n> [filas_csv]

El punto delicado del generador es el frame en que entra una fila nueva (cuando
el scroll cruza un multiplo de 8 px): si alla se reescribe una celda que aun
esta en pantalla, se ve un "mordisco". Este script escucha el UART del juego (que
emite UNA letra por frame con la fila de scroll, 0..7) y dispara la captura justo
cuando la letra pasa a '0'/'7' (antes/despues del salto), guardando PNG + estado.

Requiere un build con traza por UART (ver tools/traza_frame.py).
"""
import os
import re
import socket
import sys
import time
import urllib.request

DBG = "http://127.0.0.1:8080"


def scroll_y():
    t = urllib.request.urlopen(DBG + "/state.txt?t=%f" % time.time(), timeout=2).read().decode()
    return int(re.search(r"scroll: x=\d+ y=(\d+)", t).group(1))


def png(path):
    urllib.request.urlretrieve(DBG + "/screen.png?t=%f" % time.time(), path)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "cap_filas"
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 8
    os.makedirs(out, exist_ok=True)
    log = open(os.path.join(out, "log.txt"), "w")

    s = socket.create_connection(("127.0.0.1", 4000), 3)
    s.settimeout(0.5)
    buf = b""
    cogidas = 0
    prev = None
    t0 = time.time()
    while cogidas < n and time.time() - t0 < 180:
        try:
            d = s.recv(4096)
        except socket.timeout:
            continue
        buf += d
        # la traza son digitos '0'..'7': cada uno es un frame
        while buf:
            ch = buf[:1]
            buf = buf[1:]
            if ch not in b"01234567":
                continue
            f = ch[0] - 48
            salto = prev is not None and (f - prev) % 8 not in (1, 0)
            if prev is not None and (f < prev) and f <= 1:
                p = os.path.join(out, "f%04d.png" % cogidas)
                y = scroll_y()
                png(p)
                log.write("%s fila=%d prev=%d scroll=%d\n" % (p, f, prev, y))
                print("%s fila=%d prev=%d scroll=%d" % (p, f, prev, y), flush=True)
                cogidas += 1
            prev = f
    log.close()
    s.close()


if __name__ == "__main__":
    main()
