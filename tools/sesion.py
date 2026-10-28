"""sesion.py - Sesion UART persistente + lectura de estado del emulador.

Uso IMPORTANTE (ver docs/GUIA-AGENTES.md del emulador): NO reconectar el socket
de UART por comando. Un cierre abrupto provoca RST, el emulador da la sesion por
perdida y la siguiente conexion se queda sin respuesta. Con una sola sesion
abierta y escrituras seguidas todo funciona.
"""
import json
import re
import socket
import time
import urllib.request

HOST = "127.0.0.1"
UART_PORT = 4000
DBG = "http://localhost:8080"


class Emu:
    def __init__(self):
        self.s = socket.create_connection((HOST, UART_PORT), timeout=5)
        self.s.settimeout(0.05)

    def drain(self):
        out = b""
        try:
            while True:
                d = self.s.recv(65536)
                if not d:
                    break
                out += d
        except socket.timeout:
            pass
        return out

    def cmd(self, line, wait=1.0):
        self.s.sendall((line + "\r").encode())
        time.sleep(wait)
        return self.drain()

    def key(self, k):
        self.s.sendall(k.encode())
        time.sleep(0.01)
        self.drain()

    def close(self):
        try:
            self.s.shutdown(socket.SHUT_WR)
        except OSError:
            pass
        self.s.close()


def state():
    return urllib.request.urlopen(DBG + "/state.txt", timeout=5).read().decode()


def vram():
    return json.load(urllib.request.urlopen(DBG + "/vram.json", timeout=5))


def screen(path="check.png", scale=2):
    urllib.request.urlretrieve(DBG + "/screen.png?scale=%d" % scale, path)
    return path


def snap():
    """(scroll_y, plane_x, (fuel_x, fuel_y)|None, barra_de_combustible)."""
    t = state()
    sc = int(re.search(r"scroll: x=\d+ y=(\d+)", t).group(1))
    d = vram()
    g = {s["n"]: s["tile"] for s in d["sprites"]}
    bar = "".join("#" if g.get(14 + i) == 53 else "." for i in range(8))
    p = [s for s in d["sprites"] if s["tile"] == 12]
    f = [s for s in d["sprites"] if s["tile"] == 15]
    return (sc, (p[0]["x"] if p else None),
            ((f[0]["x"], f[0]["y"]) if f else None), bar)
