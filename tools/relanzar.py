#!/usr/bin/env python3
"""relanzar.py - Reinicia el emulador con el binario actual del juego.

  Uso: python tools/relanzar.py [segundos_de_espera]
"""
import os
import subprocess
import sys
import time

EMU = r"D:\Proyectos\EmuladorFpga6502"
PROJ = r"D:\Proyectos\juegos_6502\RiverRaid"
espera = sys.argv[1] if len(sys.argv) > 1 else "4"

subprocess.run(["taskkill", "/F", "/IM", "emulador.exe"], capture_output=True)
time.sleep(1)
card = os.path.join(EMU, "tarjeta.img")
subprocess.run([sys.executable, "tools/sd_tool.py", "delete", card, "RIVER.BIN"],
               cwd=EMU, capture_output=True)
r = subprocess.run([sys.executable, "tools/sd_tool.py", "inject", card,
                    os.path.join(PROJ, "output", "game.bin"), "RIVER.BIN"],
                   cwd=EMU, capture_output=True, text=True)
print(r.stdout.strip())
subprocess.Popen(
    ["powershell", "-Command",
     "Start-Process -FilePath '.\\emulador.exe' -ArgumentList 'rom/monitor.bin','4000',"
     "'tarjeta.img','--debug=8080' -RedirectStandardOutput '.tmp\\out.log' "
     "-RedirectStandardError '.tmp\\err.log' -WindowStyle Normal"],
    cwd=EMU)
time.sleep(5)
r = subprocess.run([sys.executable, "tools/uart.py", "load RIVER.BIN 0800", "6"],
                   cwd=PROJ, capture_output=True, text=True)
print(r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr)
subprocess.run([sys.executable, "tools/uart.py", "r 0800", espera], cwd=PROJ,
               capture_output=True)
print("emulador listo")
