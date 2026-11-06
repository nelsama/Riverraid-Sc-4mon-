"""Comprueba que 'q' sale del juego en la PANTALLA DE ESPERA.

Antes solo funcionaba jugando o en GAME OVER: durante la espera de FIRE,
fire_pressed() se comia la tecla.

Como se mide: se mira el PC del 6502. Si el programa termina, main() hace
fv_clear_oam(), apaga el scroll y hace return, asi que el PC se va del rango
del juego ($0800-$3DFF) y vuelve al monitor.

  Uso: python tools/probar_tecla_q.py
"""
import re
import socket
import sys
import time
import urllib.request

BASE = "http://127.0.0.1:8080"


def get(p):
    return urllib.request.urlopen(BASE + p, timeout=10).read()


def tecla(c):
    """Manda UN caracter al juego por el UART emulado (TCP 4000).

    OJO: no sirve tools/mandos.py: ese inyecta teclas de la VENTANA y su mapa
    solo tiene las del joystick (a/d/w/s/space...). El juego lee el teclado por
    el UART, asi que las letras sueltas (q, r) van por aqui.
    """
    s = socket.create_connection(("127.0.0.1", 4000), timeout=5)
    s.sendall(c.encode())
    time.sleep(0.08)
    s.close()


def pc():
    t = get("/state.txt").decode(errors="replace")
    m = re.search(r"PC=([0-9A-F]{4})", t)
    return int(m.group(1), 16) if m else None


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
            actual = ms.group(1); continue
        mm = pat.match(linea)
        if not mm or actual not in seg or mm.group(2) in d: continue
        ini, fin = seg[actual]
        dir_ = ini + int(mm.group(1), 16)
        if dir_ <= fin:
            d[mm.group(2)] = dir_
    return d


d = offs()
data = open("output/game.bin", "rb").read()
req = urllib.request.Request(BASE + "/run?name=GAME.BIN&addr=0800",
                             data=data, method="POST")
urllib.request.urlopen(req, timeout=15).read()

for _ in range(90):
    if get("/ram.bin")[d["esperando_fire"]] == 1:
        break
    time.sleep(0.15)

print("en la PANTALLA DE ESPERA (no se ha pulsado FIRE)")
print("  esperando_fire =", get("/ram.bin")[d["esperando_fire"]])

antes = pc()
print("  PC antes de 'q' = %04X" % antes)

mandos_off = True
tecla("q")
time.sleep(1.0)

despues = pc()
print("  PC despues de 'q' = %04X" % despues)

sigue = get("/ram.bin")[d["esperando_fire"]]
print("  esperando_fire despues =", sigue)

# Si salio, el juego ya no corre: la RAM deja de cambiar.
time.sleep(0.6)
print("  PC 600 ms mas tarde = %04X" % pc())

if despues < 0x0800 or despues > 0x3FFF:
    print("\nOK: el programa salio del juego (PC fuera de $0800-$3FFF)")
    return_ok = True
else:
    print("\nFALLO: el PC sigue dentro del juego")
    return_ok = False

sys.exit(0 if return_ok else 1)
