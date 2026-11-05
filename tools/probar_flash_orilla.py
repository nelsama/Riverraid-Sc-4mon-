"""Prueba: morir contra la ORILLA no debe destellar el agua.

El bug: puente_destruir_bajo_avion() se llama en TODO choque contra orilla o
isla, y encendia el destello de paleta aunque no hubiera viga debajo. El jugador
veia el agua aclararse sin puente -> "choque con un puente invisible".

La unica senal fiable del destello es el COLOR de un pixel de agua, asi que se
lee de /screen.png (el tilemap no cambia con la paleta).

Uso:
    python tools/probar_flash_orilla.py
"""
import socket
import struct
import sys
import time
import urllib.request
import zlib

BASE = "http://127.0.0.1:8080"


def get(p):
    with urllib.request.urlopen(BASE + p, timeout=5) as r:
        return r.read()


def post(p, data):
    req = urllib.request.Request(BASE + p, data=data, method="POST")
    with urllib.request.urlopen(req, timeout=5) as r:
        return r.read()


def png_pixel(data, x, y):
    """Devuelve (r, g, b) del pixel (x, y) de un PNG RGBA/RGB de 8 bits."""
    pos = 8
    w = h = None
    idat = b""
    while pos < len(data):
        ln = struct.unpack(">I", data[pos:pos + 4])[0]
        typ = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + ln]
        if typ == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", chunk[:10])
            bpp = 4 if ctype == 6 else 3
        elif typ == b"IDAT":
            idat += chunk
        pos += 12 + ln
    raw = zlib.decompress(idat)
    stride = w * bpp
    fila = raw[y * (stride + 1) + 1: y * (stride + 1) + 1 + stride]
    return (fila[x * bpp], fila[x * bpp + 1], fila[x * bpp + 2])


def es_azul(c):
    """Agua normal (azul oscuro) o destello (azul claro): el azul manda."""
    r, g, b = c
    return b > 120 and b > r + 60 and b > g + 60


def color_agua():
    """Color del agua en el primer punto AZUL que se encuentre, o None.

    160x140 fijos no sirve: el cauce se ensancha y se estrecha, y ese pixel cae
    sobre cesped en cuanto el rio se abre. Se barre una columna central hasta
    dar con agua; si no hay (el avion esta sobre tierra) se devuelve None y la
    muestra no cuenta. """
    img = get("/screen.png")
    for y in (60, 80, 100, 120, 140, 160):
        for x in (160, 152, 168, 144, 176):
            c = png_pixel(img, x, y)
            if es_azul(c):
                return c
    return None


def main():
    with open("output/game.bin", "rb") as f:
        post("/run?name=GAME.BIN&addr=0800", f.read())
    time.sleep(1.2)

    u = socket.create_connection(("127.0.0.1", 4000), timeout=5)
    u.sendall(b" ")
    time.sleep(0.5)

    base = color_agua()
    print("agua normal: RGB%s" % (base,))

    # Forzar el choque: empujar a la derecha sin parar hasta morir.
    u.sendall(b"d" * 40)
    flashes = 0
    muestras = 0
    t0 = time.time()
    while time.time() - t0 < 12:
        c = color_agua()
        if c is not None:
            muestras += 1
            if base is not None and c != base:
                flashes += 1
                print("  destello: RGB%s -> RGB%s" % (base, c))
        time.sleep(0.05)
    u.close()

    print("muestras=%d  con color de agua DISTINTO=%d" % (muestras, flashes))
    if flashes == 0:
        print("OK: morir contra la orilla no destella el agua")
        return 0
    print("FALLO: el agua cambio de color sin haber puente")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
