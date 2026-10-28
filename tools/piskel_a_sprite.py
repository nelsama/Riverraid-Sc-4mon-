#!/usr/bin/env python3
"""piskel_a_sprite.py - Convierte un export de Piskel (uint32 0xAARRGGBB, 8x8
por frame) en patrones de sprite para el core (planos p0 y p1) y una paleta
RGB444.

Formato del core:
    indice = (p1 << 1) | p0      0 = transparente
    fila 0 = arriba, bit 7 = izquierda

  Uso: python tools/piskel_a_sprite.py RiverRaid.c
"""
import re
import sys


def rgb888_a_444(c):
    """0xAARRGGBB -> (r,g,b) de 4 bits cada uno (nibble alto)."""
    r = (c >> 16) & 0xFF
    g = (c >> 8) & 0xFF
    b = c & 0xFF
    return ((r >> 4) & 0xF, (g >> 4) & 0xF, (b >> 4) & 0xF)


def main():
    ruta = sys.argv[1] if len(sys.argv) > 1 else "RiverRaid.c"
    s = open(ruta, encoding="latin-1").read()

    w = int(re.search(r"FRAME_WIDTH (\d+)", s).group(1))
    h = int(re.search(r"FRAME_HEIGHT (\d+)", s).group(1))
    cuerpo = s[s.find("{"):]

    frames = []
    for bloque in re.findall(r"\{([^{}]*)\}", cuerpo):
        vals = [int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]{8})", bloque)]
        if len(vals) == w * h:
            frames.append(vals)

    print("// Generado por tools/piskel_a_sprite.py desde %s" % ruta)
    print("// %d frames de %dx%d" % (len(frames), w, h))

    for n, fr in enumerate(frames):
        # colores de dibujo, en orden de aparicion; transparente = alpha 0
        orden = []
        for c in fr:
            if (c >> 24) == 0:
                continue
            if c not in orden:
                orden.append(c)
        print("\n// ---- frame %d: %d colores (%d max en el core = indices 1..3)"
              % (n, len(orden), 3))
        if len(orden) > 3:
            print("// AVISO: mas de 3 colores, hay que fusionar dos")

        for idx, c in enumerate(orden, start=1):
            r, g, b = rgb888_a_444(c)
            print("//   indice %d = 0x%06X -> VC_RGB444(0x%X,0x%X,0x%X)" %
                  (idx, c & 0xFFFFFF, r, g, b))

        p0, p1 = [], []
        for fila in range(h):
            b0 = b1 = 0
            for col in range(w):
                c = fr[fila * w + col]
                if (c >> 24) == 0:
                    i = 0
                else:
                    i = orden.index(c) + 1
                b0 = (b0 << 1) | (i & 1)
                b1 = (b1 << 1) | ((i >> 1) & 1)
            p0.append(b0)
            p1.append(b1)

        print('static const uint8_t spr_%d_p0[8] = { %s };' %
              (n, ", ".join("0x%02X" % v for v in p0)))
        print('static const uint8_t spr_%d_p1[8] = { %s };' %
              (n, ", ".join("0x%02X" % v for v in p1)))

        print("//   vista:")
        for fila in range(h):
            linea = ""
            for col in range(w):
                c = fr[fila * w + col]
                linea += "." if (c >> 24) == 0 else str(orden.index(c) + 1)
            print("//     " + linea)


if __name__ == "__main__":
    main()
