#!/usr/bin/env python3
"""ciclo_vida.py - Prueba el ciclo de una vida: choque -> explosion -> avion.

Hace TODO en una sola pasada y con el estado limpio:

  1. arranca la partida con FIRE,
  2. choca contra la orilla derecha,
  3. NO pulsa nada durante la animacion,
  4. comprueba que el avion reaparece solo y el rio sigue andando,
  5. comprueba que FIRE ahora dispara.

  Uso: python ciclo_vida.py
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


def andando(pausa=0.4):
    s0 = scroll()
    time.sleep(pausa)
    return scroll() != s0


def main():
    # Reiniciar desde el menu de GAME OVER y arrancar la partida. Hacen falta DOS
    # pulsaciones si estabamos en GAME OVER: la primera vuelve al arranque
    # (esperando_fire) y la segunda arranca el rio.
    for _ in range(20):
        if andando(0.35):
            break
        uartsend.enviar(" ")
        time.sleep(0.35)
    print("1) partida en marcha, rio andando")

    boom = None
    for i in range(200):
        uartsend.enviar("dddd", 0.012)
        b = oam(5)
        if b:
            boom = b
            break
    if not boom:
        print("   NO CHOCO (avion=%s)" % (oam(0),))
        return
    print("2) explosion, tile %s, en x=%s" % (boom[2], boom[0]))

    # no pulsar nada
    t0 = time.time()
    while oam(5) and time.time() - t0 < 4:
        time.sleep(0.02)
    print("3) animacion terminada en %.2f s" % (time.time() - t0))

    time.sleep(0.5)          # margen: el frame que pinta el avion va despues
    av = oam(0)
    print("4) avion reaparecio SOLO:", av is not None, av)
    s0 = scroll()
    time.sleep(0.7)
    s1 = scroll()
    print("5) rio PARADO (esperando FIRE):", s0 == s1, "(%d -> %d)" % (s0, s1))
    time.sleep(0.6)
    print("   el avion sigue quieto:", oam(0) == av)

    uartsend.enviar(" ", 0.05)
    time.sleep(0.5)
    s2 = scroll()
    time.sleep(0.6)
    s3 = scroll()
    print("6) FIRE arranca el rio:", s2 != s3, "(%d -> %d)" % (s2, s3))
    print("   avion sigue en pantalla:", oam(0) is not None)


if __name__ == "__main__":
    main()
