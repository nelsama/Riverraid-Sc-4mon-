"""Mide las FILAS entre puentes, con UNA sola conexion UART.

Aprendido a golpes en esta sesion:
  * Los offsets de BSS CAMBIAN con cada build: hay que leerlos de
    output/game.map + build/game.lst justo antes de medir.
  * Abrir y cerrar el socket UART cada vuelta CORTA la entrada del emulador y
    el juego se queda parado. Se usa UNA conexion y se escribe sin cerrar.
  * Desde GAME OVER hay que mandar 'R'; desde la pausa, ' '.

Uso:
    python tools/medir_tramo.py [segundos]
"""
import re
import socket
import sys
import time
import urllib.request

BASE = "http://127.0.0.1:8080"
ESPERADO = 16 * 12          # GEN_BLOCK * GEN_SECTOR
VARS = ["plane_x", "world_row", "gen_sector", "lives", "esperando_fire"]


def get(p):
    with urllib.request.urlopen(BASE + p, timeout=5) as r:
        return r.read()


def post(p, data):
    req = urllib.request.Request(BASE + p, data=data, method="POST")
    with urllib.request.urlopen(req, timeout=5) as r:
        return r.read()


def dirs():
    """Lee las direcciones del build ACTUAL (cambian en cada compilacion)."""
    mapa = open("output/game.map", encoding="utf-8", errors="replace").read()
    base = int(re.search(r"BSS\s+([0-9A-F]{6})", mapa).group(1), 16)
    off = {}
    for linea in open("build/game.lst", encoding="utf-8", errors="replace"):
        m = re.match(r"^([0-9A-F]{6})r\s+1\s+_(\w+):", linea)
        if m and m.group(2) in VARS:
            off[m.group(2)] = base + int(m.group(1), 16)
    return off


def main():
    segs = float(sys.argv[1]) if len(sys.argv) > 1 else 150.0
    d = dirs()
    falta = [v for v in VARS if v not in d]
    if falta:
        print("FALTAN en el listado: %s" % falta)
        return 1
    print("direcciones: %s" % {k: "%04X" % v for k, v in d.items()})

    with open("output/game.bin", "rb") as f:
        post("/run?name=GAME.BIN&addr=0800", f.read())
    time.sleep(3.0)

    u = socket.create_connection(("127.0.0.1", 4000), timeout=5)
    u.sendall(b" ")
    time.sleep(0.6)

    prev = None
    marcas = []
    t0 = time.time()
    while time.time() - t0 < segs:
        r = get("/ram.bin")
        wr = r[d["world_row"]] | (r[d["world_row"] + 1] << 8)
        sec = r[d["gen_sector"]]
        vid = r[d["lives"]]
        esp = r[d["esperando_fire"]]
        px = r[d["plane_x"]]

        if vid == 0:
            u.sendall(b" R ")
            time.sleep(0.5)
            continue
        if esp:
            u.sendall(b" ")
            time.sleep(0.3)
            continue

        if sec != prev:
            print("t=%5.1fs world_row=%4d sec=%d vidas=%d" % (
                time.time() - t0, wr, sec, vid))
            marcas.append((wr, sec))
            prev = sec
            if len(marcas) >= 4:
                break

        # Conduccion suave hacia el centro (el cauce se centra por construccion).
        if px < 148:
            u.sendall(b"d")
        elif px > 172:
            u.sendall(b"a")

        time.sleep(0.04)

    u.close()
    print()
    for i in range(1, len(marcas)):
        a, b = marcas[i - 1], marcas[i]
        print("sec %d->%d : +%d filas  (esperado %d)" % (
            a[1], b[1], b[0] - a[0], ESPERADO))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
