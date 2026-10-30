#!/usr/bin/env python3
"""probar_reaparicion.py - El avion debe volver solo al acabar la explosion.

Comprueba el ciclo completo SIN pulsar nada durante la animacion:

  1. arrancar la partida con FIRE,
  2. chocar contra la orilla,
  3. durante la explosion: no pulsar nada,
  4. al terminar: el avion debe estar dibujado y el rio andando,
  5. FIRE debe disparar (bala visible), no arrancar la partida.

  Uso: python tools/probar_reaparicion.py
"""
import re
import time
import urllib.request

import uartsend

DBG = "http://127.0.0.1:8080"


def oam(n):
    t = urllib.request.urlopen(DBG + "/vram.txt", timeout=5).read().decode()
    m = re.search(r"sprite\s+%d: X=\s*(\d+) Y=\s*(\d+) tile=\s*(\d+)" % n, t)
    return m.groups() if m else None


def scroll():
    t = urllib.request.urlopen(DBG + "/state.txt", timeout=5).read().decode()
    return int(re.search(r"scroll: x=0 y=(\d+)", t).group(1))


def andando():
    """El rio avanza? Es la forma fiable de saber que la partida esta en marcha
    (en `esperando_fire` el avion tambien se dibuja, asi que mirar el OAM no
    sirve)."""
    s0 = scroll()
    time.sleep(0.4)
    return scroll() != s0


def main():
    # arrancar: FIRE hasta que el rio ande
    for _ in range(15):
        if andando():
            break
        uartsend.enviar(" ")
        time.sleep(0.4)
    print("1) partida arrancada, avion visible:", oam(0) is not None)
    s0 = scroll()
    time.sleep(0.5)
    print("   el rio anda:", scroll() != s0)

    # chocar contra la orilla derecha
    boom = None
    fin = time.time() + 40
    while time.time() < fin and not boom:
        uartsend.enviar("dddddd", 0.02)
        for _ in range(20):
            boom = oam(5)
            if boom:
                break
            time.sleep(0.01)
        if oam(0) is None and not boom:
            break
    if not boom:
        print("   no se pudo chocar")
        return
    print("2) explosion en marcha (tile %s)" % boom[2])

    # NO pulsar nada: esperar a que acabe
    t0 = time.time()
    while oam(5) and time.time() - t0 < 4:
        time.sleep(0.02)
    print("3) animacion terminada en %.2f s" % (time.time() - t0))

    time.sleep(0.25)
    avion = oam(0)
    print("4) avion reaparecio SOLO:", avion is not None, avion)
    s0 = scroll()
    time.sleep(0.6)
    s1 = scroll()
    print("   el rio anda solo:", s0 != s1, "(%d -> %d)" % (s0, s1))

    # FIRE ahora debe disparar, no arrancar nada
    uartsend.enviar(" ", 0.05)
    time.sleep(0.12)
    bala = oam(30) or oam(31)
    print("5) FIRE dispara (bala visible):", bala is not None)


if __name__ == "__main__":
    main()
