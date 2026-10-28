#!/usr/bin/env python3
"""seqcap.py - Captura una secuencia de pantallas del emulador moviendo el
avion y disparando por el UART del monitor.

  Uso: python tools/seqcap.py <prefijo> <n_frames> [intervalo_s]

Guarda <prefijo>00.png ... en docs/img/seq/ y un <prefijo>.txt con el estado
(avion, combustible, scroll) de cada frame.
"""
import socket
import sys
import time
import urllib.request

HOST = "127.0.0.1"
PORT = 4000
DBG = "http://127.0.0.1:8080"
OUT = "docs/img/seq/"


def send(cmd):
    try:
        s = socket.create_connection((HOST, PORT), timeout=5)
        s.sendall((cmd + "\r").encode())
        time.sleep(0.05)
        s.close()
    except OSError as e:
        print("uart:", e)


def get(path):
    with urllib.request.urlopen(DBG + path + "?t=" + str(time.time()), timeout=10) as r:
        return r.read()


def main():
    prefix = sys.argv[1] if len(sys.argv) > 1 else "cap"
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 12
    dt = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0
    moves = "adad"          # secuencia de direcciones a rotar
    log = open(OUT + prefix + ".txt", "w")
    for i in range(n):
        send(moves[i % len(moves)])
        send(" ")
        time.sleep(dt)
        png = get("/screen.png?scale=2")
        name = OUT + "%s%02d.png" % (prefix, i)
        open(name, "wb").write(png)
        st = get("/state.txt").decode("latin-1", "replace")
        plane = [l for l in st.splitlines() if "sprites activos" in l]
        log.write("%s %s\n" % (name, plane[0] if plane else "?"))
        print(name, plane[0] if plane else "?", flush=True)
    log.close()


if __name__ == "__main__":
    main()
