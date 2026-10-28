#!/usr/bin/env python3
"""emu.py - Crea la tarjeta SD del emulador, lo lanza y carga un .bin.

  Uso: python tools/emu.py [ruta/al/bin] [nombre_8000] [espera]

Por defecto inyecta output/game.bin como RIVER.BIN. Un solo comando para no
encadenar pasos a mano (el emulador se cuelga si se lanza sin Start-Process).
"""
import os
import subprocess
import sys
import time

EMU = r"D:\Proyectos\EmuladorFpga6502"
PROJ = r"D:\Proyectos\juegos_6502\RiverRaid"

bin_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(PROJ, "output", "game.bin")
name = sys.argv[2] if len(sys.argv) > 2 else "RIVER.BIN"
wait = sys.argv[3] if len(sys.argv) > 3 else "4"


def run(args, cwd):
    return subprocess.run(args, cwd=cwd, capture_output=True, text=True)


run(["taskkill", "/F", "/IM", "emulador.exe"], EMU)
time.sleep(1)
card = os.path.join(EMU, "tarjeta.img")
for n in ("RIVER.BIN", "BENCH.BIN", "IDTEST.BIN", "PATTEST.BIN", "Q.BIN"):
    run([sys.executable, "tools/sd_tool.py", "delete", card, n], EMU)
r = run([sys.executable, "tools/sd_tool.py", "inject", card, bin_path, name], EMU)
print(r.stdout.strip() or r.stderr.strip())
subprocess.Popen(
    ["powershell", "-Command",
     "Start-Process -FilePath '.\\emulador.exe' -ArgumentList 'rom/monitor.bin','4000',"
     "'tarjeta.img','--debug=8080' -RedirectStandardOutput '.tmp\\out.log' "
     "-RedirectStandardError '.tmp\\err.log' -WindowStyle Normal"],
    cwd=EMU)
time.sleep(4)
r = run([sys.executable, "tools/uart.py", "load %s 0800" % name, "5"], PROJ)
print(r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr)
r = run([sys.executable, "tools/uart.py", "r 0800", wait], PROJ)
print(r.stdout.strip())
