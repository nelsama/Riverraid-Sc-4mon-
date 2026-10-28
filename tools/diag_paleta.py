#!/usr/bin/env python3
"""diag_paleta.py - Demuestra el bug del puntero de paleta (entrada 31).

Requisitos: emulador vivo y en el prompt del monitor (sin el juego cargado).

Toda la prueba se hace con el monitor de la ROM, que ya sabe escribir bytes:
  W dir val   -> escribe un byte (sirve para la VRAM: $D810 lo, $D811 hi,
                 $D812 dato)
  D dir n     -> volcado para verificar

Pasos:
  1. Tilemap entero = tile 1 (patron solido), atributos = $E3
     (paleta 3, color 3) y los 32 sprites apagados.
  2. FASE A: escribir SOLO la entrada 31 de la paleta con verde ($0F0).
  3. FASE B: escribir SOLO la entrada 0 (color 3 de la paleta 0) con azul.

En el RTL el puntero de paleta es de 5 bits (video_bus.vhd), asi que la
entrada 31 no se puede direccionar: al escribirla el puntero se queda en 0 y
se pisa la entrada 0. El fondo no cambia en A.
"""
import socket
import sys
import time
import zlib

UART = ("127.0.0.1", 4000)
DEBUG = "http://127.0.0.1:8080"
D_ADDR_LO, D_ADDR_HI, D_DATA = 0xD810, 0xD811, 0xD812
D_PAL_PTR, D_PAL_LO, D_PAL_HI = 0xD813, 0xD814, 0xD815

ENTRADA = 31
VERDE, AZUL = 0x0F0, 0x00F


def abrir():
    """Una unica conexion: cada conexion nueva resetea el monitor."""
    s = socket.create_connection(UART, timeout=120)
    s.settimeout(120)
    time.sleep(0.6)
    return s


def sesion(s, comandos, pausa=0.0, leer=1.0):
    """Manda todos los comandos y despues lee la respuesta entera."""
    for c in comandos:
        s.sendall(c.encode() + b"\r")
        if pausa:
            time.sleep(pausa)
    time.sleep(leer)
    return _leer(s, 0.15).decode("latin-1", errors="replace")


def _leer(s, espera):
    s.settimeout(espera)
    out = b""
    try:
        while True:
            d = s.recv(65536)
            if not d:
                break
            out += d
    except OSError:
        pass
    return out


def cmds_banco(area, valor):
    """Comandos para dejar un banco de 2048 bytes (tilemap o atributos)."""
    out = []
    for p in range(8):
        out.append(f"W {D_ADDR_HI:04X} {(area | p):02X}")
        for i in range(256):
            out.append(f"W {D_ADDR_LO:04X} {i:02X}")
            out.append(f"W {D_DATA:04X} {valor:02X}")
    return out


def cmds_paleta(entrada, rgb):
    return [f"W {D_PAL_PTR:04X} {entrada & 0x1F:02X}",
            f"W {D_PAL_LO:04X} {rgb & 0xFF:02X}",
            f"W {D_PAL_HI:04X} {(rgb >> 8) & 0x0F:02X}"]


def png_px(png):
    import struct
    pos, idat, w_, h_, ctype = 8, b"", 0, 0, 2
    while pos < len(png):
        ln = struct.unpack(">I", png[pos:pos + 4])[0]
        typ = png[pos + 4:pos + 8]
        if typ == b"IHDR":
            w_, h_, _d, ctype = struct.unpack(">IIBB", png[pos + 8:pos + 18])
        elif typ == b"IDAT":
            idat += png[pos + 8:pos + 8 + ln]
        pos += 12 + ln
    raw = zlib.decompress(idat)
    bpp = 4 if ctype == 6 else 3
    stride = w_ * bpp
    prev = bytearray(stride)
    px = []
    p = 0
    for _ in range(h_):
        f = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 0xFF
            elif f == 2:
                line[i] = (line[i] + b) & 0xFF
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 0xFF
            elif f == 4:
                q = a + b - c
                pa, pb, pc = abs(q - a), abs(q - b), abs(q - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        prev = line
        px.append([tuple(line[i * bpp:i * bpp + 3]) for i in range(w_)])
    return w_, h_, px


def color_fondo(etiqueta=None):
    from collections import Counter
    import urllib.request
    with urllib.request.urlopen(DEBUG + "/screen.png?scale=1&t=" + str(time.time()),
                                timeout=15) as r:
        png = r.read()
    if etiqueta:
        open(etiqueta, "wb").write(png)
    w_, h_, px = png_px(png)
    c = Counter()
    for y in range(200, 460, 5):
        for x in range(40, w_ - 40, 5):
            c[px[y][x]] += 1
    return c.most_common(1)[0][0]


def rgb444(v):
    return (((v >> 8) & 0xF) * 0x11, ((v >> 4) & 0xF) * 0x11, (v & 0xF) * 0x11)


def main():
    print("[1/3] pantalla de prueba: tilemap=1, atributos=$E3, sprites off")
    s = abrir()
    sesion(s, cmds_banco(0x00, 0x01))
    sesion(s, cmds_banco(0x40, 0xE3))
    sesion(s, [f"W {0xC000 + i * 5 + 1:04X} F8" for i in range(32)])
    base = color_fondo("doc/diag_pal_00.png")
    print("    fondo base:", base)

    print(f"[2/3] fase A: SOLO entrada {ENTRADA} = verde")
    sesion(s, cmds_paleta(ENTRADA, VERDE), leer=0.4)
    a = color_fondo("doc/diag_pal_A.png")
    print("    fondo tras A:", a)

    print("[3/3] fase B: SOLO entrada 0 (color 3 de paleta 0) = azul")
    sesion(s, cmds_paleta(0, AZUL), leer=0.4)
    b = color_fondo("doc/diag_pal_B.png")
    s.close()
    print("    fondo tras B:", b)

    print()
    print("RESULTADO")
    print(f"  A esperado si la entrada {ENTRADA} funcionase: {rgb444(VERDE)}")
    print(f"  A observado: {a}")
    print(f"  B observado: {b}")
    print("  => VEREDICTO:", "entrada 31 NO escribible"
          if a != rgb444(VERDE) else "entrada 31 escribible")


if __name__ == "__main__":
    main()
