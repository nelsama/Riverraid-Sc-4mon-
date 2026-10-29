#!/usr/bin/env python3
"""mandos.py - Utilidad para inyectar teclas en la ventana del emulador.

IMPORTANTE (coste de media hora averiguarlo): el lParam de WM_KEYDOWN tiene que
ser

    (scancode | 0x0100 si es tecla extendida) << 16 | 1

SIN los bits de repeticion 0x00010001 que se usan habitualmente. Con esos bits
SDL2 traduce el scancode CORRIDO UNO y "a" llega como "s" (DOWN): las flechas
laterales no se pueden pulsar.

Mapa del emulador (src/emu_window.cpp):
    SDLK_LEFT / SDLK_a   -> IZQUIERDA
    SDLK_RIGHT / SDLK_d  -> DERECHA
    SDLK_UP   / SDLK_w   -> ARRIBA
    SDLK_DOWN / SDLK_s   -> ABAJO
    SDLK_SPACE / SDLK_z / SDLK_LCTRL / SDLK_RETURN -> FUEGO
"""
import ctypes
import time

TITULO = "Emulador FPGA 6502 - Tang Nano 9K"

# nombre: (vk, scancode, extendida)
TECLAS = {
    "left":  (0x25, 0x4B, True),
    "right": (0x27, 0x4D, True),
    "up":    (0x26, 0x48, True),
    "down":  (0x28, 0x50, True),
    "a":     (0x41, 0x1E, False),
    "d":     (0x44, 0x20, False),
    "w":     (0x57, 0x11, False),
    "s":     (0x53, 0x1F, False),
    "space": (0x20, 0x39, False),
    "z":     (0x5A, 0x2C, False),
    "ctrl":  (0x11, 0x1D, False),
    "enter": (0x0D, 0x1C, False),
}


def _ventana():
    u = ctypes.windll.user32
    h = u.FindWindowW(None, TITULO)
    u.SetForegroundWindow(h)
    time.sleep(0.05)
    return u, h


def abajo(nombre):
    vk, sc, ext = TECLAS[nombre]
    u, h = _ventana()
    u.PostMessageW(h, 0x0100, vk, ((sc | (0x100 if ext else 0)) << 16) | 1)


def arriba(nombre):
    vk, sc, ext = TECLAS[nombre]
    u, h = _ventana()
    u.PostMessageW(h, 0x0101, vk, ((sc | (0x100 if ext else 0)) << 16) | 1 | (1 << 30))


def pulsa(nombre, ms=250):
    abajo(nombre)
    time.sleep(ms / 1000.0)
    arriba(nombre)


if __name__ == "__main__":
    import sys
    n = sys.argv[1] if len(sys.argv) > 1 else "space"
    ms = int(sys.argv[2]) if len(sys.argv) > 2 else 250
    pulsa(n, ms)
    print("pulsado", n)
