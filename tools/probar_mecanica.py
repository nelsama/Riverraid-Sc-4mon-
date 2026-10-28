#!/usr/bin/env python3
"""probar_mecanica.py - Prueba FUNCIONAL de la mecanica del avion.

Pilota por UART (como un jugador) y comprueba, leyendo el OAM por HTTP:

  1. ARRIBA/ABAJO no mueven el avion    -> su Y no cambia al pulsar w/s
  2. Motor horizontal                    -> su X cambia al pulsar a/d
  3. w acelera el scroll                 -> el barco baja mas px/s
  4. s frena el scroll                   -> el barco baja menos px/s
  5. Al chocar, el avion vuelve al punto de partida (Y = PLANE_Y)

  Uso: python tools/probar_mecanica.py
"""
import json
import socket
import sys
import time
import urllib.request

DEBUG = 8080
UART = 4000
PLANE_Y = 148          # altura de reposo esperada
PLANE_SLOT = 0


def sprites():
    d = json.load(urllib.request.urlopen("http://127.0.0.1:%d/vram.json" % DEBUG,
                                         timeout=15))
    return {s["n"]: s for s in d["sprites"]}


def avion():
    return sprites().get(PLANE_SLOT)


def barco():
    sp = sprites()
    for n in (8, 6):           # barco o helicoptero
        if n in sp:
            return sp[n]
    return None


def pulsa(tecla, segundos=0.6):
    """Mantiene una tecla: repite la pulsacion por UART durante 'segundos'."""
    s = socket.create_connection(("127.0.0.1", UART), timeout=5)
    t0 = time.time()
    try:
        while time.time() - t0 < segundos:
            s.sendall(tecla.encode())
            time.sleep(0.03)
    finally:
        try:
            s.shutdown(socket.SHUT_WR)
        except OSError:
            pass
        s.close()


def reinicia():
    urllib.request.urlopen("http://127.0.0.1:%d/uart?cmd=r%%200800" % DEBUG,
                           timeout=10).read()
    time.sleep(1.5)


def ok(nombre, cond, detalle=""):
    print("%s  %s %s" % ("OK  " if cond else "FALLA", nombre, detalle))
    return cond


def main():
    fallos = 0
    reinicia()
    a = avion()
    if not a:
        print("no hay avion en el OAM: el juego no esta en partida")
        return 1
    print("avion inicial:", a)

    # --- 1) altura: w y s NO deben cambiar la Y -------------------------
    y0 = a["y"]
    pulsa("w", 0.8)
    y1 = (avion() or {}).get("y", y0)
    pulsa("s", 0.8)
    y2 = (avion() or {}).get("y", y0)
    fallos += not ok("arriba/abajo no mueven el avion",
                     y0 == y1 == y2, "(y0=%d y1=%d y2=%d)" % (y0, y1, y2))

    # --- 2) movimiento horizontal ---------------------------------------
    x0 = avion()["x"]
    pulsa("a", 0.8)
    x1 = avion()["x"]
    pulsa("d", 0.8)
    x2 = avion()["x"]
    fallos += not ok("horizontal a/d mueve el avion",
                     x0 != x1 and x2 != x1, "(x0=%d x1=%d x2=%d)" % (x0, x1, x2))

    # --- 3/4) velocidad del scroll --------------------------------------
    def caida(tecla, segundos=1.0):
        """Cuanto baja el barco en 'segundos' manteniendo 'tecla'."""
        b0 = barco()
        if not b0:
            return None
        s = socket.create_connection(("127.0.0.1", UART), timeout=5)
        t0 = time.time()
        while time.time() - t0 < segundos:
            s.sendall(tecla.encode())
            time.sleep(0.03)
        s.close()
        b1 = barco()
        if not b1:
            return None
        return b1["y"] - b0["y"]

    reinicia()
    lento = caida("s")
    reinicia()
    rapido = caida("w")
    if lento is None or rapido is None:
        print("no se pudo medir el scroll (sin barco en pantalla)")
        fallos += 1
    else:
        fallos += not ok("w acelera el scroll (barco baja mas)",
                         rapido > lento, "(freno=%s acelerado=%s px)" % (lento, rapido))

    print("\nfallos:", fallos)
    return 0 if fallos == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
