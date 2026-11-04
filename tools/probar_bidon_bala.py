#!/usr/bin/env python3
"""probar_bidon_bala.py - Comprueba que una bala destruye el bidon.

En el River Raid de verdad el deposito de combustible SE REVIENTA de un
disparo (no se recoge). Este robot alinea el avion con el bidon, dispara y
mira si el bidon desaparece.

Notas que costaron tiempo:
  - Por UART SOLO el espacio dispara; read_input() no mapea 'f'.
  - Hay que alinear los CENTROS: la bala sale en plane_x+3 y se dibuja 3 px
    mas a la derecha (BULLET_OFF_X), o sea el centro cae en plane_x+7; el
    bidon es 8x8, asi que su centro es fuel_x+4.
  - Si el juego esta en pausa (esperando FIRE) el scroll no avanza y no hay
    bidones: hay que mandar espacio hasta que arranque.

Uso: python tools/probar_bidon_bala.py   (emulador en :8080)
"""
import json
import socket
import time
import urllib.request

import mandos

BASE = "http://127.0.0.1:8080"
SPR_PLANE, SPR_FUEL, SPR_BOOM = 11, 16, 12


def leer():
    return {s["n"]: s for s in
            json.load(urllib.request.urlopen(BASE + "/vram.json"))["sprites"]}


def estado():
    t = urllib.request.urlopen(BASE + "/state.txt", timeout=5).read().decode()
    for ln in t.splitlines():
        if "scroll:" in ln:
            return int(ln.split("y=")[1].split()[0])
    return -1


def visible(sp):
    return bool(sp and sp["y"] < 240)


def reanudar(s):
    """Si el rio no avanza, el juego espera FIRE: manda espacio."""
    y0 = estado()
    time.sleep(0.25)
    if estado() != y0:
        return True
    for _ in range(4):
        s.sendall(b" ")
        time.sleep(0.35)
        y1 = estado()
        time.sleep(0.2)
        if estado() != y1:
            return True
    mandos.pulsa("space", 300)
    time.sleep(0.6)
    return True


def main():
    s = socket.create_connection(("127.0.0.1", 4000), timeout=5)
    s.settimeout(0.05)

    def drain():
        try:
            while s.recv(1024):
                pass
        except Exception:
            pass

    reanudar(s)

    intentos = exitos = 0
    t0 = time.time()
    while intentos < 6 and time.time() - t0 < 240:
        sp = leer()
        av, fu = sp.get(SPR_PLANE), sp.get(SPR_FUEL)

        if not (visible(av) and visible(fu)):
            if not visible(fu):
                reanudar(s)
            drain()
            time.sleep(0.03)
            continue

        # El bidon tiene que estar DELANTE y lejos del borde superior (la bala
        # sube 4 px/frame: si esta muy arriba no hay tiempo de alcanzarlo).
        if not (55 < fu["y"] < av["y"] - 30):
            drain()
            time.sleep(0.03)
            continue

        dx = (fu["x"] + 4) - (av["x"] + 7)
        if abs(dx) > 1:
            s.sendall(b"d" if dx > 0 else b"a")
            drain()
            time.sleep(0.03)
            continue

        y_bidon = fu["y"]
        s.sendall(b" ")          # dispara
        drain()

        hit = boom = False
        for _ in range(25):
            time.sleep(0.02)
            sp2 = leer()
            if not visible(sp2.get(SPR_FUEL)):
                hit = True
                boom = visible(sp2.get(SPR_BOOM))
                break

        intentos += 1
        if hit:
            exitos += 1
        print(f"  disparo {intentos}: bidon en y={y_bidon} -> "
              f"destruido={hit} explosion={boom}")
        time.sleep(0.25)

    s.close()
    print(f"RESULTADO: {exitos}/{intentos} disparos destruyeron el bidon")
    return 0 if intentos and exitos == intentos else 1


if __name__ == "__main__":
    raise SystemExit(main())
