# Islas: diagnóstico (y lo que NO es la causa)

Documento de trabajo. Recoge lo comprobado, lo descartado y lo que sigue abierto
sobre las "islas fantasma" de River Raid.

> **Ojo con el método**, porque aquí se ha perdido mucho tiempo por medir mal:
> - `/screen.png` = **último frame presentado**. Es lo que ve el jugador.
> - `/vram.json` = **estado vivo** del tilemap. Cambia entre dos consultas
>   seguidas: la isla se borra y se repinta cada 8 px de scroll.
> - `/ram.bin` = RAM del 6502. **Las direcciones de la BSS cambian en cada
>   compilación**: hay que sacarlas del `.map` de *ese* binario, o se lee basura
>   (aparecen valores imposibles tipo `activa=FF`, `y=FFFF`).

## Estado del código

`game.c` es la versión **que sí pinta las islas** (`game.c.bak_antes_isla_entidad`).
Compila en 12.259 bytes. Copias guardadas:

| Fichero | Qué es |
|---|---|
| `game.c.bak_isla_generador` | Isla pintada por el generador + `isla_tile_at()` (no pintaba) |
| `output/game_isla_generador.bin` | Su binario |
| `game.c.bak_sin_scrrow` | Restaurada + sin la restricción `scr_row` (tampoco arregló) |
| `game.c.bak_antes_isla_entidad` | **La versión activa**: pinta islas |

## Cómo funciona la isla en la versión activa

- Son 4 tiles (`TILE_ISLAND`, `_R`, `_L2`, `_R2`) con paleta `PAL_ISLAND`.
- `generate_world_row()` decide, de forma determinista respecto a la fila de
  mundo `n`, si esa fila lleva isla. Lo guarda en `island_l/r[]` y deja
  `gen_pending_l/r` armado para que la fila siguiente pinté la mitad de abajo.
- El bloque se pinta **en pareja** (las dos filas de cinta de una vez).
- `isla_bit[][]` marca las celdas que son isla, para que la animación del agua
  no las repinte.

## Comprobado (no es la causa)

1. **Agua, césped e isla se pintan igual**: mismas dos escrituras directas a
   `$D800/$D801/$D802` (tile) y al área de atributos. El mecanismo no es el
   problema.
2. **La condición de creación sí se cumple.** Simulación de 2000 filas de mundo
   con las condiciones reales (`(n&1)==0`, `h = (n*37+11)&0xFF < 30`,
   `width >= 14`, cabe en el cauce):

   ```
   filas simuladas        2000
   par && h < 30           118     (1 isla cada ~17 filas)
   width >= 14             118
   cabe en el cauce        118
   anchos del cauce: [16, 18, 20]
   ```

3. **`islas_update()` y `isla_crear()` sí se ejecutan** en una versión anterior:
   se verificó con marcas en el tilemap (`0x2A` cada frame, `0x2B` al crear,
   `0x32` al crear con éxito). Descartado "el código no corre".
4. **Quitar la restricción `scr_row >= 8 && <= 24` no arregla nada.** Esa
   restricción impedía crear islas en las filas de cinta 0–7 y 25–31 (el borde
   por donde deberían entrar), y era una buena sospecha, pero al quitarla el
   comportamiento fue el mismo.

## Lo que sigue abierto

Muestreo de 40 s con el juego en marcha (`/vram.json` cada 50 ms):

```
0.00s  filas [10,11] [14,15] [28,29]   col [19,20]
0.47s  filas [10,11] [15]     [28,29]   col [19,20]
0.59s  filas [10,11]        [28,29]   col [19,20]
```

Dos hechos:

1. **No nace ninguna isla nueva** en 40 s (debería salir una cada ~17 filas).
2. La pareja `[14,15]` **pierde la fila 14** y queda `[15]` sola: eso es
   exactamente el medio bloque que se ve como "isla fantasma".

Sospecha principal, sin confirmar: `gen_pending_l` se queda armado y todas las
filas siguientes entran por la rama "soy la mitad de abajo", lo que impide crear
islas nuevas y deja parejas a medias. **Falta comprobarlo leyendo esa variable
en el emulador con la dirección de la compilación correcta.**

## Siguiente paso propuesto

1. Confirmar si `gen_pending_l` vuelve a `0xFF` tras consumirse.
2. Si no vuelve: revisar el orden en que `generate_world_row()` consume y arma
   `gen_pending_l/r` (hay un camino que lo arma y no lo limpia).
3. Verificar siempre **por imagen** (`/screen.png`), no por `/vram.json`.
