#!/usr/bin/env python3
"""autoplay.py - Piloto automatico para probar el juego en el emulador.

Persigue el deposito de combustible (sprite 4) moviendo el avion con teclas
por el UART. Sirve para validar recogida, colisiones y supervivencia.

Uso: python tools/autoplay.py [segundos]
"""
import json
import socket
import sys
import time
import urllib.request

BASE = "http://127.0.0.1:8080"
DUR = float(sys.argv[1]) if len(sys.argv) > 1 else 15.0

SPR_PLANE = 0
SPR_FUEL = 4

s = socket.create_connection(("127.0.0.1", 4000), timeout=10)
s.settimeout(0.05)


def drain():
    try:
        while True:
            if not s.recv(1024):
                break
    except socket.timeout:
        pass


def send(k):
    s.sendall(k.encode())


def state():
    d = json.load(urllib.request.urlopen(BASE + "/vram.json"))
    sp = {x["n"]: x for x in d["sprites"]}
    return sp.get(SPR_PLANE), sp.get(SPR_FUEL)


drain()
t0 = time.time()
while time.time() - t0 < DUR:
    p, f = state()
    if not p:
        break
    px, py = p["x"], p["y"]
    if f:
        fx, fy = f["x"], f["y"]
        # Acercarse al deposito: primero horizontal, luego vertical.
        if px + 4 < fx:
            send("d")
        elif px + 4 > fx + 8:
            send("a")
        if py > fy + 2:
            send("w")
        elif py < fy - 2:
            send("s")
    drain()
    time.sleep(0.03)
s.close()
print("autoplay terminado")
