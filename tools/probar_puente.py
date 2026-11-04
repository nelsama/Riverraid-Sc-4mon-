#!/usr/bin/env python3
"""probar_puente.py - El avion choca con el puente y AMBOS se destruyen.

En el River Raid el puente es macizo: no hay hueco que esquivar. El avion
vuela a la altura del puente, asi que si llega sin haberlo volado, choca.

Este script arranca la partida, no toca el mando (el avion queda centrado) y
espera al primer puente. Comprueba:
  1. que el avion pierde una vida,
  2. que el tile de la viga (29) deja de estar donde choco,
  3. que la animacion de explosion del avion se dispara.

Uso: python tools/probar_puente.py   (emulador en :8080)
"""
import json
import time
import urllib.request

import mandos

BASE = "http://127.0.0.1:8080"
SPR_PLANE, SPR_BOOM, SPR_LIVES_NUM = 11, 12, 10
PAT_DIGIT0 = 42
TILE_BRIDGE = 29


def vram():
    return json.load(urllib.request.urlopen(BASE + "/vram.json"))


def sprites():
    return {s["n"]: s for s in vram()["sprites"]}


def vidas():
    d = sprites().get(SPR_LIVES_NUM)
    return d["tile"] - PAT_DIGIT0 if d else None


def viga_arriba(tm, desde):
    """Primera fila de cinta (desde abajo) que tiene viga por encima del avion."""
    for f in range(29, desde, -1):
        if TILE_BRIDGE in tm[f]:
            return f
    return None


def main():
    mandos.pulsa("space", 400)
    time.sleep(1.6)
    v0 = vidas()
    print(f"vidas al empezar: {v0}")
    if v0 is None:
        print("no se pudo leer el HUD")
        return 1

    # Esperar a que haya un puente visible por delante del avion.
    t0 = time.time()
    vistos = explosiones = 0
    vidas_tras = v0
    while time.time() - t0 < 60:
        d = vram()
        tm = d["tilemap"]
        sp = {s["n"]: s for s in d["sprites"]}
        av = sp.get(SPR_PLANE)

        if av and av["y"] < 240:
            f = viga_arriba(tm, 29)
            if f is not None:
                vistos += 1
                # El avion va a 180 px; la fila de cinta que ve en esa y:
                # si la viga llega a su altura, choca.
                pass
        b = sp.get(SPR_BOOM)
        if b and b["y"] < 240:
            explosiones += 1

        v = vidas()
        if v is not None and v < v0:
            vidas_tras = v
            break
        time.sleep(0.05)

    if vidas_tras == v0:
        print("no se llego a chocar en 60 s")
        return 1

    print(f"choque: vidas {v0} -> {vidas_tras}")
    print(f"frames con sprite de explosion: {explosiones}")

    # Tras el choque, las filas de viga del tramo deben desaparecer.
    time.sleep(1.0)
    tm = vram()["tilemap"]
    quedan = sum(fila.count(TILE_BRIDGE) for fila in tm)
    print(f"tiles de viga que quedan en pantalla: {quedan}")

    ok = (v0 - vidas_tras == 1)
    print("OK: el puente destruyo al avion (1 vida)" if ok else "FALLO")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
