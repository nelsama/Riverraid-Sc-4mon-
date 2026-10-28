#!/usr/bin/env python3
"""perfread.py - Lee por el UART las lineas de rendimiento del juego.

El juego (compilado con PERF=1) emite cada 60 frames:
  |max,avg,last|   total del frame en us
  |a,b,c|          medias de logica / sprites+HUD / scroll+regeneracion

El UART es el mismo que el del monitor, asi que hay que mantener la conexion
abierta y no enviar nada.

  Uso: python tools/perfread.py [segundos]
"""
import re
import socket
import sys
import time

HOST = "127.0.0.1"
PORT = 4000

VBLANK_US = 1430    # 45 de 525 lineas a 27 MHz (video_core.vhd: v_cnt >= 480)
FRAME_US = 16683


def main():
    secs = float(sys.argv[1]) if len(sys.argv) > 1 else 8.0
    s = socket.create_connection((HOST, PORT), timeout=10)
    s.settimeout(0.3)
    buf = b""
    t0 = time.time()
    while time.time() - t0 < secs:
        try:
            d = s.recv(4096)
            if not d:
                break
            buf += d
        except socket.timeout:
            pass
    s.close()
    txt = buf.decode("latin-1", "replace")
    tot = re.findall(r"\|(\d+),(\d+),(\d+)\|", txt)
    ph = re.findall(r"<(\d+),(\d+),(\d+)>", txt)
    print("VBLANK real = %d us; frame = %d us" % (VBLANK_US, FRAME_US))
    if not tot:
        print("sin datos. crudo:", repr(txt[-200:]))
        return
    for mx, avg, last in tot[-8:]:
        print("total  max=%6s  medio=%6s  ultimo=%6s us" % (mx, avg, last))
    for a, b, c in ph[-8:]:
        print("fases  logica=%5s  sprites=%5s  scroll=%5s us" % (a, b, c))


if __name__ == "__main__":
    main()
