"""decodificar_docx.py - Que produce REALMENTE el LFSR del documento .docx.

  Uso: python tools/decodificar_docx.py [bloques]

El documento dice en el punto 3 que "cada paso genera un bloque de terreno
equivalente a 32 lineas de escaneo". Es decir: el LFSR avanza UNA vez por
bloque, no por fila de pantalla. Este script imprime, para cada bloque, lo que
pide su tabla de bits (ancho, isla, entidad) usando el ASM del 6502 tal cual.

Sirve para decidir el generador del juego con datos, no con la impresion.
"""
import sys

sys.path.insert(0, __file__.rsplit("\\", 1)[0])
from lfsr_original import RiverRaidLFSR, decode  # noqa: E402

ENTIDADES = ["fuel", "barco", "heli", "jet", "edificio", "-", "-", "-"]


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 24
    lfsr = RiverRaidLFSR()
    print("bloque  SEED_HI SEED_LO  ancho  isla  entidad")
    for k in range(n):
        val = lfsr.step()
        d = decode(val)
        print("%6d    %02X      %02X     %5d   %-5s %s"
              % (k, val >> 8, val & 0xFF, d["width"],
                 "SI" if d["split"] else "--", ENTIDADES[d["entity"]]))


if __name__ == "__main__":
    main()
