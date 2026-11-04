#!/usr/bin/env python3
"""probar_doble_explosion.py - Verifica que un choque resta UNA vida.

El bug que vigila: al apagarse la animacion de explosion se ponia
plane_dead = 0 y esperando_fire = 1, pero la colision solo miraba
plane_dead, asi que el avion ya colocado en pantalla podia recibir un
segundo golpe y VOLVIA A EXPLOTAR sin que el jugador tocara nada.

COMO SE MIDE: el numero de vidas se ve en el HUD (sprite 10, un digito).
Se choca una vez, NO se pulsa FIRE y se comprueba que las vidas bajan
EXACTAMENTE uno. Si la animacion se encadena, bajan dos o mas.

Uso: python tools/probar_doble_explosion.py   (emulador en :8080)
"""
import json
import socket
import time
import urllib.request

import mandos

BASE = "http://127.0.0.1:8080"
SPR_PLANE, SPR_LIVES_NUM = 11, 10
PAT_DIGIT0 = 42


def sprites():
    return {s["n"]: s for s in
            json.load(urllib.request.urlopen(BASE + "/vram.json"))["sprites"]}


def vidas():
    sp = sprites()
    d = sp.get(SPR_LIVES_NUM)
    if not d:
        return None
    return d["tile"] - PAT_DIGIT0


def main():
    s = socket.create_connection(("127.0.0.1", 4000), timeout=5)
    s.settimeout(0.05)

    def drain():
        try:
            while s.recv(1024):
                pass
        except Exception:
            pass

    # Arrancar la partida y leer las vidas iniciales.
    mandos.pulsa("space", 400)
    time.sleep(1.6)
    mandos.pulsa("space", 400)
    time.sleep(1.0)

    v0 = vidas()
    print(f"vidas al empezar: {v0}")
    if v0 is None:
        print("no se pudo leer el HUD de vidas")
        s.close()
        return 1

    # Chocar contra la orilla derecha.
    t0 = time.time()
    while time.time() - t0 < 25:
        sp = sprites()
        av = sp.get(SPR_PLANE)
        if not av or av["y"] >= 240:
            break
        s.sendall(b"d")
        drain()
        time.sleep(0.02)
    print("choque detectado")

    # NO pulsar FIRE: esperar a que acabe la animacion y quedarse quieto.
    time.sleep(4.0)
    v1 = vidas()
    s.close()

    print(f"vidas tras el choque (sin pulsar FIRE): {v1}")
    perdidas = v0 - v1 if v1 is not None else 99
    if perdidas == 1:
        print("OK: exactamente una vida por un choque")
        return 0
    print(f"FALLO: se perdieron {perdidas} vidas en un solo choque")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
