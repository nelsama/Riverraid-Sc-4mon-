#!/usr/bin/env bash
# ============================================================================
# run_emu.sh - Recompila River Raid, lo inyecta en la SD del emulador,
#              relanza el emulador y lo carga/ejecuta.
#
#   Uso:  ./tools/run_emu.sh          (build limpio + emulador + carga)
#         ./tools/run_emu.sh nobuild  (sin recompilar)
#
#   Requiere el emulador en /d/Proyectos/EmuladorFpga6502 (ya compilado).
# ============================================================================
set -u

PROJ="$(cd "$(dirname "$0")/.." && pwd)"
EMU="/d/Proyectos/EmuladorFpga6502"
BIN="$PROJ/output/game.bin"

if [ "${1:-build}" != "nobuild" ]; then
  echo "== Compilando =="
  ( cd "$PROJ" && make CC65_HOME=D:/cc65 ) || exit 1
fi

if [ ! -f "$BIN" ]; then echo "Falta $BIN"; exit 1; fi

echo "== Parando emulador anterior =="
taskkill //F //IM emulador.exe >/dev/null 2>&1 || true
sleep 1

echo "== Inyectando en la SD =="
( cd "$EMU" && python tools/sd_tool.py delete tarjeta.img RIVER.BIN >/dev/null 2>&1 || true
  python tools/sd_tool.py inject tarjeta.img "$BIN" RIVER.BIN )

echo "== Lanzando emulador (desacoplado) =="
( cd "$EMU" && powershell -Command "Start-Process -FilePath '.\emulador.exe' -ArgumentList 'rom/monitor.bin','4000','tarjeta.img','--debug=8080' -RedirectStandardOutput '.tmp\out.log' -RedirectStandardError '.tmp\err.log' -WindowStyle Normal" )
sleep 4

echo "== Cargando y ejecutando =="
python "$PROJ/tools/uart.py" "load RIVER.BIN 0800" 4 >/dev/null
python "$PROJ/tools/uart.py" "r 0800" 2 >/dev/null

echo "== Listo. Debug: http://127.0.0.1:8080/ =="
