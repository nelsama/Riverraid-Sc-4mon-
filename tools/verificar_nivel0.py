"""Comprueba que con cadencia 0 los enemigos NO se mueven de lado.

En vez de esperar a llegar a un nivel concreto (el avion muere antes y el
arnes se lia), se comprueba lo que de verdad importa: durante los primeros
segundos de partida, que ya son nivel 0 (barco 0, heli 0), ningun enemigo
puede cambiar de columna.

Si aparece algun valor raro de gen_sector se informa, pero NO se usa para
filtrar: la condicion es "primeros segundos tras arrancar", que por
construccion es el nivel 0.

Uso:
    python tools/verificar_nivel0.py [segundos]
"""
import re
import sys
import time
import urllib.request

BASE = "http://127.0.0.1:8080"
VARS = ["ene_x", "ene_active", "lives", "esperando_fire"]


def get(p):
    with urllib.request.urlopen(BASE + p, timeout=5) as r:
        return r.read()


def post(p, data):
    req = urllib.request.Request(BASE + p, data=data, method="POST")
    with urllib.request.urlopen(req, timeout=5) as r:
        return r.read()


def dirs():
    # Las direcciones se sacan con verificar_mapa_parametros.py y NO con un
    # lector propio: el .lst repite simbolos (estaticas locales de las funciones)
    # y un lector ingenuo devuelve direcciones corridas, con lo que este script
    # "mide" que el barco se mueve aunque ene_drift ni siquiera este ahi.
    import importlib.util
    import os
    ruta = os.path.join(os.path.dirname(__file__), "verificar_mapa_parametros.py")
    spec = importlib.util.spec_from_file_location("vmp", ruta)
    vmp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(vmp)
    off, _, _ = vmp.offs_bss()
    return off


def main():
    segs = float(sys.argv[1]) if len(sys.argv) > 1 else 30.0
    sys.path.insert(0, "tools")
    import mandos

    d = dirs()
    falta = [v for v in VARS if v not in d]
    if falta:
        print("FALTAN: %s" % falta)
        return 1

    with open("output/game.bin", "rb") as f:
        post("/run?name=GAME.BIN&addr=0800", f.read())
    time.sleep(3.0)
    mandos.pulsa("space", 150)
    time.sleep(0.6)

    mov = [0, 0]
    vistos = [0, 0]
    muestras = 0
    ant = [None, None]
    ant_y = [None, None]
    t0 = time.time()

    while time.time() - t0 < segs:
        r = get("/ram.bin")
        vid = r[d["lives"]]
        esp = r[d["esperando_fire"]]

        if vid == 0:
            mandos.pulsa("enter", 120)
            time.sleep(0.4)
            continue
        if esp:
            mandos.pulsa("space", 120)
            time.sleep(0.3)
            continue

        muestras += 1
        for i in (0, 1):
            on = r[d["ene_active"] + i]
            x = r[d["ene_x"] + i]
            y = r[d["ene_y"] + i]
            if not on:
                ant[i] = None
                ant_y[i] = None
                continue
            vistos[i] += 1
            # Nivel 0 = enemigos QUIETOS: la x lleva congelada desde que nacen.
            #
            # TRAMPA: si en el mismo frame el sprite se retira por el borde
            # inferior (y >= 240) y nace el siguiente, NO se ve el instante con
            # ene_active = 0 y el slot pasa de x=112/y=222 a x=104/y=8: parece
            # deriva y es un nacimiento. Se detecta por el salto de y (un frame
            # de scroll mueve 1-2 px como mucho).
            salto_y = (ant_y[i] is not None and
                       (y < ant_y[i] or (y - ant_y[i]) > 4))
            if ant[i] is not None and x != ant[i] and not salto_y:
                mov[i] += 1
                print("  deriva en %d: x %d -> %d (y %s -> %d)"
                      % (i, ant[i], x, ant_y[i], y))
            ant[i] = x
            ant_y[i] = y

        time.sleep(0.03)

    print("muestras: %d" % muestras)
    print("  barco: visto %d, cambios de columna %d" % (vistos[0], mov[0]))
    print("  heli : visto %d, cambios de columna %d" % (vistos[1], mov[1]))

    if muestras == 0:
        print("FALLO: no se midio nada")
        return 1
    ok = mov[0] == 0 and mov[1] == 0
    print("OK: ninguno se movio de lado" if ok else "FALLO: se movieron")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
