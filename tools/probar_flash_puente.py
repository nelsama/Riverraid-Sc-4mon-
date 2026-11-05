"""Prueba: al reventar un PUENTE de verdad SI debe destellar el agua.

Complementa a probar_flash_orilla.py: aquel comprueba que morir contra la
orilla NO destella, este que volar el puente SI lo hace (si no, se habria
arreglado el falso positivo rompiendo la funcion).

Uso:
    python tools/probar_flash_puente.py
"""
import json
import socket
import sys
import time
import urllib.request

sys.path.insert(0, "tools")
from probar_flash_orilla import color_agua, get, post  # noqa: E402

TILE_BRIDGE = 29


def vigas():
    tm = json.loads(get("/vram.json"))["tilemap"]
    return [r for r in range(32) if TILE_BRIDGE in tm[r]]


def main():
    with open("output/game.bin", "rb") as f:
        post("/run?name=GAME.BIN&addr=0800", f.read())
    time.sleep(1.2)

    u = socket.create_connection(("127.0.0.1", 4000), timeout=5)
    u.sendall(b" ")
    time.sleep(0.6)

    base = color_agua()
    print("agua normal: RGB%s" % (base,))

    t0 = time.time()
    while time.time() - t0 < 8 and not vigas():
        time.sleep(0.05)
    if not vigas():
        print("FALLO: no hay puente en pantalla")
        u.close()
        return 1
    print("puente en cinta %s" % vigas())

    u.sendall(b" ")          # un tiro
    destellos = 0
    t0 = time.time()
    while time.time() - t0 < 1.5:
        c = color_agua()
        if c is not None and base is not None and c != base:
            destellos += 1
        time.sleep(0.02)
    u.close()

    print("muestras con agua aclarada: %d" % destellos)
    if destellos > 0:
        print("OK: el puente destruido SI destella el agua")
        return 0
    print("FALLO: el puente se destruyo pero el agua no destello")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
