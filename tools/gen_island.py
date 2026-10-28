#!/usr/bin/env python3
"""gen_island.py - Genera el patron de SPRITE 8x8 de la isla (escala 2x = 16x16
en pantalla) y lo escribe en game.c entre marcas.

La isla ya no es un cuadrado de 2x2 tiles: es un sprite. En 8x8 (ampliado 2x)
hay sitio para una isla de 6x6 con orilla de arena y vegetacion, que es lo que
se ve en el arcade.

Paleta del sprite (SPR_ISLA_PAL = SPPAL_3):
  1 = arena, 2 = vegetacion, 3 = arena oscura (sombra del lado del sol)

  .  transparente (agua)      a  arena      v  vegetacion      s  sombra
  bit 7 = izquierda, fila 0 = arriba.

  Uso: python tools/gen_island.py
"""
import re

ISLA = [
    "..aaaa..",
    ".aavaa..",
    "aavvaa..",
    "aavvaa..",
    "aavaaa..",
    ".aaaaas.",
    "..assa..",
    "........",
]

PAL = {'.': 0, 'a': 1, 'v': 2, 's': 3}

MARCA_INI = "/* --- ISLA (GENERADO por tools/gen_island.py) --- */"
MARCA_FIN = "/* --- FIN ISLA --- */"


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
    p0, p1 = bits(ISLA)
    txt = (MARCA_INI + "\n"
           + c_array("spr_isla_p0", p0)
           + c_array("spr_isla_p1", p1)
           + "/* --- FIN ISLA --- */\n")
    s = open("game.c", encoding="latin-1").read()
    if MARCA_INI not in s:
        raise SystemExit("no encuentro las marcas de la isla en game.c")
    s = re.sub(re.escape(MARCA_INI) + r".*?" + re.escape(MARCA_FIN) + r"\n",
               txt, s, count=1, flags=re.S)
    open("game.c", "w", encoding="latin-1").write(s)
    print("game.c actualizado")


if __name__ == "__main__":
    main()
