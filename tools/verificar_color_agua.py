"""Comprueba que tras el destello el agua vuelve al MISMO azul de antes.

El bug: despues del destello el agua se quedaba VERDE (#007700, que es el
indice 2 de PAL_ISLAND). Eso pasa si el destello escribe en entradas de otra
paleta (auto-incremento mal usado).

IMPORTANTE: hay que comparar EL MISMO pixel antes y despues. El agua usa dos
tiles (0x11 y 0x12) con indices de color distintos, asi que "el primer azul que
se encuentre" puede ser uno u otro y da falsos negativos.

Uso:
    python tools/verificar_color_agua.py
"""
import struct
import sys
import time
import zlib

sys.path.insert(0, "tools")
from probar_flash_orilla import es_azul, get, png_pixel, post  # noqa: E402


def es_verde(c):
    r, g, b = c
    return g > 90 and g > r + 50 and g > b + 50


def busca_pixel_agua(img):
    """Devuelve (x, y) de un pixel de agua estable, o None."""
    for y in (70, 90, 110, 130, 150):
        for x in (160, 152, 168):
            if es_azul(png_pixel(img, x, y)):
                return (x, y)
    return None


def main():
    with open("output/game.bin", "rb") as f:
        post("/run?name=GAME.BIN&addr=0800", f.read())
    time.sleep(2.5)

    img = get("/screen.png")
    pos = busca_pixel_agua(img)
    if pos is None:
        print("FALLO: no se encuentra agua en pantalla")
        return 1
    x, y = pos
    antes = png_pixel(img, x, y)
    print("agua en (%d,%d) en reposo: RGB%s" % (x, y, antes))

    # Forzar el destello disparando y esperando a que termine.
    import socket
    u = socket.create_connection(("127.0.0.1", 4000), timeout=5)
    u.sendall(b" ")
    for _ in range(6):
        time.sleep(0.2)
        u.sendall(b" ")
    u.close()
    time.sleep(1.5)

    despues = png_pixel(get("/screen.png"), x, y)
    print("agua en (%d,%d) despues:   RGB%s" % (x, y, despues))

    if es_verde(despues):
        print("FALLO: el agua se quedo VERDE (paleta de isla tocada)")
        return 1
    if despues == antes:
        print("OK: el agua vuelve exactamente a su azul original")
        return 0
    if es_azul(despues):
        print("OK: sigue siendo azul (pixel distinto por el oleaje)")
        return 0
    print("FALLO: el agua no es azul: RGB%s" % (despues,))
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
