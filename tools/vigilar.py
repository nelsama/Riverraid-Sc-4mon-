#!/usr/bin/env python3
"""vigilar.py - Mide la forma del cauce en MUCHAS capturas y solo informa de lo
que se salga de lo esperado. Pensado para lanzarlo mientras se juega.

  Uso: python tools/vigilar.py [segundos] [capturas_por_segundo]

Para cada captura (PNG) y la VRAM del mismo instante comprueba:
  1. HUECO: cesped/orilla DENTRO del agua de una fila.
  2. BORDE: orilla perdida (agua pegada al cesped, o cesped pegado al agua).
  3. VRAM vs PANTALLA: si la celda de VRAM no coincide con lo que se ve.
  4. AVION: si el juego pierde vidas / se queda sin HUD (sprites activos).
Al final imprime un resumen con el numero de fallos de cada tipo y las
capturas donde aparecen. Asi se distingue un fallo REAL de una impresion.
"""
import json
import os
import sys
import time
import urllib.request
from io import BytesIO

from PIL import Image

DBG = "http://127.0.0.1:8080"
AGUA = (34, 102, 255)
ORILLA = (221, 136, 51)
CESPED = (0, 170, 0)
SOMBRA = (0, 102, 0)
BORDE = (0x17, 0x18, 0x19, 0x1A)


def get(path):
    return urllib.request.urlopen(DBG + path + "?t=%f" % time.time(), timeout=5).read()


def simbolo(px):
    if px == AGUA:
        return "W"
    if px == ORILLA:
        return "A"
    if px in (CESPED, SOMBRA):
        return "g"
    return "?"


def main():
    segs = float(sys.argv[1]) if len(sys.argv) > 1 else 30.0
    fps = float(sys.argv[2]) if len(sys.argv) > 2 else 4.0
    dt = 1.0 / fps
    out = sys.argv[3] if len(sys.argv) > 3 else "vigilar"
    os.makedirs(out, exist_ok=True)

    fallos = {"hueco": [], "borde": [], "vram": [], "avion": []}
    n = 0
    t0 = time.time()
    while time.time() - t0 < segs:
        t = time.time()
        try:
            d = json.loads(get("/vram.json"))
            png = get("/screen.png")
            st = get("/state.txt").decode("latin-1", "replace")
        except Exception as e:                       # noqa: BLE001
            print("captura fallida:", e)
            time.sleep(dt)
            continue
        im = Image.open(BytesIO(png)).convert("RGB")
        tmap, attr = d["tilemap"], d["atributos"]
        n += 1
        nombre = "%s/c%05d.png" % (out, n)

        # --- 1 y 2: forma del cauce en lo que se VE (filas 4..29) ---
        mal_h = mal_b = 0
        for r in range(4, 30):
            vals = [simbolo(im.getpixel((c * 8 + 4, r * 8 + 4))) for c in range(40)]
            w = [c for c, v in enumerate(vals) if v == "W"]
            if len(w) < 3:
                continue
            if any(vals[c] != "W" for c in range(w[0], w[-1] + 1)):
                mal_h += 1
            if vals[w[0] - 1] != "A" or vals[w[-1] + 1] != "A":
                mal_b += 1
        # --- 3: VRAM vs pantalla (solo celdas del terreno) ---
        mal_v = 0
        for r in range(4, 30):
            for c in range(40):
                tl = tmap[r][c]
                if tl in (0x11, 0x12):
                    esp = "W"
                elif tl in BORDE:
                    esp = "A"
                elif tl == 0x10:
                    esp = "g"
                else:
                    continue
                if simbolo(im.getpixel((c * 8 + 4, r * 8 + 4))) != esp:
                    mal_v += 1
        if mal_h:
            fallos["hueco"].append((n, mal_h))
            im.save(nombre)
        if mal_b:
            fallos["borde"].append((n, mal_b))
            im.save(nombre)
        if mal_v:
            fallos["vram"].append((n, mal_v))
            im.save(nombre)
        # --- 4: vidas / HUD ---
        if "sprites activos" in st:
            sp = int(st.split("sprites activos: ")[1].split()[0])
            if sp < 10:
                fallos["avion"].append((n, sp))
                im.save(nombre)

        print("%05d huecos=%d bordes=%d vram=%d sprites=%s"
              % (n, mal_h, mal_b, mal_v, sp), flush=True)
        resto = dt - (time.time() - t)
        if resto > 0:
            time.sleep(resto)

    print("\n=== RESUMEN (%d capturas) ===" % n)
    for k, v in fallos.items():
        print("%-6s %d capturas %s" % (k, len(v), v[:8]))


if __name__ == "__main__":
    main()
