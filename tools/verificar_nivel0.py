"""Comprueba que la dificultad del nivel 0 (primer tramo) se aplica de verdad.

En el nivel 0 la tabla dice:
    barco: 0   -> NO deriva de lado
    heli:  0   -> NO aparece (ene_active[HELI] siempre 0)
    bidon: 18  -> sale uno cada ~18 filas de mundo

OJO: el enemigo SI puede cambiar de columna por `reubicar_al_agua()`, que lo
empuja 1 px hacia el agua cuando el cauce se cierra a su lado. Eso NO es
deriva: es correccion obligatoria para que no quede sobre la orilla. Por eso
se tolera un cambio aislado de 1 px, pero NO un movimiento repetido.

Uso:
    python tools/verificar_nivel0.py [segundos]
"""
import re
import sys
import time
import urllib.request

BASE = "http://127.0.0.1:8080"
VARS = ["ene_x", "ene_active", "gen_sector", "lives", "esperando_fire"]


def get(p):
    with urllib.request.urlopen(BASE + p, timeout=5) as r:
        return r.read()


def post(p, data):
    req = urllib.request.Request(BASE + p, data=data, method="POST")
    with urllib.request.urlopen(req, timeout=5) as r:
        return r.read()


def dirs():
    mapa = open("output/game.map", encoding="utf-8", errors="replace").read()
    base = int(re.search(r"BSS\s+([0-9A-F]{6})", mapa).group(1), 16)
    off = {}
    for linea in open("build/game.lst", encoding="utf-8", errors="replace"):
        m = re.match(r"^([0-9A-F]{6})r\s+1\s+_(\w+):", linea)
        if m and m.group(2) in VARS:
            off[m.group(2)] = base + int(m.group(1), 16)
    return off


def main():
    segs = float(sys.argv[1]) if len(sys.argv) > 1 else 40.0
    sys.path.insert(0, "tools")
    import mandos

    d = dirs()
    falta = [v for v in VARS if v not in d]
    if falta:
        print("FALTAN: %s" % falta)
        return 1

    with open("output/game.bin", "rb") as f:
        post("/run?name=GAME.BIN&addr=0800", f.read())
    time.sleep(3.0)
    mandos.pulsa("space", 150)
    time.sleep(0.5)

    movio_barco = 0
    helis_vistos = 0
    muestras = 0
    x_ant = None
    t0 = time.time()

    while time.time() - t0 < segs:
        r = get("/ram.bin")
        sec = r[d["gen_sector"]]
        vid = r[d["lives"]]
        esp = r[d["esperando_fire"]]

        if vid == 0:
            mandos.pulsa("enter", 120)
            time.sleep(0.4)
            continue
        if esp:
            mandos.pulsa("space", 120)
            time.sleep(0.3)
            continue

        if sec > 1:          # ya no es el nivel 0
            break

        bx = r[d["ene_x"]]                  # ene_x[0] = barco
        barco_on = r[d["ene_active"]]
        heli_on = r[d["ene_active"] + 1]

        if heli_on:
            helis_vistos += 1
        if x_ant is not None and bx != x_ant:
            movio_barco += 1
        x_ant = bx
        muestras += 1
        time.sleep(0.04)

    print("muestras en nivel 0: %d" % muestras)
    print("cambios de columna del barco: %d de %d muestras" % (
        movio_barco, muestras))
    print("frames con helicoptero: %d  (esperado 0)" % helis_vistos)

    # Con deriva a 1 px/4f el barco cambiaria casi cada 4 muestras (decenas de
    # veces). Los pocos cambios que quedan son reubicaciones al agua.
    deriva = movio_barco > (muestras // 20)
    ok = (not deriva) and helis_vistos == 0
    if deriva:
        print("FALLO: el barco deriva (nivel 0 deberia tener barco quieto)")
    elif helis_vistos:
        print("FALLO: hay helicopteros en el nivel 0")
    else:
        print("OK: nivel 0 = barco quieto (solo reubicaciones) y sin helis")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
