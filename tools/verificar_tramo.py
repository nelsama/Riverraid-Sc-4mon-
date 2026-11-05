"""Comprueba que el tramo entre puentes es de 192 filas (~25,6 s).

Con GEN_SECTOR = 12 y GEN_BLOCK = 16, entre dos vigas tiene que haber
12 * 16 = 192 filas de mundo. Se mide contando filas generadas entre la
aparicion de una viga y la siguiente.

Como medir 192 filas en vivo tarda ~25 s, se hace con el scroll a tope
(ARRIBA) para ir mas rapido, y se informa del tiempo real.

Uso:
    python tools/verificar_tramo.py [puentes]
"""
import json
import socket
import sys
import time
import urllib.request

BASE = "http://127.0.0.1:8080"
VIGA = 29
GEN_BLOCK, GEN_SECTOR = 16, 12
ESPERADO = GEN_BLOCK * GEN_SECTOR


def get(p):
    with urllib.request.urlopen(BASE + p, timeout=5) as r:
        return r.read()


def post(p, data):
    req = urllib.request.Request(BASE + p, data=data, method="POST")
    with urllib.request.urlopen(req, timeout=5) as r:
        return r.read()


def viga():
    tm = json.loads(get("/vram.json"))["tilemap"]
    return [r for r in range(32) if VIGA in tm[r]]


def main():
    cuantos = int(sys.argv[1]) if len(sys.argv) > 1 else 2
    with open("output/game.bin", "rb") as f:
        post("/run?name=GAME.BIN&addr=0800", f.read())
    time.sleep(2.5)

    u = socket.create_connection(("127.0.0.1", 4000), timeout=5)
    u.sendall(b" ")                       # arrancar
    time.sleep(0.5)
    u.sendall(b"w" * 20)                  # scroll a tope: acelera la medicion

    vistos = 0
    previo = set()
    t0 = time.time()
    marcas = []
    while vistos < cuantos and time.time() - t0 < 200:
        f = set(viga())
        if f and not previo:
            vistos += 1
            marcas.append(time.time() - t0)
            print("puente %d a %.1f s" % (vistos, marcas[-1]))
        previo = f
        time.sleep(0.1)
    u.close()

    if len(marcas) >= 2:
        dt = marcas[-1] - marcas[-2]
        print("\nseparacion medida: %.1f s" % dt)
        print("esperado con 192 filas a scroll normal: ~25.6 s")
        print("(aqui va mas rapido porque se acelera con ARRIBA)")
    print("\nfilas esperadas entre puentes: %d" % ESPERADO)


if __name__ == "__main__":
    main()
