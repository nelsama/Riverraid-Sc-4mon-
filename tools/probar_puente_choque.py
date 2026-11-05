"""Comprueba que al CHOCAR con el puente no queda ninguna astilla dibujada.

El puente se pinta como:
    viga  = TILE_BRIDGE (0x1D), de gen_left a gen_right
    calle = TILE_ROAD   (0x1E), fuera de ese rango
y las filas de viga son puente_bit[fila] != 0.

Aqui se juega hasta chocar, y en ese momento se mira el tilemap: ninguna de las
filas que eran de puente debe conservar tile de viga ni de calle.

  Uso: python tools/probar_puente_choque.py [segundos]
"""
import json
import re
import sys
import time
import urllib.request

BASE = "http://127.0.0.1:8080"
TILE_BRIDGE = 0x1D
TILE_ROAD = 0x1E
VC_MAP_ROWS = 32


def get(p, timeout=10):
    with urllib.request.urlopen(BASE + p, timeout=timeout) as r:
        return r.read()


def offs():
    txt = open("output/game.map", encoding="utf-8", errors="replace").read()
    seg = {}
    for n in ("DATA", "BSS"):
        m = re.search(r"^%s\s+([0-9A-F]{6})\s+[0-9A-F]{6}\s+([0-9A-F]{6})" % n, txt, re.M)
        ini = int(m.group(1), 16)
        seg[n] = (ini, ini + int(m.group(2), 16) - 1)
    pat = re.compile(r"^([0-9A-F]{6})r\s+1\s+_?(\w+):")
    actual, d = None, {}
    for linea in open("build/game.lst", encoding="utf-8", errors="replace"):
        ms = re.search(r'\.segment\s+"(\w+)"', linea)
        if ms:
            actual = ms.group(1)
            continue
        mm = pat.match(linea)
        if not mm or actual not in seg or mm.group(2) in d:
            continue
        ini, fin = seg[actual]
        dir_ = ini + int(mm.group(1), 16)
        if dir_ <= fin:
            d[mm.group(2)] = dir_
    return d


def filas_con_viga(tm):
    out = []
    for r in range(32):
        if any(v == TILE_BRIDGE or v == TILE_ROAD for v in tm[r]):
            out.append(r)
    return out


def main():
    segs = float(sys.argv[1]) if len(sys.argv) > 1 else 45.0
    sys.path.insert(0, "tools")
    import mandos

    d = offs()
    data = open("output/game.bin", "rb").read()
    req = urllib.request.Request(BASE + "/run?name=GAME.BIN&addr=0800",
                                 data=data, method="POST")
    urllib.request.urlopen(req, timeout=15).read()
    for _ in range(90):
        if get("/ram.bin")[d["esperando_fire"]] == 1:
            break
        time.sleep(0.15)
    mandos.pulsa("space", 120)

    # 1) BUSCAR PUENTE: mirar el tilemap hasta que aparezca una viga
    print("buscando un puente en pantalla...")
    t0 = time.time()
    visto = False
    while time.time() - t0 < segs:
        d2 = json.loads(get("/vram.json"))
        if filas_con_viga(d2["tilemap"]):
            visto = True
            break
        if get("/ram.bin")[d["game_over"]]:
            mandos.pulsa("enter", 120)
            time.sleep(0.7)
            mandos.pulsa("space", 120)
        time.sleep(0.15)

    if not visto:
        print("no se llego a ver un puente en %.0f s" % segs)
        return 2

    filas_antes = filas_con_viga(d2["tilemap"])
    print("puente visto en las filas de cinta: %s" % filas_antes)
    ram0 = get("/ram.bin")
    print("puente_bit: %s" % [k for k in range(32)
                              if ram0[d["puente_bit"] + k]])

    # 2) DEJAR QUE CHOQUE (el avion esta quieto, el rio lo arrastra)
    print("esperando el choque...")
    vidas_ant = get("/ram.bin")[d["lives"]]
    t0 = time.time()
    while time.time() - t0 < 40:
        r = get("/ram.bin")
        if r[d["lives"]] < vidas_ant:
            print("CHOQUE detectado (vidas %d -> %d)" % (vidas_ant, r[d["lives"]]))
            break
        if r[d["game_over"]]:
            break
        time.sleep(0.05)
    else:
        print("no choco en 40 s")
        return 2

    time.sleep(0.4)
    tm = json.loads(get("/vram.json"))["tilemap"]
    for k in range(8):
        ram1 = get("/ram.bin")
        pb = [i for i in range(32) if ram1[d["puente_bit"] + i]]
        print("  +%.2fs puente_bit=%s esperando_fire=%d plane_dead=%d"
              % (0.1 * (k + 1), pb, ram1[d["esperando_fire"]],
                 ram1[d["plane_dead"]]))
        time.sleep(0.1)

    malas = []
    for r in filas_antes:
        for c, v in enumerate(tm[r]):
            if v in (TILE_BRIDGE, TILE_ROAD):
                malas.append((r, c, hex(v)))

    if malas:
        print("FALLO: quedan %d celdas de puente sin borrar" % len(malas))
        for m in malas[:12]:
            print("   fila %d col %d tile %s" % m)
        return 1

    print("OK: el puente desaparecio por completo (viga y calle)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
