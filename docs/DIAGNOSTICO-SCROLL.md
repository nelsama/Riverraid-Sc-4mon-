# Diagnóstico de vídeo/scroll (River Raid ↔ emulador)

Estado tras la revisión del emulador. Lo comprobado va separado de lo que no.

> Fuente de verdad del emulador: `EmuladorFpga6502/docs/NOTAS-CAMPANA-RIVERRAID.md`
> y `PROCEDIMIENTOS.md`. Este documento recoge solo lo del juego.

## Comprobado

- **Convención de patrones de fondo**: fila 0 = arriba, bit 7 = izquierda,
  `cell_addr = y*64 + x`. Coincide con `video_core.vhd`
  (`pixcode <= pat_dout(15 - bitidx2) & pat_dout(7 - bitidx2)`).
- **El índice 0 del patrón es siempre transparente** (se ve BG_COLOR) y no se
  puede reprogramar. Era la causa de las orillas negras: el agua de los tiles
  de borde iba en el índice 0.
- **`scroll_y` funciona** y **las capturas no congelan la emulación**: el
  "congelado" y el desfase captura/estado eran dos bugs del servidor de
  depuración (interbloqueo de cierre y `localhost`→IPv6), ya corregidos.
- **Coste por frame medido con el RTC**: ~12.000 µs antes de optimizar,
  ~7.000 µs después (VBLANK real ~1.430 µs).

## Correcciones aplicadas al juego

- Tiles de orilla rehechos: 1 = verde, 2 = arena, 3 = sombra; el agua del borde
  va en el índice 0 y deja ver BG_COLOR (puesto al azul del río).
- Paletas 2 y 3 (borde izquierdo/derecho): 1 = verde, 2 = arena, 3 = sombra.
- HUD a 2x con patrones 42-63; solo reescribe lo que cambia.
- Filas y animación del agua por escritura directa a `$D800-$D802`.
- Objetos que no se redibujan si no cambian (avión, barco, balas).

## Pendiente en el juego

- **Congelar el scroll al entrar en GAME OVER**: el panel se pinta una vez y el
  tilemap sigue moviéndose, así que el texto se descoloca.
- **Ocultar el HUD en GAME OVER**: los sprites tapan el texto del panel.

## Herramientas del proyecto

- `tools/emu.py` — crea la SD, lanza el emulador y carga un `.bin`.
- `tools/snap.py`, `tools/pix.py` — captura y volcado de píxeles.
- `tools/rate.py` — ritmo con keep-alive.
- `make PERF=1` + `tools/perfread.py` — coste por frame y por fase.

Nota de método: analizar los PNG con Pillow, nunca con decodificadores caseros
(mal hechos dieron conclusiones falsas durante la sesión).
