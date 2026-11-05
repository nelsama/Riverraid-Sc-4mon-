"""Comprueba que el mapa de VARIABLES del .lst coincide con el de game.c.

Por que existe: varios scripts (verificar_nivel0.py, verificar_colision.py...)
leen direcciones de RAM de build/game.lst buscando las lineas de `.res` de BSS.
Hay TRES trampas, descubiertas midiendo:

  1. cc65 reparte las variables `static` LOCALES de las funciones a un segundo
     bloque que tambien acaba en ".res" y con los MISMOS nombres de simbolo. Ese
     bloque empieza justo despues de los simbolos de verdad (en este build, en
     0x10F cuando el ultimo simbolo real acaba en 0x10E).
  2. Las `static` que llevan INICIALIZADOR no van a BSS, van a DATA
     (gen_seed_hi = 0xA8, hud_last_lives = 0xFF).
  3. La columna "End" del mapa es el ultimo byte UTIL, no Start+Size: con
     alineacion hay un byte de hueco. Lo que manda es Size.

  Uso: python tools/verificar_mapa_parametros.py
"""
import re
import sys

# Variables que el codigo de verificacion necesita leer por direccion.
VARS = ["ene_x", "ene_active", "ene_y", "ene_drift", "lives", "esperando_fire",
        "gen_sector", "fuel", "score", "plane_x", "plane_y"]


def offs_bss():
    """Devuelve ({variable: direccion absoluta}, inicio, fin) de DATA + BSS."""
    txt = open("output/game.map", encoding="utf-8", errors="replace").read()
    seg = {}
    for nombre in ("DATA", "BSS"):
        m = re.search(r"^%s\s+([0-9A-F]{6})\s+([0-9A-F]{6})\s+([0-9A-F]{6})"
                      % nombre, txt, re.M)
        if not m:
            raise SystemExit("no encuentro el segmento %s en output/game.map" % nombre)
        inicio = int(m.group(1), 16)
        seg[nombre] = (inicio, inicio + int(m.group(3), 16) - 1)

    pat = re.compile(r"^([0-9A-F]{6})r\s+1\s+_?(\w+):")
    actual = None
    off = {}
    for linea in open("build/game.lst", encoding="utf-8", errors="replace"):
        ms = re.search(r'\.segment\s+"(\w+)"', linea)
        if ms:
            actual = ms.group(1)
            continue
        mm = pat.match(linea)
        if not mm or actual not in seg:
            continue
        if mm.group(2) in off:          # el bloque de estaticas locales repite
            continue
        inicio, fin = seg[actual]
        direccion = inicio + int(mm.group(1), 16)
        if direccion > fin:             # fuera del segmento: basura del .lst
            continue
        off[mm.group(2)] = direccion
    return off, seg["DATA"][0], seg["BSS"][1]


def main():
    off, inicio, fin = offs_bss()
    print("DATA+BSS %04X-%04X  variables encontradas: %d" % (inicio, fin, len(off)))

    falta = [v for v in VARS if v not in off]
    if falta:
        print("FALTAN: %s" % ", ".join(falta))
        return 1
    for v in VARS:
        print("  %-16s %04X" % (v, off[v]))

    # Sanidad: dos variables no pueden compartir direccion, y el primer simbolo
    # de BSS tiene que ser anterior al bloque de estaticas locales.
    if len(set(off.values())) != len(off):
        print("FALLO: hay direcciones repetidas")
        return 1
    print("OK: mapa coherente")
    return 0


if __name__ == "__main__":
    sys.exit(main())
