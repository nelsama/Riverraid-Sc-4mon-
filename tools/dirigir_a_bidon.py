#!/usr/bin/env python3
"""dirigir_a_bidon.py - Mide la TASA DE ACIERTO disparando al bidon.

La bala sube BULLET_SPEED px por frame y el bidon mide 8 px de alto, asi que
el solape solo dura uno o dos frames y depende de la fase: por eso "algunos
bidones explotan y otros no". Este script dispara muchas veces con el avion
alineado y reporta cuantos explotan.

Uso: python tools/dirigir_a_bidon.py [disparos]
"""
import json
import socket
import sys
import time
import urllib.request

import mandos

BASE = "http://127.0.0.1:8080"
SPR_PLANE, SPR_FUEL, SPR_BOOM = 11, 16, 12
OBJETIVO = int(sys.argv[1]) if len(sys.argv) > 1 else 10
DESFASE = int(sys.argv[2]) if len(sys.argv) > 2 else 0


def leer():
    return {s["n"]: s for s in
            json.load(urllib.request.urlopen(BASE + "/vram.json"))["sprites"]}


def visible(sp):
    return bool(sp and sp["y"] < 240)


def main():
    s = socket.create_connection(("127.0.0.1", 4000), timeout=5)
    s.settimeout(0.05)

    def drain():
        try:
            while s.recv(1024):
                pass
        except Exception:
            pass

    mandos.pulsa("space", 400)
    time.sleep(1.5)

    intentos = exitos = 0
    t0 = time.time()
    while intentos < OBJETIVO and time.time() - t0 < 300:
        sp = leer()
        av, fu = sp.get(SPR_PLANE), sp.get(SPR_FUEL)

        if not (visible(av) and visible(fu)):
            drain()
            time.sleep(0.03)
            continue

        # Solo cuando esta delante, centrado y lejos del borde superior.
        if not (40 < fu["y"] < av["y"] - 30):
            drain()
            time.sleep(0.03)
            continue

        dx = (fu["x"] + 4) - (av["x"] + 7) - DESFASE
        if abs(dx) > 1:
            s.sendall(b"d" if dx > 0 else b"a")
            drain()
            time.sleep(0.03)
            continue

        # Antes de disparar se toma la referencia: si el bidon desaparece ES
        # por la bala, no porque el avion muriera y el juego se reiniciara.
        x_av, y_av = av["x"], av["y"]
        x_t, y_t = fu["x"], fu["y"]
        s.sendall(b" ")
        drain()

        hit = False
        for _ in range(30):
            time.sleep(0.02)
            sp2 = leer()
            av2, fu2 = sp2.get(SPR_PLANE), sp2.get(SPR_FUEL)
            if not visible(av2):
                # Murio el avion: el intento no cuenta.
                hit = None
                break
            if not visible(fu2):
                hit = True
                break

        if hit is None:
            print(f"  (descartado: murio el avion, bidon y={y_t})")
            time.sleep(0.3)
            continue
        intentos += 1
        exitos += hit
        print(f"  {intentos:2d}. bidon y={y_t:3d} -> "
              f"{'EXPLOTA' if hit else 'no explota'}")
        time.sleep(0.2)

    s.close()
    pct = (100.0 * exitos / intentos) if intentos else 0.0
    print(f"ACIERTOS: {exitos}/{intentos} ({pct:.0f}%)")
    return 0 if intentos and exitos == intentos else 1


if __name__ == "__main__":
    raise SystemExit(main())
