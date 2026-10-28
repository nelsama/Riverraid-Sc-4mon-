#!/usr/bin/env python3
"""gen_tiles.py - Genera los patrones 2bpp de isla y orilla a partir de dibujos
en ASCII y los escribe en game.c (entre marcas).

Convenciones del core (comprobadas):
  indice = (p1 << 1) | p0   ->  0 trasparente, 1, 2, 3
  fila 0 = arriba, bit 7 = izquierda.

Paletas:
  isla   (PAL_ISLAND): 1 = arena, 2 = vegetacion, 3 = agua
  orilla (PAL_EDGE):   1 = verde claro, 2 = arena, 3 = verde oscuro
El agua de la orilla se pone en 0 (transparente) para que se vea BG_COLOR,
que es el mismo azul del rio.

  Uso: python tools/gen_tiles.py
"""
import re

# ---------------------------------------------------------------------------
# ISLA 16x16 en cuatro cuadrantes de 8x8.
#   .  agua       a  arena      v  vegetacion
# La isla es un rombo/elipse de arena con vegetacion dentro.
# ---------------------------------------------------------------------------
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


def cuadrante(rows, ox, oy):
    """8x8 de la isla; fuera de la forma se rellena con agua ('.')."""
    out = []
    for y in range(oy, oy + 8):
        fila = rows[y][ox:ox + 8] if y < len(rows) else ""
        out.append(fila.ljust(8, '.'))
    return out

# ---------------------------------------------------------------------------
# ORILLA IZQUIERDA 8x8: cesped a la izq (fuera, lo pone el tile de cesped),
# arena en el medio y agua a la derecha.
#   g  verde claro    a  arena     .  agua (transparente)
# ---------------------------------------------------------------------------
ORILLA_L0 = [
    "ggggaaaa",
    "ggggaaaa",
    "ggggaaaa",
    "ggggaaaa",
    "gggggaaa",
    "gggggaaa",
    "gggggaaa",
    "gggggaaa",
]
ORILLA_L1 = [
    "gggggaaa",
    "gggggaaa",
    "gggggaaa",
    "gggggaaa",
    "ggggaaaa",
    "ggggaaaa",
    "ggggaaaa",
    "ggggaaaa",
]

PAL = {
    'isla': {'.': 3, 'a': 1, 'v': 2},      # agua opaca (indice 3) en la isla
    'orilla': {'g': 1, 'a': 2, '.': 0},    # agua transparente en la orilla
}


def bits(rows, pal):
    """Devuelve (p0[], p1[]) a partir de filas ASCII."""
    p0, p1 = [], []
    for r in rows:
        b0 = b1 = 0
        for ch in r:
            idx = pal[ch]
            b0 = (b0 << 1) | (idx & 1)
            b1 = (b1 << 1) | ((idx >> 1) & 1)
        p0.append(b0)
        p1.append(b1)
    return p0, p1


def c_array(name, vals):
    body = ", ".join("0x%02X" % v for v in vals)
    return "static const uint8_t %s[8] = { %s };\n" % (name, body)


def espejo(rows):
    """Espeja horizontalmente (para el borde derecho)."""
    return [r[::-1] for r in rows]


def empaqueta(dibujos):
    txt = "/* --- GENERADO por tools/gen_tiles.py --- */\n"
    for name, rows, clave in dibujos:
        p0, p1 = bits(rows, PAL[clave])
        txt += c_array(name + "_p0", p0)
        txt += c_array(name + "_p1", p1)
    return txt + "/* --- FIN GENERADO --- */\n"


def main():
    t = empaqueta([
        ("tile_island_tl", cuadrante(ISLA, 0, 0), 'isla'),
        ("tile_island_tr", cuadrante(ISLA, 8, 0), 'isla'),
        ("tile_island_bl", cuadrante(ISLA, 0, 8), 'isla'),
        ("tile_island_br", cuadrante(ISLA, 8, 8), 'isla'),
        ("tile_edge_l0", ORILLA_L0, 'orilla'),
        ("tile_edge_l1", ORILLA_L1, 'orilla'),
        ("tile_edge_r0", espejo(ORILLA_L0), 'orilla'),
        ("tile_edge_r1", espejo(ORILLA_L1), 'orilla'),
    ])

    p = "game.c"
    s = open(p, encoding="latin-1").read()
    ini = s.find("/* --- GENERADO por tools/gen_tiles.py --- */")
    fin = s.find("/* --- FIN GENERADO --- */")
    if ini >= 0 and fin >= 0:
        s = s[:ini] + t + s[fin + len("/* --- FIN GENERADO --- */\n"):]
    else:
        print("AVISO: no hay marcas; se anade ANTES de 'PATRONES DE SPRITE'")
        marca = "/* ===========================================================================\n * PATRONES DE SPRITE"
        i = s.find(marca)
        s = s[:i] + t + "\n" + s[i:]
    open(p, "w", encoding="latin-1").write(s)
    print("game.c actualizado")


if __name__ == "__main__":
    main()
