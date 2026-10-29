# RIVER RAID — REGLAS DEL JUEGO (requisito)

Documento de **requisitos**, no de implementación. Si el código y este documento
no coinciden, el bug está en el código (o el documento se actualiza primero y
después el código, nunca al revés).

Estado de cada regla respecto al código actual:

- **OK** — implementado y verificado en el emulador.
- **DIF** — implementado distinto (hay que corregir el código).
- **FALTA** — no existe.

---

## 1. Vista y movimiento

| # | Regla | Estado |
|---|---|---|
| 1.1 | Vista cenital, el mundo baja por la pantalla (scroll vertical continuo). | OK |
| 1.2 | El avión **solo se mueve en horizontal**. Su altura en pantalla es fija, en la parte baja. | OK |
| 1.3 | **Arriba / abajo** no mueven el avión: **aceleran / frenan el scroll**. Al soltar, vuelve a la velocidad normal. | OK |
| 1.4 | Empieza **parado** (río quieto) y arranca **al pulsar FIRE**. Igual tras perder una vida. | OK |
| 1.5 | FIRE dispara el cañón (máx. 2 balas en vuelo). | OK |

## 2. El río

| # | Regla | Estado |
|---|---|---|
| 2.1 | El avión vuela **solo dentro del agua**. Si toca tierra (orilla o cualquier obstáculo dentro del río) **pierde una vida**. | OK |
| 2.2 | Los bordes del cauce ondulan suavemente en escalera (como el arcade), nunca "vibran". | OK |
| 2.3 | **Partida del cauce**: el río se abre en **dos brazos** durante unas filas y luego se vuelve a juntar. Lo que hay en medio es **tierra del propio cauce** (hierba/arena), **no** una "isla" como entidad aparte. | DIF — hoy existe como entidad `island_l/island_r` con 4 tiles de isla; debe pasar a ser simplemente "el río se estrecha a 0 en el centro" |
| 2.4 | La partida **nace y muere dentro de la pantalla**: nunca aparece ni desaparece a media altura (las "islas fantasma"). | DIF |
| 2.5 | Si el cauce se parte, **ambos brazos son navegables** (≥4 celdas cada uno). | OK |

## 3. Enemigos

| # | Regla | Estado |
|---|---|---|
| 3.1 | **Barco**: avanza por el agua en sentido contrario. | OK |
| 3.2 | **Helicóptero**: vuela por el agua moviéndose lateralmente. | OK |
| 3.3 | **Ninguno de los dos sale a tierra**: si llegan al borde del agua **rebotan y cambian de dirección**. | OK — los límites salen del perfil de su fila, recalculados cada frame (incluye la isla) |
| 3.4 | Si el avión choca con un barco/helicóptero **pierde una vida** (y el enemigo explota). | OK |
| 3.5 | Disparar a un barco/heli lo destruye (puntos). | OK |
| 3.6 | **Tanque**: va **por tierra**, siempre pegado a un **puente**, y dispara **misiles** hacia arriba por el río. | FALTA |
| 3.7 | Un **misil** que toca al avión → **pierde una vida**. Los misiles se pueden destruir a tiros. | FALTA |

## 4. Puentes (fin de nivel)

| # | Regla | Estado |
|---|---|---|
| 4.1 | El **puente** cruza el río de orilla a orilla; el agua pasa por debajo. | PARCIAL — `TILE_BRIDGE` de `gen_left` a `gen_right`, con hueco. Falta que el agua se vea POR DEBAJO (hoy es viga maciza en la fila) |
| 4.2 | El puente **tapa** (prioridad de tile) al avión cuando pasa bajo él. | FALTA |
| 4.3 | Al llegar al puente hay que **pasar por el hueco del puente**; si el avión toca el puente → pierde una vida. | OK — hueco `BRIDGE_GAP_L/R`=19..20, `puente_bit[]` en RAM para la colisión |
| 4.4 | Cruzar un puente **completa un nivel**: la puntuación "congelada" (ver 6.3) se suma y empieza el nivel siguiente. | PARCIAL — el checkpoint LFSR funciona (morir repite el tramo). Falta puntuación y subida de dificultad |
| 4.5 | Cada puente lleva su **tanque** cerca (3.6). | FALTA |

## 5. Combustible

| # | Regla | Estado |
|---|---|---|
| 5.1 | El depósito baja solo, con el **tiempo de juego** (no con la distancia). | OK |
| 5.2 | El **bidón** se recoge **pasando por encima**; lo que cargas depende del **tiempo de contacto** (rozarlo carga poco, quedarse encima carga más). No es instantáneo. | DIF — la lógica está, pero con el depósito casi lleno el bidón **no se puede coger**, porque el tope actual (200) no deja sumar y la comprobación de solape depende de bordes de celda |
| 5.3 | **Si el depósito llega a 0 → pierde una vida.** | OK (pero ver 5.4) |
| 5.4 | Al reaparecer **no se regala combustible**: el depósito se queda como estaba (si murió sin gasolina, el bidón siguiente es la única salida). | DIF — hoy regala 100 unidades (`FUEL_RESPAWN`) |

## 6. Vidas y partida

| # | Regla | Estado |
|---|---|---|
| 6.1 | **3 vidas como máximo**, mostradas en el HUD. | OK |
| 6.2 | Se pierde una vida por: choque con tierra/isla/puente, choque con enemigo, **misil** o **quedarse sin combustible**. | OK salvo misil (FALTA) |
| 6.3 | Al perder una vida: explosión, el avión vuelve al **punto de partida**, el río se **para** y hay que pulsar **FIRE** para continuar. La puntuación **no se resetea**. | OK |
| 6.4 | En el arcade, la puntuación ganada desde el último puente se muestra **parpadeando** y solo se consolida al cruzar el siguiente puente (si mueres la pierdes). | FALTA |
| 6.5 | Con 0 vidas → **GAME OVER** con panel de texto y FIRE/`R` para reintentar, `q` para salir. | OK |
| 6.6 | **Pausa**: FIRE con el río en marcha… (a decidir: en el original es el botón de color / consola). | FALTA / a decidir |

## 7. Puntuación

| Objeto | Puntos |
|---|---|
| Barco | 100 |
| Helicóptero | 200 |
| Misil | 300 |
| Bidón de combustible | 80 |
| Puente cruzado | 500 |

(La tabla exacta del arcade varía por revisión; estos valores son los acordados
para este proyecto.)

---

## 8. Glosario (para no confundir términos)

- **Cauce**: la zona de agua navegable de una fila, de `river_l` a `river_r`.
- **Partida del cauce**: filas en las que el agua está en **dos tramos**
  separados por tierra (lo que antes se llamaba "isla central").
- **Orilla**: el tile de transición tierra↔agua (arena), no navegable.
- **Puente**: estructura que cruza el río por encima del agua (tapa al avión).

---

## 9. Pendiente de decidir

1. ¿La **partida del cauce** durará filas fijas (p. ej. 6–10) o variable?
2. ¿El **tanque** aparece siempre en cada puente o solo en algunos?
3. Cadencia y velocidad del **misil** (1 cada cuántos frames, velocidad).
4. **Pausa**: ¿qué tecla (o ninguna, como el original de 2600)?
