#!/usr/bin/env python3
"""snap.py - Capturas de la ventana del emulador (lo que se ve en el LED/HDMI)
por el puerto de depuracion, sin ventana de Windows de por medio.

  Uso: python tools/snap.py <destino.png> [n]

Hace n capturas separadas ~0.6 s (como el visor HTML) y se queda con la mas
repetida entre ellas: si la pantalla esta estable, esa es la buena. Solo usa la
biblioteca estandar (zlib + struct) para no depender de Pillow.
"""
import struct
import sys
import time
import urllib.request
import zlib

BASE = "http://127.0.0.1:8080"


def fetch_png():
    with urllib.request.urlopen(BASE + "/screen.png?scale=2&t=" + str(time.time()),
                                timeout=10) as r:
        return r.read()


def decode(png):
    """Decodifica un PNG RGB/RGBA de 8 bits (sin filtros raros)."""
    pos = 8
    w = h = 0
    idat = b""
    ctype = 0
    while pos < len(png):
        ln = struct.unpack(">I", png[pos:pos + 4])[0]
        typ = png[pos + 4:pos + 8]
        data = png[pos + 8:pos + 8 + ln]
        if typ == b"IHDR":
            w, h, bitd, ctype = struct.unpack(">IIBB", data[:10])
        elif typ == b"IDAT":
            idat += data
        pos += 12 + ln
    ch = 4 if ctype == 6 else 3
    raw = zlib.decompress(idat)
    stride = w * ch
    out = bytearray(h * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        if f == 1:
            for i in range(ch, stride):
                line[i] = (line[i] + line[i - ch]) & 0xFF
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif f == 3:
            for i in range(stride):
                a = line[i - ch] if i >= ch else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif f == 4:
            for i in range(stride):
                a = line[i - ch] if i >= ch else 0
                b = prev[i]
                c = prev[i - ch] if i >= ch else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return w, h, ch, out


def colour_hist(w, h, ch, px):
    hist = {}
    for y in range(0, h, 4):
        row = y * w * ch
        for x in range(0, w, 4):
            o = row + x * ch
            c = (px[o], px[o + 1], px[o + 2])
            hist[c] = hist.get(c, 0) + 1
    return hist


def main():
    dest = sys.argv[1] if len(sys.argv) > 1 else "docs/img/seq/snap.png"
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 6
    shots = []
    for _ in range(n):
        png = fetch_png()
        w, h, ch, px = decode(png)
        shots.append((colour_hist(w, h, ch, px), png, w, h, ch, px))
        time.sleep(0.6)
    # La captura con MAS colores distintos es la que tiene el buffer limpio.
    best = max(shots, key=lambda s: sum(1 for k, v in s[0].items() if v > 20))
    hists = [sum(1 for k, v in s[0].items() if v > 20) for s in shots]
    print("colores por captura:", hists)
    _, png, w, h, ch, px = best
    open(dest, "wb").write(png)
    print("guardada", dest, "%dx%d" % (w, h))
    # Mosaico de colores dominantes, para diagnosticar de un vistazo.
    for c, v in sorted(best[0].items(), key=lambda kv: -kv[1])[:6]:
        print("  #%02X%02X%02X  %d" % (c[0], c[1], c[2], v))


if __name__ == "__main__":
    main()
