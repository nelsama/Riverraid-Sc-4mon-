#!/usr/bin/env python3
"""probar_bloqueo_fire.py - Comprueba que FIRE se ignora mientras arde el avion.

1. Provoca un choque.
2. En cuanto aparece la explosion, martillea FIRE durante toda la animacion.
3. Comprueba que el avion NO reaparece hasta que la animacion termina.
4. Despues, comprueba que FIRE si funciona (el juego arranca).

  Uso: python tools/probar_bloqueo_fire.py
"""
import re
import time
import urllib.request

import juego
import uartsend

DBG = "http://127.0.0.1:8080"


def oam(n):
    t = urllib.request.urlopen(DBG + "/vram.txt", timeout=5).read().decode()
    m = re.search(r"sprite\s+%d: X=\s*(\d+) Y=\s*(\d+) tile=\s*(\d+)" % n, t)
    return m.groups() if m else None


def scroll():
    t = urllib.request.urlopen(DBG + "/state.txt", timeout=5).read().decode()
    return int(re.search(r"scroll: x=0 y=(\d+)", t).group(1))


def main():
    for _ in range(15):
        if oam(0):
            break
        uartsend.enviar(" ")
        time.sleep(0.5)
    print("avion visible:", oam(0) is not None)

    # chocar: ir hacia la orilla hasta que aparezca la explosion
    boom = None
    fin = time.time() + 25
    while time.time() < fin and not boom:
        uartsend.enviar("dddddd", 0.02)
        for _ in range(20):
            boom = oam(5)
            if boom:
                break
            time.sleep(0.01)
    if not boom:
        print("no se vio la explosion")
        return
    print("explosion en", boom[:2], "tile", boom[2])

    # martillear FIRE mientras dura la animacion
    t0 = time.time()
    reaparecio_durante = False
    while oam(5) and time.time() - t0 < 3.0:
        uartsend.enviar(" ", 0.005)
        if oam(0):
            reaparecio_durante = True
            break
    dur = time.time() - t0
    print("duracion de la animacion: %.2f s" % dur)
    print("reaparecio DURANTE la animacion:", reaparecio_durante)

    time.sleep(0.2)
    print("avion tras la animacion:", oam(0) is not None)

    # ahora FIRE debe funcionar
    s0 = scroll()
    uartsend.enviar(" ", 0.05)
    time.sleep(0.5)
    s1 = scroll()
    print("FIRE tras la animacion arranca el juego:", s0 != s1)


if __name__ == "__main__":
    main()
