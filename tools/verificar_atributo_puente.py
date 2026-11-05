"""Verifica que destruir el puente deja TILE y ATRIBUTO de agua.

El bug que se arreglo: fv_cell() solo escribe el TILE. La viga se pinto con
PAL_WOOD (atributo 0), asi que al borrarla quedaba un tile de agua con atributo
de madera = seguia viendose marron. Ese era el "disparo al puente y queda tal
cual".

El script busca el puente en /vram.json (reintentando: el endpoint va con
retraso), dispara y comprueba que esas celdas pasan a tile de agua Y atributo
de agua.

Uso:
    python tools/verificar_atributo_puente.py
"""
import json
import socket
import time
import urllib.request

BASE = "http://127.0.0.1:8080"
VIGA, CALLE = 29, 30
TILE_WATER, TILE_WATER2 = 0x11, 0x12
PAL_WATER = 1


def get(p):
    with urllib.request.urlopen(BASE + p, timeout=5) as r:
        return r.read()


def post(p, data):
    req = urllib.request.Request(BASE + p, data=data, method="POST")
    with urllib.request.urlopen(req, timeout=5) as r:
        return r.read()


def vram():
    return json.loads(get("/vram.json"))


def busca_viga(segs=10):
    """Espera a que haya viga en el tilemap. Devuelve (fila, filas) o (None, [])."""
    t0 = time.time()
    while time.time() - t0 < segs:
        v = vram()
        filas = [r for r in range(32) if VIGA in v["tilemap"][r]]
        if filas:
            return filas[0], filas
        time.sleep(0.05)
    return None, []


def main():
    with open("output/game.bin", "rb") as f:
        post("/run?name=GAME.BIN&addr=0800", f.read())
    time.sleep(2.0)

    f, filas = busca_viga()
    if f is None:
        print("FALLO: no aparece la viga tras el reinicio")
        return 1

    v = vram()
    tm, at = v["tilemap"], v["atributos"]
    # Celdas de viga de esa fila.
    celdas = [c for c in range(64) if tm[f][c] == VIGA]
    print("viga en fila %d: columnas %s" % (f, celdas[:6]))
    print("  ANTES  tile=%d attr=%d" % (tm[f][celdas[0]], at[f][celdas[0]]))
    if at[f][celdas[0]] != 0:
        print("  AVISO: el atributo de la viga no es 0 (PAL_WOOD)")

    u = socket.create_connection(("127.0.0.1", 4000), timeout=5)
    u.sendall(b" ")          # arrancar
    time.sleep(0.3)
    u.sendall(b" ")          # disparar
    time.sleep(0.6)
    u.close()

    ok_tile = ok_attr = 0
    for c in celdas:
        t, a = tm[f][c], at[f][c]
        v2 = vram()
        t, a = v2["tilemap"][f][c], v2["atributos"][f][c]
        if t in (TILE_WATER, TILE_WATER2):
            ok_tile += 1
        if a == PAL_WATER:
            ok_attr += 1

    print("  DESPUES: %d/%d celdas con tile de agua, %d/%d con atributo de agua"
          % (ok_tile, len(celdas), ok_attr, len(celdas)))

    if ok_tile == len(celdas) and ok_attr == len(celdas):
        print("OK: el puente queda AGUA de verdad (tile y atributo)")
        return 0
    print("FALLO: quedan celdas con el aspecto de madera")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
