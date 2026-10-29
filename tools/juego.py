#!/usr/bin/env python3
"""juego.py - Arranca la partida de forma fiable y expone utilidades.

Arrancar es fragil: el juego muestrea el FIRE una vez por frame y la ventana
pierde el foco entre mensajes, asi que un solo pulsado corto se pierde a
menudo. Aqui se pulsa FIRE en bucle hasta que el scroll se mueve de verdad.
"""
import re
import time
import urllib.request

import mandos

DBG = "http://127.0.0.1:8080"
MUERTE = 0x03E0          # DBG_MUERTE (ver game.c)
CAUSA = {0: "vivo", 1: "ORILLA/ISLA", 2: "ENEMIGO", 3: "SIN COMBUSTIBLE"}


def ram():
    return urllib.request.urlopen(DBG + "/ram.bin", timeout=5).read()


def estado():
    t = urllib.request.urlopen(DBG + "/state.txt", timeout=5).read().decode()
    sy = int(re.search(r"scroll: x=0 y=(\d+)", t).group(1))
    m = re.search(r"sprite\s+0: X=\s*(\d+) Y=\s*(\d+)", t)
    return sy, (int(m.group(1)), int(m.group(2))) if m else None


def causa():
    return ram()[MUERTE]


def arrancar(intentos=12, verbose=False):
    """Pulsa FIRE hasta que el scroll se mueva. Devuelve True si arranco."""
    for i in range(intentos):
        s1, _ = estado()
        mandos.abajo("space")
        time.sleep(0.30)
        mandos.arriba("space")
        time.sleep(0.35)
        s2, _ = estado()
        s3, _ = estado()
        if s2 != s3:
            if verbose:
                print("arrancado en el intento %d (scroll %d)" % (i + 1, s3))
            time.sleep(0.2)
            return True
    return False


def mover(dirn, seg):
    """Mantiene una direccion durante seg segundos. Devuelve la causa si murio."""
    mandos.abajo(dirn)
    fin = time.time() + seg
    muerto = 0
    try:
        while time.time() < fin:
            c = causa()
            if c:
                muerto = c
                break
            time.sleep(0.03)
    finally:
        mandos.arriba(dirn)
    return muerto
