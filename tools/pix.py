#!/usr/bin/env python3
"""pix.py - Volcado de pixeles de columnas concretas en ASCII, desde el PNG.

  Uso: python tools/pix.py x0 x1 [y0 y1]

L=cesped  a=arena  W=agua  #=negro  ?=otro. Se imprime un caracter por pixel
de juego (el PNG viene a escala 2 si se pide con ?scale=2).
"""
import struct
import sys
import time
import urllib.request
import zlib

BASE = "http://127.0.0.1:8080"
LEG = {
    (0x00, 0xAA, 0x00): 'L',
    (0x00, 0x77, 0x00): '.',
    (0xDD, 0x88, 0x33): 'a',
    (0x22, 0x66, 0xFF): 'W',
    (0x11, 0x44, 0xEE): 'w',
    (0x00, 0x00, 0x00): '#',
}


def fetch():
    with urllib.request.urlopen(BASE + "/screen.png?t=" + str(time.time()), timeout=10) as r:
        return r.read()


def decode(png):
    pos = 8
    w = h = ctype = 0
    idat = b""
    while pos < len(png):
        ln = struct.unpack(">I", png[pos:pos + 4])[0]
        typ = png[pos + 4:pos + 8]
        data = png[pos + 8:pos + 8 + ln]
        if typ == b"IHDR":
            w, h, _b, ctype = struct.unpack(">IIBB", data[:10])
        elif typ == b"IDAT":
            idat += data
        pos += 12 + ln
    ch = 4 if ctype == 6 else 3
    raw = zlib.decompress(idat)
    out = bytearray()
    stride = w * ch
    prev = bytearray(stride)
    p = 0
    for _ in range(h):
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
        out += line
        prev = line
    return w, h, ch, out


x0 = int(sys.argv[1])
x1 = int(sys.argv[2])
y0 = int(sys.argv[3]) if len(sys.argv) > 3 else 0
y1 = int(sys.argv[4]) if len(sys.argv) > 4 else 120
w, h, ch, px = decode(fetch())
print("PNG %dx%d; x=%d..%d y=%d..%d" % (w, h, x0, x1, y0, y1))
for y in range(y0, min(y1, h)):
    s = ''
    for x in range(x0, min(x1, w)):
        o = (y * w + x) * ch
        s += LEG.get((px[o], px[o + 1], px[o + 2]), '?')
    print("%3d %s" % (y, s))
