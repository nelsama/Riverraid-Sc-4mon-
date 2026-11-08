"""Auditoria de RAM: lista los simbolos de BSS/DATA/RODATA con su tamano.

Por que existe: el .map de ld65 da los segmentos pero NO los simbolos
individuales de BSS. Para saber donde estan los 282 bytes de BSS hay que leer
el .lst de ca65.

EL TRAMANO NO VIENE EN EL .lst. La columna que parece un tamano ("000049r 1")
es el NUMERO DE BYTES DE CODIGO de esa linea, no el del simbolo: todos los
datos salen con 1. El tamano real de cada simbolo se calcula por DIFERENCIA
de desplazamiento con el SIGUIENTE simbolo del mismo segmento; al ultimo se le
asigna lo que falte hasta el final del segmento.

TRAMPAS QUE EVITA (todas costaron tiempo antes):
  - Los simbolos aparecen REPETIDOS (una vez por cada .segment que se abre).
    Sin desduplicar, los totales salen inflados.
  - Las `static` con inicializador van a DATA, no a BSS.
  - La columna End del .map es el ULTIMO byte util.

  Uso: python tools/auditar_ram.py
"""
import re

MAP = "output/game.map"
LST = "build/game.lst"

RAM = ("BSS", "DATA", "RODATA")


def segmentos():
    txt = open(MAP, encoding="utf-8", errors="replace").read()
    seg = {}
    for n in RAM:
        m = re.search(r"^%s\s+([0-9A-F]{6})\s+([0-9A-F]{6})\s+([0-9A-F]{6})"
                      % n, txt, re.M)
        if m:
            seg[n] = (int(m.group(1), 16), int(m.group(2), 16),
                      int(m.group(3), 16))
    return seg


def simbolos():
    # Las dos lineas empiezan por la direccion y el tamano de CODIGO de la
    # linea ("000000r 1"); despues va el contenido. Un ".segment" sale asi:
    #   000000r 1               .segment\t"DATA"
    # y un simbolo de datos, asi:
    #   000049r 1               _river_l:
    re_seg = re.compile(r'^[0-9A-F]{6}r [0-9A-F]+\s+\.segment\s+"(\w+)"')
    re_sym = re.compile(r"^([0-9A-F]{6})r [0-9A-F]+\s+_?(\w+):\s*$")

    out = {k: {} for k in RAM}
    actual = None
    for linea in open(LST, encoding="utf-8", errors="replace"):
        ms = re_seg.match(linea)
        if ms:
            actual = ms.group(1)
            continue
        if actual not in out:
            continue
        mm = re_sym.match(linea)
        if not mm:
            continue
        off = int(mm.group(1), 16)
        out[actual].setdefault(mm.group(2), off)   # desduplicar
    return out


def main():
    seg = segmentos()
    syms = simbolos()
    for nombre in RAM:
        ini, fin, tam = seg[nombre]
        if nombre not in syms or not syms[nombre]:
            continue
        orden = sorted(syms[nombre].items(), key=lambda kv: kv[1])
        print("=" * 70)
        print("%s  $%04X-$%04X  (%d bytes)" % (nombre, ini, fin, tam))
        print("=" * 70)
        for i, (nom, off) in enumerate(orden):
            if i + 1 < len(orden):
                t = orden[i + 1][1] - off
            else:
                t = tam - off
            marca = "" if t > 0 else "  <-- OJO: tamano 0, revisar"
            print("  %-30s %4d B   $%04X%s" % (nom, t, ini + off, marca))
        print("  %-30s %4d B" % ("---- total", tam))
        print()


if __name__ == "__main__":
    main()
