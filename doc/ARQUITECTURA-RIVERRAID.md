# ARQUITECTURA — RIVER RAID (Core de Vídeo `vc` / 6502 / Tang Nano 9K)

Documento de diseño con las decisiones fijadas para el clon de **River Raid**
(arcade 1982, Carol Shaw) sobre el **Core de Vídeo** (`videocore-6502-cc65`) y el
**monitor 6502**.

> **Origen y base:** la infraestructura (`startup.s`, `makefile`,
> `config/programa.cfg`, `include/`, `lib/vc.lib`) se clona de `snake` (el
> proyecto base más simple) manteniendo la plantilla autonoma de
> `videocore-6502-cc65/examples/demo/`.
>
> 📌 Antes de tocar código, lee `doc/TIPS-Y-TRAMPA.md` (guía general del Core de
> Vídeo): tiles, sprites, flips, paletas, texto, SID, depuración.

---

## 1. Resumen del juego y su novedad técnica

River Raid es un *shoot'em up* de **scroll vertical continuo**: el avión del
jugador vuela hacia arriba por un río **infinito**, destruyendo barcos y
helicópteros, esquivando puentes y las **partidas del cauce**, y repostando
combustible.

A diferencia de los 5 proyectos hermanos (Arkanoid, Pac-man, Snake,
Space Invaders, Tetris — todos de **pantalla fija** o cámara estática), River
Raid introduce **tres novedades** sobre la misma base:

1. **Scroll vertical continuo** (`vc_set_scroll_y`), que desplaza la cámara sobre
   un tilemap 64×32 mientras se **regenera** la franja de terreno que entra por
   abajo.
2. **Mundo infinito por generación procedimental** (LFSR determinista), en lugar
   de un estado finito en RAM (`board[]`, `brick[][]`, `maze_bits`).
3. **Dos sistemas de coordenadas**: tiles en el *espacio del mapa* (afectados por
   el scroll) y sprites en el *espacio de la pantalla* (OAM es absoluto), que hay
   que mantener sincronizados.

---

## 2. Geometría y presupuesto de recursos (decisiones fijadas)

### 2.1 Pantalla y mapa

| Parámetro | Valor | Nota |
|---|---|---|
| Pantalla visible | 40×30 celdas (320×240 px) | fijo del core |
| Mapa de fondo | 64×32 celdas | se recicla como "cinta" |
| Tile | 8×8 px | |
| Scroll | `scroll_y` 16 bits (bits 2:0 → 32 filas) | envolvente |

### 2.2 La "cinta" de scroll

El mundo se representa como una **cinta cerrada de 32 filas** (el alto del mapa
64×32). Conforme el avión "sube", `scroll_y` crece y la fila que sale por arriba
se **regenera** abajo:

- El scroll envuelve en módulo 32 filas (256 px) por el propio hardware.
- Se genera **1 fila nueva por cada 8 px de scroll** (una fila de tiles por
  "paso" de terreno), manteniendo un **cursor de generación** (`gen_row`) en el
  espacio del mapa.
- Solo se redibuja la **franja del río** (unas 6–10 celdas centrales); las dos
  **orillas** (verdes) son tiles estáticos ya presentes en el mapa.

> Esto reduce el coste por frame a **una fila de ~8 celdas**, no a 40×30.

### 2.3 Presupuesto de recursos (heredado de los otros proyectos)

| Recurso | Límite | Uso previsto en River Raid |
|---|---|---|
| Sprites (OAM) | 32 (máx. **8 por línea**) | avión + enemigos + balas + partículas |
| Patrones de fondo | 256 (fuente en `$20-$7F`) | agua (2–3), orilla (2), isla, puente |
| Patrones de sprite | 64 | avión (1 base + flips), helicóptero, barco, bala, depósito, explosión |
| Tiles propios (libres) | `$01-$1F` | `$01-$1F` para terreno (fuera de la fuente) |
| RAM | `$0800-$3DFF` (~13.5 KB) | variables de estado + buffers de generación |

---

## 3. Reparto tiles / sprites (la decisión estructural)

Igual que en los otros juegos, el hardware dibuja con **tiles + sprites, sin
framebuffer**, y limita la OAM a **32 sprites / 8 por línea**. La regla ya
recogida en `TIPS-Y-TRAMPA.md` (§3.1) es:

> "Objetos pocos y móviles libres → **sprites**; muchos en rejilla o fondo → **tiles**."

| Elemento | Tipo | Justificación |
|---|---|---|
| Río (agua) | **tiles** (2–3 patrones de oleaje) | fondo continuo, se regenera por franjas |
| Orillas (verde/arena) | **tiles** estáticos | ocupan los laterales, no cambian |
| Partida del cauce | **tiles** dentro del canal | es terreno: el río se estrecha a 0 en el centro |
| Puentes (por encima, tapan) | **tiles** + `VC_ATTR_PRIO` | se cruza por debajo |
| Avión del jugador | **sprite** (avión de 16×8 con 2 sprites 8×8, o 8×8 con scale2x) | móvil fino |
| Helicóptero enemigo | **sprite** | pocos, móviles |
| Barco enemigo | **sprite** | pocos, móviles, anclado al río |
| Depósito de combustible | **sprite** o **tile** | a decidir en la fase 2 |
| Balas (jugador y enemigas) | **sprites** | móviles, efímeras |
| Explosión | **sprites** (animación de 2–3 patrones) | efímera |

> **Atención al tope de 8/línea:** el río es una banda horizontal estrecha; en un
> frame pueden coincidir avión + 2 balas + 2 enemigos + explosión. Hay que
> presupuestar el *pool* de OAM y, si hace falta, recortar enemigos por línea.

---

## 4. El scroll vertical (mecánica central)

### 4.1 Registros usados

```
vc_set_scroll_y(scroll_y);     // desplaza la cámara (bits 2:0 = 32 filas)
vc_set_raster(line0, line1);   // split en bandas (probablemente sin split)
```

El HUD (marcador, combustible, vidas) es **fijo**, así que el split de raster
`vc_set_raster` puede reservarse para anclar un HUD arriba/abajo **sin scroll**, o
bien mantener `0xFF/0xFF` y dibujar el HUD como sprites fijos. **Decisión
inicial:** HUD por tiles en banda fija usando split de raster (a validar en la
fase 1 del prototipo); si el split resta ciclos, pasar a HUD estático sin split.

### 4.2 Sistema de coordenadas (el punto más delicado)

Existen **dos espacios**, y hay que traducir entre ellos en todo momento:

```
y_mundo      = coordenada lógica del mundo (crece hacia arriba, sin tope)
y_pantalla   = posición visible de un objeto en la OAM (0..239)
scroll_y     = offset de la cámara (crece al avanzar)

y_pantalla = y_mundo - scroll_y        (para dibujar un sprite anclado al mundo)
```

- **Tiles del terreno** viven en el espacio del **mapa** (se escriben por fila en
  el tilemap 64×32, en la fila `gen_row`).
- **Sprites** viven en el espacio de la **pantalla**: cada frame hay que
  recalcular su `y` como `y_mundo - scroll_y`, y **deshabilitarlos** (`Y >= 248`)
  si ya salieron por arriba.

> **Por qué importa:** en los otros juegos scroll=0 y ambos espacios coinciden.
> Aquí, un enemigo "anclado" a una altura del mundo debe **bajar en pantalla** a
> medida que la cámara sube. Si no se traduce, los enemigos "flotan" sobre un río
> que se mueve independientemente.

### 4.3 Generación del terreno (cinta + LFSR)

El mundo es infinito y **no cabe en RAM** como un array completo. Se genera sobre
la marcha con un **LFSR determinista** (como el arcade original):

- Un **LFSR de 16 bits** decide, por fila generada, si aparece: una **partida del
  cauce** (el río se abre en dos durante N filas), barco, helicóptero, depósito
de  combustible o puente (con su tanque).
- Con **semilla fija**, la disposición del río es **idéntica cada partida**
  (reproducible); para variar entre partidas se puede sembrar con `snd_random()`
  + `rom_get_micros()`.
- El jugador solo ve la ventana de 30 filas; el resto se descarta.

> **Nota:** el patrón `snd_random()` (SID `$D41B` en modo ruido silencioso) ya
> está en `sound.c`/`sound.h` de la base. Para el LFSR de terreno se usa **semilla
> determinista o no determinista** según se quiera partida reproducible o no.

---

## 5. El jugador y el movimiento

### 5.1 Controles

| Acción | Joystick | Teclado UART |
|---|---|---|
| Mover a la izquierda | stick ← | `a` / `4` / flecha ← |
| Mover a la derecha | stick → | `d` / `6` / flecha → |
| Subir/bajar (ajuste) | stick ↑/↓ | `w`/`s` |
| Disparar | FIRE | `ESPACIO` / `ENTER` |
| Salir | — | `q` |

Entrada en paralelo (joystick + UART), igual que los otros proyectos
(`joy.c`/`read_input()`).

### 5.2 Movimiento del avión

- El avión se mueve **solo en horizontal** (X de OAM) dentro de la ventana visible;
  su **altura en pantalla es fija**, en la parte baja.
- **Arriba/abajo aceleran o frenan el scroll**, no mueven el avión (como el arcade).
- **Colisión contra orillas y tierra del río:** por **coordenada X pura** contra el
  perfil del cauce de la fila (`river_l`/`river_r`, con el agua en **dos tramos**
  cuando el cauce se parte). Si el avión se sale del agua → muere.
- **Colisión contra enemigos/misiles:** por **cajas AABB** (`vc_box_overlap`).

> Las reglas completas y verificables están en **`doc/REGLAS-RIVERRAID.md`**.

---

## 6. Enemigos y generación por filas

### 6.1 Tipos (fase inicial)

| Enemigo | Comportamiento | Puntos |
|---|---|---|
| Barco | avanza por el agua en sentido contrario; **rebota** en la orilla | 100 |
| Helicóptero | se desplaza lateralmente por el agua; **rebota** en la orilla | 200 |
| Tanque | va por **tierra**, junto a cada puente, y dispara misiles | 300 (misil) |
| Partida del cauce | el río se abre en dos brazos y se vuelve a juntar | — |
| Puente | se cruza por el hueco; tapa al avión (`VC_ATTR_PRIO`) | 500 |
| Depósito combustible | se recoge por contacto (carga por tiempo de contacto) | 80 |

### 6.2 Generación

Cada fila nueva del río genera, según el LFSR, **0..2** objetos. Los enemigos
entran por la **parte baja** de la pantalla (anclados a `y_mundo`) y "bajan" en
pantalla conforme sube la cámara.

---

## 7. Combustible, vidas y puntuación

- **Combustible**: decrece con el tiempo de juego; el depósito lo recarga por
  tiempo de contacto. Si llega a 0, se pierde una vida.
- **Vidas**: 3 iniciales; se pierde una al chocar con orilla/enemigo/tierra del
  río, al recibir un misil o al quedarse sin combustible.
- **Puntuación**: enemigos destruidos y puentes cruzados; se muestra en el HUD.
- **Game over**: al acabar vidas → pantalla `GAME OVER`, esperar
  FIRE/`R` para reiniciar, `q` para salir.

> Detalle completo en **`doc/REGLAS-RIVERRAID.md`**.

*(Detalles de balance — velocidades, tasas, puntajes exactos — se fijan en la
fase 2, tras validar el scroll.)*

---

## 8. Sonido (SID 6581, `$D400-$D41F`)

Se reutiliza el driver `sound.c`/`sound.h` de la base (ya incluye
`snd_init`, `snd_random`, `snd_update`, `snd_silence`). Reparto de voces propuesto:

| Voz | Uso |
|---|---|
| 1 | motor del avión (zumbido/ruido continuo) |
| 2 | disparo del jugador (sweep) |
| 3 | explosiones / choque |

> Igual que en los otros proyectos: efectos **percutivos no bloqueantes**,
> `snd_update()` una vez por frame.

---

## 9. Fases de desarrollo (hoja de ruta)

| Fase | Objetivo | Estado |
|---|---|---|
| **0** | Infraestructura clonada (makefile, startup, lib, headers) | ✅ |
| **1** | **Prototipo de scroll vertical**: río + orillas + avión estático + scroll continuo | → siguiente |
| **2** | Generación LFSR: islas, barcos, helicópteros, combustible | pendiente |
| **3** | Jugador: movimiento, disparo, colisiones, combustible | pendiente |
| **4** | Sonido, HUD, vidas, game over | pendiente |
| **5** | Pulido: paletas, arte Piskel, balance, highscore | pendiente |

---

## 10. Decisiones abiertas (a cerrar con el prototipo)

- **HUD**: ¿anchura fija por split de raster o HUD en sprites/tiles fijos?
- **Avión 16×8** (2 sprites) vs **8×8 escala 2x** (`VC_SPR_SCALE2X`).
- **Scroll por franjas**: velocidad en px/frame y granularidad de regeneración
  (1 fila cada 8 px vs cada pixel).
- **Split de raster**: reservar banda para HUD o no.
- **LFSR**: polinomio y longitud (16 bits) exactos.

---

## Referencia rápida (mismo hardware que los 5)

| Rango | Contenido |
|---|---|
| `$0000-$3FFF` | RAM (16 KB) |
| `$0800-$3F9F` | programa (código + datos + BSS) |
| `$3FA0-$3FFD` | pila software cc65 |
| `$3FFE-$3FFF` | flag AUTOBOOT (⚠️ no usar) |
| `$4000-$7FFF` | VRAM |
| `$8000-$BFFF` | ROM (monitor) |
| `$C000-$C0xx` | I/O (GPIO, UART, ...) |
| `$D400-$D41F` | SID |
| `$D800-$D87F` | Vídeo (registros del core) |

Compilación: `make CC65_HOME=D:/cc65` (desde shell Windows, NO desde WSL).
Carga: `LOAD GAME 0800` → `R 0800` en el monitor.
