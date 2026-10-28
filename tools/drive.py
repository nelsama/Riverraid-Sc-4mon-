#!/usr/bin/env python3
"""drive.py - Pilota el avion por el UART y guarda capturas en secuencia.

Se mueve por columnas a lo ancho del rio y dispara de vez en cuando, para pasar
por islas y orillas. Cada captura se guarda y se anota la fila de cinta del
avion y las islas que hay en el tilemap, para detectar islas "fantasma" (filas
aisladas o que no son pares contiguos).

  Uso: python tools/drive.py [segundos] [prefijo]
"""
import http.client
import io
import json
import os
import socket
import sys
import time

from PIL import Image

HOST = "127.0.0.1"
PORT = 4000
DBG = 8080

secs = float(sys.argv[1]) if len(sys.argv) > 1 else 40.0
prefix = sys.argv[2] if len(sys.argv) > 2 else "drv"
os.makedirs("docs/img/seq", exist_ok=True)


def uart(cmd):
    try:
        s = socket.create_connection((HOST, PORT), timeout=5)
        s.sendall((cmd + "\r").encode())
        time.sleep(0.05)
        s.close()
    except OSError:
        pass


conn = http.client.HTTPConnection("127.0.0.1", DBG, timeout=15)

t0 = time.time()
i = 0
while time.time() - t0 < secs:
    # movimiento: barrido izquierda-derecha
    t = time.time() - t0
    if int(t) % 4 < 1:
        uart("a")
    elif int(t) % 4 < 2:
        uart("d")
    else:
        uart("d")
    if int(t) % 3 == 0:
        uart(" ")          # dispara

    conn.request("GET", "/screen.png")
    png = conn.getresponse().read()
    conn.request("GET", "/vram.json")
    vram = json.loads(conn.getresponse().read())

    im = Image.open(io.BytesIO(png)).convert("RGB")
    name = "docs/img/seq/%s%02d.png" % (prefix, i)
    im.save(name)

    tm = vram["tilemap"]
    filas = [r for r in range(32) if any(v in (4, 5, 6, 7) for v in tm[r])]
    # Una isla son dos filas de cinta CONTIGUAS. La mitad que sale de la cinta
    # deja su fila a 0 y la otra queda sola al principio/final del recorrido,
    # asi que solo cuentan como "suelta" las filas aisladas que NO estan en el
    # borde de la ventana (0/1 o 30/31), que son el caso normal de media isla
    # entrando o saliendo.
    borde = {0, 1, 30, 31}
    sueltas = [r for r in filas
               if r not in borde and (r - 1 not in filas and r + 1 not in filas)]
    sprites = sum(1 for s in vram["sprites"] if s["y"] < 248)
    print("%2d t=%4.1f islas=%s sueltas=%s sprites=%d" %
          (i, t, filas, sueltas, sprites), flush=True)
    i += 1
    time.sleep(0.5)
