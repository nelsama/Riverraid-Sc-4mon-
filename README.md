# River Raid para 6502 / Core de Vídeo

Recreación de **River Raid** (Atari 2600) para un computador 6502 casero con
core de vídeo propio (Sipeed Tang Nano 9K). Está escrito en **C con cc65**, se
carga en `$0800` y ocupa **~13,7 KB** de RAM.

El objetivo es fidelidad al original: río generado por LFSR, puentes que se
destruyen a tiros, enemigos que rebotan en las orillas y dificultad que sube a
cada puente cruzado.

---

## Cómo compilar

Necesitas **cc65** y **GNU Make**. El makefile usa utilidades POSIX
(`mkdir -p`, `ls`, `awk`), así que hay que ejecutarlo desde **Git Bash**, no
desde `cmd`.

```bash
export PATH="/d/cc65/bin:/d/make/bin:$PATH"
make CC65_HOME=D:/cc65
```

Sale `output/game.bin` (+ `output/game.map`).

| Comando | Qué hace |
|---|---|
| `make` | compila el juego |
| `make PERF=1` | añade un medidor de coste de frame por UART (`perf.c`) |
| `make info` | tamaño del binario |
| `make map` | mapa de memoria |
| `make clean` | borra `build/` y `output/` (conserva `lib/vc.lib`) |

### Cargar en el emulador o en hardware

```
LOAD GAME 0800
R 0800
```

---

## Controles

| Tecla | Acción |
|---|---|
| **← →** (o `a` / `d`) | mover el avión en horizontal |
| **↑ ↓** (o `w` / `s`) | **acelerar / frenar el scroll** (el avión no se mueve en vertical) |
| **FIRE** (espacio) | disparar |
| **FIRE** estando parado | arrancar la partida / salir de pausa |
| `R` | reiniciar desde GAME OVER |

Por **UART** funcionan `a`/`d`, `w`/`s` y espacio.

---

## Mecánica

### El avión

Vuela a altura fija, en la parte baja de la pantalla. Solo se mueve en
horizontal. **Si toca tierra —orilla, isla o la viga de un puente— pierde una
vida.**

### El río

Generado por un **LFSR de 16 bits** con el mismo polinomio que el original
(`EOR #$B4` + `ROR`), avanzado una vez por bloque:

- El cauce **serpentea y cambia de ancho** bloque a bloque, con interpolación de
  1 celda por fila (nunca hay saltos que dejen al avión contra una pared que no
  se ve).
- Cada cierto tramo el cauce **se abre en dos brazos** con tierra en medio.
- El periodo del LFSR es de **65 535 bloques**: en la práctica el trazado no se
  repite nunca.

### Puentes

- Cruzan el río **de orilla a orilla**, con la calle a los lados.
- **No se esquivan**: si llegas sin haberlo volado, chocas y pierdes una vida.
- **Un solo disparo se lleva el puente entero.** Al reventarlo hay explosión y
  el agua destella un instante.
- Cruzarlo **salva el progreso**: al morir vuelves al último puente cruzado.
- Aparecen cada **192 filas de mundo ≈ 25 s** a velocidad normal.

### Enemigos y bidones

- **Barcos** y **helicópteros** avanzan hacia ti por el agua. Rebotes al llegar a
  la orilla.
- **Nunca aparecen los dos a la misma altura**.
- Los **bidones** recargan combustible mientras el avión esté encima: cuanto
  más tiempo de contacto, más carga. También se pueden reventar a tiros.

### Combustible

Se gasta con el tiempo. Si llega a 0, pierdes una vida.

---

## Progresión de dificultad

La dificultad vive en la tabla `prog_tab[]` de `game.c`, una fila por nivel. El
nivel sube **un paso por puente cruzado** y satura en el 5.

| Nivel | Puentes | Barcos | Helis | Bidón (filas) | Ancho mín. |
|---|---|---|---|---|---|
| 0 | 1 | quietos | ausentes | 18 | 20 |
| 1 | 2 | quietos | quietos | 20 | 19 |
| 2 | 3 | 1 px / 4 f | ausentes | 22 | 18 |
| 3 | 4 | 1 px / 3 f | 1 px / 3 f | 26 | 17 |
| 4 | 5 | 1 px / 2 f | 1 px / 2 f | 30 | 16 |
| 5 | 6+ | 1 px / 1 f | 1 px / 1 f | 34 | 15 |

- **`quieto`** = no se desplaza de lado; sigue bajando con el scroll.
- **`ausente`** = no aparece en ese tramo.
- Los bidones están **más juntos al principio** y se espacian después.

Al morir, el nivel **retrocede** al del tramo guardado en el checkpoint.

---

## Estructura del proyecto

```
game.c            lógica principal (~157 KB: editar por zonas, no reescribir)
joy.c             lectura del joystick
startup.s         arranque en ensamblador
perf.c            medidor de frame opcional (make PERF=1)

include/          video.h, fastvideo.h, romapi.h, joy.h, perf.h
lib/vc.lib        biblioteca del core de vídeo
config/           programa.cfg (script del linker)
doc/              reglas del juego, arquitectura y diagnósticos
docs/             análisis del original de Atari 2600
tools/            scripts de verificación y diagnóstico en Python
```

### Mapa de memoria

`config/programa.cfg` reserva:

| Segmento | Rango |
|---|---|
| ZP | `$20`–`$7F` |
| Programa (código + datos + BSS) | `$0800`–`$3EBF` |
| Stack | `$3EC0`–`$3FFF` (320 B) |

**Cuidado con el stack**: se probó recortarlo a 256 B y quedaba **por debajo
del final de BSS**, así que las rutinas de división del runtime pisaban las
variables (las vidas y el marcador salían mal). 320 B deja ~54 B de holgura
sobre BSS y el stack real solo usa 23.

---

## Verificación

No hay suite de tests unitarios. La comprobación es empírica, contra el
emulador:

```bash
python tools/verificar_slots.py            # ningún sprite comparte slot
python tools/verificar_colision.py 2000    # pintado vs colisión, 2000 filas
python tools/probar_puente.py              # choque con el puente
python tools/probar_puente_bala.py         # un disparo destruye el puente
python tools/probar_flash_orilla.py        # morir contra la orilla NO destella
python tools/verificar_color_agua.py       # el agua vuelve a su azul
python tools/verificar_nivel0.py           # nivel 0: barcos quietos, sin helis
```

### Trampas conocidas al medir

- **`/vram.json` puede ir con retraso.** A veces devuelve el frame anterior o
  vacío. `/screen.png` es más fiable para lo visual.
- **Las direcciones de BSS cambian en cada compilación.** Los scripts que leen
  `/ram.bin` deben recalcularlas desde `output/game.map` + `build/game.lst`.
- **Abrir y cerrar el socket UART (4000) en bucle para la entrada** del
  emulador. Hay que mantener una sola conexión.
- Desde el **UART** solo FIRE (espacio) arranca la partida; si el emulador está
  minimizado las teclas no llegan.

---

## Herramientas destacadas (`tools/`)

| Script | Para qué |
|---|---|
| `mandos.py` | inyecta teclas en la ventana del emulador |
| `verificar_colision.py` | simula el generador y compara pintado vs colisión |
| `verificar_slots.py` | comprueba que ningún sprite comparte slot |
| `gen_tiles.py`, `gen_ships.py`, `gen_isla_tiles.py` | generan los patrones de los tiles y sprites |
| `sim_world.py`, `simular_cauce.py` | generadores simulados en Python |
| `medir_*.py` | mediciones de scroll, cauce, isla, tramo |

---

## Estado

Implementado y verificado: scroll, avión, río con isla, enemigos con rebote,
bidones, combustible, puentes (choque y disparo), HUD, GAME OVER y la
progresión por niveles.

Pendiente (ver `doc/REGLAS-RIVERRAID.md`):

- **Tanque** pegado a cada puente y sus **misiles** (reglas 3.6 y 3.7).
- **Puntuación congelada** al cruzar un puente (regla 6.4).
- **Pausa** con el botón de color / consola (regla 6.6).

---

## Licencia

Proyecto personal de aprendizaje. *River Raid* es una marca de Activision; este
código es una reimplementación no oficial con fines educativos.
