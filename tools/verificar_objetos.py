#!/usr/bin/env python3
"""verificar_objetos.py - Comprueba que avion, enemigos y bidones estan en AGUA.

  Uso: python tools/verificar_objetos.py <dir_capturas>

Para cada PNG:
  1. Detecta los sprites (pixeles fuera de la paleta del fondo).
  2. Los agrupa en COMPONENTES CONEXAS (dos objetos cercanos son dos objetos).
  3. Comprueba que las celdas que ocupaba estan sobre agua.

Las celdas se leen por sus BORDES (x*8 y x*8+7), no por el centro: el centro
cae dentro del propio sprite y devuelve su color.

Salida: exit code 1 si hay algun fallo, para usarlo como prueba automatica.
"""
import glob
import os
import sys
from collections import deque

from PIL import Image

# Colores del fondo (paletas de tiles). Todo lo que no este aqui es un sprite.
FONDO = {
    (0, 170, 0),     # cesped
    (34, 102, 255),  # agua
    (0, 102, 0),     # cesped oscuro
    (221, 136, 51),  # arena
    (17, 68, 238),   # oleaje
}
AGUA = {(34, 102, 255), (17, 68, 238)}

# HUD: filas 1..2 de pantalla (Y=10..17). No es un objeto del juego.
HUD_Y1 = 20


def componentes(im, w, h):
    """Devuelve lista de (x0, x1, y0, y1) de cada sprite detectado."""
    mask = [[im.getpixel((x, y)) not in FONDO for x in range(w)] for y in range(h)]
    visto = [[False] * w for _ in range(h)]
    cajas = []
    for y in range(HUD_Y1, h):
        for x in range(w):
            if not mask[y][x] or visto[y][x]:
                continue
            # BFS
            q = deque([(x, y)])
            visto[y][x] = True
            x0 = x1 = x
            y0 = y1 = y
            n = 0
            while q:
                cx, cy = q.popleft()
                n += 1
                x0, x1 = min(x0, cx), max(x1, cx)
                y0, y1 = min(y0, cy), max(y1, cy)
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        nx, ny = cx + dx, cy + dy
                        if 0 <= nx < w and HUD_Y1 <= ny < h \
                                and not visto[ny][nx] and mask[ny][nx]:
                            visto[ny][nx] = True
                            q.append((nx, ny))
            if n >= 6:          # ignora motas sueltas
                cajas.append((x0, x1, y0, y1))
    return cajas


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else "capturas"
    fallos = 0
    total = 0
    for f in sorted(glob.glob(os.path.join(d, "c*.png"))):
        im = Image.open(f).convert("RGB")
        w, h = im.size
        cajas = componentes(im, w, h)
        if not cajas:
            continue
        total += 1
        for (x0, x1, y0, y1) in cajas:
            c0, c1 = x0 // 8, x1 // 8
            if not (0 <= c0 < 40 and 0 <= c1 < 40):
                continue

            # Un sprite mide 16 px. Si la caja es mas alta es que dos objetos se
            # solapan (avion + enemigo): se parte en trozos de 16 px.
            alto = y1 - y0 + 1
            trozos = []
            if alto <= 20:
                trozos = [(y0, y1)]
            else:
                y = y0
                while y <= y1:
                    trozos.append((y, min(y + 15, y1)))
                    y += 16

            for (ta, tb) in trozos:
                # Se comprueba el perfil en VARIAS filas del sprite y se exige
                # que este sobre agua en la fila donde el objeto tiene pixeles
                # (su fila central puede caer ya en otra celda del rio).
                filas = [(y, [x for x in range(x0, x1 + 1)
                              if im.getpixel((x, y)) not in FONDO])
                         for y in range(ta, tb + 1)]
                filas = [(y, xs) for y, xs in filas if xs]
                if not filas:
                    continue
                solapado = 0
                for (y, xs) in filas:
                    a, b = min(xs) // 8, max(xs) // 8
                    celdas = range(max(a, c0), min(b, c1) + 1)
                    for c in celdas:
                        if not (im.getpixel((c * 8, y)) in AGUA or
                                im.getpixel((c * 8 + 7, y)) in AGUA):
                            solapado += 1
                            break
                # Un objeto puede rozar la orilla un frame mientras gira; solo
                # se denuncia si la mayor parte de sus filas estan fuera.
                if solapado * 2 > len(filas):
                    fallos += 1
                    print("FALLO %s  sprite x=%d..%d (celdas %d..%d) y=%d..%d: "
                          "%d de %d filas sobre tierra"
                          % (os.path.basename(f), x0, x1, c0, c1, ta, tb,
                             solapado, len(filas)))
    print("\n%d capturas con sprites, %d fallos" % (total, fallos))
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main())
