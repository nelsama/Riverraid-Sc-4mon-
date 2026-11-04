# AGENTS.md — River Raid (6502 / Video Core)

Juego estilo River Raid para el **Core de Video (vc)** y el monitor 6502.
C portable (cc65) + ensamblador. El binario se carga en `$0800` con el monitor.

## Toolchain

- **cc65** en `D:\cc65` (`CC65_HOME`): `cl65`, `ca65`, `ld65` en `D:\cc65\bin`.
- **GNU Make 4.4.1** en `D:\make\bin\make.exe`.
- El makefile usa utilidades POSIX (`mkdir -p`, `ls`, `awk`, `rm -rf`):
  ejecutá `make` desde **Git Bash** (o con `sh.exe` en PATH), no desde `cmd`.
- **Python 3** para los scripts de `tools/`.

## Build

- `make` → `output/game.bin` (+ `output/game.map`).
- `make PERF=1` → agrega el medidor de coste de frame por UART (`perf.c`).
- `make info` (tamaño), `make map` (mapa de memoria), `make clean`.
- Carga en el monitor: `LOAD GAME 0800` y luego `R 0800`.
- Flags: `-t none -O --cpu 6502 -I include` (ver la regla de `game.o`).

## Estructura

- `game.c` — lógica principal. Es enorme (~140 KB): editá por zonas puntuales,
  no lo reescribas entero ni lo reformatees.
- `joy.c`, `startup.s` — joystick y entrada. `sound.c` tiene regla en el makefile
  pero **no está en `APP_C`**: no se linkea salvo que lo agregues explícitamente.
- `include/` — `video.h`, `fastvideo.h`, `romapi.h`, `fastsound.h`, `joy.h`, `perf.h`.
- `lib/vc.lib` + `config/programa.cfg` — biblioteca y linker script copiados de
  `videocore-6502-cc65`. **No los edites acá**; si actualizás la lib, volvé a
  copiarla (`cp ../videocore-6502-cc65/output/vc.lib lib/vc.lib`).
- `build/`, `output/` — generados. No editarlos ni commitearlos a mano.
- `doc/` y `docs/` — arquitectura, reglas del juego, diagnósticos de scroll,
  islas y LFSR. **Leelos antes de tocar el generador de niveles o el scroll.**
- `tools/` — verificación en Python (`piloto.py`, `verificar_colision.py`).

## Convenciones

- Comentarios y documentación en **español**.
- Restricciones de 6502: sin `malloc`, RAM y ciclos de CPU limitados, presupuesto
  de frame fijo. Vigilá el tamaño final (`make info`).
- Un cambio de comportamiento del juego se acompaña del doc correspondiente en `doc/`.

## Verificación

- No hay suite de tests. Compilá con `make` y, si tocás colisiones o el piloto,
  corré los scripts de `tools/` con Python.
- `.game_wip.c` es un archivo de trabajo sin trackear: no lo borres sin preguntar.
