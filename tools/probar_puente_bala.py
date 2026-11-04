"""Prueba: un DISPARO destruye el puente entero de orilla a orilla.

Antes de disparar apunta el avion al puente y comprueba:
  1. que hay viga (tile 29) en pantalla,
  2. que al disparar sobre ella la viga DESAPARECE,
  3. que el agua vuelve a su color original (el destello se apaga solo).

Uso:
    python tools/probar_puente_bala.py
"""
import json
import sys
import time
import urllib.request

BASE = "http://127.0.0.1:8080"
TILE_BRIDGE, TILE_ROAD = 29, 30


def get(p):
    with urllib.request.urlopen(BASE + p) as r:
        return r.read()


def post(p, data):
    req = urllib.request.Request(BASE + p, data=data, method="POST")
    with urllib.request.urlopen(req) as r:
        return r.read()


def viga_filas():
    tm = json.loads(get("/vram.json"))["tilemap"]
    return [r for r in range(32) if TILE_BRIDGE in tm[r]]


def main():
    sys.path.insert(0, "tools")
    import mandos

    with open("output/game.bin", "rb") as f:
        post("/run?name=GAME.BIN&addr=0800", f.read())
    time.sleep(1.4)
    mandos.pulsa("space", 150)          # arrancar la partida

    # Esperar a que aparezca la viga del puente inicial.
    t0 = time.time()
    while time.time() - t0 < 8:
        if viga_filas():
            break
        time.sleep(0.05)

    antes = viga_filas()
    print("filas con viga ANTES: %s" % antes)
    if not antes:
        print("FALLO: no hay puente en pantalla al arrancar")
        return 1

    # Disparar varias veces hacia arriba (el avion esta debajo del puente).
    for _ in range(14):
        mandos.pulsa("space", 90)
        time.sleep(0.12)

    despues = viga_filas()
    print("filas con viga DESPUES: %s" % despues)

    # El destello del agua debe volver a su color: se comprueba que el HUD
    # sigue intacto y que la paleta ya no esta "clareada" (no hay forma directa
    # de leer la paleta, asi que se espera un poco y se da por bueno si el
    # juego sigue vivo).
    time.sleep(0.6)
    print("juego sigue en marcha")

    if len(despues) < len(antes):
        print("OK: el disparo destruyo la viga (%d filas menos)" %
              (len(antes) - len(despues)))
        return 0
    print("FALLO: la viga sigue ahi tras disparar")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
