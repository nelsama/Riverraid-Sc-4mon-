"""Prueba: UN SOLO disparo destruye el puente ENTERO (todas las filas de viga).

Antes del cambio cada tiro se llevaba solo la fila que tocaba, asi que hacian
falta ~3 tiros. Ahora el barrido recorre las filas de viga alrededor de la
tocada y se las lleva todas.

Los mandos van por el UART (puerto 4000), que es la via fiable: `mandos.py`
depende de que exista la ventana del emulador y tenga el foco.

Uso:
    python tools/probar_puente_bala.py
"""
import json
import re
import socket
import time
import urllib.request

BASE = "http://127.0.0.1:8080"
UART = ("127.0.0.1", 4000)
TILE_BRIDGE = 29


def get(p):
    with urllib.request.urlopen(BASE + p) as r:
        return r.read()


def post(p, data):
    req = urllib.request.Request(BASE + p, data=data, method="POST")
    with urllib.request.urlopen(req) as r:
        return r.read()


def vigas():
    tm = json.loads(get("/vram.json"))["tilemap"]
    return [r for r in range(32) if TILE_BRIDGE in tm[r]]


def vidas():
    v = json.loads(get("/vram.json"))
    for s in v["sprites"]:
        if s["n"] == 10:
            return s["tile"] - 42
    return None


def scroll():
    t = get("/state.txt").decode("utf-8", "replace")
    return int(re.search(r"scroll: x=0 y=(\d+)", t).group(1))


def main():
    with open("output/game.bin", "rb") as f:
        post("/run?name=GAME.BIN&addr=0800", f.read())
    time.sleep(1.2)

    u = socket.create_connection(UART, timeout=5)

    # Las vidas se leen ANTES de arrancar (rio parado, HUD estable).
    print("vidas: %s" % vidas())

    u.sendall(b" ")                      # FIRE: arrancar

    # Esperar a que el rio ANDE de verdad (el FIRE puede tardar un frame).
    t0 = time.time()
    s0 = scroll()
    while time.time() - t0 < 5:
        time.sleep(0.15)
        if scroll() != s0:
            break
    print("rio en marcha")

    # Y esperar a que haya viga en pantalla.
    t0 = time.time()
    while time.time() - t0 < 8 and not vigas():
        time.sleep(0.05)

    antes = vigas()
    print("filas con viga ANTES : %s" % antes)
    if not antes:
        print("FALLO: no hay puente en pantalla")
        u.close()
        return 1

    u.sendall(b" ")                      # UN SOLO disparo
    time.sleep(0.7)

    despues = vigas()
    print("filas con viga DESPUES: %s" % despues)
    u.close()

    if not despues:
        print("OK: un solo tiro se llevo el puente ENTERO (%d filas)" %
              len(antes))
        return 0
    print("FALLO: quedan %d filas de viga tras un tiro" % len(despues))
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
