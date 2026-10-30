#!/usr/bin/env python3
"""uartsend.py - Inyecta teclas al juego por el UART del monitor (TCP 4000).

Ventaja frente a la ventana: no depende del foco de Windows. El juego lee estos
caracteres en read_input():  a/A = izquierda, d/D = derecha,
w/W = acelerar, s/S = frenar, espacio = disparar.

  Uso: python tools/uartsend.py <cadena> [ms_entre_caracteres]
"""
import socket
import sys
import time

HOST, PORT = "127.0.0.1", 4000


def enviar(texto, pausa=0.05):
    s = socket.create_connection((HOST, PORT), timeout=3)
    s.settimeout(0.15)
    time.sleep(0.15)
    try:
        while s.recv(4096):
            pass
    except Exception:
        pass
    for c in texto:
        s.sendall(c.encode())
        time.sleep(pausa)
    time.sleep(0.1)
    s.close()


if __name__ == "__main__":
    texto = sys.argv[1] if len(sys.argv) > 1 else " "
    pausa = int(sys.argv[2]) / 1000.0 if len(sys.argv) > 2 else 0.05
    enviar(texto, pausa)
    print("enviado", repr(texto))
