#!/usr/bin/env python3
"""tecla.py - Manda una tecla a la ventana del emulador.

  Uso: python tools/tecla.py <tecla> [ms_abajo]

Las teclas van a la VENTANA (SDL), no al UART: desde un script hay que
inyectarlas. `keybd_event` no vale (SDL lee el teclado por estado y el emulador
no tiene el foco), y `PostMessage` sin el WM_CHAR no mueve nada: SDL2 procesa
WM_KEYDOWN y, para teclas con traduccion de texto, WM_CHAR. Se mandan los dos.
"""
import ctypes
import sys
import time

u = ctypes.windll.user32
TITULO = "Emulador FPGA 6502 - Tang Nano 9K"

# vk, scan, char (0 = tecla de control sin WM_CHAR)
TECLAS = {
    "space": (0x20, 0x39, 0x20),
    "w": (0x57, 0x11, 0x77),
    "a": (0x41, 0x1E, 0x61),
    "s": (0x53, 0x1F, 0x73),
    "d": (0x44, 0x20, 0x64),
    "up": (0x26, 0x48, 0),
    "down": (0x28, 0x50, 0),
    "left": (0x25, 0x4B, 0),
    "right": (0x27, 0x4D, 0),
    "f": (0x46, 0x21, 0x66),
}


def ventana():
    h = u.FindWindowW(None, TITULO)
    if not h:
        print("NO se encontro la ventana del emulador")
        sys.exit(1)
    return h


def pulsa(nombre, ms=250):
    vk, sc, ch = TECLAS[nombre]
    h = ventana()
    u.SetForegroundWindow(h)
    lparam = 0x00010001 | (sc << 16)
    u.PostMessageW(h, 0x0100, vk, lparam)              # WM_KEYDOWN
    if ch:
        u.PostMessageW(h, 0x0102, ch, lparam)          # WM_CHAR
    time.sleep(ms / 1000.0)
    u.PostMessageW(h, 0x0101, vk, 0xC0010001 | (sc << 16))   # WM_KEYUP


if __name__ == "__main__":
    nombre = (sys.argv[1] if len(sys.argv) > 1 else "space").lower()
    ms = int(sys.argv[2]) if len(sys.argv) > 2 else 250
    pulsa(nombre, ms)
    print("pulsado", nombre)
