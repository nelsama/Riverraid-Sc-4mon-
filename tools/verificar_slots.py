#!/usr/bin/env python3
"""verificar_slots.py - Detecta SPRITES QUE COMPARTEN SLOT DE OAM en game.c.

Un bug real: al replanificar los slots, SPR_HELI quedo en 15 y el pool de
balas ocupa 14 y 15. El segundo disparo escribia OAM en el slot del
helicoptero y el enemigo se convertia en una "barra roja" (el patron de la
bala) en mitad de la pantalla.

Este script lee los #define SPR_* de game.c, expande las familias de sprites
contiguos (SPR_HUD_BASE + i, SPR_BULLET + i, ...) y avisa si dos cosas
distintas caen en el mismo slot.

Uso: python tools/verificar_slots.py
"""
import re
import sys
from pathlib import Path

GAME_C = Path(__file__).resolve().parent.parent / "game.c"

# nombre -> (slot base, cuantos sprites contiguos usa)
# El barco y el helicoptero COMPARTEN slot a proposito: draw_boat() dibuja uno
# u otro, nunca los dos. Se comprueba con la clave SPR_ENEMIGO.
FAMILIAS = {
    "SPR_HUD_BASE": 4,     # puntos 0..3
    "SPR_HUD_FUEL": 0,     # se expande con HUD_FUEL_W
    "SPR_HUD_LIVES": 2,    # icono + numero
    "SPR_PLANE": 1,
    "SPR_BOOM": 1,         # explosion / bomba: un sprite, 3 patrones
    "SPR_ENEMIGO": 1,      # barco o helicoptero
    "SPR_BULLET": 2,       # pool de 2 balas
    "SPR_FUEL": 1,
}


def defines(texto):
    """Solo los #define cuyo valor es un entero (SPR_FUEL_PAL no lo es)."""
    out = {}
    for m in re.finditer(r"^#define\s+(SPR_\w+|HUD_FUEL_W)\s+\(?(\w+)\)?",
                         texto, re.M):
        try:
            out[m.group(1)] = int(m.group(2), 0)
        except ValueError:
            pass
    return out


def main():
    texto = GAME_C.read_text(encoding="utf-8", errors="replace")
    d = defines(texto)
    if "HUD_FUEL_W" not in d:
        print("no encuentro HUD_FUEL_W en game.c")
        return 1
    FAMILIAS["SPR_HUD_FUEL"] = d["HUD_FUEL_W"]
    d["SPR_ENEMIGO"] = d["SPR_BOAT"]

    # Los dos tipos de enemigo tienen que caer en el mismo slot.
    if d["SPR_BOAT"] != d["SPR_HELI"]:
        print(f"barco ({d['SPR_BOAT']}) y helicoptero ({d['SPR_HELI']}) "
              "deberian compartir slot")
        return 1

    ocupados = {}
    for nombre, ancho in FAMILIAS.items():
        base = d[nombre]
        for i in range(ancho):
            slot = base + i
            if slot in ocupados:
                print(f"SOLAPE en slot {slot}: {ocupados[slot]}[+{i}] y {nombre}[+{i}]")
            ocupados.setdefault(slot, []).append(f"{nombre}[{i}]")

    # 32 slots de OAM.
    fuera = [s for s in ocupados if s > 31]
    print(f"slots usados: {len(ocupados)} de 32   ({min(ocupados)}..{max(ocupados)})")
    if fuera:
        print(f"FUERA DEL OAM: {fuera}")
        return 1
    if any(len(v) > 1 for v in ocupados.values()):
        return 1
    print("OK: ningun slot compartido")
    return 0


if __name__ == "__main__":
    sys.exit(main())
