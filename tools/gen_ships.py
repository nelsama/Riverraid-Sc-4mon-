#!/usr/bin/env python3
"""gen_ships.py - Genera los patrones de sprite del avion, helicoptero y barco
a partir de dibujos ASCII y los escribe en game.c entre marcas.

Los tres van a ESCALA 2x (8x8 -> 16x16 en pantalla), igual que el resto.

Convencion del core: indice = (p1 << 1) | p0.  0 = transparente.
Cada paleta de sprite tiene 3 colores utiles (1, 2, 3):

  avion        (SPR_PLANE_PAL) : 3 = amarillo, 1 = amarillo oscuro
  helicoptero  (SPR_HELI_PAL)  : 1 = azul,       2 = beige (rotor),  3 = azul claro
  barco        (SPR_BOAT_PAL)  : 1 = azul (casco), 2 = rojo (cubierta), 3 = gris

  .  transparente   A  color 1   B  color 2   C  color 3

  Uso: python tools/gen_ships.py
"""
import re

# --- AVION: morro arriba, alas en flecha, cola en H -------------------------
AVION = [
    "...CC...",
    "...CC...",
    "..CCCC..",
    "CCCCCCCC",
    "CC.CC.CC",
    "...CC...",
    "..C..C..",
    "........",
]

# --- HELICOPTERO: rotor beige arriba, cuerpo azul, cola con rotor -----------
HELI = [
    "..BBBB..",
    "B.BBBB.B",
    "..BBBB..",
    "...CC...",
    "..CCCC..",
    "..AAAA..",
    "...AA...",
    "....A...",
]

# --- BARCO: cubierta roja, casco azul, sombra gris --------------------------
BARCO = [
    "........",
    "...BB...",
    "..BBBB..",
    "BBBBBBB.",
    "BBBBBBBB",
    ".AAAAAA.",
    "..AAAA..",
    "........",
]

PAL = {'.': 0, 'A': 1, 'B': 2, 'C': 3}

MARCA_INI = "/* --- NAVES (GENERADO por tools/gen_ships.py) --- */"
MARCA_FIN = "/* --- FIN NAVES --- */"


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
    for pref, dibujo in (("spr_plane", AVION), ("spr_heli", HELI),
                         ("spr_boat", BARCO)):
        p0, p1 = bits(dibujo)
        txt += c_array(pref + "_p0", p0)
        txt += c_array(pref + "_p1", p1)
    txt += MARCA_FIN + "\n"

    s = open("game.c", encoding="latin-1").read()
    pat = re.compile(re.escape(MARCA_INI) + r".*?" + re.escape(MARCA_FIN) + r"\n",
                     re.S)
    if not pat.search(s):
        raise SystemExit("no encuentro las marcas de las naves en game.c")
    s = pat.sub(lambda _: txt, s, count=1)
    open("game.c", "w", encoding="latin-1").write(s)
    print("game.c actualizado")


if __name__ == "__main__":
    main()
