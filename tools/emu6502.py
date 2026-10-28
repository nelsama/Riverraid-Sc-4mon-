#!/usr/bin/env python3
"""emu6502.py - Helper del emulador FPGA 6502 para este proyecto.

Sigue la GUIA-AGENTES.md del emulador (D:\\Proyectos\\EmuladorFpga6502):

  - El UART acepta UNA sola conexion: se usa una sesion por ciclo.
  - El ciclo completo es UN paso: stop -> start -> inject -> load -> run -> snap.
  - Lo que se cargó se verifica en /console.txt ("bytes en").
  - La RAM del 6502 se puede leer en /ram.bin (no hace falta publicar nada en
    el tilemap: /ram.bin?start=A&len=N devuelve RAM[0..16384)).

Uso:
  python tools/emu6502.py ciclo            # reinicia, carga output/game.bin y ejecuta
  python tools/emu6502.py snap salida.png  # captura el ultimo frame presentado
  python tools/emu6502.py ram 3720 32      # bytes de RAM desde decimal 0x... o decimal
  python tools/emu6502.py estado           # /state.txt (CPU, video, GPIO)
  python tools/emu6502.py consola          # /console.txt
  python tools/emu6502.py stop
  python tools/emu6502.py start            # solo arranca (sin cargar nada)
"""
import os
import socket
import subprocess
import sys
import time
import urllib.request

EMU = r"D:\Proyectos\EmuladorFpga6502"
PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEBUG = 8080
UART = 4000


# --------------------------------------------------------------------- HTTP
def http(ruta, timeout=15):
    return urllib.request.urlopen(
        "http://127.0.0.1:%d%s" % (DEBUG, ruta), timeout=timeout).read()


def vivos():
    if os.name != "nt":
        return b"emulador" in subprocess.run(["pgrep", "-f", "emulador"],
                                             capture_output=True).stdout
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq emulador.exe"],
                         capture_output=True, text=True).stdout
    return "emulador.exe" in out


def para():
    if os.name == "nt":
        subprocess.run(["taskkill", "/F", "/IM", "emulador.exe"],
                       capture_output=True)
    else:
        subprocess.run(["pkill", "-f", "emulador"], capture_output=True)


def arranca(espera=4.0):
    subprocess.Popen(
        ["powershell", "-Command",
         "Start-Process -FilePath '.\\emulador.exe' -ArgumentList "
         "'rom/monitor.bin','%d','tarjeta.img','--debug=%d' "
         "-WorkingDirectory (Get-Location) "
         "-RedirectStandardOutput '.tmp\\emu.out' "
         "-RedirectStandardError '.tmp\\emu.err' -WindowStyle Normal" % (UART, DEBUG)],
        cwd=EMU)
    t0 = time.time()
    while time.time() - t0 < 15:
        try:
            http("/state.txt", 3)
            print("emulador listo (HTTP %d)" % DEBUG)
            return True
        except OSError:
            time.sleep(0.3)
    print("el emulador no respondio en 15 s", file=sys.stderr)
    return False


# --------------------------------------------------------------------- UART
def sesion(comandos):
    """Una sola conexion para todos los comandos (el UART atiende uno a uno)."""
    s = socket.create_connection(("127.0.0.1", UART), timeout=5)
    salida = b""
    try:
        s.settimeout(0.4)
        for cmd, espera in comandos:
            s.sendall(cmd.encode() + b"\r")
            t0 = time.time()
            while time.time() - t0 < espera:
                try:
                    c = s.recv(65536)
                    if c:
                        salida += c
                        t0 = time.time()
                except socket.timeout:
                    pass
    finally:
        try:
            s.shutdown(socket.SHUT_WR)
        except OSError:
            pass
        s.close()
    return salida.decode("latin-1", errors="replace")


def inyecta(bin_path, nombre="RIVER.BIN"):
    """Copia el binario a la imagen de la SD por HTTP (remonta solo)."""
    datos = open(bin_path, "rb").read()
    req = urllib.request.Request(
        "http://127.0.0.1:%d/sdcard/write?name=%s" % (DEBUG, nombre),
        data=datos, method="POST")
    return urllib.request.urlopen(req, timeout=20).read().decode(errors="replace")


# --------------------------------------------------------------- acciones
def ciclo(bin_path=None, nombre="RIVER.BIN", direccion="0800", extra_espera=0.0):
    bin_path = bin_path or os.path.join(PROJ, "output", "game.bin")
    tam = os.path.getsize(bin_path)
    para()
    time.sleep(1)
    if not arranca():
        return False
    print(inyecta(bin_path, nombre).strip())
    salida = sesion([("load %s %s" % (nombre, direccion), 3.0),
                     ("r %s" % direccion, 2.5 + extra_espera)])
    time.sleep(1.0)
    consola = http("/console.txt").decode("latin-1", errors="replace")
    bytes_en = [l for l in consola.splitlines() if "bytes en" in l]
    print("binario local: %d bytes" % tam)
    print("monitor: %s" % (bytes_en[-1].strip() if bytes_en else "(sin respuesta)"))
    if not bytes_en or str(tam) not in bytes_en[-1]:
        print("AVISO: el monitor no reporta el tamano esperado", file=sys.stderr)
    return True


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    accion = sys.argv[1]

    if accion == "stop":
        para()
        print("emulador detenido")
        return 0
    if accion == "start":
        if vivos():
            print("ya estaba en ejecucion")
            return 0
        return 0 if arranca() else 1
    if accion == "ciclo":
        bin_path = sys.argv[2] if len(sys.argv) > 2 else None
        return 0 if ciclo(bin_path) else 1

    if not vivos():
        print("el emulador no esta en ejecucion: usa 'ciclo' o 'start'",
              file=sys.stderr)
        return 1

    if accion == "ping":
        # Ordena al monitor que ejecute el juego: el HTTP /uart solo inyecta en
        # el UART del 6502 (como pulsar una tecla), NO escribe en la consola.
        print(http("/uart?cmd=r%200800").decode(errors="replace").strip())
        time.sleep(3)
        est = http("/state.txt").decode("latin-1", errors="replace")
        print(" ".join(est.splitlines()[:5]))
    elif accion == "snap":
        destino = sys.argv[2] if len(sys.argv) > 2 else "pantalla.png"
        open(destino, "wb").write(http("/screen.png"))
        print("%s (%d bytes)" % (destino, os.path.getsize(destino)))
    elif accion == "ram":
        inicio = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0
        n = int(sys.argv[3], 0) if len(sys.argv) > 3 else 32
        datos = http("/ram.bin?start=%d&len=%d" % (inicio, n))
        print(" ".join("%02X" % b for b in datos))
    elif accion == "estado":
        print(http("/state.txt").decode("latin-1", errors="replace"))
    elif accion == "consola":
        print(http("/console.txt").decode("latin-1", errors="replace"))
    elif accion == "vram":
        print(http("/vram.json").decode("latin-1", errors="replace"))
    else:
        print("accion desconocida: %s" % accion, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
