#!/usr/bin/env python3
"""ver_sprites.py - Dibuja en ASCII cada sprite con los colores de su paleta.

Muestra, para cada sprite del juego, el arte a 8x8 con el INDICE de color en
cada pixel (0 = transparente) y la traduccion de ese indice al color real de la
paleta que usa. Sirve para revisar de un vistazo quien ocupa que colores.

  Uso: python tools/ver_sprites.py
"""
PAL = {
    0: ("VC_SPPAL_0", {1: "piel #FF8800", 2: "marron #884400", 3: "negro #000000"}),
    1: ("VC_SPPAL_1", {1: "azul #0000FF", 2: "cian #00FFFF", 3: "blanco #FFFFFF"}),
    2: ("VC_SPPAL_2", {1: "magenta #FF00FF", 2: "rojo #FF0000", 3: "blanco #FFFFFF"}),
    3: ("VC_SPPAL_3", {1: "verde #00FF00", 2: "naranja #FF8800", 3: "blanco #FFFFFF"}),
}

# nombre, paleta, patron, sprite(s) OAM, escala
SPRITES = [
    ("AVION",    1, "spr_plane",  [0],        "2x"),
    ("BALA",     1, "spr_bullet", [30, 31],   "1x"),
    ("EXPL. 16", 1, "spr_boom",   [5],        "2x"),
    ("EXPL. 17", 1, "spr_boom2",  [5],        "2x"),
    ("EXPL. 18", 1, "spr_boom3",  [5],        "2x"),
    ("BARCO",    2, "spr_boat",   [8],        "2x"),
    ("HELI",     0, "spr_heli",   [6],        "2x"),
    ("BIDON",    3, "spr_fuel",   [4],        "2x"),
]

# Patrones del HUD (paleta 1, escala 1x)
HUD = [
    ("HUD digito 0", [0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00]),
    ("HUD vidas",    [0x18,0x3C,0x7E,0xFF,0x18,0x18,0x18,0x00]),
    ("HUD barra ON", [0xFF]*8),
    ("HUD barra OFF",[0x81]*8),
]

PATS = {
    "spr_plane":  ([0x10,0x38,0x6C,0xC6,0x92,0x38,0x6C,0x00],
                   [0x00,0x00,0x10,0x10,0x00,0x00,0x10,0x54]),
    "spr_bullet": ([0x18]*6 + [0x00,0x00], [0x18]*6 + [0x00,0x00]),
    "spr_boom":   ([0x00,0x00,0x10,0x10,0x00,0x00,0x10,0x00],
                   [0x00,0x10,0x28,0x44,0x10,0x38,0x28,0x00]),
    "spr_boom2":  ([0x00,0x38,0x6C,0x54,0x44,0x6C,0x38,0x00],
                   [0x00,0x38,0x7C,0x6C,0x54,0x7C,0x28,0x00]),
    "spr_boom3":  ([0x00,0x38,0x44,0x82,0x92,0x82,0x46,0x38],
                   [0x00,0x38,0x44,0x92,0xAA,0x92,0x46,0x38]),
    "spr_boat":   ([0x00,0x04,0x0C,0x3E,0x00,0x00,0x3E,0x00],
                   [0x00,0x00,0x00,0x00,0xFF,0x7F,0x3E,0x00]),
    "spr_heli":   ([0xFE,0x10,0x00,0x00,0xFF,0x00,0x00,0x00],
                   [0x00,0x00,0x39,0xFF,0xFF,0xFC,0x10,0x7C]),
    "spr_fuel":   ([0x7E,0x5A,0x7E,0x7E,0x7E,0x7E,0x7E,0x00],
                   [0x7E,0x5A,0x4E,0x5E,0x4E,0x5E,0x7E,0x00]),
}


def arte(p0, p1):
    filas = []
    for r in range(8):
        fila = []
        for c in range(8):
            bit = 7 - c
            a = (p0[r] >> bit) & 1
            b = (p1[r] >> bit) & 1
            fila.append((b << 1) | a)
        filas.append(fila)
    return filas


def main():
    for nombre, pal, clave, slots, esc in SPRITES:
        pnom, colores = PAL[pal]
        p0, p1 = PATS[clave]
        filas = arte(p0, p1)
        usados = sorted({v for f in filas for v in f})
        print("=== %s   paleta %s (%s)   sprite OAM %s   escala %s"
              % (nombre, pnom, ", ".join("%d=%s" % (k, v) for k, v in colores.items()),
                 slots, esc))
        for f in filas:
            print("   " + "".join(str(v) if v else "." for v in f))
        print("   indices usados: %s" % usados)
        faltan = [i for i in (1, 2, 3) if i not in usados]
        if faltan:
            print("   (no usa el %s: %s)"
                  % ("/" .join(map(str, faltan)),
                     ", ".join(colores[i] for i in faltan)))
        print()

    print("=== HUD (paleta VC_SPPAL_1, escala 1x, slots 10..23)")
    print("    (se cargan con el MISMO byte en los dos planos, o sea todo color 3 = blanco")
    print("     salvo los '0' del patron, que son transparentes)")
    for nombre, p in HUD:
        print(" %s" % nombre)
        for r in p:
            print("   " + "".join("3" if r & (0x80 >> c) else "." for c in range(8)))


if __name__ == "__main__":
    main()
