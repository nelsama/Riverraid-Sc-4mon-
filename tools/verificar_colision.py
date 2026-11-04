#!/usr/bin/env python3
"""verificar_colision.py - Comprueba que lo PINTADO y lo COLISIONABLE coinciden.

El bug "choque con algo invisible" aparece cuando el tilemap y el perfil del
canal (river_l/r/island_l, que es lo que consulta celda_agua) no dicen lo
mismo. Este script reproduce el generador fila a fila y, para cada celda,
compara:

    lo que se pinta   -> tile (agua / costa / isla / cesped / viga)
    lo que colisiona  -> celda_agua()

Si una celda esta pintada como AGUA pero celda_agua() dice TIERRA, el avion
muere sin chocar contra nada visible. Y al reves: atraviesa tierra.

  Uso: python tools/verificar_colision.py [filas]
"""
import sys
import importlib.util
import os

SPEC = os.path.join(os.path.dirname(__file__), "simular_cauce.py")
_spec = importlib.util.spec_from_file_location("sim", SPEC)
sim = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(sim)

ZONE_L, ZONE_R = sim.ZONE_L, sim.ZONE_R
ISLAND_W = sim.ISLAND_W
ISLE_MIN = sim.ISLE_MIN
GAP_L, GAP_R = 19, 20


def perfil(g, n):
    """Devuelve (l, r, il, ir, viga) igual que generate_world_row."""
    l, r, il, ir, viga, fu, bi, sec = g.row()
    return l, r, il, ir, viga


def tile_pintado(l, r, il, ir, viga, col):
    """Que se PINTA en esta celda (misma logica que el bucle de game.c)."""
    if col < l or col > r:
        return "cesped"
    if col == l or col == r:
        return "costa"
    if il is not None and il <= col <= ir:
        if col == il or col == ir:
            return "costa"
        return "cesped"          # isla: cesped en el centro
    if viga and (col < GAP_L or col > GAP_R):
        return "viga"
    return "agua"


def colisiona_agua(l, r, il, ir, viga, col):
    """Que dice celda_agua(): usa el borde DERECHO REAL de la isla."""
    if col <= l or col >= r:
        return False
    if il is not None:
        if il <= col <= ir:
            return False
    if viga and (col < GAP_L or col > GAP_R):
        return False
    return True


def verificar_antigua(filas):
    """Modo --viejo: reproduce el bug (colision usa il + ISLAND_W - 1).

    Sirve para demostrar que la herramienta DETECTA el fallo: este modo tiene
    que dar discrepancias.
    """
    g = sim.Gen()
    errores = 0
    for n in range(filas):
        l, r, il, ir, viga = perfil(g, n)
        for col in range(ZONE_L, ZONE_R + 1):
            pint = tile_pintado(l, r, il, ir, viga, col)
            if col <= l or col >= r:
                agua = False
            elif il is not None and il <= col <= (il + ISLAND_W - 1):
                agua = False
            elif viga and (col < GAP_L or col > GAP_R):
                agua = False
            else:
                agua = True
            if (pint == "agua") != agua:
                errores += 1
    return errores


def main():
    filas = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
    if "--viejo" in sys.argv:
        e = verificar_antigua(filas)
        print("MODO VIEJO (celda_agua usaba il + GEN_ISLAND_W - 1):")
        print("  discrepancias: %d  <- asi se detecta el choque invisible" % e)
        return 1 if e else 0
    g = sim.Gen()
    errores = []
    for n in range(filas):
        l, r, il, ir, viga = perfil(g, n)
        for col in range(ZONE_L, ZONE_R + 1):
            pint = tile_pintado(l, r, il, ir, viga, col)
            agua = colisiona_agua(l, r, il, ir, viga, col)

            # La regla: pintado "agua" <=> colisiona agua.
            # "viga" tambien es solida, igual que "costa" y "cesped".
            pint_es_agua = (pint == "agua")
            if pint_es_agua != agua:
                errores.append((n, col, l, r, il, ir, pint, agua,
                                g.block_idx, g.sector))

    print("filas simuladas: %d   celdas: %d" % (filas, filas * (ZONE_R - ZONE_L + 1)))
    print("DISCREPANCIAS pintado vs colision: %d" % len(errores))
    if errores:
        print()
        print("fila  col   l   r  isla        pintado   colision  blk sec")
        for e in errores[:20]:
            n, col, l, r, il, ir, pint, agua, bi, sec = e
            isla = "%d-%d" % (il, ir) if il is not None else "-"
            print("%4d %4d %3d %3d  %-9s %-8s %-8s %3d %3d" % (
                n, col, l, r, isla, pint,
                "agua" if agua else "tierra", bi, sec))
        if len(errores) > 20:
            print("... y %d mas" % (len(errores) - 20))

    # Resumen por tipo de fallo
    invisibles = sum(1 for e in errores if e[6] != "agua" and e[7])
    atravesable = sum(1 for e in errores if e[6] == "agua" and not e[7])
    print()
    print("  pintado TIERRA pero colisiona AGUA (se atraviesa): %d" % atravesable)
    print("  pintado AGUA pero colisiona TIERRA (CHOQUE INVISIBLE): %d" % invisibles)
    return 1 if errores else 0


if __name__ == "__main__":
    sys.exit(main())
