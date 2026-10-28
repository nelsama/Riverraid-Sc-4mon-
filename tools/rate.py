#!/usr/bin/env python3
"""rate.py - Mide el ritmo del juego con UNA sola conexion HTTP.
Uso: python tools/rate.py <ruta> [segundos] [etiqueta]

Las peticiones HTTP independientes congelan el core mientras se atienden (cada
peticion parece pausar el emulador), asi que se usa keep-alive y se compara el
primer y el ultimo volcado. Para el tilemap, el numero de filas que difieren es
una cota del numero de filas de mundo regeneradas en el intervalo.
"""
import http.client
import json
import sys
import time

host, port = "127.0.0.1", 8080
path = sys.argv[1] if len(sys.argv) > 1 else "vram.json"
path = "/" + path.split("/")[-1]
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 10.0
label = sys.argv[3] if len(sys.argv) > 3 else path


def get(conn):
    conn.request("GET", path)
    r = conn.getresponse()
    body = r.read()
    return r.status, body


conn = http.client.HTTPConnection(host, port, timeout=30)
status, body = get(conn)
print("primera:", status, len(body))
if path == "/vram.json":
    tm0 = json.loads(body)["tilemap"]
    time.sleep(secs)
    status, body = get(conn)
    tm1 = json.loads(body)["tilemap"]
    d = sum(1 for a, b in zip(tm0, tm1) if a != b)
    print("%s: filas distintas tras %.1f s: %d  (min esperado %.1f)" %
          (label, secs, d, 7.5 * secs))
else:
    b0 = body
    time.sleep(secs)
    status, body = get(conn)
    print("%s: %d bytes -> %d, igual=%s" % (label, len(b0), len(body), b0 == body))
conn.close()
