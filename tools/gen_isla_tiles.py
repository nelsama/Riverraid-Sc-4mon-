#!/usr/bin/env python3
"""gen_isla_tiles.py - Escribe en game.c los patrones de FONDO de la isla
(4 cuadrantes con la paleta PAL_ISLAND) entre marcas.

La isla vuelve a ser parte del tilemap (2x2 celdas, patrones TILE_ISLAND..+3),
pero con Y de mundo: la coloca `islas_render()` en la fila de cinta que toca
en cada momento, no el generador del rio.

Paleta PAL_ISLAND (fondo, paleta 2):
  1 = arena, 2 = vegetacion, 3 = agua (mismo azul del cauce)

  .  agua (indice 3)
  a  arena      v  vegetacion      s  sombra de arena (indice 1, ya es arena)

  bit 7 = izquierda, fila 0 = arriba.

  Uso: python tools/gen_isla_tiles.py
"""
import re

ISLA = [
    "....aaaa....",
    "..aaaaaaaa..",
    ".aaavvvaaaa.",
    ".aavvvvvaaa.",
    "aavvvvvvvaaa",
    "aavvvvvvvaaa",
    "aavvvvvvvaaa",
    "aaavvvvvaaaa",
    ".aaavvvaaaa.",
    ".aaaaaaaaaa.",
    "..aaaaaaaa..",
    "....aaaa....",
]

PAL = {'.': 3, 'a': 1, 'v': 2}

MARCA_INI = "/* --- ISLA EN TILES (GENERADO por tools/gen_isla_tiles.py) --- */"
MARCA_FIN = "/* --- FIN ISLA EN TILES --- */"


def cuadrante(ox, oy):
    out = []
    for y in range(oy, oy + 8):
        fila = ISLA[y][ox:ox + 8] if y < len(ISLA) else ""
        out.append(fila.ljust(8, '.'))
    return out


def bits(rows):
    p0, p1 = [], []
    for r in rows:
        b0 = b1 = 0
        for ch in r:
            idx = PAL[ch]
            b0 = (b0 << 1) | (idx & 1)
            b1 = (b1 << 1) | ((idx >> 1) & 1)
        p0.append(b0)
        p1.append(b1)
    return p0, p1


def c_array(name, vals):
    return "static const uint8_t %s[8] = { %s };\n" % (
        name, ", ".join("0x%02X" % v for v in vals))


def main():
    txt = MARCA_INI + "\n"
    for nombre, ox, oy in (("tile_island_tl", 0, 0), ("tile_island_tr", 8, 0),
                           ("tile_island_bl", 0, 8), ("tile_island_br", 8, 8)):
        p0, p1 = bits(cuadrante(ox, oy))
        txt += c_array(nombre + "_p0", p0)
        txt += c_array(nombre + "_p1", p1)
    txt += MARCA_FIN + "\n"

    s = open("game.c", encoding="latin-1").read()
    pat = re.compile(re.escape(MARCA_INI) + r".*?" + re.escape(MARCA_FIN) + r"\n",
                     re.S)
    if not pat.search(s):
        raise SystemExit("no encuentro las marcas de la isla en tiles en game.c")
    s = pat.sub(lambda _: txt, s, count=1)
    open("game.c", "w", encoding="latin-1").write(s)
    print("game.c actualizado")


if __name__ == "__main__":
    main()
