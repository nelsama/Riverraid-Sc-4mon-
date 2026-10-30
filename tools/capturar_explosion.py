#!/usr/bin/env python3
"""capturar_explosion.py - Captura los frames de la explosion del avion.

Arranca la partida, choca a proposito contra la orilla y guarda una tira de
capturas durante el medio segundo siguiente, recortando la zona donde esta el
avion en el momento del choque.

  Uso: python tools/capturar_explosion.py [dir]
"""
import io
import sys
import time
import urllib.request

from PIL import Image

import juego
import mandos

DBG = "http://127.0.0.1:8080"


def shot():
    return Image.open(io.BytesIO(urllib.request.urlopen(
        DBG + "/screen.png", timeout=5).read())).convert("RGB")


def main():
    dirn = sys.argv[1] if len(sys.argv) > 1 else "right"
    if not juego.arrancar(verbose=True):
        print("no arranco")
        return
    time.sleep(0.4)

    sy0, pl0 = juego.estado()
    if not pl0:
        print("no hay avion")
        return

    mandos.abajo(dirn)
    for _ in range(400):
        if juego.causa():
            break
        time.sleep(0.01)
    mandos.arriba(dirn)

    px, py = pl0
    x0 = max(0, px - 10)
    y0 = max(0, py - 10)
    box = (x0, y0, min(320, x0 + 40), min(240, y0 + 40))

    frames = []
    for i in range(16):
        frames.append(shot().crop(box))
        time.sleep(0.05)

    tira = Image.new("RGB", (box[2] - box[0], (box[3] - box[1]) * len(frames)))
    for i, f in enumerate(frames):
        tira.paste(f, (0, i * f.height))
    tira = tira.resize((tira.width * 3, tira.height * 3), Image.NEAREST)
    tira.save("../build/explosion.png")
    print("guardado build/explosion.png  box=%s  causa=%s"
          % (box, juego.CAUSA.get(juego.causa())))


if __name__ == "__main__":
    main()
