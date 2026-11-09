/**
 * ============================================================================
 * game.c - RIVER RAID para el Core de Video (vc) / Monitor 6502
 * ============================================================================
 * FASE 2 (correccion): mundo infinito procedimental + scroll vertical estable.
 *
 * CORRECCIONES respecto a la version anterior:
 *   1. Los SPRITES se dibujan PRIMERO en el frame; la regeneracion del tilemap
 *      (que es mas pesada) va DESPUES. Asi el avion responde a la entrada y no
 *      se pierde cuando el VBLANK se agota escribiendo celdas.
 *   2. Solo se regenera el CANAL (12 columnas), no la fila completa (64). Las
 *      orillas son estaticas y se dibujan UNA sola vez en setup.
 *   3. El barco se dibuja como un casco claro y se mueve de verdad en pantalla.
 *
 * MUNDO / SCROLL (Manual 07-MANUAL-PROGRAMACION.md sec.7):
 *   - Mapa 64x32; scroll Y en pixeles 0..255, envuelve modulo 256 (32 filas).
 *   - El avion vuela hacia arriba: la camara sube por el mapa, o sea el valor
 *     de scroll DECREMENTA (validado en Fase 1: terreno baja en pantalla).
 *   - "world_row" cuenta las filas de mundo generadas (crece sin tope). La fila
 *     de mundo N se dibuja siempre en la fila de cinta (N & 31); al envolver,
 *     la fila que salio por abajo se reutiliza para la nueva que entra.
 * ============================================================================
 */

#include <stdint.h>
#include "video.h"
#include "fastvideo.h"    /* primitivas en linea: no enlaza lib/vc.lib      */
#include "romapi.h"
#include "fastsound.h"    /* SID en linea: no enlaza sound.c                */
#include "joy.h"
#include "perf.h"

/* ===========================================================================
 * GEOMETRIA DEL RIO
 * =========================================================================== */
#define RIVER_LEFT   8     /* primera columna de agua  */
#define RIVER_RIGHT  31    /* ultima columna de agua   */
#define RIVER_W      (RIVER_RIGHT - RIVER_LEFT + 1)   /* 24 columnas (~192 px) */

/* Limites de movimiento del avion (px). El avion (sprite 8x8) puede moverse
 * por TODO el rio en la banda media (por debajo del HUD, filas 3+). */
#define PLANE_MIN_X  (RIVER_LEFT * 8)
#define PLANE_W      16     /* el avion (8x8 a escala 2x) ocupa 16x16 px */
#define PLANE_H      16
/* Margen TRANSPARENTE del patron del avion (8x8 dibujado, 2 px de aire a
 * escala 2x). La colision ignora esos pixeles: si no, el avion muere con el
 * borde invisible rozando la orilla ("choque contra algo que no se ve"). */
#define PLANE_HIT_INSET  1
#define PLANE_MAX_X  ((RIVER_RIGHT + 1) * 8 - 16)   /* avion 16 px de ancho */

/* El BARCO no puede salir del cauce: rebota en las orillas. */
/* Limites del barco: ya NO son fijos. update_boat() los recalcula cada frame
 * con el perfil del cauce de su fila (river_l/r y la isla central). */
/* El HELICOPTERO tambien se mueve por el CAUCE: en el arcade vuela sobre el
 * agua, nunca sobre el cesped. Sus limites salen del mismo perfil. */
#define PLANE_MIN_Y  24     /* el HUD ocupa Y=10..17 (ver HUD_Y) */
#define PLANE_MAX_Y  (VC_SCREEN_H - 16 - 8)   /* margen inferior (overscan)     */

/* Velocidades (px/frame). */
#define PLANE_SPEED  3
/* Scroll: el joystick ARRIBA acelera y ABAJO frena. Velocidad en "pasos" de
 * 1/8 px por frame para que la diferencia sea suave: cada frame se suma
 * (scroll_acc += scroll_speed) y los 3 bits bajos son la parte fraccionaria.
 *   MIN  = 4  -> 0.5 px/frame (freno)
 *   NORM = 8  -> 1 px/frame   (velocidad normal)
 *   MAX  = 16 -> 2 px/frame   (aceleracion)
 * Empezar en NORM y volver a NORM al soltar es lo que hace el arcade. */
#define SCROLL_MIN    4
#define SCROLL_NORM   8
#define SCROLL_MAX    16
/* Pasos de scroll que tocan ESTE frame (lo deja advance_scroll). Barco, bidon
 * y explosion lo usan para acompanar al rio con la misma velocidad. */
static uint8_t  scroll_step;

/* ===========================================================================
 * JUEGO: vidas, combustible, puntuacion
 * =========================================================================== */
#define MAX_LIVES       3
#define FUEL_MAX        200    /* combustible inicial (unidades)     */
#define FUEL_DRAIN      1      /* unidades por 'n' frames (ver tick) */
#define FUEL_DRAIN_EVERY 10    /* drenar 1 unidad cada N frames       */
#define FUEL_RESPAWN    100    /* combustible con el que se reaparece */
/* Recarga por CONTACTO con el bidon: no es instantanea y el bidon NO se
 * consume. Cada FUEL_REFUEL_EVERY frames tocando el bidon se suma
 * FUEL_REFUEL_STEP y suena UN TINK, asi que mas tiempo de contacto = mas
 * combustible y mas golpes. Rozarlo da 1-2 tinks; sobrevolarlo entero da una
 * rafaga, y cada tink se oye separado (ver TINK_SPACE en fastsound.h).
 *
 * FUEL_REFUEL_EVERY = 4 coincide a proposito con el hueco del tink (3 frames
 * de impulso + 2 de silencio = 5, mas uno de margen). Si el pulso fuese mas
 * rapido que el hueco, algun tink se descartaria y el jugador oiria MENOS de
 * lo que carga, asi que el sonido y la recarga van al mismo ritmo.
 *
 * STEP = 6 esta ajustado a la ESCALA DE LA BARRA, no al reves: la barra tiene
 * HUD_FUEL_W = 8 piezas para FUEL_MAX = 200, o sea 25 unidades por pieza. Un
 * sobrevuelo da unos 6 pulsos = 36 unidades, que es 1,4 piezas: se VE subir la
 * barra en cada bidon. Con STEP = 1 un sobrevuelo daba 6 unidades = 0,24
 * piezas, no se encendia NINGUNA pieza y repostar parecia no hacer nada (se
 * midio: 6 bidones seguidos = +34 unidades = 0 piezas). */
#define FUEL_REFUEL_STEP  6    /* unidades por pulso de recarga  */
#define FUEL_REFUEL_EVERY 4    /* frames entre pulsos (= 1 tink) */

/* Puntos. */
#define SCORE_SHIP     100
#define SCORE_FUEL     150

/* ===========================================================================
 * HUD (split de raster: banda superior fija)
 * ===========================================================================
 * El HUD se dibuja en las filas 1-2 del tilemap, que quedan en la banda
 * superior (scroll 0). La banda media (filas 3+) scrollea con el rio. */
#define HUD_RASTER_LINE0  24   /* banda superior = 3 filas (0 = margen, 1-2 HUD) */
#define HUD_RASTER_LINE1  216  /* banda inferior empieza en linea 216 (filas 27-29) */

/* Sprite del deposito de combustible. La paleta 3 se reprograma en
 * setup_video(): el bidon se ve ROJO con la "F" BLANCA. Ver la tabla de
 * paletas junto a SPR_PLANE_PAL, mas abajo. */
/* Slots de OAM. PRIORIDAD: el core dibuja el sprite de MENOR INDICE por
 * encima (manual del core, §6.1). El HUD va en los slots MAS BAJOS para que
 * ningun objeto del juego lo tape: antes el bidon (4) y la explosion (5)
 * pasaban por delante de la barra de combustible y de los digitos.
 *
 *   0-4    puntos  5-9 barra  10-11 vidas
 *   12 avion  13 explosion  14 barco  15 heli  17 bidon  20-21 balas
 *
 * OJO: al mover slots hay que revisar tambien el comentario de SPR_HUD_BASE. */
#define SPR_FUEL       17
#define SPR_FUEL_PAT   15
#define SPR_FUEL_PAL   VC_SPPAL_3

/* --- HUD en SPRITES (fijo, fuera del tilemap que scrollea) ---
 *
 * SIN escala: 8x8 por sprite. Con SCALE2X el core toma el patron y el
 * patron+1 (indices 6 bits), asi que un digito necesitaba DOS patrones y el
 * ultimo (9) se salia de los 64 disponibles (68..69 -> vuelven a 4..5).
 *
 * Rango de patrones: 42..51, por debajo de 64 y sin tocar 0..2 (los pisa la
 * fase 3 del init) ni 12..15 (objetos del juego). */
#define HUD_SCALE      0
/* DIGITOS DEL MARCADOR: 5. Antes eran 4 y el marcador se calculaba con
 * `score % 10000`, asi que al pasar de 9999 VOLVIA A 0 en mitad de la partida.
 * El score es uint16_t, o sea que llega a 65535: con 5 digitos se ve entero
 * hasta 99999.
 *
 * OJO CON EL LIMITE DE 8 SPRITES POR LINEA: 5 digitos + icono de vida = 6 en
 * la fila 1, y 5 de barra + numero de vidas = 6 en la fila 2. Los slots de OAM
 * son 0..4 (digitos), 5..9 (barra) y 10..11 (vidas), o sea hasta el 11.
 * El avion usa el 12 para no solaparse. */
#define HUD_SCORE_DIGITS 5

/* Slots de OAM del HUD. Van los PRIMEROS a proposito: el core dibuja el sprite
 * de MENOR indice por encima, y son los que nunca deben quedar tapados. */
#define SPR_HUD_BASE   0       /* puntos: 0..4 (5 digitos)           */
#define SPR_HUD_FUEL   5       /* barra: 5..9                        */
#define SPR_HUD_LIVES  10      /* icono de vida (10) y numero (11)   */
/* Y=10: la fila 1 del tilemap (pixeles 8..15). La fila 0 (0..7) la recorta
 * el overscan de muchos monitores (manual del core, 1.9 y 5): con Y=2 el HUD
 * quedaba medio fuera de pantalla. Aqui sobra 2 px de margen por arriba. */
/* Y=10: la fila 1 del tilemap (pixeles 8..15). La fila 0 (0..7) la recorta
 * el overscan de muchos monitores (manual del core, 1.9 y 5). */
#define HUD_Y          10      /* FILA 1 del HUD (puntos + icono vida) */
#define HUD_Y2         19      /* FILA 2 del HUD (barra + numero vidas):
                                * 10 + 9 deja 1 px de aire y cabe en la
                                * fila 2 del tilemap (16..23)           */
#define HUD_SCORE_X    24      /* puntos (4 digitos x 8 px)          */
/* HUD_SCALE = 0: los sprites del HUD son de 8x8, NO de 16x16. Por eso cada
 * pieza de la barra ocupa 8 px y el paso HUD_FUEL_STEP es mayor que 8: deja
 * un hueco entre piezas en vez de solaparlas. */
#define HUD_FUEL_X     24      /* barra de combustible (FILA 2)      */
#define HUD_FUEL_W     5       /* celdas de la barra: 5 + vidas = 6, y
                                * deja 2 huecos del line buffer (8)
                                * para los objetos que entran por
                                * arriba (avion, helicoptero, bidon) */
/* Paso de 9 px: la pieza mide 7 y deja 1 px transparente (columna 7 del
 * patron) + 1 px de hueco, asi la barra se lee como una barra continua. */
#define HUD_FUEL_STEP  9       /* 7 px de relleno + 2 px de hueco     */
/* VIDAS: el icono va en la FILA 1 (a la derecha de los puntos) y el NUMERO en
 * la FILA 2 (a la derecha de la barra). Asi la fila 2 queda en 5+1 = 6
 * sprites y la fila 1 en 4+1 = 5, ambas dentro del limite.
 *
 * OJO CON EL REPARTO: el core de video NO dibuja los 32 sprites uno sobre
 * otro. Tiene un line buffer de 8 entradas (NSL = 8 en video_core.vhd): los
 * primeros 8 sprites que cruzan una linea se guardan y el 9.o EN ADELANTE NO
 * SE DIBUJA. El HUD tenia los 14 en Y=10, asi que en hardware real
 * desaparecian las 4 ultimas barras y el bloque de vidas. Por eso va partido
 * en dos filas. */
#define HUD_LIVES_X    72      /* icono de vida   (Y = HUD_Y )       */
#define HUD_LIVESNUM_X 72      /* numero de vidas (Y = HUD_Y2)       */
/* 72 cae justo a la derecha de la barra: esta acaba en
 * HUD_FUEL_X + (HUD_FUEL_W-1)*HUD_FUEL_STEP + 7 = 24 + 36 + 7 = 67, asi que
 * quedan 5 px de aire (antes estaban en 120, a media pantalla, y el bloque de
 * vidas se leia como un elemento suelto). Icono y numero van alineados en la
 * misma columna. */

/* Patrones de sprite del HUD. */
#define HUD_PAT_DIGIT0 42       /* 10 digitos = 42..51                */
#define HUD_PAT_LIVES  52       /* icono de vida (avion pequeno)      */
#define HUD_PAT_LIVES2 53       /* icono de vida ALTERNATIVO (morado) */
#define HUD_PAT_BAR_ON 54       /* celda de barra llena               */
#define HUD_PAT_BAR_OFF 55      /* celda de barra vacia               */

/* ===========================================================================
 * TILES
 * ===========================================================================
 * IMPORTANTE: los patrones de fondo 0x00-0x07 y los de sprite 0-2 los
 * SOBRESCRIBE el core en su setup ($D816) con los de demo, y el setup puede
 * repetirse. Si el cesped o el agua caen ahi, su patron se pierde y la celda
 * se pinta con el color de fondo (solo se ven las orillas). Verificado en el
 * emulador. Por eso TODOS los tiles viven en 0x10 en adelante. */
#define TILE_BLANK    0x00
#define TILE_GRASS    0x10
#define TILE_WATER    0x11
#define TILE_WATER2   0x12
#define TILE_ISLAND   0x13   /* isla arriba-izquierda */
#define TILE_ISLAND_R 0x14   /* isla arriba-derecha   */
#define TILE_ISLAND_L2 0x15  /* isla abajo-izquierda  */
#define TILE_ISLAND_R2 0x16  /* isla abajo-derecha    */
#define TILE_COAST    0x17   /* BASE: cesped a la IZQ, arena a la DER.
                              * Va donde el MAR esta a su DERECHA.       */
#define TILE_COAST_M  0x19   /* ESPEJO: arena a la IZQ, cesped a la DER.
                              * Va donde el MAR esta a su IZQUIERDA.     */
/* Solo hay estos dos. OJO al leer el patron: el bit 7 es el pixel IZQUIERDO y
 * el indice es (p0<<1)|p1, pero el dibujo sale al REVES de lo que sugiere el
 * byte, asi que ante la duda se mira una captura. */
#define TILE_TREE     0x1B   /* arbusto sobre la isla central            */
#define TILE_PANEL    0x1C   /* fondo solido del panel de GAME OVER       */
#define TILE_BRIDGE   0x1D   /* viga del puente, variante A (madera)      */
#define TILE_ROAD     0x1E   /* calle del puente, sobre el cesped        */

/* ===========================================================================
 * PALETAS
 * =========================================================================== */
#define PAL_WATER    VC_BGPAL_1   /* agua: azul (1), oleaje (2), blanco (3)    */
#define PAL_ISLAND   VC_BGPAL_2   /* isla y tierra: 1 = arena, 2 = vegetacion,
                                   * 3 = agua (igual que el cauce) */
#define PAL_EDGE     VC_BGPAL_3   /* cesped y orillas. 1 = verde claro,
                                   * 2 = verde oscuro, 3 = arena. El agua de la
                                   * orilla va en el indice 0 (transparente) y
                                   * deja ver el BG_COLOR (azul del rio). */
/* MADERA: paleta 0, libre desde que se quito el panel de GAME OVER (el cesped
 * usa PAL_EDGE y la isla PAL_ISLAND). Se usa SOLO en las filas del puente, asi
 * que no compite con nada. Con esta paleta la viga tiene su propio par de
 * tonos sin tocar los de la isla:
 *   1 = marron oscuro (junta)  2 = arena/madera clara (tabla) */
#define PAL_WOOD     VC_BGPAL_0
/* Panel de GAME OVER: ya no existe; se deja el nombre por el codigo de paleta. */
#define PAL_MENU     VC_BGPAL_0
/* ===========================================================================
 * SPRITES
 * ===========================================================================
 * PRIORIDAD (el menor indice se dibuja ENCIMA):
 *   0-11  HUD      12  avion      13  explosion
 *   14    barco    15  helicoptero
 *   17    bidon    20-21 balas
 * Asi el HUD queda siempre visible y el avion pasa por delante del enemigo,
 * de las balas y del bidon.
 * NOTA: el HUD NO puede ir en el tilemap, porque la banda media de scroll
 * recorre las 32 filas del mapa y lo haria aparecer abajo (comprobado con el
 * demo oficial: le pasa lo mismo). Por eso el HUD va en SPRITES.
 * =========================================================================== */
/* Patrones de los OBJETOS: 12..15.
 *
 * El core de video reescribe los patrones de sprite 0, 1 y 2 al inicializar
 * (video_core.vhd, fase 3: 24 filas desde la direccion 0 = una cara, un rombo
 * y una X; a cero si es un setup por $D816). Esos tres indices NO son del
 * juego.
 *
 * En hardware el avion (0), el barco (1) y las balas (2) salian con el
 * contenido del core; el helicoptero (9) se veia bien por estar fuera.
 *
 * Rango disponible: 3..63. Las fases 2 y 4 escriben en pat_arr (fondo), no en
 * spr_arr, asi que los patrones de sprite 32..63 NO se tocan.
 * Ver doc/PATRONES-SPRITE-CAUSA.md. */
#define SPR_PLANE      12    /* sprite unico del avion (escala 2x)
                              * Menor que enemigos y objetos, para dibujarse
                              * por encima (el menor indice gana). */
#define SPR_PLANE_PAT  12
#define SPR_BOAT       14
#define SPR_BOAT_PAT   13
#define SPR_HELI       15    /* slot PROPIO: antes compartia el 13 con el
                              * barco y solo podia haber un enemigo. */
#define SPR_HELI_PAT   9
#define SPR_BULLET     20    /* 20 y 21 (pool de 2 balas). Tiene que quedar
                              * FUERA de 17 (bidon) y de 14/15 (enemigos):
                              * draw_fuel() y draw_enemy() no reescriben el
                              * TILE en cada frame, asi que si otro objeto
                              * comparte slot le deja su patron. */
#define SPR_BULLET_PAT 14
/* Desfase entre el ORIGEN del sprite de la bala y su dibujo: el patron ocupa
 * las columnas 3 y 4, asi que el dibujo empieza en bullet_x + BULLET_OFF_X y
 * mide 2 px. Cualquier colision tiene que usar BULLET_HIT_X(), no bullet_x
 * directo: antes se comprobaba desde bullet_x y la caja salia 3 px a la
 * izquierda del dibujo. */
#define BULLET_OFF_X   3
/* CAJA DE COLISION, mas ancha que el DIBUJO a proposito.
 *
 * El dibujo son 2 px y el bidon 8: medido, el margen de acierto salia de 9 px
 * pero MUY desequilibrado (1 px por un lado y 7 por el otro), asi que el
 * jugador veia pasar la bala por encima del bidon sin que explotara.
 * Por eso se usa una caja de 4 px CENTRADA en el dibujo: BULLET_HIT_X arranca
 * 1 px antes (OFF_X - 1) y mide 4. Resultado medido: 9 px de margen,
 * 4 a cada lado. Es colision "generosa", como en el arcade: al jugador le
 * importa que la bala pase por encima del objeto, no el pixel exacto. */
#define BULLET_INSET   1
#define BULLET_W       4
#define BULLET_H       6
#define BULLET_HIT_X(i) ((uint8_t)(bullet_x[i] + BULLET_OFF_X - BULLET_INSET))
#define SPR_BOOM       13
/* OJO: los 3 patrones de la explosion tienen que caer en indices LIBRES.
 * Antes eran 10, 11 y 12, y el 12 es SPR_PLANE_PAT: el tercer frame del fuego
 * machacaba el dibujo del avion y en vuelo normal se veia un anillo blanco en
 * lugar del avion. 16..41 estan libres (9 heli, 13 barco, 14 bala, 15 bidon y
 * 42..54 el HUD). */
#define SPR_BOOM_PAT   16     /* 16, 17 y 18 = 3 frames de explosion */
#define SPR_BOOM_FRAMES 3     /* patrones distintos                    */
/* La explosion del AVION usa el mismo sprite 5 y los mismos 3 patrones, pero
 * se dibuja desde draw_plane() y con un temporizador propio. 36 frames (0,6 s
 * a 60 Hz) es lo que tarda en verse el fogonazo, la bola y el humo; antes de
 * eso no se deja reaparecer, para que el choque se "lea" en pantalla. */
#define PLANE_BLAST_FRAMES 36

/* Patrones del mosaico del avion (16x16). */
#define PAT_PLANE_TL 0     /* el avion vuelve a ser un solo sprite (patron 0) */
#define PAT_PLANE_TR 0
#define PAT_PLANE_BL 0
#define PAT_PLANE_BR 0

/* --- QUE PALETA USA CADA COSA -------------------------------------------
 *
 * Los nombres VC_SPPAL_0..3 de include/video.h son NEUTROS: describen el
 * preset de fabrica (piel/marron/negro, azul/cian/blanco, ...), pero
 * setup_video() REPROGRAMA varias entradas. Los colores que de verdad se ven
 * son los de setup_video(), no los del preset. Resumen medido:
 *
 *   paleta | quien la usa                      | color 1        | color 2 | color 3
 *   -------+-----------------------------------+----------------+---------+---------------
 *      0   | helicoptero (slot 6)              | cian #22FFFF   | negro   | rojo #BB1100
 *      1   | avion (0), bala (30-31), expl. (5)| #00FFFF        | #22AAEE | blanco #FFFFFF
 *          | HUD (10-23)                       | (bala y HUD:   |         |
 *          |                                   |  solo color 3) |         |
 *      2   | barco (slot 8)                    | negro #000000  | azul    | rojo #BB0011
 *      3   | BIDON (slot 4)                    | blanco #FFEEEE | naranja | rojo #DD1111
 *          |                                   |                | (libre) |
 *
 * OJO con la paleta 3: el preset la anuncia como verde/naranja/blanco, pero
 * setup_video() pisa el 3 con ROJO (ese rojo del bidon) y el 1 con blanco, y
 * deja el naranja del 2 sin reprogramar. Como spr_fuel solo emite los indices
 * 1 y 3, el bidon se ve ROJO con la "F" BLANCA. El naranja (indice 2) esta
 * libre por si se quiere redibujar el bidon como en el arcade.
 */
#define SPR_PLANE_PAL  VC_SPPAL_1   /* avion: cian / azul / (no usa 3) */
#define SPR_BOAT_PAL   VC_SPPAL_2   /* barco: negro / azul / rojo       */
#define SPR_HELI_PAL   VC_SPPAL_0   /* heli:  cian / negro / rojo       */
/* La bala y la explosion usan la MISMA paleta que el avion: los tres patrones
 * de fuego estan dibujados con los indices 1 (cian), 2 (azul) y 3 (blanco) de
 * esa paleta, asi el fuego queda en la gama del avion y no en el rojo. */
#define SPR_BULLET_PAL SPR_PLANE_PAL
#define SPR_BOOM_PAL   SPR_PLANE_PAL

/* Balas del jugador (pool pequeño). */
#define NBULLETS      2

/* Cooldown de disparo (frames). */
#define FIRE_COOLDOWN  8

/* Velocidad de la bala del jugador (px/frame, hacia arriba). */
#define BULLET_SPEED   4

/* ===========================================================================
 * LFSR (xorshift 16 bits, SEMILLA FIJA => rio reproducible)
 * =========================================================================== */
#define RIVER_SEED  0x2A5E

/* ===========================================================================
 * ESTADO GLOBAL
 * =========================================================================== */
static uint16_t rng_state;

/* DIAGNOSTICO: causa de la ultima muerte del avion, en $03E0.
 *   0 = sin morir, 1 = orilla/isla/viga, 2 = enemigo, 3 = sin combustible
 * Se lee desde el emulador con /ram.bin. El detalle del choque ($03E1-$03E4) y
 * la recarga de combustible ($03E5-$03EA) se quitaron para recuperar RAM: el
 * detalle lo cubre tools/verificar_colision.py, que simula el generador. */
#define DBG_MUERTE (*(volatile uint8_t *)0x03E0)

/* Diagnostico de la RECARGA por contacto ($03E5-$03EA). Son 6 bytes de
 * contadores que se actualizan una vez por frame; sirven para medir con
 * /ram.bin cuanto sube `fuel` y cuantos frames dura el contacto, sin depender
 * de capturas de pantalla ni del reloj de pared. */

/* El avion se dibuja con su esquina superior en plane_y. Para que este CENTRADO
 * en las filas de cinta que ocupa, el centro (plane_y + PLANE_H/2) debe caer en
 * mitad de una fila de cinta (celda) => plane_y = 4 + 8k.
 * PLANE_Y es la posicion fija tras reaparecer. OJO: NO se puede derivar de aqui
 * una fila de cinta constante para consultar el perfil; la cinta gira con el
 * scroll y hay que usar FILA_CINTA(plane_y + dy). */
#define PLANE_Y    (4 + 8 * 22)          /* = 180: altura fija, abajo */

static uint8_t  plane_x;   /* 8 bits: nunca pasa de 232 ni baja de 48 */
static uint8_t  plane_y;
static uint16_t scroll_val;   /* valor del registro de scroll (0..255) */
static uint8_t  scroll_speed; /* 1/8 px por frame: SCROLL_MIN..SCROLL_MAX */
static uint8_t  scroll_acc;   /* acumulador con los 3 bits fraccionarios  */
static uint32_t world_row;    /* ultima fila de mundo generada         */
static uint32_t scroll_total; /* pixeles recorridos desde el inicio    */
static uint8_t  quit_flag;
static uint8_t  sprites_off;   /* 1 = ya se ocultaron avion/objetos (game over) */
static uint8_t  oam_cache_invalida;  /* 1 = redibujar TODO el OAM (patron incluido) */

/* ENEMIGOS: DOS independientes, uno de cada tipo.
 *
 * Antes habia UN solo enemigo que era barco o helicoptero al azar y ambos
 * compartian el slot 13 del OAM, asi que no podian coincidir nunca. Ahora cada
 * tipo tiene su slot (SPR_BOAT y SPR_HELI) y su propio estado.
 *
 * REGLA: los dos pueden estar en pantalla, pero NUNCA a la misma altura.
 * Antes de soltar uno se mira la y del otro (ENEMIGO_SEP_Y). */
#define NENEMIGOS  2
#define ENEMIGO_BOAT 0
#define ENEMIGO_HELI 1
#define ENEMIGO_SEP_Y 20    /* separacion vertical minima entre los dos (px) */
#define ENEMIGO_W  16       /* 8x8 a escala 2x */
#define ENEMIGO_H  16

static uint8_t  ene_active[NENEMIGOS];
static uint8_t  ene_x[NENEMIGOS];
static uint8_t  ene_y[NENEMIGOS];
static uint8_t  ene_dir[NENEMIGOS];   /* 0 = derecha, 1 = izquierda (deriva) */
static uint8_t  ene_drift[NENEMIGOS]; /* contador de frames del paso lateral */
static uint32_t ene_next[NENEMIGOS];  /* world_row en que vuelve a salir     */

/* Explosion (sprite efimero al destruir un barco). */
static uint8_t  boom_active;
static uint8_t  boom_x;
static uint8_t  boom_y;
static uint8_t  boom_frames;

/* DESTELLO DEL PUENTE: al reventar la viga el agua se ve un momento en azul
 * claro (como en el original) y luego vuelve a su color. Se lleva aqui porque
 * la paleta del agua la comparten todas las filas: no se puede pintar "un
 * trozo" mas claro, se aclara el agua entera unos frames. */
static uint8_t  puente_flash;        /* frames de destello que quedan */

/* Columna de la isla de la fila anterior (para islas de 2 filas). */


/* Balas del jugador. */
static uint8_t   bullet_alive[NBULLETS];
static uint8_t   bullet_x[NBULLETS];  /* 8 bits: nunca pasa de 240 */
static uint8_t   bullet_y[NBULLETS];
static uint8_t   fire_cooldown;

/* Flag de muerte del avion (parpadeo breve antes de reubicar). */
static uint8_t   plane_dead;
/* Explosion del avion: frames que le quedan y su posicion en el momento del
 * choque. Se dibuja DONDE murio el avion, no donde reaparece. */
static uint8_t   plane_blast;
/* X del choque en 8 bits: plane_x nunca pasa de PLANE_MAX_X (232), asi que el
 * byte alto era siempre 0. */
static uint8_t   plane_blast_x;
static uint8_t   plane_blast_y;
static uint8_t   esperando_fire;   /* 1 = avion fijo, rio parado, espera FIRE */

/* Estado del juego. */
static uint8_t   lives;
static uint8_t   fuel;
static uint16_t  score;
static uint8_t   fuel_tick;      /* contador de frames para drenar combustible */
static uint8_t   game_over;

/* Contacto con el bidon (ver FUEL_REFUEL_*). */
static uint8_t   fuel_touching;   /* 1 = el frame anterior tocaba el bidon */
static uint8_t   fuel_refuel_tick;/* contador de frames de contacto       */

/* Mapa del canal en RAM (perfil por fila):
 *   river_l/r   = bordes de agua de la fila
 *   island_l/r  = isla central que parte el cauce (0xFF = sin division)
 *
 * OJO CON island_r: SE GUARDA el borde derecho REAL, no se deduce. La isla
 * CRECE de 1 a GEN_ISLAND_W columnas dentro de cada bloque, asi que su ancho
 * cambia fila a fila. Antes no existia este array y celda_agua suponia
 * `il + GEN_ISLAND_W - 1`, o sea el ancho MAXIMO: en una fila donde la isla
 * medía 5 columnas la colision creia que medía 9, y el avion moria contra 4
 * columnas de agua que en pantalla se veian azules. ESE era el "choque con
 * algo invisible" del lado derecho de la isla.
 * Medido con tools/verificar_colision.py: 638 celdas discrepantes en 2000
 * filas con el codigo viejo, 0 con este. */
static uint8_t   river_l[VC_MAP_ROWS];
static uint8_t   river_r[VC_MAP_ROWS];
static uint8_t   island_l[VC_MAP_ROWS];
static uint8_t   island_r[VC_MAP_ROWS];

/* PUENTE: 1 si la fila de cinta es la viga (mata salvo el hueco). Se lleva en
 * RAM porque gen_bridge solo vale para la fila que se esta generando. */
static uint8_t   puente_bit[VC_MAP_ROWS];

/* MARCA DE ISLA: vacia. Existio como mapa de bits (256 bytes) para que la
 * animacion del oleaje no repintara tierra, pero esa animacion esta desactivada
 * y nadie mas lo leia, asi que se quito para liberar RAM. Se dejan los macros
 * como no-op para no tocar las llamadas que marcan celdas al pintar la fila. */
#define ISLA_SET(row, col)  ((void)0)
#define ISLA_CLR(row, col)  ((void)0)

/* Deposito de combustible (sprite). */
static uint8_t   fuel_active;
static uint8_t   fuel_x;
static uint8_t   fuel_y;
static uint32_t  fuel_next;

/* ===========================================================================
 * RNG
 * =========================================================================== */
static uint8_t rng_next(void) {
    rng_state ^= (uint16_t)(rng_state << 7);
    rng_state ^= (uint16_t)(rng_state >> 9);
    rng_state ^= (uint16_t)(rng_state << 8);
    return (uint8_t)(rng_state & 0xFF);
}

/* ===========================================================================
 * PATRONES DE TILE
 * =========================================================================== */
/* Cesped con textura sutil: manchas dispersas de verde oscuro (indice 2)
 * sobre verde claro (indice 1). Poco densa para que no se vea repetitiva. */
static const uint8_t tile_grass_p0[8] = { 0xFF,0xFF,0xEF,0xFF,0xFF,0xFB,0xFF,0xFF };
static const uint8_t tile_grass_p1[8] = { 0x00,0x10,0x00,0x00,0x08,0x00,0x00,0x04 };

/* Borde del rio estilo Atari 2600: cesped, linea de ARENA y agua.
 *
 * CLAVE VISUAL: la costa es una DIAGONAL continua a 45 grados, no un escalon.
 *
 * DISPOSICION DE LAS CELDAS (importante, el agua va SIEMPRE pegado al canal):
 *   cesped | borde L (arena -> agua) | agua del rio ... agua | borde R (agua ->
 *   arena) | cesped.
 * El agua del borde va en indice 0 porque es SIEMPRE transparente en el fondo y
 * deja ver el BG_COLOR, que es exactamente el azul del rio (igual que el cauce).
 * Asi el tile de borde solo necesita dos colores (1 = verde y 3 = arena), y el
 * agua se funde con el cauce sin costuras. Si el agua fuese opaca habria un
 * borde recto entre dos azules ligeramente distintos, que es lo que se veia.
 *
 *   indice 1 = verde (continuidad con el cesped)
 *   indice 2 = arena
 *   indice 3 = verde oscuro (sombra de la orilla)
 *   indice 0 = BG_COLOR (azul del agua)
 *
 * Los tiles usan el plano 1 con el MISMO byte en los dos planos, asi que el
 * color es 3 (arena). El plano 0 deja a 0 las columnas de agua, que son las
 * que muestran BG_COLOR. La arena avanza 1 px por mitad de celda entre las dos
 * variantes, para que al encadenarse con el escalon del rio la costa sea una
 * linea continua.
 */
#define E(w, a) ((w) | ((a) << 1))   /* fila 0 = agua | fila 1 = arena */

/* --- Patrones de ORILLA (a mano, NO generados) ---
 *
 * POR QUE A MANO: tools/gen_tiles.py generaba dos variantes (L0 y L1) con DOS
 * peldaños de 4 px cada una, y en game.c solo se cargaba la L0. El resultado
 * eran "dientes cuadrados": el borde de la orilla saltaba 4 px de golpe entre
 * una fila de cinta y la siguiente, en vez de bajar suave.
 *
 * REGLA: el borde CESPED|AGUA baja 1 px por fila de las 8. Asi, al encadenar
 * filas de cinta (que van de 8 en 8 px), la orilla es una recta continua.
 *
 * El patron se lee como columnas de IZQUIERDA a DERECHA (bit 7 = pixel 0):
 *
 *   tile_edge_l0  (orilla IZQUIERDA, el agua queda a la DERECHA)
 *     fila 0:  g g g a a a a .     <- cesped 3 px, arena 4 px, agua 1 px
 *     fila 1:  g g g g a a a .
 *     ...
 *     fila 7:  g g g g g g g .
 *   El agua arranca en la columna 7 SIEMPRE (pegada al cauce) y la arena se
 *   va estrechando mientras el cesped avanza: eso es la diagonal.
 *
 *   tile_edge_r0 = espejo exacto (agua a la IZQUIERDA).
 *
 * En game.c: PAL_EDGE es 1 = verde claro, 2 = arena, 3 = verde oscuro, y el
 * indice 0 deja ver el BG_COLOR (el mismo azul del cauce), asi que la orilla
 * se funde con el rio sin costura.
 *
 * (E() de arriba queda para quien quiera calcular a mano: indice = (p1<<1)|p0) */
/* ORILLA IZQUIERDA: cesped a la izq, agua en la columna 7, arena en medio.
 *
 * OJO CON LA DIAGONAL: NO va dentro del tile. Cada fila de cinta pinta el
 * MISMO patron, asi que una diagonal de 8 px se reinicia en cada tile y la
 * orilla sale como una sierra de triangulos repetidos (visto en el emulador).
 *
 * La inclinacion la da el ESCALON entre filas del cauce (gen_left cambia de
 * 1 en 1 celda), no el dibujo del tile. Por eso aqui el borde es RECTO:
 * 3 columnas de cesped, 4 de ARENA y 1 de agua, igual en las 8 filas. Asi,
 * cuando la fila de al lado tiene el cauce una celda mas a la derecha, el
 * escalon es de 8 px limpios y la orilla se lee como una linea continua. */
static const uint8_t tile_edge_l0_p0[8] = { 0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0 };
static const uint8_t tile_edge_l0_p1[8] = { 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E };
/* ORILLA DERECHA: espejo exacto (agua en la columna 0). */
static const uint8_t tile_edge_r0_p0[8] = { 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 };
static const uint8_t tile_edge_r0_p1[8] = { 0x78, 0x78, 0x78, 0x78, 0x78, 0x78, 0x78, 0x78 };
/* --- FIN ORILLA --- */

/* Arbol/vegetacion para el interior del islote. La forma debe seguir el
 * cuadrante que sustituye (arriba-derecha), para no romper el circulo:
 *   3 = agua, 1 = arena, 2 = vegetacion */

/* INDICE DEL PIXEL DE FONDO: color = (p0 << 1) | p1, o sea PLAN0 lleva el bit
 * ALTO. Con PLAN1 = 0 el indice es 0 o 2; con PLAN1 = 1 es 1 o 3.
 * OJO: el indice 0 es TRANSPARENTE y deja ver lo que haya debajo (el cesped
 * verde). Para pintar una veta hay que emitir un indice NO nulo: por eso las
 * columnas de veta llevan p1 = 1 y p0 = 1 -> indice 3.
 *
 * VIGA DEL PUENTE: dos patrones que ALTERNAN por fila de cinta (A, B, A...)
 * porque un tile suelto se repite identico cada 8 px y la viga parecia lisa.
 *   fondo (tabla) = indice 2     veta (junta) = indice 3
 * En PAL_WOOD: 2 = tabla clara #CC8822, 3 = junta oscura #884411. */
static const uint8_t tile_bridge_p0[8] = { 0xFF,0xFF,0xFB,0xFF,0xFF,0xFE,0xFF,0xFF };
/* PLAN1 lleva un 1 en las mismas columnas que los ceros de PLAN0, para que la
 * veta pase de indice 0 (transparente) a indice 3. */
static const uint8_t tile_bridge_p1[8] = { 0x00,0x00,0x04,0x00,0x00,0x01,0x00,0x00 };


/* CALLE DEL PUENTE (TILE_ROAD): la franja de tierra que llega a los dos lados
 * del cauce, en las mismas filas que la viga. Se pinta con PAL_EDGE, donde el
 * indice 2 es ARENA; el 0 es transparente (dejaria ver el fondo), asi que va
 * con p1=1 -> indice (1<<1)|0 = 2. */
static const uint8_t tile_road_p1[8] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };


/* Fondo del panel de GAME OVER: opaco y en INDICE 1, que en PAL_MENU es el
 * NEGRO. La fuente siempre pinta el indice 3 (BLANCO en PAL_MENU), asi que
 * cada pieza usa un indice distinto. Con un tile transparente (0) se veria el rio. */
/* (tile_panel_p0/p1: fondo del panel de GAME OVER, ELIMINADO junto con el
 * panel. No se usaban y ocupaban bytes de RODATA.) */

/* Agua OPACA: fondo azul claro (indice 3) con oleaje suave en un azul
 * intermedio (indice 2). Se evita SIEMPRE el indice 0 (transparente), que
 * mostraria el BG_COLOR entre las olas. indice = (p1<<1)|p0. */
static const uint8_t tile_water_p0[8]  = { 0xFF,0xFF,0xFF,0xFF,0xEF,0xFF,0xFF,0xFF };
static const uint8_t tile_water_p1[8]  = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

/* Segunda fase del oleaje: la ola baja una fila. Antes solo tenia bits de
 * columnas sueltas (0xF7), asi que el agua parecia llena de puntos. */
static const uint8_t tile_water2_p0[8] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xEF,0xFF };
static const uint8_t tile_water2_p1[8] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

/* ===========================================================================
 * PATRONES DE SPRITE
 * ===========================================================================
 * Regla: 0 = transparente. Para dar volumen se usan dos tonos de la paleta de
 * sprite del objeto. Los colores concretos son los de setup_video(): los
 * presets de include/video.h quedan pisados al arrancar. Cada bloque de aqui
 * abajo dice el color FINAL de cada indice que ese patron emite.
 * =========================================================================== */

/* Explosion: 3 patrones que crecen y se apagan, como el arcade. Los usan
 * tanto el avion como los enemigos (barco y helicoptero comparten estos
 * mismos patrones; solo cambia quien los lanza y el patron elegido).
 *   16 = estrella de chispas (primer instante)
 *   17 = bola llena (el fogonazo)
 *   18 = anillo abierto (el humo que se deshace)
 *
 * USAN LA PALETA DEL AVION (VC_SPPAL_1):
 *   1 = cian #00FFFF   2 = azul #22AAEE   3 = blanco #FFFFFF
 * Escala 2x, o sea 16x16 en pantalla. El cian es el nucleo, el azul el borde
 * del nucleo y el blanco la corona/onda.
 *
 * El AVION es spr_plane (mas abajo, en NAVES), no esta explosion: los dos
 * comparten el slot SPR_BOOM del OAM, por eso draw_boom() se aparta cuando el
 * avion esta explotando. */
static const uint8_t spr_boom_p0[8]  = { 0x00,0x00,0x10,0x10,0x00,0x00,0x10,0x00 };
static const uint8_t spr_boom_p1[8]  = { 0x00,0x10,0x28,0x44,0x10,0x38,0x28,0x00 };
static const uint8_t spr_boom2_p0[8] = { 0x00,0x38,0x6C,0x54,0x44,0x6C,0x38,0x00 };
static const uint8_t spr_boom2_p1[8] = { 0x00,0x38,0x7C,0x6C,0x54,0x7C,0x28,0x00 };
static const uint8_t spr_boom3_p0[8] = { 0x00,0x38,0x44,0x82,0x92,0x82,0x46,0x38 };
static const uint8_t spr_boom3_p1[8] = { 0x00,0x38,0x44,0x92,0xAA,0x92,0x46,0x38 };

/* --- DIGITOS 0..9 (8x8) para el HUD. Paleta 1, un solo color: indice 3 =
 * blanco #FFFFFF. El color 0 es el hueco del digito (transparente). --- */
static const uint8_t hud_digit[10][8] = {
    { 0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00 }, /* 0 */
    { 0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00 }, /* 1 */
    { 0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0x00 }, /* 2 */
    { 0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00 }, /* 3 */
    { 0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0x00 }, /* 4 */
    { 0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00 }, /* 5 */
    { 0x3C,0x66,0x60,0x7C,0x66,0x66,0x3C,0x00 }, /* 6 */
    { 0x7E,0x66,0x0C,0x18,0x18,0x18,0x18,0x00 }, /* 7 */
    { 0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00 }, /* 8 */
    { 0x3C,0x66,0x66,0x3E,0x06,0x66,0x3C,0x00 }  /* 9 */
};

/* Icono de vida: el mismo AVION del juego visto de frente, con las alas en
 * las dos primeras filas (la variante anterior tenia una flecha vertical que
 * no se leia como un avion). Paleta 1, indice 3 = blanco #FFFFFF. */
static const uint8_t hud_lives_pat[8] = { 0x18,0xFF,0x7E,0x3C,0x24,0x18,0x18,0x00 };
/* Variante morada (indices 1 y 2 = azul y cian) para poder comparar las dos
 * en el emulador antes de quedarse con una. */
/* (hud_lives2_pat: icono de vida alternativo morado, ELIMINADO. No se usaba y
 * ocupaba bytes de RODATA que hacian falta.) */

/* Celdas de la barra de combustible (llena / vacia). Paleta 1:
 *   llena = columnas 0-6 en indice 3 (blanco #FFFFFF); la columna 7 queda
 *           transparente para dejar el pixel de separacion.
 *   vacia = marco: solo la fila superior y la inferior, el centro se ve.
 *           El trazo de arriba lleva la columna 7 a color 2 (cian) para
 *           probar la paleta: si se ve blanco, esa fila se quita. */
static const uint8_t hud_bar_on[8]  = { 0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE };
static const uint8_t hud_bar_off[8] = { 0xFE,0x82,0x82,0x82,0x82,0x82,0x82,0xFE };

/* --- NAVES (GENERADO por tools/gen_ships.py) --- */
/* --- Sprites nuevos (export de Piskel, tools/piskel_a_sprite.py) ---
 *
 * Cada frame es 8x8 (64 valores 0xAARRGGBB): alfa 0 = transparente. El core
 * admite 3 colores utiles por sprite (indices 1,2,3; el 0 es transparente),
 * asi que los tres caben. La paleta es POR SPRITE, de ahi que cada uno tenga
 * sus entradas con sus colores.
 *
 * Los colores que siguen son los FINALES (los de setup_video()), porque la
 * paleta se reprograma al arrancar y los presets de video.h ya no valen.
 */

/* AVION del jugador (hacia arriba, escala 2x => 16x16 en pantalla). Paleta 1.
 *   1 = cian #00FFFF (fuselaje)   2 = azul #22AAEE (detalles)   3 = sin usar */
static const uint8_t spr_plane_p0[8] = { 0x10,0x38,0x6C,0xC6,0x92,0x38,0x6C,0x00 };
static const uint8_t spr_plane_p1[8] = { 0x00,0x00,0x10,0x10,0x00,0x00,0x10,0x54 };

/* BARCO (hacia la izquierda, escala 2x). Paleta 2.
 *   1 = negro #000000 (casco)   2 = azul #0000FF (quilla)   3 = rojo #BB0011 */
static const uint8_t spr_boat_p0[8] = { 0x00,0x04,0x0C,0x3E,0x00,0x00,0x3E,0x00 };
static const uint8_t spr_boat_p1[8] = { 0x00,0x00,0x00,0x00,0xFF,0x7F,0x3E,0x00 };

/* HELICOPTERO (hacia la izquierda, escala 2x). Paleta 0 SIN CAMBIOS. Arte
 * nuevo del usuario (colores #29F6F5, #B51100 y negro). Los tres caen
 * exactamente en los colores que la paleta 0 ya tiene:
 *   #29F6F5 -> RGB444 (2,F,F) = cian, ya en el indice 1
 *   #000000 -> RGB444 (0,0,0) = negro, ya en el indice 2
 *   #B51100 -> RGB444 (B,1,0) = rojo,  ya en el indice 3 */
static const uint8_t spr_heli_p0[8] = { 0x00,0xFE,0x40,0x00,0xFF,0x00,0x00,0x00 };
static const uint8_t spr_heli_p1[8] = { 0x00,0x00,0x79,0xFF,0xFF,0x10,0x7C,0x00 };
/* --- FIN NAVES --- */
/* Bala del jugador: 2x6 vertical, escala 1x. Paleta 1, solo el indice 3 =
 * blanco #FFFFFF; los indices 1 y 2 no se emiten. */
static const uint8_t spr_bullet_p0[8] = { 0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00 };
static const uint8_t spr_bullet_p1[8] = { 0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00 };

/* Deposito de combustible: bidon, arte nuevo del usuario. Paleta 3 SIN CAMBIOS.
 * El dibujo original venia en azul/blanco; se ha REMAPEADO a los colores que
 * la paleta 3 ya tenia, sin tocar la paleta:
 *   #05A4FA (azul claro)  -> indice 2 = naranja #FFDD88
 *   #0505FA (azul oscuro) -> indice 3 = rojo    #DD1111
 *   #FFFFFF (blanco)      -> indice 1 = blanco  #FFEEEE
 * Forma (2 = naranja, 3 = rojo, 1 = blanco):
 *    ..2222..
 *    .322223.
 *    .233332.
 *    .211122.
 *    .312223.
 *    .211332.
 *    .312223.
 *    ..3333.. */
static const uint8_t spr_fuel_p0[8] = { 0x00,0x42,0x3C,0x38,0x62,0x3C,0x62,0x3C };
static const uint8_t spr_fuel_p1[8] = { 0x3C,0x7E,0x7E,0x46,0x5E,0x4E,0x5E,0x3C };

/* ===========================================================================
 * PROTOTIPOS (funciones usadas antes de su definicion)
 * =========================================================================== */
static uint8_t fire_bullet(void);
static void hud_draw(void);
static void game_over_screen(void);
static void plane_place_safe(void);
static uint8_t celda_agua(uint8_t cinta_row, uint8_t c);

/* ===========================================================================
 * ENTRADA
 * =========================================================================== */
/* FIRE pulsado? Se mira el joystick y la tecla ' ' del UART, sin efectos
 * secundarios (no dispara): es el "arrancar" de la espera inicial.
 *
 * OJO: esta funcion es la UNICA que lee el UART durante la espera de FIRE, y
 * se lleva por delante todo lo que no sea FIRE. Por eso la 'q' tambien se
 * atiende AQUI: si no, la tecla de salir se tiraba a la basura justo en la
 * pantalla de inicio y en la espera tras cada muerte, que es donde uno la
 * prueba primero. Deja quit_flag puesto y devuelve 0 (no arranca la partida). */
static uint8_t fire_pressed(void) {
    uint8_t j = joy_read();

    if (j & JOY_A_FIRE) return 1;
    while (rom_uart_rx_ready()) {
        char c = rom_uart_getc();
        if (c == ' ' || c == 'f' || c == 'F') return 1;
        if (c == 'q' || c == 'Q') { quit_flag = 1; return 0; }
    }
    return 0;
}

static void read_input(void) {
    uint8_t j = joy_read();

    if (j & JOY_A_LEFT)  { if (plane_x > PLANE_MIN_X) plane_x -= PLANE_SPEED; }
    if (j & JOY_A_RIGHT) { if (plane_x < PLANE_MAX_X) plane_x += PLANE_SPEED; }
    /* ARRIBA/ABAJO no mueven el avion: aceleran o frenan el scroll (como el
     * arcade). El avion va a altura FIJA y solo se mueve en horizontal. */
    if (j & JOY_A_UP)    { if (scroll_speed < SCROLL_MAX) scroll_speed++; }
    if (j & JOY_A_DOWN)  { if (scroll_speed > SCROLL_MIN) scroll_speed--; }
    if (j & JOY_A_FIRE)  { fire_bullet(); }

    while (rom_uart_rx_ready()) {
        char c = rom_uart_getc();
        switch (c) {
            case 'a': case 'A': case 'j': case 'J': case '4': case 0x12:
                if (plane_x > PLANE_MIN_X) plane_x = (uint8_t)(plane_x - PLANE_SPEED); break;
            case 'd': case 'D': case 'l': case 'L': case '6': case 0x13:
                if (plane_x < PLANE_MAX_X) plane_x = (uint8_t)(plane_x + PLANE_SPEED); break;
            case 'w': case 'W': case 'i': case 'I': case '8': case 0x10:
                if (scroll_speed < SCROLL_MAX) scroll_speed++; break;
            case 's': case 'S': case 'k': case 'K': case '2': case 0x11:
                if (scroll_speed > SCROLL_MIN) scroll_speed--; break;
            case ' ':
                fire_bullet(); break;
            case 'q': case 'Q':
                quit_flag = 1; break;
            default: break;
        }
    }
}


/* ===========================================================================
 * HUD EN SPRITES
 * ===========================================================================
 * El HUD se dibuja con sprites en la franja superior (fuera del tilemap), de
 * modo que el scroll del rio NO puede arrastrarlo. Distribucion (una fila):
 *
 *   [digito score x4]   [barra FUEL x8]   [icono vida][digito de vidas]
 *
 * Los sprites se colocan UNA vez en hud_setup(); hud_draw() solo reescribe el
 * patron de lo que cambia (digitos, celdas de la barra y vidas).
 * =========================================================================== */

/* Dibuja el HUD completo en sprites.
 *
 * Rendimiento: fv_sprite_move() cuesta ~14,6 us medida con el RTC (3 escrituras
 * de OAM mas estacado y calculo del bit 8 de X). Con 16 sprites de HUD eran
 * ~230 us por frame, o sea un 16 % del VBLANK. Por eso solo se reescribe lo que
 * ha CAMBIADO: la posicion se fija una vez (hud_setup) y aqui solo se escribe
 * el patron de los digitos y de la barra cuando su valor cambia.
 */
static uint8_t  hud_last_digit[HUD_SCORE_DIGITS];
static uint8_t  hud_last_slot_on[HUD_FUEL_W];  /* estado de cada pieza de la barra */
static uint8_t  hud_last_lives = 0xFF;

/* Fija posicion y paleta de todos los sprites del HUD (una sola vez). */
static void hud_setup(void) {
    uint8_t i;
    for (i = 0; i < HUD_SCORE_DIGITS; i++)
        fv_sprite_move((uint8_t)(SPR_HUD_BASE + i), (uint16_t)(HUD_SCORE_X + i * 8),
                       HUD_Y, VC_SPPAL_1 | HUD_SCALE);
    for (i = 0; i < HUD_FUEL_W; i++)
        fv_sprite_move((uint8_t)(SPR_HUD_FUEL + i),
                       (uint16_t)(HUD_FUEL_X + i * HUD_FUEL_STEP),
                       HUD_Y2, VC_SPPAL_1 | HUD_SCALE);
    /* Icono en la FILA 1; el numero en la FILA 2 (ver la rejilla de HUD_LIVES_X). */
    fv_sprite_move((uint8_t)(SPR_HUD_LIVES), HUD_LIVES_X, HUD_Y,
                   VC_SPPAL_1 | HUD_SCALE);
    fv_sprite_move((uint8_t)(SPR_HUD_LIVES + 1),
                   HUD_LIVESNUM_X, HUD_Y2, VC_SPPAL_1 | HUD_SCALE);

    /* COLL_POINT a 0 en los 11 sprites del HUD. El barrido de colision del core
     * recorre los 32 sprites SIN filtrar por Y (manual, 6.1), asi que un sprite
     * de HUD con el punto de colision sobre una celda solida encenderia
     * SOLID_HIT y el juego lo leeria como que el AVION toco tierra.
     *
     * Se hace con UN bucle de 0 a 10 porque el HUD ocupa slots CONSECUTIVOS
     * (SPR_HUD_BASE .. SPR_HUD_LIVES+1 = 0..10). Once llamadas sueltas costaban
     * ~99 bytes y dejaban el stack a cero de margen. */
    for (i = 0; i <= SPR_HUD_LIVES + 1; i++)
        fv_oam(i, VC_OAM_COLL, 0);
    for (i = 0; i < HUD_SCORE_DIGITS; i++)
        hud_last_digit[i] = 0xFF;
    hud_last_lives = 0xFF;
}

/* Dibuja el HUD completo en sprites (solo lo que cambia). */
static void hud_draw(void) {
    uint8_t i;
    /* OJO: SIN modulo. Antes era `score % 10000` y el marcador volvia a 0 al
     * pasar de 9999. El score es uint16_t (max 65535) y con 5 digitos cabe
     * entero. */
    uint16_t v = score;

    /* Puntos: HUD_SCORE_DIGITS digitos, de izquierda a derecha. */
    for (i = 0; i < HUD_SCORE_DIGITS; i++) {
        uint16_t div = 1;
        uint8_t k;
        uint8_t d;
        for (k = (uint8_t)(HUD_SCORE_DIGITS - 1 - i); k; k--)
            div = (uint16_t)(div * 10);
        d = (uint8_t)((v / div) % 10u);
        if (d != hud_last_digit[i]) {
            hud_last_digit[i] = d;
            fv_oam((uint8_t)(SPR_HUD_BASE + i), VC_OAM_TILE,
                       (uint8_t)(HUD_PAT_DIGIT0 + d));
        }
    }

    /* Barra de combustible: X = HUD_FUEL_X + i * HUD_FUEL_STEP. Cada pieza
     * ocupa 9 px (7 de relleno + 2 de hueco), asi que el CENTRO de la pieza i
     * cae en +4 y consulta la celda (4 / 9) = 0: cada pieza es una celda. */
    {
        uint8_t filled = (uint8_t)(((uint16_t)fuel * HUD_FUEL_W) / FUEL_MAX);
        for (i = 0; i < HUD_FUEL_W; i++) {
            uint16_t center = (uint16_t)(HUD_FUEL_X + i * HUD_FUEL_STEP + 4);
            uint8_t cell = (uint8_t)((center - HUD_FUEL_X) / HUD_FUEL_STEP);
            uint8_t on   = (uint8_t)(cell < filled);

            if (on != hud_last_slot_on[i]) {
                hud_last_slot_on[i] = on;
                fv_oam((uint8_t)(SPR_HUD_FUEL + i), VC_OAM_TILE,
                           (uint8_t)(on ? HUD_PAT_BAR_ON : HUD_PAT_BAR_OFF));
            }
        }
    }

    /* Icono de vida + numero de vidas. */
    if (lives != hud_last_lives) {
        hud_last_lives = lives;
        fv_oam(SPR_HUD_LIVES, VC_OAM_TILE, HUD_PAT_LIVES);
        fv_oam((uint8_t)(SPR_HUD_LIVES + 1), VC_OAM_TILE,
                   (uint8_t)(HUD_PAT_DIGIT0 + (lives % 10)));
    }
}



/* ===========================================================================
 * PERFIL DEL RIO POR FILA
 * ===========================================================================
 * Para que el cauce no sea una banda fija (el rio se abre en dos y vuelve a
 * juntarse), cada fila de la cinta guarda su propio perfil:
 *
 *   river_l[r] / river_r[r]   : columnas de AGUA utiles (bordes del cauce)
 *   island_l[r]               : borde IZQUIERDO de la isla central que parte
 *                               el cauce (0xFF = el cauce es uno solo). El
 *                               borde derecho NO se guarda: es siempre
 *                               island_l + GEN_ISLAND_W - 1.
 *
 * Con la isla central el agua queda a su izquierda y a su derecha: dos brazos.
 * =========================================================================== */
/* Estado del generador (persistente entre filas).
 *
 * LFSR del ORIGINAL (Carol Shaw). Rutina del 2600, reproducida tal cual:
 *
 *      LDA SEED_HI
 *      LSR              ; C = bit0 de SEED_HI ; SEED_HI >>= 1
 *      BCC NoXor
 *      EOR #$B4         ; SEED_HI ^= $B4 si el bit que salio era 1
 *   NoXor:
 *      STA SEED_HI
 *      ROR SEED_LO      ; SEED_LO rota metiendo el CARRY del LSR
 *
 * OJO: el bit que entra en SEED_LO es el CARRY, no el LSB de SEED_LO (el
 * pseudocodigo del .docx lo pone mal). Ver doc/LFSR-HALLAZGOS.md.
 *
 * El registro se avanza UNA vez por BLOQUE (GEN_BLOCK filas). De cada valor:
 *   bits 0-3 -> ancho objetivo del cauce
 *   bits 4-5 -> 00 = el cauce se parte en dos (tierra en medio)
 *   bits 6-8 -> entidad del bloque (0 fuel, 1 barco, 2 heli, 3 jet, 4 edificio)
 *
 * GENERACION POR INTERPOLACION (opcion elegida): el ancho objetivo cambia de
 * golpe cada bloque, pero el cauce se mueve hacia el a razon de 1 celda por
 * fila. Asi el borde nunca "vibra" (0 saltos medidos) y la partida del cauce
 * nace y muere progresivamente, sin piezas sueltas ni islas fantasma. */
/* ===========================================================================
 * GENERADOR DEL CAUCE (LFSR + CURVA + ISLA + SECTOR)
 * ===========================================================================
 * Reparto de bits elegido: el de la "Especificacion Tecnica Profunda"
 * (docs/Especificación Técnica Profunda ... .md), no el del .docx corto.
 * Los dos documentos NO coinciden, y este es el unico que da CURVA:
 *
 *   bits 0-3  -> ANCHO del cauce        W = (va & 0x0F) + 4 celdas (4..19)
 *   bits 4-6  -> CURVA (desplaza el centro)  C = ((va >> 4) & 7) - 3 celdas
 *   bits 7-8  -> ISLA (parte el cauce)      00 = si
 *   bits 9-11 -> ENTIDAD del bloque
 *
 * Ademas, en bloques de 32 filas y 16 bloques por sector:
 *   bloque 15 del sector -> EMBUDO: el cauce se cierra y se centra para que
 *                           el puente encaje (como el original)
 *   bloque 16 del sector -> PUENTE (marca de nivel)
 *
 * INTERPOLACION: el borde no salta de un bloque al siguiente. Cada fila mueve
 * el borde 1 celda hacia el objetivo del bloque (lo dice el documento:
 * "interpolacion lineal scanline por scanline"). Asi el cauce serpentea y se
 * ensancha sin escalones de golpe y sin vibracion.
 * =========================================================================== */
/* OJO: GEN_BLOCK tiene que DIVIDIR A 32 (filas de la cinta de scroll).
 *
 * Con 40 el nivel de ancho cambiaba a mitad de cinta (n=40 -> cinta 8) y al
 * envolver en n=32 la forma quedaba PARTIDA en dos trozos: una mitad seguia en
 * pantalla acortandose y la otra bajaba. Ese era el "el cauce se va componiendo
 * sobre el anterior". Con un divisor de 32 cada tramo de ancho ocupa filas
 * contiguas de la cinta, asi que baja ENTERO y sale por abajo. */
#define GEN_BLOCK      16     /* filas de mundo por bloque (medio tramo)  */
/* BLOQUES POR TRAMO: 12 -> 12*16 = 192 filas entre puentes.
 * A SCROLL_NORM (8/8 = 1 px/frame) son 1536 px = 1536 frames = ~25,6 s a
 * 60 Hz. Antes eran 16 bloques (256 filas, ~34 s) y el usuario notaba que la
 * dificultad tardaba demasiado en subir.
 * GEN_BLOCK sigue siendo 16 porque TIENE que dividir a 32; el que cambia es
 * cuantos bloques hay por tramo. */
#define GEN_SECTOR     12     /* bloques entre puentes (1 puente cada uno)*/
#define GEN_AHEAD      2      /* filas de cinta fuera de la vista (ver
                               * advance_scroll): 32 de cinta - 30 visibles */
/* ANCHO: el usuario lo quiere ancho de verdad. El avion mide 16 px (2 celdas)
 * y la orilla ocupa 1 celda mas a cada lado, asi que un cauce de 7 celdas deja
 * solo 5 de agua = 40 px de recorrido lateral: el jugador lo vive como "choco
 * con algo invisible" porque a un toque de la tecla ya toca la orilla. El
 * minimo se sube para que quepan varias columnas de juego a cada lado. */
#define GEN_W_MIN      9      /* ancho minimo del cauce en el primer tramo */
#define GEN_W_HARD_MIN 7      /* ... y en el ultimo (dificultad maxima)    */
#define GEN_W_MAX      22     /* ancho maximo sin isla (cabe en la cinta) */
#define GEN_CURVE_MAX  3      /* desplazamiento maximo del centro         */
/* ISLA: GEN_ISLAND_W es el HUECO central (suelo + las dos orillas), o sea las
 * columnas de tierra que se ven. Con 9 columnas y dos brazos de 5 el cauce se
 * abre a GEN_SPLIT_W = 19 celdas. */
#define GEN_ARM_MIN    5      /* ancho MINIMO de cada brazo (tramo duro).
                              * OJO: gen_arm_now() NO usa esta constante, usa
                              * la columna PROG_BRAZO de prog_tab[] (que baja
                              * a 5 en el nivel 7). Se deja aqui para que el
                              * nombre no mienta si alguien lo busca. */
#define GEN_ARM_EASY   8      /* ancho de cada brazo en el primer tramo    */
#define GEN_ISLAND_W   9      /* isla visible = 9 columnas de tierra      */
/* ISLA MINIMA, en COLUMNAS VISIBLES (costa + cesped + costa).
 *
 * gen_split_w cuenta columnas que se ven, no celdas de cesped: con 3 columnas
 * la isla es "arena, cesped, arena", o sea UNA sola celda de cesped de 8 px.
 * Eso sigue siendo una tira que aparece y desaparece con el scroll y mata sin
 * que se vea ("choque contra algo invisible"), asi que el minimo se pone en 5
 * = 3 celdas de cesped entre las dos costas. */
#define ISLE_MIN_COLS  5
/* Con isla el cauce se abre a GEN_SPLIT_W: el rio se ensancha para que quepan
 * la tierra central y los dos brazos MAS ANCHOS (los del arranque). */
#define GEN_SPLIT_W    (GEN_ISLAND_W + 2 * GEN_ARM_EASY)  /* 23 */

/* ===========================================================================
 * PROGRESION DE DIFICULTAD: UNA TABLA, UN NIVEL POR PUENTE
 * ===========================================================================
 * Cada puente cruzado sube un nivel. El nivel satura en NIVEL_MAX, o sea que a
 * partir del puente 6 la dificultad se queda quieta (no se estrecha ni se
 * acelera sin fin: llega un punto en que el juego es injugable).
 *
 *   nivel | puentes | barcos   | helis    | bidon | ancho min
 *   ------+---------+----------+----------+-------+----------
 *     0   |    1    | quieto   | ausente  |  18   |    20
 *     1   |    2    | quieto   | quieto   |  20   |    19
 *     2   |    3    | 1 px/4f  | ausente  |  22   |    18
 *     3   |    4    | 1 px/3f  | 1 px/3f  |  26   |    17
 *     4   |    5    | 1 px/2f  | 1 px/2f  |  30   |    16
 *     5   |    6    | 1 px/1f  | 1 px/1f  |  34   |    15
 *     6   |    7    | 1 px/1f  | 1 px/1f  |  36   |    14
 *     7   |    8    | 1 px/1f  | 1 px/1f  |  38   |    13
 *     8   |   9+    | 1 px/1f  | 1 px/1f  |  40   |    12
 *
 * "1 px/Nf" = se desplaza 1 px de lado cada N frames. 0 = NO se desplaza de
 * lado (sigue bajando con el scroll, o sea que se le puede disparar y chocar).
 *
 * El HELICOPTERO ausente (HX = 0) no aparece hasta el nivel 2: los dos
 * primeros tramos son solo barcos.
 *
 * CAUCE: el ancho minimo es el suelo que gen_w_min_now() impone al bloque.
 * Ademas de estrecharse, deja menos agua entre orillas = mas estrecho de
 * verdad.
 *
 * DE LOS NIVELES 6-8 (ancho 14, 13 y 12): el ancho cuenta las DOS orillas
 * (1 celda cada una), asi que al nivel 8 el jugador tiene 10 celdas de agua =
 * 80 px. El avion mide 16 px: cabe, pero el margen por toque de tecla es de
 * pocos px y ahi ya no hay perdon. 12 es el SUELO del juego: por debajo se
 * vuelve el "choque con algo invisible" que costo tanto quitar, porque el
 * jugador no ve donde esta el limite real del cauce. Si algun dia se toca,
 * que sea con una partida grabada delante, no a ojo. */
#define NIVEL_MAX  8

/* Columnas de la tabla. */
#define PROG_BARCO   0   /* frames entre pasos de lado del barco (0=quieto) */
#define PROG_HELI    1   /* ... del helicoptero (0 = no aparece)            */
#define PROG_BIDON   2   /* filas de mundo entre bidones                    */
#define PROG_ANCHO   3   /* ancho minimo del cauce                          */
#define PROG_BRAZO   4   /* ancho minimo de cada brazo junto a la isla      */
#define PROG_COLS    5

static const uint8_t prog_tab[NIVEL_MAX + 1][PROG_COLS] = {
    /* barco  heli  bidon  ancho  brazo */
    {     0,     0,    18,    20,     8 },   /* nivel 0: puente 1 */
    {     0,     4,    20,    19,     8 },   /* nivel 1: puente 2 */
    {     4,     0,    22,    18,     7 },   /* nivel 2: puente 3 */
    {     3,     3,    26,    17,     7 },   /* nivel 3: puente 4 */
    {     2,     2,    30,    16,     6 },   /* nivel 4: puente 5 */
    {     1,     1,    34,    15,     6 },   /* nivel 5: puente 6 */
    {     1,     1,    36,    14,     6 },   /* nivel 6: puente 7 */
    {     1,     1,    38,    13,     5 },   /* nivel 7: puente 8 */
    {     1,     1,    40,    12,     5 }    /* nivel 8: puente 9+ */
};

/* Nivel actual, derivado del tramo (gen_sector empieza en 1).
 * Va mas abajo, junto a gen_sector, porque necesita esa variable. */
static uint8_t nivel_actual(void);
static uint8_t nivel_val(uint8_t col);

/* (GEN_RAMP_SECTORS y su comentario se eliminaron: la progresion del ancho ya
 * no se calcula por tramos, sale de prog_tab[].) */

/* PUENTE: coordenadas FIJAS (documento de puentes, X1=34 X2=66 sobre 100).
 * Sobre 100 de ancho de pantalla -> 13,6..26,4; en nuestras 40 columnas
 * visibles eso es 13..26 en el CAUCE, o sea 12..27 contando las orillas. El
 * embudo converge aqui para que el puente encaje igual todos los tramos y el
 * cauce se vea ANGOSTARSE al llegar y ENSANCHARSE despues. */
#define GEN_PIER_L     12     /* borde izquierdo del cauce en el puente   */
#define GEN_PIER_R     27     /* borde derecho (= 12..27 con orillas)     */
#define GEN_FUNNEL_BLK 10     /* primer bloque de embudo (10 y 11 de 12)  */
/* PUENTE MACIZO: el travesaño cruza el cauce ENTERO, sin hueco. En el River
 * Raid de verdad el avion vuela a la altura del puente, asi que no hay forma de
 * esquivarlo: si llegas sin haberlo destruido, chocas. Antes habia un hueco de
 * dos celdas (19..20) por el que se pasaba de lado, y eso NO es del original. */
/* ALTO DEL PUENTE en filas de mundo (8 px cada una). El bloque del puente dura
 * GEN_BLOCK = 16 filas (128 px de pantalla), o sea media pantalla de viga: en
 * el arcade el puente es un travesaño fino. Solo las PRIMERAS filas del bloque
 * llevan viga; las demas se pintan como agua. OJO: el bloque entero sigue
 * siendo de puente para el checkpoint (gen_bridge no cambia). */
#define BRIDGE_ROWS    3

/* Limites de la zona de agua dentro de la cinta (deja cesped a los lados). */
#define GEN_ZONE_L     7      /* primera columna jugable                  */
#define GEN_ZONE_R     33     /* ultima  (cinta de 64, 40 visibles)       */

static uint8_t  gen_seed_hi = 0xA8;   /* SEED_HI del original */
static uint8_t  gen_seed_lo = 0x14;   /* SEED_LO del original */
static uint8_t  gen_left;             /* borde izquierdo de agua (fila)   */
static uint8_t  gen_right;            /* borde derecho de agua (fila)     */
static uint8_t  gen_t_left;           /* objetivo del bloque en curso     */
static uint8_t  gen_t_right;          /* (se interpola 1 celda por fila)  */
static uint8_t  gen_split_on;         /* 1 = este tramo tiene tierra      */
static uint8_t  gen_split_w;          /* ancho ACTUAL de la isla (1..9)   */
/* Ancho del cauce en la ultima fila CON isla: al desaparecer la tierra el cauce
 * volvia en pocas filas al ancho del bloque y dejaba un bache. Mientras la
 * curva pida menos, se mantiene este ancho. */
static uint8_t  gen_w_tras_isla;
static uint8_t  gen_split_target;     /* semiancho objetivo de la tierra  */
static uint8_t  gen_funnel;           /* 1 = bloque de embudo (pre-puente)*/
static uint8_t  gen_bridge;
/* 1 = la proxima fila que se genere sera el puente de arranque (ver
 * gen_reset()). Lo consume generate_world_row(). */
static uint8_t  gen_puente_inicial;           /* 1 = esta fila es del PUENTE      */
static uint8_t  gen_block_row;        /* filas desde el ultimo bloque     */
static uint8_t  gen_block_idx;        /* bloque dentro del sector (0..15) */
static uint16_t gen_sector;           /* numero de sector (= puente)      */

/* --- NIVEL DE DIFICULTAD -------------------------------------------------
 * Sale de gen_sector, que es el tramo en el que vamos (1 = primero). El nivel
 * satura en NIVEL_MAX, asi que a partir del puente 6 la dificultad no cambia.
 * Al morir, gen_restore_checkpoint() retrocede gen_sector, con lo que el nivel
 * baja tambien: la dificultad del tramo es la del tramo, no la de la partida. */
static uint8_t nivel_actual(void) {
    uint16_t n = (uint16_t)(gen_sector - 1);
    if (n > NIVEL_MAX) n = NIVEL_MAX;
    return (uint8_t)n;
}

/* Valor de la tabla para el nivel actual. */
static uint8_t nivel_val(uint8_t col) {
    return prog_tab[nivel_actual()][col];
}

/* CHECKPOINT (documento de puentes): al cruzar un puente se guarda el estado
 * del generador; al morir se restaura y el tramo se vuelve a empezar. Asi cada
 * puente marca un nivel de verdad. */
static uint8_t  cp_hi, cp_lo;         /* semilla guardada                 */
static uint8_t  cp_block_row;         /* filas dentro del bloque          */
static uint8_t  cp_block_idx;         /* bloque del tramo                 */
static uint16_t cp_sector;            /* numero de puente                 */
static uint8_t  gen_crossed;          /* 1 = se acaba de cruzar un puente */

/* Un paso del LFSR: reproduce exactamente el ASM del 2600. */
static void gen_lfsr_step(void) {
    uint8_t carry = (uint8_t)(gen_seed_hi & 1);

    gen_seed_hi >>= 1;
    if (carry) gen_seed_hi ^= 0xB4;                 /* EOR #$B4 */
    gen_seed_lo = (uint8_t)((gen_seed_lo >> 1) | (uint8_t)(carry << 7)); /* ROR */
}

/* Avanza el semiancho hacia el objetivo del bloque. El cauce se mueve como
 * mucho 1 celda por fila y SOLO cuando el bloque lo pide (cada GEN_BLOCK
 * filas): entre bloques el agua es una pared recta, como en el arcade. Mover
 * el borde TODAS las filas dejaba 30 anchos distintos en pantalla y la orilla
 * se veia a jirones (bug detectado con capturas seguidas). */
static uint8_t gen_curve_toward(uint8_t cur, uint8_t target) {
    if (cur < target) return (uint8_t)(cur + 1);
    if (cur > target) return (uint8_t)(cur - 1);
    return cur;
}

/* Ancho del cauce segun los bits 0-3 del LFSR.
 *
 * OJO: `(bits & 0x0F) + 4` da 4..19 celdas sobre una zona de 27, y medido con
 * el LFSR real los 7 primeros bloques caen en 4..9: el arranque salia en un
 * canal de 6 celdas, casi una rendija. El documento asume una pantalla de 100
 * unidades, no nuestras 27 celdas; aqui se reparte en el rango jugable con una
 * TABLA: la escala lineal dejaba los estrechamientos casi sin usar y el cauce
 * pasaba casi siempre por los mismos dos anchos.
 *
 * SE DEJA COMO TABLA y no como formula: se probo `GEN_W_MIN + (bits*13)/15` y
 * difiere en 7 de los 16 valores (9,9,10,11... frente a 9,10,11,12...), o sea
 * que CAMBIA EL TRAZADO DEL RIO. Son 16 bytes y el comportamiento es el
 * validado; no merece la pena arriesgar la forma del cauce por ellos. */
static const uint8_t gen_width_tab[16] = {
    9, 10, 11, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22
};

static uint8_t gen_width_from(bits) {
    return gen_width_tab[bits & 0x0F];
}

/* --- PROGRESION: de facil (tramo 1) a dificil (a partir del nivel 5) ---
 *
 * Se aplica SOLO aqui, al generar un bloque nuevo. Nunca se resta nada a
 * gen_left/gen_right ya calculados: como la curva se interpola 1 celda por
 * fila, cualquier resta se deshace en la fila siguiente y el ajuste no llega a
 * verse. Por eso el minimo se aplica sobre el ANCHO OBJETIVO del bloque.
 *
 * El valor sale de prog_tab[] (ver arriba), no de una formula: asi la tabla
 * del juego y el codigo no se pueden desincronizar. */
static uint8_t gen_w_min_now(void) {
    return nivel_val(PROG_ANCHO);
}

/* Ancho minimo de cada brazo junto a la isla (columna PROG_BRAZO).
 *
 * Baja de GEN_ARM_EASY a GEN_ARM_MIN: un cauce estrecho tambien tiene que dejar
 * menos agua a cada lado de la isla. */
static uint8_t gen_arm_now(void) {
    return nivel_val(PROG_BRAZO);
}

/* Precalcula el bloque: avanza el LFSR y fija el ANCHO, la CURVA y la ISLA
 * objetivos del tramo. Los bordes actuales se acercan a ellos 1 celda por
 * fila (interpolacion). */
static void gen_new_block(void) {
    uint8_t w, c, center, l, r;

    gen_lfsr_step();

    /* --- Sector: 16 bloques y el 16 es el puente --- */
    gen_block_idx++;
    gen_funnel = 0;
    gen_bridge = 0;

    if (gen_block_idx >= GEN_SECTOR) {
        /* PUENTE: cuenta como nivel y usa el ancho FIJO del puente (el embudo
         * de los bloques 14 y 15 ya ha llevado el cauce hasta ahi). */
        gen_bridge = 1;
        gen_sector++;
        gen_block_idx = 0;
        gen_t_left  = GEN_PIER_L;
        gen_t_right = GEN_PIER_R;
        gen_split_on = 0;
        gen_split_target = 0;
        return;
    }

    if (gen_block_idx >= GEN_FUNNEL_BLK) {
        /* EMBUDO (bloques 14 y 15): el cauce CONVERGE a las coordenadas fijas
         * del puente y se apaga la generacion de islas, para que el puente
         * siempre caiga sobre un cauce centrado y sin obstaculos. */
        gen_funnel = 1;
        gen_t_left  = GEN_PIER_L;
        gen_t_right = GEN_PIER_R;
        gen_split_target = 0;
        gen_split_on = (uint8_t)(gen_split_w != 0);
        return;
    }

    /* --- Ancho del cauce: bits 0-3 (escalado al rango jugable) --- */
    w = gen_width_from(gen_seed_lo);

    /* El cauce NUNCA baja del minimo del tramo: en el arranque 7 celdas y a
     * partir del 4.o puente 5. Sin esto los primeros tramos salian con rendijas
     * de 6 celdas igual que el final, y la curva hacia la dificultad no se
     * notaba. */
    {
        uint8_t wmin = gen_w_min_now();
        if (w < wmin) w = wmin;
    }

    /* --- Curva: bits 4-6 desplazan el CENTRO (el cauce serpentea) --- */
    c = (uint8_t)((gen_seed_hi >> 4) & 0x07);        /* 0..7 */
    if (c > 6) c = 5;                                /* 0..5 */
    if (c >= 3) c = (uint8_t)(c - 3);                /* derecha */
    else        c = (uint8_t)(3 - c);                /* izquierda */
    if (c > GEN_CURVE_MAX) c = GEN_CURVE_MAX;
    {
        uint8_t mid = (uint8_t)((GEN_ZONE_L + GEN_ZONE_R + 1) / 2);
        if (gen_seed_hi & 0x40) center = (uint8_t)(mid + c);
        else                     center = (uint8_t)(mid - c);
    }
    l = (uint8_t)(center - (w / 2));
    r = (uint8_t)(l + w - 1);
    if (l < GEN_ZONE_L) { l = GEN_ZONE_L; r = (uint8_t)(l + w - 1); }
    if (r > GEN_ZONE_R) { r = GEN_ZONE_R; l = (uint8_t)(r - w + 1); }
    gen_t_left  = l;
    gen_t_right = r;

    /* --- Isla: bits 7-8, solo si caben dos brazos navegables ---
     * OJO: `((gen_seed_lo >> 7) & 3) == 0` pide dos bits a cero y el LFSR los
     * da a la vez solo el 10% de las veces: la isla salia casi nunca. Se usa
     * el bit 7, que es 1 de cada 2... pero eso daba isla en la MITAD de los
     * bloques (medido: 198 de 400), y con media pantalla de tierra el rio no
     * se ve ANCHO nunca. Se pide ademas que el bloque sea par: 1 de cada 4,
     * que deja tramos de cauce libre entre isla e isla.
     *
     * BUG CORREGIDO: la isla pedia 19 columnas de cauce para empezar a crecer,
     * pero solo se ensanchaba el cauce DESPUES de crecer la tierra. Circulo
     * sin salida: la isla se activaba y moria sin dibujarse (5% de las filas).
     * Ahora, si el bloque lleva isla, el cauce nace con GEN_SPLIT_W = 23. */
    if ((gen_seed_lo & 0x80) == 0 && (gen_block_idx & 1)) {
        /* El ensanche del cauce para que quepa la isla se hace al generar la
         * fila (generate_world_row), donde ya se conoce el ancho real. El
         * objetivo es el ANCHO de la isla en columnas (9), no su semiancho. */
        gen_split_target = GEN_ISLAND_W;
        gen_split_on = 1;

        /* OJO: el bloque tiene que ser ANCHO de verdad. La tierra solo crece
         * si el cauce ya tiene sitio para ella (`ancho >= split_w + 1 +
         * 2*brazo + 2`, o sea 19 columnas para empezar), pero el cauce solo se
         * ensancha DESPUES de que la tierra haya crecido (mas abajo, cuando
         * split_w >= ISLE_MIN_COLS). Con el ancho aleatorio (9..22, casi
         * siempre 11..15) ese circulo no se rompia NUNCA: la isla se activaba
         * y moria en el mismo bloque sin dibujarse ni una fila (medido: solo
         * el 5% de las filas tenia isla). Forzando el ancho del cauce partido
         * (GEN_SPLIT_W = 23) la tierra ya cabe desde el primer frame del
         * bloque y crece 1 columna por fila como estaba previsto.
         *
         * Se reutiliza el `center` de arriba y se acota igual que el caso
         * normal: sin variables temporales nuevas (el programa va justo de
         * RAM y cualquier otra costaba 46 bytes de BSS al enlazador). */
        w = GEN_SPLIT_W;
        l = (uint8_t)(center - (w / 2));
        if (l < GEN_ZONE_L || (uint8_t)(l + w - 1) > GEN_ZONE_R) {
            gen_split_on = 0;
            gen_split_target = 0;
        } else {
            gen_t_left  = l;
            gen_t_right = (uint8_t)(l + w - 1);
        }
    } else {
        gen_split_target = 0;
        gen_split_on = 0;
    }

    /* --- Entidad del bloque: bits 9-11 (0 fuel, 1 barco, 2 heli, 3 jet...) - */
}

static void gen_save_checkpoint(void);

/* Reinicia el generador al estado del puente inicial (partida nueva).
 * Devuelve gen_block_row a 1: la fila de mundo 0 YA la ha generado el marcador
 * de posicion de la cinta (cinta = 32 - 0 = 32, no 0), asi que un LFSR limpio
 * en la 0 daria el MISMO nivel que en la 32 y la forma del cauce se repetiria
 * cada 32 filas. Ver generate_world_row(). */
static void gen_reset(void) {
    gen_seed_hi = 0xA8;
    gen_seed_lo = 0x14;
    gen_left    = GEN_ZONE_L;
    gen_right   = GEN_ZONE_R;
    gen_t_left  = gen_left;
    gen_t_right = gen_right;
    gen_split_on = 0;
    gen_split_w = 0;
    gen_split_target = 0;
    gen_funnel  = 0;
    gen_bridge  = 0;
    /* Se entra en el bloque 1 del tramo 1: el bloque 0 es el puente y al
     * arrancar ya se ha cruzado (si no, la partida empieza dentro del puente).
     * OJO: aqui NO se avanza el LFSR; el valor del bloque 0 lo consume
     * gen_new_block(), igual que en todos los demas bloques.
     * OJO TAMBIEN con el valor: gen_new_block() hace gen_block_idx++ nada mas
     * entrar, asi que hay que poner 0 para que el primer bloque del tramo sea
     * el 1. Con 1 se saltaba un bloque y el puente llegaba UNA FILA antes de
     * tiempo: el tramo medido salia de 160 filas en vez de 192. */
    gen_block_row = 0;
    gen_block_idx = 0;
    gen_sector    = 1;
    gen_new_block();

    /* El PRIMER bloque es de PUENTE: asi la partida arranca con el avion recien
     * pasado un puente (como el original) y lo primero que se ve es la calle y
     * la viga justo debajo.
     * OJO: NO vale con poner gen_bridge = 1 aqui, porque la primera fila llama
     * a gen_new_block() (gen_block_row == 0) y lo vuelve a poner a 0. Se usa un
     * flag aparte que consume la primera fila generada. No toca el LFSR: el
     * trazado del rio sigue siendo el de siempre. */
    gen_puente_inicial = 1;

    /* Checkpoint inicial = estado de salida del tramo 1. */
    gen_crossed = 0;
    gen_save_checkpoint();
}

/* Guarda el estado del generador en el ultimo puente cruzado. */
static void gen_save_checkpoint(void) {
    cp_hi        = gen_seed_hi;
    cp_lo        = gen_seed_lo;
    cp_block_row = gen_block_row;
    cp_block_idx = gen_block_idx;
    cp_sector    = gen_sector;
}

/* Vuelve al ultimo checkpoint (al perder una vida). El tramo se reinicia con
 * la MISMA forma que la primera vez, como en el arcade. */
static void gen_restore_checkpoint(void) {
    gen_seed_hi   = cp_hi;
    gen_seed_lo   = cp_lo;
    gen_block_row = cp_block_row;
    gen_block_idx = cp_block_idx;
    gen_sector    = cp_sector;
    gen_crossed   = 0;
}

/* -------------------------------------------------------------------------
 * FILA DE CINTA DE UNA FILA DE MUNDO: cinta = (-n) & 31
 *
 * En este core la celda que se ve arriba es, por la guia del emulador:
 *     fila_visible = ((punto_y + scroll_y) / 8) mod 32
 * El juego hace DECRECER scroll_val para que el terreno baje, y la celda de
 * pantalla y=0 se ve en scroll_val. La fila de mundo que esta en pantalla es
 *     fila_mundo = scroll_total / 8
 * asi que la de arriba tiene que estar en cinta (32 - fila_mundo) & 31, y al
 * crecer fila_mundo el indice BAJA: el mundo sube de indice de cinta, o sea el
 * terreno baja por pantalla con la velocidad real del scroll. Es la misma
 * formula que usa la colision (scroll_val + y) >> 3.
 *
 * No se puede usar n & 31 directamente: eso ordena las filas al reves y el
 * perfil del cauce se movia en contra del scroll (medido: los tramos se
 * acortaban por abajo en vez de bajar enteros).
 *
 * En pantalla se ven 30 filas de cinta (40x30), asi que hay DOS filas de la
 * cinta fuera de la vista: la 31 (por arriba) y la 30 (por abajo). Son las
 * unicas que se pueden reescribir sin tocar lo que ve el jugador.
 * ---------------------------------------------------------------------- */

/* ===========================================================================
 * GENERACION DE UNA FILA DE MUNDO (n)
 * ===========================================================================
 * El cauce (ancho del LFSR, interpolado) y, cuando el bloque lo pide, una
 * TIERRA CENTRAL que parte el agua en dos brazos. La tierra es terreno, no una
 * entidad: crece y se encoge 1 celda por fila, asi que la partida nunca aparece
 * ni desaparece de golpe.
 * =========================================================================== */
/* Genera la fila de mundo n. Devuelve SIEMPRE 1: ninguna fila escribe en
 * vecinas, asi que no se puede pisar la fila que se acaba de pintar. */
static uint8_t generate_world_row(uint32_t n) {
    uint8_t cinta_row = (uint8_t)((VC_MAP_ROWS - n) & (VC_MAP_ROWS - 1));
    uint8_t wave = (uint8_t)((n & 1) ? TILE_WATER2 : TILE_WATER);
    uint8_t col;
    uint8_t il = 0xFF, ir = 0xFF;

    /* --- Bloque del LFSR ---
     * Cada GEN_BLOCK filas se precalcula el siguiente bloque (ancho, curva,
     * isla, entidad y si es embudo o puente). gen_block_row cuenta las filas
     * ya generadas con el bloque actual. */
    if (gen_block_row == 0) {
        gen_new_block();
        /* Puente de arranque: solo la primera fila del mundo.
         * Ademas de marcar la viga se APAGA LA ISLA de golpe: gen_split_on = 0
         * solo impide que la isla CREZCA, pero gen_split_w seguia valiendo 9,
         * asi que las filas de arriba del puente se generaban partidas por una
         * isla que ya no tocaba (se veia una isla larga justo encima del
         * puente de arranque). */
        if (gen_puente_inicial) {
            gen_bridge = 1;
            gen_split_on = 0;
            gen_split_target = 0;
            gen_split_w = 0;
            gen_puente_inicial = 0;
        }
    }

    /* --- VIGA: solo las primeras filas del bloque de puente ---
     * `gen_bridge` marca el BLOQUE entero (para el checkpoint), pero la viga
     * visible dura BRIDGE_ROWS filas. Se decide ANTES de incrementar
     * gen_block_row, que vale 0 en la primera fila del bloque. */
    {
        uint8_t es_viga = (uint8_t)(gen_bridge && gen_block_row < BRIDGE_ROWS);
        /* Un bit por fila: al regenerar hay que poner O quitar el suyo. */
        puente_bit[cinta_row] = es_viga;

        /* Cruzar el puente = nuevo checkpoint, una vez por puente: cuenta en la
         * PRIMERA fila de la viga, no al terminar el bloque (asi la muerte tras
         * pasar el travesaño ya repite desde el puente y no desde antes). */
        if (es_viga) {
            gen_crossed = 1;
        } else if (gen_crossed) {
            gen_save_checkpoint();
            gen_crossed = 0;
        }
    }

    if (++gen_block_row >= GEN_BLOCK) gen_block_row = 0;

    /* --- Ancho y CURVA: los bordes se acercan al objetivo 1 celda por fila ---
     *
     * Interpolacion (documento): no se salta del ancho de un bloque al del
     * siguiente, se DIBUJA la transicion. Asi el cauce serpentea y se ensancha
     * suavemente y el borde nunca vibra. */
    gen_left  = gen_curve_toward(gen_left,  gen_t_left);
    gen_right = gen_curve_toward(gen_right, gen_t_right);
    if (gen_left + 3 > gen_right) {          /* nunca menos de 4 celdas */
        gen_right = (uint8_t)(gen_left + 3);
    }

    /* --- Tierra central: crece y se encoge 1 celda por fila ---
     *
     * La isla se decide ANTES de fijar los bordes, porque es ella la que dicta
     * el ancho MINIMO del cauce: si se calcula despues hay que rehacer los
     * bordes de golpe, y ese salto es justo lo que dejaba dos filas
     * incompatibles pegadas (el sprite cabe en una y no en la de al lado, y el
     * avion muere contra una "pared" que no se ve).
     *
     * ORDEN: PRIMERO se ensancha el cauce (1 celda por lado y fila) y SOLO
     * cuando ya hay sitio la tierra empieza a crecer. Con el orden contrario la
     * tierra pedia 9 columnas de golpe en una fila de 6 y el cauce saltaba de
     * 19 a 23 celdas de una vez. */
    {
        uint8_t ancho = (uint8_t)(gen_right - gen_left + 1);
        uint8_t goal  = (uint8_t)(gen_split_on ? gen_split_target : 0);

        if (gen_split_w < goal) {
            /* Solo crece si la tierra CABE: hace falta hueco para las dos
             * costas de la isla MAS los dos brazos a su ancho actual. Con un
             * margen fijo de 2 celdas (lo que habia antes) la isla crecia en un
             * cauce de 14 con brazos de 5, que necesitan 21: el ensanche no
             * llegaba nunca y los brazos quedaban en 2 celdas pintadas. */
            if (ancho >= (uint8_t)(gen_split_w + 1 + 2 * gen_arm_now() + 2)) {
                gen_split_w++;
            }
        } else if (gen_split_w > goal) {
            gen_split_w--;
        }
    }

    /* EL CAUCE NO SE ESTRECHA JUSTO TRAS UNA ISLA: al desvanecerse la tierra
     * el cauce volvia al ancho del bloque (12 en el tramo 1) y se quedaba asi
     * unas filas, o sea un estrechamiento feo pegado al ensanche. Se recuerda
     * el ancho de la ultima fila CON isla y, mientras la curva lo pida, no se
     * baja de ahi. */
    if (gen_split_w >= ISLE_MIN_COLS) {
        gen_w_tras_isla = (uint8_t)(gen_right - gen_left + 1);
    }

    /* El cauce se reparte alrededor del CENTRO de la zona de agua.
     * Si en esta fila hay isla, el cauce se ENSANCHA para que quepan la
     * tierra y los dos brazos (el original tambien abre el rio). */
    {
        uint8_t center = (uint8_t)((GEN_ZONE_L + GEN_ZONE_R + 1) / 2);
        uint8_t l = gen_left, r = gen_right;
        uint8_t w = (uint8_t)(r - l + 1);
        uint8_t suelo = gen_w_min_now();


        if (gen_split_w >= ISLE_MIN_COLS) {
            /* OJO con las DOS costas: gen_left..gen_right incluye la celda de
             * costa en cada extremo, y la isla tambien se lleva las suyas. El
             * agua navegable de cada brazo es (ancho - isla - 2 costas)/2, asi
             * que para pedir N celdas de brazo el ancho es
             *     isla + 2*N + 2 costas */
            uint8_t need = (uint8_t)(gen_split_w + 2 * gen_arm_now() + 2);
            if (suelo < need) suelo = need;
        }

        /* SUELO del tramo (y de la isla si la hay) en TODAS las filas. El borde
         * NO salta: si hay que ensanchar para que quepan los dos brazos, se
         * abre 1 celda por fila y la isla no se dibuja hasta que cabe. Hacerlo
         * de golpe dejaba dos filas incompatibles pegadas: el sprite de 16 px
         * cabia en una y no en la vecina, y el avion moria contra una "pared"
         * que el jugador no ve.
         *
         * OJO, ESTO ERA EL "DIENTE" DE LA ORILLA: antes se recalculaba
         *     l = center - (w / 2)
         * o sea que se TIRABA el gen_left que acababa de interpolar la linea de
         * arriba y se volvia a centrar el cauce desde cero. Con los ajustes de
         * ancho disparandose en filas alternas, el borde oscilaba 8, 9, 8, 9...
         * y la orilla se veia como dientes de sierra de una celda (medido en
         * RAM: river_l[4..15] = 8,9,8,9,8,9...).
         *
         * Ahora el ajuste se reparte sobre el borde INTERPOLADO: se ensancha
         * quitando (o sumando) la mitad a cada lado, sin tocar el centro. Asi el
         * borde sigue bajando 1 celda por fila como pide gen_curve_toward(). */
        if (w < suelo) {
            /* El suelo se alcanza 1 CELDA POR FILA, no dos: abrir los dos lados
             * a la vez mueve los dos bordes 2 px... 2 CELDAS por fila y eso se
             * ve como un escalon doble (medido en la simulacion: R subia 11,
             * 13, 15, 17...). Se abre alternando: se le quita la mitad al brazo
             * que este mas lejos de la orilla, para que el cauce crezca
             * centrado pero de 1 en 1. */
            if ((r - l) & 1) {
                if (r < GEN_ZONE_R) r++;
            } else {
                if (l > GEN_ZONE_L) l--;
            }
            w = (uint8_t)(r - l + 1);
        } else if (w > GEN_W_MAX) {
            /* Sin isla el cauce no puede ser tan ancho que parezca un mar. Se
             * CIERRA 1 celda por fila, sobre el borde interpolado. */
            if ((r - l) & 1) {
                if (l < r - 3) l++;
            } else {
                if (r > l + 3) r--;
            }
            w = (uint8_t)(r - l + 1);
        } else if (gen_w_tras_isla && w < gen_w_tras_isla &&
                   gen_split_w < ISLE_MIN_COLS) {
            /* Acaba de desaparecer una isla: no se encoge por debajo del ancho
             * que tenia con ella, para no dejar un bache justo despues. Tambien
             * de 1 en 1. */
            if ((r - l) & 1) {
                if (r < GEN_ZONE_R) r++;
            } else {
                if (l > GEN_ZONE_L) l--;
            }
            w = (uint8_t)(r - l + 1);
        }
        /* El ancho ya paso el bache: se suelta la referencia. */
        if (!gen_split_w && gen_w_tras_isla && w >= gen_w_tras_isla) {
            gen_w_tras_isla = 0;
        }

        /* Limites de la zona jugable (la cinta tiene 64 columnas, 40 visibles).
         *
         * OJO: aqui se RECORTA el borde, NO se recalcula un borde a partir del
         * otro. Antes era `r = l + w - 1`, y al topar con el limite el ancho
         * salia mal y el cauce daba un salto de 2 celdas (medido: filas con
         * L=7 R=30 entre filas con L=9 R=32). Se recorta cada lado por su
         * cuenta; si queda muy estrecho, lo vuelve a abrir la rama del suelo en
         * la fila siguiente, ya de 1 en 1. */
        if (l < GEN_ZONE_L) l = GEN_ZONE_L;
        if (r > GEN_ZONE_R) r = GEN_ZONE_R;
        if (r <= l + 1) r = (uint8_t)(l + 2);   /* al menos una celda de agua */

        gen_left  = l;
        gen_right = r;
        /* OJO: NO se tocan gen_t_left/gen_t_right aqui. Si se copian en cada
         * fila con isla, el objetivo del bloque queda machacado con el ancho
         * ensanchado y al salir de la isla el cauce se QUEDA en 19 para
         * siempre (medido: 60 filas seguidas a 19). El objetivo del bloque
         * manda; el ensanche solo afecta a esta fila. */
    }

    /* Tierra central que parte el cauce (no es una entidad: es terreno).
     *
     * Hace falta un MINIMO antes de que cuente como isla: una isla de 1-2
     * celdas es una franja de arena de 8-16 px que aparece pegada a la orilla
     * segun el scroll y mata sin que el jugador vea nada ("choque invisible").
     * Con ISLE_MIN_COLS la tierra no aparece hasta tener cuerpo. */
    if (gen_split_w >= ISLE_MIN_COLS) {
        /* La isla NO se dibuja hasta que el cauce ya se ha ENSANCHADO lo
         * suficiente para que quepan sus dos costas con al menos una celda de
         * agua a cada lado. Si se dibuja antes, la tierra aparece pegada a la
         * orilla justo en la fila en que el borde aun esta abriendose. */
        uint8_t w = gen_split_w;
        uint8_t ancho = (uint8_t)(gen_right - gen_left + 1);
        if (w > GEN_ISLAND_W) w = GEN_ISLAND_W;
        if (ancho < (uint8_t)(w + 4)) {
            /* Faltan brazos: esta fila es agua limpia y la isla sigue
             * creciendo en las siguientes. */
        } else {
            /* La isla crece de 1 a GEN_ISLAND_W columnas VISIBLES (9).
             * gen_split_w guarda ese ancho en curso, no un semiancho: asi los
             * pasos son 1..9 y el estado estable es exactamente 9 columnas de
             * tierra. */
            il = (uint8_t)(gen_left + ((ancho - w) / 2));
            ir = (uint8_t)(il + w - 1);
        }
    }

    /* Guarda el perfil de esta fila ANTES de pintar. Es lo que consulta la
     * colision (plane_on_bank): si no se actualiza, la colision usa datos de
     * otra fila y el avion muere sin obstaculo. */
    river_l[cinta_row]  = gen_left;
    river_r[cinta_row]  = gen_right;
    island_l[cinta_row] = il;
    /* Borde derecho REAL de esta fila (ver island_r). */
    island_r[cinta_row] = ir;
    /* El borde derecho de la isla se reconstruye en celda_agua(): siempre mide
     * GEN_ISLAND_W pegada a il (ver el comentario de island_l). Se guarda
     * `ir` en la variable local solo para pintar la fila. */

    /* --- Pinta la fila en UNA sola pasada, con tile y atributo siempre ---
     *
     * Escritura DIRECTA al puerto del core ($D800/$D801/$D802) en vez de
     * fv_put_cell()/fv_put_attr(): esas funciones suman una llamada, un
     * calculo de fila*64+col y DOS escrituras por celda, y medido con el RTC
     * son ~15 us por celda (64 celdas = casi 1 ms de los 1,43 ms del VBLANK).
     * Aqui la direccion se avanza con INC en vez de recalcularla, o sea ~7 us
     * por celda. El atributo se escribe SIEMPRE (la paleta 0 seria blanca).
     */
    {
        uint16_t cell = (uint16_t)((uint16_t)cinta_row << 6); /* fila*64 */

        for (col = 0; col < VC_MAP_COLS; col++, cell++) {
            uint8_t tile, attr;

            if (col < gen_left || col > gen_right) {
                tile = TILE_GRASS;                       attr = PAL_EDGE;
            } else if (col == gen_left) {
                /* Costa de la ORILLA IZQUIERDA: el agua queda a la DERECHA,
                 * o sea el tile base. */
                tile = TILE_COAST;
                attr = PAL_EDGE;          /* cesped+orilla */
            } else if (col == gen_right) {
                /* Costa de la ORILLA DERECHA: el agua queda a la IZQUIERDA,
                 * o sea el ESPEJO del anterior. */
                tile = TILE_COAST_M;
                attr = PAL_EDGE;          /* cesped+orilla */
            } else if ((il != 0xFF) && (col >= il) && (col <= ir)) {
                /* TIERRA CENTRAL (isla). Aqui la tierra esta en el CENTRO, asi
                 * que las costas miran al contrario que las del mapa:
                 *   - borde IZQUIERDO de la isla -> agua a la IZQUIERDA -> espejo
                 *   - borde DERECHO  de la isla -> agua a la DERECHA  -> base
                 * Antes se usaban los mismos que la orilla y la arena salia
                 * por fuera, pegada al agua, con el cesped por dentro. */
                if (col == il) {
                    tile = TILE_COAST_M;          /* espejo: agua a la izq.  */
                    attr = PAL_EDGE;
                } else if (col == ir) {
                    tile = TILE_COAST;          /* base: agua a la derecha  */
                    attr = PAL_EDGE;
                } else {
                    tile = TILE_GRASS;
                    attr = PAL_EDGE;
                }
                /* Marca la celda como TIERRA para que el oleaje no la pise
                 * (update_water_anim borra huecos si repinta un tile que no es
                 * agua). Se marcaba solo con ISLA_CLR, o sea nunca. */
                ISLA_SET(cinta_row, col);
            } else {
                tile = wave;                             attr = PAL_WATER;
            }

            /* --- PUENTE: travesaño macizo de orilla a orilla -------------
             * Antes tenia un HUECO de dos celdas por el que se pasaba
             * esquivandolo. Eso NO es el River Raid: el avion vuela a la altura
             * del puente, asi que no se esquiva, se DESTRUYE (con misil, o
             * chocando). Aqui la viga cruza el cauce entero. */
            if (puente_bit[cinta_row]) {
                if (col >= gen_left && col <= gen_right) {
                    /* VIGA: mismo patron que la calle, pero en el indice 3 de
                     * PAL_ISLAND (madera) para que se distinga del camino. */
                    /* VIGA: dos variantes que alternan por fila de cinta, para
                     * que la madera no se vea como una banda lisa. */
                    tile = TILE_BRIDGE;
                    attr = PAL_WOOD;           /* 2 = tabla, 3 = junta */
                } else if (col < gen_left || col > gen_right) {
                    /* CALLE: la franja de tierra que llega a los lados del
                     * cauce, en las mismas filas que el puente. Sin esto la
                     * viga "arrancaba del cesped" y no habia camino: es el
                     * puente completo (calle + viga + hueco). */
                    tile = TILE_ROAD;
                    attr = PAL_EDGE;           /* indice 2 = arena */
                }
            }

            VID_ADDR_LO = (uint8_t)cell;
            VID_ADDR_HI = (uint8_t)((cell >> 8) & 0x07);   /* area 0 = tilemap */
            VID_DATA    = tile;
            ISLA_CLR(cinta_row, col);

            VID_ADDR_LO = (uint8_t)cell;
            VID_ADDR_HI = (uint8_t)(VC_AREA_ATTR | ((cell >> 8) & 0x07));
            VID_DATA    = attr;
        }
    }
    return 1;
}

/* Rellena la cinta de scroll de 32 filas y deja la camara como al arrancar.
 *
/* Rellena la cinta de scroll de 32 filas y deja la camara como al arrancar.
 *
 * PUENTE INICIAL: en el original la partida empieza con el avion ya pasado un
 * puente, asi que lo primero que se ve es la calle y la viga justo DEBAJO del
 * avion. Sin esto la partida arranca sobre agua sin nada que mirar.
 *
 * El avion vuela a PLANE_Y (borde inferior), y el eje Y de la OAM crece hacia
 * ABAJO: el puente tiene que caer en las ultimas filas de la cinta (cerca de
 * la 30-31), que son las de abajo. Ver FILA_CINTA(): cinta = (scroll + y) / 8.
 *
 * La usan setup_video() y reset_game(): antes cada una tenia su propia copia
 * del bucle y era facil que se desincronizaran. */
static void llenar_cinta_inicial(void) {
    uint8_t row;

    gen_reset();
    for (row = 0; row < VC_MAP_ROWS; row++) {
        generate_world_row((uint32_t)row);
    }

    world_row    = VC_MAP_ROWS - 2;
    scroll_total = (uint32_t)((VC_MAP_ROWS - 1) * 8);
    scroll_val   = (uint16_t)((0 - scroll_total) & 0xFF);
    scroll_speed = SCROLL_NORM;
    scroll_acc   = 0;
    scroll_step  = 1;
    vid_set_scroll_y(scroll_val);
}

/* ===========================================================================
 * SETUP DE VIDEO
 * =========================================================================== */
static void draw_plane(void);
/* Patrones de fondo y de sprite.
 *
 * vid_clear_vram() expansión de la fuente: borra TODOS los patrones, asi que
 * esto hay que llamarlo despues de cada limpieza (arranque y reinicio con FIRE,
 * porque la partida puede reiniciarse encima del programa sin resetear el core). */
static void load_patterns(void) {
    fv_load_bg_pattern(TILE_GRASS,  tile_grass_p0,  tile_grass_p1);
    fv_load_bg_pattern(TILE_WATER,  tile_water_p0,  tile_water_p1);
    fv_load_bg_pattern(TILE_WATER2, tile_water2_p0, tile_water2_p1);
    /* TILE_ISLAND / _R / _L2 / _R2 no se cargan: el generador nunca asigna esos
     * tiles (la isla se pinta con TILE_GRASS y las dos costas). Cargarlos
     * costaba 64 bytes de RODATA que hacen falta para el stack. */

    /* Transicion tierra->agua: arena y agua. */
    fv_load_bg_pattern(TILE_COAST, tile_edge_l0_p0, tile_edge_l0_p1);
    fv_load_bg_pattern(TILE_COAST_M, tile_edge_r0_p0, tile_edge_r0_p1);

    fv_load_bg_pattern(TILE_BRIDGE,  tile_bridge_p0,  tile_bridge_p1);
    /* TILE_TREE no se carga: nunca se pinta. */
    fv_load_bg_pattern(TILE_ROAD,    tile_bridge_p1,  tile_road_p1);

    fv_load_spr_pattern(SPR_PLANE_PAT, spr_plane_p0, spr_plane_p1);
    fv_load_spr_pattern(SPR_BOAT_PAT,  spr_boat_p0,  spr_boat_p1);
    fv_load_spr_pattern(SPR_HELI_PAT,  spr_heli_p0,  spr_heli_p1);
    fv_load_spr_pattern(SPR_BULLET_PAT, spr_bullet_p0, spr_bullet_p1);
    fv_load_spr_pattern(SPR_FUEL_PAT,   spr_fuel_p0,   spr_fuel_p1);
    fv_load_spr_pattern(SPR_BOOM_PAT,      spr_boom_p0,  spr_boom_p1);
    fv_load_spr_pattern((uint8_t)(SPR_BOOM_PAT + 1), spr_boom2_p0, spr_boom2_p1);
    fv_load_spr_pattern((uint8_t)(SPR_BOOM_PAT + 2), spr_boom3_p0, spr_boom3_p1);

    /* Patrones del HUD (digitos, icono de vida y celdas de la barra).
     * Sin escala: un patron por pieza, todos por debajo de 64. */
    {
        uint8_t d;
        for (d = 0; d < 10; d++)
            fv_load_spr_pattern((uint8_t)(HUD_PAT_DIGIT0 + d), hud_digit[d], hud_digit[d]);
        fv_load_spr_pattern(HUD_PAT_LIVES, hud_lives_pat, hud_lives_pat);
        fv_load_spr_pattern(HUD_PAT_BAR_ON, hud_bar_on, hud_bar_on);
        fv_load_spr_pattern(HUD_PAT_BAR_OFF, hud_bar_off, hud_bar_off);
    }

}

static void setup_video(void) {
    uint8_t row;

    /* Configura el joystick (bits 3-7 del Puerto 1 como entrada). Sin esto,
     * joy_read() lee basura y las direcciones no responden. */
    joy_init();

    vid_wait_ready();
    vid_wait_setup();    /* que el setup del core haya terminado de verdad */
    fv_clear_oam();      /* el OAM NO lo limpia vid_clear_vram(): en hardware
                          * trae basura y el campo TILE daba '7'/'8' ASCII */

    /* COLOR DEL TRAZO DE LA FUENTE ($D818 = 3).
     *
     * Es GLOBAL para toda la fuente y NO se puede elegir por celda: cada celda
     * solo aporta su PALETA (bits 1:0 del atributo), y el color dentro de esa
     * paleta lo fija $D818. Con 3, el texto de una celda sale de la entrada
     * `paleta*4 + 3`, que es lo que asume el comentario de PAL_MENU.
     *
     * VA ANTES DEL SETUP, y ahi esta el motivo: escribir $D818 no repinta nada
     * por si solo, porque los glifos ya estan expandidos en la BSRAM. Solo
     * VID_SETUP los vuelve a expandir con el color nuevo. Como el setup de
     * abajo es el que expande la fuente, el color tiene que estar puesto ya.
     *
     * El core lo deja en 3 al arrancar, pero el juego se recarga encima de si
     * mismo con LOAD/R sin resetear el core, asi que no hay que fiarse. */
    vid_set_font_color(3);

    vid_wait_vblank();
    vid_clear_vram();

    vid_set_bgcolor(VC_RGB444(0x2, 0x6, 0xF));

    /* Paleta 0 = MADERA (la del puente). OJO: el antiguo panel de GAME OVER
     * escribia esta MISMA paleta unas lineas mas abajo y la dejaba en negro
     * (el puente salia negro). Ahora solo la usa la viga:
     *   1 = marron oscuro (no se emite)   2 = tabla clara   3 = junta oscura */
    vid_pal_set_bg(PAL_WOOD, 1, VC_RGB444(0x8, 0x4, 0x1));   /* marron oscuro */
    vid_pal_set_bg(PAL_WOOD, 2, VC_RGB444(0xC, 0x8, 0x2));   /* tabla clara   */
    vid_pal_set_bg(PAL_WOOD, 3, VC_RGB444(0x8, 0x4, 0x1));   /* junta oscura  */

    /* ---- PALETAS DE FONDO (tile) ----
     * Los presets de video.h NO valen: aqui van los colores FINALES, con el hex
     * al lado. Solo se escribe lo que cambia. El indice 0 es transparente y
     * deja ver el BG_COLOR (azul del rio, arriba). */

    /* Paleta 1 (PAL_WATER): agua. 1 = azul #0022AA, 2 = oleaje #1144EE, 3 = azul claro #2266FF. */
    vid_pal_set_bg(PAL_WATER, 1, VC_RGB444(0x0, 0x2, 0xA));
    vid_pal_set_bg(PAL_WATER, 2, VC_RGB444(0x1, 0x4, 0xE));   /* oleaje */
    vid_pal_set_bg(PAL_WATER, 3, VC_RGB444(0x2, 0x6, 0xF));

    /* Paleta 2 (PAL_ISLAND y tierra): 1 = arena #DD8833, 2 = vegetacion #007700,
     * 3 = agua #2266FF. El 3 debe ser el MISMO azul del cauce para que la isla
     * no tenga borde negro. */
    vid_pal_set_bg(PAL_ISLAND, 1, VC_RGB444(0xD, 0x8, 0x3));
    vid_pal_set_bg(PAL_ISLAND, 2, VC_RGB444(0x0, 0x7, 0x0));

    /* Paleta 3 (cesped y orillas): 1 = verde claro #00AA00, 2 = arena #DD8833,
     * 3 = verde oscuro #006600. El agua de la orilla es transparente
     * (indice 0) y deja ver el BG_COLOR, que es el mismo azul del rio.
     *
     * OJO CON EL 3: `vid_pal_set_bg(paleta, color)` escribe la entrada
     * `paleta*4 + color`, asi que (PAL_EDGE, 3) es la entrada 15, y la entrada 15
     * es el BG_COLOR (paleta 3, color 3). O sea: este "verde oscuro de la orilla"
     * es TAMBIEN el fondo de toda la pantalla y el color del texto de las celdas
     * de paleta 3. Cambiarlo no afecta solo al cesped.
     *
     * Aqui no pasa nada porque vid_set_bgcolor() de arriba ya puso el mismo azul
     * en la 15, y este verde lo sobreescribe a proposito para el cesped: el
     * cauce no depende de el (va opaco, sin indice 0). Pero si algun dia se
     * quiere tocar el fondo del rio, hay que tocar tambien esta linea. */
    vid_pal_set_bg(PAL_EDGE, 1, VC_RGB444(0x0, 0xA, 0x0));
    vid_pal_set_bg(PAL_EDGE, 2, VC_RGB444(0xC, 0x8, 0x2));   /* arena: calle del puente */
    vid_pal_set_bg(PAL_EDGE, 3, VC_RGB444(0x0, 0x6, 0x0));   /* = BG_COLOR (entrada 15) */

    /* ---- PALETAS DE SPRITE: solo se escriben las entradas que CAMBIAN ----
     * Las que no aparecen se quedan con el preset de include/video.h. El color
     * 0 es siempre transparente y no se puede reprogramar. */

    /* Paleta 1. Compartida por avion, bala, explosion y HUD. Aqui pasa a ser
     *   1 = amarillo claro #FFFF00   2 = amarillo #FFCC00   3 = blanco #FFFFFF
     *
     * OJO CON EL 1 Y EL 2: los comparte el avion con el HUD. Los DIGITOS del
     * marcador y la barra de combustible usan estos indices (no el 3), asi que
     * cambiarlos cambia tambien esas dos cosas. Solo el icono de vida es
     * blanco. Si algun dia se quiere el HUD entero blanco, hay que cambiar los
     * indices que emiten los patrones de hud_digit y hud_bar_*, no esta paleta.
     *
     * OJO CON EL 3: no basta con dejarlo al preset. El preset solo vale si
     * nadie ha tocado la entrada, y en este core la paleta se reprograma (y el
     * setup de VRAM la puede dejar a medias). Si el 3 no se escribe aqui, el
     * HUD y el avion salen con el color que hubiera de antes: se vio el marcador
     * AMARILLO en vez de blanco. Se escribe SIEMPRE, aunque coincida con el
     * preset; son 9 bytes y el HUD depende de ello. */
    vid_pal_set_spr(SPR_PLANE_PAL, 1, VC_RGB444(0xF, 0xF, 0x0));  /* amarillo claro */
    vid_pal_set_spr(SPR_PLANE_PAL, 2, VC_RGB444(0xF, 0xC, 0x0));  /* amarillo       */
    vid_pal_set_spr(SPR_PLANE_PAL, 3, VC_RGB444(0xF, 0xF, 0xF));  /* blanco         */

    /* Paleta 2. Solo el barco.
     *   1 = negro #000000   2 = azul #0000FF   3 = rojo #BB0011 */
    vid_pal_set_spr(SPR_BOAT_PAL,  1, VC_RGB444(0x0, 0x0, 0x0));  /* negro */
    vid_pal_set_spr(SPR_BOAT_PAL,  2, VC_RGB444(0x0, 0x0, 0xF));  /* azul  */
    vid_pal_set_spr(SPR_BOAT_PAL,  3, VC_RGB444(0xB, 0x0, 0x1));  /* rojo  */

    /* Paleta 0. Solo el helicoptero.
     *   1 = cian #22FFFF   2 = negro #000000   3 = rojo #BB1100 */
    vid_pal_set_spr(SPR_HELI_PAL,  1, VC_RGB444(0x2, 0xF, 0xF));  /* cian  */
    vid_pal_set_spr(SPR_HELI_PAL,  2, VC_RGB444(0x0, 0x0, 0x0));  /* negro */
    vid_pal_set_spr(SPR_HELI_PAL,  3, VC_RGB444(0xB, 0x1, 0x0));  /* rojo  */

    /* Paleta 3. Solo el bidon de combustible.
     *   1 = blanco #FFEEEE (la "F")   2 = naranja #FF8800 (SIN USAR)   3 = rojo #DD1111
     * El 3 se pisa a proposito: el preset lo dejaria blanco y el bidon
     * quedaria invisible sobre el agua. Alli el rojo lo saca setup_video(), no
     * la ROM ni el preset. El naranja del 2 queda libre: si algun dia se
     * redibuja spr_fuel para que emita ese indice, el bidon saldra naranja sin
     * tocar la paleta. */
    vid_pal_set_spr(SPR_FUEL_PAL,  3, VC_RGB444(0xD, 0x1, 0x1));  /* rojo (cuerpo) */
    vid_pal_set_spr(SPR_FUEL_PAL,  1, VC_RGB444(0xF, 0xE, 0xE));  /* blanco (F/asa) */

    load_patterns();

    /* SIN bandas de raster: todo el fondo usa el scroll global. El HUD va en
     * sprites, asi que no necesita banda fija. */
    vid_set_raster(0xFF, 0xFF);
    vid_set_band2_scroll(0, 0);
    vid_set_band3_scroll(0, 0);

    /* Semilla del rio. */
    rng_state = RIVER_SEED;

    /* Reinicia el generador del LFSR al estado del puente 1. El core de video
     * puede traer basura de una ejecucion anterior: se limpia la VRAM antes de
     * pintar para que TODAS las celdas queden escritas por este generador. */
    vid_clear_vram();
    vid_set_scroll_y(0);
    llenar_cinta_inicial();

    plane_place_safe();

    /* Los DOS enemigos. El helicoptero sale un poco mas tarde que el barco para
     * que no nazcan juntos y se note el cambio de enemigo. */
    ene_active[ENEMIGO_BOAT] = 0;
    ene_active[ENEMIGO_HELI] = 0;
    ene_next[ENEMIGO_BOAT]   = 40;
    ene_next[ENEMIGO_HELI]   = 56;

    /* Estado de las balas y muerte. */
    for (row = 0; row < NBULLETS; row++) bullet_alive[row] = 0;
    fire_cooldown = 0;
    plane_dead = 0;
    plane_blast = 0;
    esperando_fire = 1;       /* avion fijo al empezar; arranca con FIRE */

    /* EXPLOSION DE ENEMIGO / BIDON (sprite SPR_BOOM).
     *
     * OJO: esto faltaba aqui y solo se limpiaba en reset_game(). El programa se
     * puede recargar ENCIMA de una ejecucion anterior sin resetear el core (es
     * lo normal con LOAD/R del monitor), y en ese caso BSS conserva lo que
     * hubiera: boom_active puede quedar a un valor cualquiera y draw_boom()
     * pinta una EXPLOSION FANTASMA con la posicion de la partida anterior, que
     * baja con el scroll sin que nadie haya destruido nada. */
    boom_active = 0;
    boom_frames = 0;
    boom_x = 0;
    boom_y = 0;

    /* Idem con el destello del puente: cambia la PALETA del agua, asi que si
     * queda encendido de antes el agua arranca clara toda la partida. */
    puente_flash = 0;

    /* Estado del deposito de combustible. */
    fuel_active = 0;
    fuel_next   = 2;

    /* Estado del juego. */
    lives = MAX_LIVES;
    fuel  = FUEL_MAX;
    score = 0;
    fuel_tick = 0;
    game_over = 0;

    snd_init();

    hud_setup();
    hud_draw();

    /* El avion se pinta YA: la cache de draw_plane() cree que el OAM sigue
     * escrito, pero vid_clear_vram() lo acaba de borrar. Sin esto el avion no
     * aparece hasta que cambie de posicion (y el jugador se queda sin saber
     * donde esta). */
    oam_cache_invalida = 1;
    draw_plane();
}

/* ===========================================================================
 * DIBUJO DE SPRITES (se ejecutan ANTES que la regeneracion pesada)
 * =========================================================================== */
static void draw_plane(void) {
    /* Solo se toca el OAM cuando cambia algo (posicion o parpadeo): escribir
     * los 5 campos del sprite cuesta ~7 us cada uno y el avion se mueve poco.
     *
     * OJO: la cache NO puede sobrevivir a un reinicio de partida. Tras
     * fv_clear_oam() el OAM tiene el patron a 0 y esta cache creeria que ya
     * esta escrito (en hardware se veia el avion como un '7'). */
    static uint16_t last_x = 0xFFFF;
    static uint8_t  last_y = 0xFF;
    static uint8_t  last_flags = 0xFF;
    static uint8_t  blast_visible;   /* 1 = el sprite 5 tiene el fuego puesto */
    uint8_t flags = (uint8_t)(SPR_PLANE_PAL | VC_SPR_SCALE2X);

    if (oam_cache_invalida) {
        last_x = 0xFFFF; last_y = 0xFF; last_flags = 0xFF;
    }

    /* EXPLOSION: mientras dura, en el sitio del avion se ve el fuego y el
     * avion NO se dibuja. Antes el avion simplemente parpadeaba, que no se lee
     * como un choque. Los 3 patrones van de fogonazo a humo. */
    if (plane_blast) {
        uint8_t t = (uint8_t)(PLANE_BLAST_FRAMES - plane_blast);
        uint8_t f = (uint8_t)(t / (PLANE_BLAST_FRAMES / SPR_BOOM_FRAMES));
        if (f >= SPR_BOOM_FRAMES) f = (uint8_t)(SPR_BOOM_FRAMES - 1);

        fv_sprite_disable(SPR_PLANE);
        last_flags = 0xFE;

        fv_sprite_move(SPR_BOOM, plane_blast_x, plane_blast_y,
                       SPR_BOOM_PAL | VC_SPR_SCALE2X);
        fv_oam(SPR_BOOM, VC_OAM_TILE, (uint8_t)(SPR_BOOM_PAT + f));
        blast_visible = 1;
        return;
    }

    /* Si esta muerto y ya no hay fuego, se apaga el sprite del fuego y no se
     * dibuja el avion: se espera a que el jugador pulse FIRE.
     *
     * OJO: hay que APAGAR el sprite de la explosion. Si solo se deja de
     * escribir, el OAM conserva el ultimo patron del fuego y en el punto del
     * choque queda un circulo rojo fijo mientras se espera el FIRE. */
    if (plane_dead) {
        if (blast_visible) {
            fv_sprite_disable(SPR_BOOM);
            blast_visible = 0;
        }
        if (last_flags != 0xFE) {
            fv_sprite_disable(SPR_PLANE);
            last_flags = 0xFE;
        }
        return;
    }

    if (last_flags != flags) {
        /* Primera vez (o vuelve de invisible): coloca todo. */
        fv_sprite_move(SPR_PLANE, plane_x, plane_y, flags);
        fv_oam_tile(SPR_PLANE, SPR_PLANE_PAT);
        last_x = plane_x; last_y = plane_y; last_flags = flags;
    } else if (plane_x != last_x || plane_y != last_y) {
        fv_sprite_move(SPR_PLANE, plane_x, plane_y, flags);
        last_x = plane_x; last_y = plane_y;
    }
}

static void draw_enemy(uint8_t i) {
    /* Igual que el avion: solo se escribe el OAM si cambia (fv_sprite_move y
     * fv_oam cuestan ~7-15 us cada uno). Cada enemigo tiene SU cache: antes
     * compartian el slot 13 y uno apagaba al otro. */
    static uint8_t last_on[NENEMIGOS];
    static uint8_t last_x[NENEMIGOS];
    static uint8_t last_y[NENEMIGOS];
    static uint8_t last_dir[NENEMIGOS];
    static uint8_t inicializado;

    uint8_t spr = (i == ENEMIGO_HELI) ? SPR_HELI : SPR_BOAT;
    uint8_t on  = ene_active[i];
    /* Los dos sprites estan dibujados mirando a la IZQUIERDA. Si el enemigo se
     * mueve hacia la derecha (dir == 0) se voltea con VC_SPR_FLIP_X. */
    uint8_t flip = (uint8_t)((ene_dir[i] == 0) ? VC_SPR_FLIP_X : 0);

    if (oam_cache_invalida || !inicializado) {
        last_on[0] = last_on[1] = 0xFF;
        last_dir[0] = last_dir[1] = 0xFF;
        inicializado = 1;
    }

    if (on == last_on[i] &&
        (!on || (ene_x[i] == last_x[i] && ene_y[i] == last_y[i] && flip == last_dir[i])))
        return;

    if (!on) {
        fv_sprite_disable(spr);
        last_on[i] = 0;
        return;
    }

    if (!last_on[i]) {
        /* Aparicion: coloca posicion, flags y patron. */
        fv_sprite_move(spr, ene_x[i], ene_y[i],
                       (uint8_t)(((i == ENEMIGO_HELI) ? SPR_HELI_PAL : SPR_BOAT_PAL)
                                 | VC_SPR_SCALE2X | flip));
        fv_oam(spr, VC_OAM_TILE,
               (uint8_t)((i == ENEMIGO_HELI) ? SPR_HELI_PAT : SPR_BOAT_PAT));
    } else {
        fv_sprite_move(spr, ene_x[i], ene_y[i],
                       (uint8_t)(((i == ENEMIGO_HELI) ? SPR_HELI_PAL : SPR_BOAT_PAL)
                                 | VC_SPR_SCALE2X | flip));
        /* El FLIP va en las FLAGS: si solo cambia el sentido de marcha (misma
         * posicion), sin esto el enemigo se quedaba mirando al lado contrario. */
        fv_oam(spr, VC_OAM_FLAGS,
               (uint8_t)(((i == ENEMIGO_HELI) ? SPR_HELI_PAL : SPR_BOAT_PAL)
                         | VC_SPR_SCALE2X | flip));
    }
    last_on[i]  = 1;
    last_x[i]   = ene_x[i];
    last_y[i]   = ene_y[i];
    last_dir[i] = flip;
}

static void draw_boat(void) {
    draw_enemy(ENEMIGO_BOAT);
    draw_enemy(ENEMIGO_HELI);
}

static void draw_fuel(void) {
    if (!fuel_active) {
        fv_sprite_disable(SPR_FUEL);
        return;
    }
    fv_sprite_move(SPR_FUEL, fuel_x, fuel_y, SPR_FUEL_PAL | VC_SPR_SCALE2X);
    fv_oam(SPR_FUEL, VC_OAM_TILE, SPR_FUEL_PAT);
}

/* Explosion: parpadea entre sus 2 frames mientras dura.
 *
 * OJO: comparte el sprite 5 con la EXPLOSION DEL AVION (la dibuja draw_plane).
 * Si el avion esta ardiendo, aqui NO se toca el sprite: si se apagara, borraria
 * el fuego del avion y la animacion no se veria nunca (el avion desaparecia y
 * volvia sin fogonazo). */
static void draw_boom(void) {
    if (plane_blast) return;              /* el sprite 5 es del avion ahora */
    if (!boom_active) {
        fv_sprite_disable(SPR_BOOM);
        return;
    }
    fv_sprite_move(SPR_BOOM, boom_x, boom_y, SPR_BOOM_PAL | VC_SPR_SCALE2X);
    fv_oam(SPR_BOOM, VC_OAM_TILE,
               (uint8_t)(SPR_BOOM_PAT + ((boom_frames >> 1) & 1)));
}

static void update_boom(void) {
    if (!boom_active) return;
    boom_y = (uint8_t)(boom_y + scroll_step);    /* acompana al scroll */
    if (boom_frames) boom_frames--;
    else boom_active = 0;
}

/* ===========================================================================
 * DISPARO DEL JUGADOR
 * ===========================================================================
 * Dispara UNA bala hacia arriba desde la nariz del avion. Hay un pool pequeno
 * (NBULLETS) y un cooldown para no generar rafagas al pulsar. Devuelve 1 si
 * disparo.
 * =========================================================================== */
static uint8_t fire_bullet(void) {
    uint8_t i;

    if (fire_cooldown) return 0;

    for (i = 0; i < NBULLETS; i++) {
        if (!bullet_alive[i]) break;
    }
    if (i == NBULLETS) return 0;   /* pool lleno */

    /* Centrada en el MORRO del avion.
     *
     * MEDIDO sobre los patrones (no a ojo):
     *   - el avion se dibuja a escala 2x y su dibujo ocupa las columnas 0..6
     *     del patron, o sea los pixeles plane_x+0 .. plane_x+13, con el centro
     *     exacto en plane_x + 6,5;
     *   - la bala NO va escalada y su dibujo ocupa las columnas 3 y 4, o sea
     *     los pixeles bullet_x+3 y bullet_x+4, con el centro en bullet_x+3,5.
     * Para que los dos centros coincidan: bullet_x = plane_x + 6,5 - 3,5, es
     * decir plane_x + 3. Con el "- 1" de antes la bala salia 3 px a la derecha
     * del morro, que es el desvio que se ve en pantalla. */
    bullet_x[i] = (uint8_t)(plane_x + 3);
    bullet_y[i] = (uint8_t)(plane_y - 2);                   /* justo delante        */
    bullet_alive[i] = 1;
    fire_cooldown = FIRE_COOLDOWN;
    snd_shoot();
    return 1;
}

/* ===========================================================================
 * DIBUJO / ACTUALIZACION DE LAS BALAS
 * =========================================================================== */
static void draw_bullets(void) {
    uint8_t i;
    /* Solo se toca el OAM si la bala aparece/desaparece: mover una bala ya
     * activa solo cuesta un fv_sprite_move. */
    static uint8_t last_alive[NBULLETS] = { 0xFF, 0xFF };

    for (i = 0; i < NBULLETS; i++) {
        if (bullet_alive[i]) {
            if (!last_alive[i]) {
                fv_sprite_move((uint8_t)(SPR_BULLET + i), bullet_x[i], bullet_y[i],
                               SPR_BULLET_PAL);
                fv_oam((uint8_t)(SPR_BULLET + i), VC_OAM_TILE, SPR_BULLET_PAT);
            } else {
                fv_sprite_move((uint8_t)(SPR_BULLET + i), bullet_x[i], bullet_y[i],
                               SPR_BULLET_PAL);
            }
            last_alive[i] = 1;
        } else if (last_alive[i] != 0) {
            fv_sprite_disable((uint8_t)(SPR_BULLET + i));
            last_alive[i] = 0;
        }
    }
}

static void update_bullets(void) {
    uint8_t i;

    if (fire_cooldown) fire_cooldown--;

    for (i = 0; i < NBULLETS; i++) {
        if (!bullet_alive[i]) continue;

        if (bullet_y[i] < BULLET_SPEED) {
            bullet_alive[i] = 0;
            continue;
        }
        bullet_y[i] = (uint8_t)(bullet_y[i] - BULLET_SPEED);
    }
}

/* ===========================================================================
 * COLISIONES (software)
 * ===========================================================================
 * Usamos cajas AABB (vc_box_overlap) para objeto<->objeto y coordenada/celda
 * para objeto<->terreno (islas/orillas).
 *
 * IMPORTANTE: el terreno (islas) es un TILE del tilemap, no consultable desde
 * la lib. Por eso para la colision avion<->isla reconstruimos la celda por
 * coordenada y consultamos una copia en RAM del canal. Para mantenerlo simple
 * en esta fase, tratamos la isla como una banda: si el avion toca la orilla o
 * una isla (por banda de celda) muere.
 * =========================================================================== */

/* Devuelve 1 si el punto (sx,sy) del sprite (8x8) toca una orilla. */
/* Devuelve 1 si el avion pisa cesped o la isla central de su fila.
 * Se usa el CENTRO del avion (no la caja completa) para que el roce visual de
 * las alas con el borde no cuente como choque: el juego debe castigar el
 * cuerpo, no el borde de 1 px. */
/* Devuelve 1 si el avion pisa cesped o la isla central de su fila.
 *
 * OJO con la formula de la fila: el scroll de este juego DECRECE (scroll_val
 * se decrementa para que el terreno baje). La fila de cinta que se ve en la
 * pantalla en la coordenada y es (scroll_val + y) >> 3, pero como el valor
 * del registro es el OFFSET de la camara en sentido contrario, hay que
 * restarlo: se usa (scroll_val + y) con el signo correcto de world_y.
 * Para no depender del signo se usa la MISMA expresion que al dibujar
 * (advance_scroll usa scroll_val directamente), que es la validada.
 */
/**
 * FILA DE CINTA que se ve en la coordenada y de la pantalla.
 *
 * Se usa LA MISMA cuenta que hace el core para dibujar:
 *     fila_visible = (scroll_val + y) >> 3   (mod 32)
 * con scroll_val, el byte que se escribe en el registro. NO con scroll_total:
 * scroll_total crece en sentido contrario al registro, asi que las dos dan
 * filas distintas desde el primer paso y la colision acababa mirando el cauce
 * de otra fila (moria en mitad del agua y sobrevivia sobre la orilla). */
#define FILA_CINTA(y)  ((uint8_t)(((scroll_val + (y)) >> 3) & (VC_MAP_ROWS - 1)))

/* Devuelve 1 si la celda es AGUA NAVEGABLE (ni orilla, ni isla, ni viga).
 *
 * Es la UNICA fuente de verdad para "aqui se puede estar": la usan la colision
 * del avion, los rebotes de barcos y helicopteros y la aparicion de bidones y
 * enemigos. Tenerla duplicada fue el origen de que cada cosa consultara el
 * perfil a su manera y hubiera objetos sobre el cesped. */
static uint8_t celda_agua(uint8_t cinta_row, uint8_t c) {
    uint8_t l  = river_l[cinta_row];
    uint8_t r  = river_r[cinta_row];
    uint8_t il = island_l[cinta_row];
    uint8_t ir = island_r[cinta_row];   /* borde REAL, no il + GEN_ISLAND_W - 1 */

    /* Los bordes son tiles de ARENA, no agua: el agua va de l+1 a r-1. */
    if (c <= l || c >= r) return 0;
    if (il != 0xFF && c >= il && c <= ir) return 0;

    /* PUENTE: la viga cruza el cauce entero, sin hueco. Tocar = chocar. */
    if (puente_bit[cinta_row]) return 0;
    return 1;
}

/* Lo contrario: cesped, orilla, isla o viga. */
static uint8_t celda_en_tierra(uint8_t cinta_row, uint8_t c) {
    return (uint8_t)(!celda_agua(cinta_row, c));
}

/* ===========================================================================
 * DESTELLO DEL PUENTE
 * ===========================================================================
 * Al reventar la viga, el agua pasa unos frames a AZUL CLARO y luego vuelve a
 * su color, como en el original. No se puede aclarar "solo el trozo" del
 * puente: las filas comparten la paleta PAL_WATER, asi que se aclara el agua
 * entera mientras dura el destello. Son 6 escrituras al empezar y 6 al acabar,
 * o sea mas barato que repintar celdas. */
#define PUENTE_FLASH_FRAMES  14   /* ~0,23 s a 60 Hz: se ve el fogonazo */

/* Se usa vid_pal_set_bg(), que fija el puntero de entrada en CADA escritura.
 *
 * La version anterior fijaba VID_PAL_PTR una sola vez y contaba con el
 * auto-incremento del core, pero el incremento lo dispara VID_PAL_HI y el primer
 * VID_PAL_LO de cada color se colaba en la entrada anterior: los tres colores
 * acababan machacandose y el agua se quedaba en el color de OTRA paleta (verde,
 * que es PAL_ISLAND).
 *
 * rgb444 en hexadecimal 0xRGB. */
static void puente_flash_on(void) {
    vid_pal_set_bg(PAL_WATER, 1, 0x26F);   /* azul medio       */
    vid_pal_set_bg(PAL_WATER, 2, 0x49F);   /* oleaje claro     */
    vid_pal_set_bg(PAL_WATER, 3, 0x8DF);   /* azul casi blanco */
}

static void puente_flash_off(void) {
    vid_pal_set_bg(PAL_WATER, 1, 0x02A);   /* #0022AA (original) */
    vid_pal_set_bg(PAL_WATER, 2, 0x14E);   /* #1144EE (oleaje)   */
    vid_pal_set_bg(PAL_WATER, 3, 0x26F);   /* #2266FF (claro)    */
}

static void update_puente_flash(void) {
    /* Espejo del contador en $03E1: permite MEDIR el destello desde el emulador
     * (el color del agua no se puede leer directo y /screen.png va con retraso). */
    *(volatile uint8_t *)0x03E1 = puente_flash;
    if (!puente_flash) return;
    if (--puente_flash) return;
    puente_flash_off();
}

/* Borra la VIGA (y la calle) de una fila de cinta y deja AGUA de verdad.
 *
 * ALCANCE: de river_l a river_r, o sea EXACTAMENTE lo que pinta
 * generate_world_row() en una fila de puente:
 *     viga  = TILE_BRIDGE, de gen_left a gen_right (ambos INCLUSIVE)
 *     calle = TILE_ROAD,   fuera de ese rango
 *
 * Antes el bucle iba de river_l+1 a river_r-1 (el agua del cauce, dejando
 * fuera las costas) y las DOS COLUMNAS DE LOS EXTREMOS se quedaban de madera:
 * al chocar con el puente quedaba una astilla vertical pegada a la orilla.
 *
 * OJO: hay que escribir TILE **y ATRIBUTO**. fv_cell() solo escribe el tile, y
 * la viga se pinto con PAL_WOOD: si se deja el atributo, el agua sale MARRON y
 * el puente parece seguir intacto aunque ya no mate. Ese era el bug del
 * "disparo al puente y queda tal cual".
 *
 * Devuelve 1 si esa fila era de puente. */
static uint8_t puente_borrar_fila(uint8_t cr) {
    uint8_t col;
    uint16_t cell = (uint16_t)((uint16_t)cr << 6);
    uint8_t tile  = (uint8_t)((cr & 1) ? TILE_WATER2 : TILE_WATER);

    if (!puente_bit[cr]) return 0;
    puente_bit[cr] = 0;

    for (col = river_l[cr]; col <= river_r[cr]; col++) {
        fv_cell((uint16_t)(cell + col), tile);
        fv_attr_cell((uint16_t)(cell + col), PAL_WATER);
    }
    return 1;
}

/* Destruye la viga ENTERA del puente, sea cual sea la fila por la que se
 * entre. Devuelve 1 si habia viga.
 *
 * La viga son BRIDGE_ROWS filas de cinta contiguas, pero NO se puede suponer
 * que caigan dentro de un margen fijo alrededor de la fila de impacto:
 *
 *   - el sprite del avion ocupa 2-3 filas y la viga 3, asi que chocando por el
 *     borde quedaba siempre la fila de abajo sin borrar (una FRANJA DE TILES de
 *     madera bajo el puente);
 *   - la viga puede cruzar el final del tilemap (filas 30, 31, 0), y ahi
 *     cualquier resta con envoltura se queda corta.
 *
 * Se barren las 32 filas: puente_bit[] es un byte por fila y
 * puente_borrar_fila() sale en el acto si la fila no es de puente, asi que el
 * coste es despreciable y no quedan casos raros. Lo usan TANTO el choque como
 * el disparo, para que no se desincronicen. */
static uint8_t puente_borrar_entero(void) {
    uint8_t cr;
    uint8_t habia = 0;

    for (cr = 0; cr < VC_MAP_ROWS; cr++) {
        habia |= puente_borrar_fila(cr);
    }
    return habia;
}

/* Destruye el PUENTE que hay debajo del avion.
 *
 * Se llama al chocar con la viga: en el original, si llegas al puente sin
 * haberlo volado, se destruyen los dos. Tiene que ir ANTES de plane_hit(),
 * porque esa funcion restaura el checkpoint y volveria a poner el puente.
 *
 * OJO: esta funcion se llama en TODO choque contra orilla o isla, no solo
 * contra la viga. El destello solo se enciende si de verdad habia puente
 * debajo; si no, el agua parpadeaba al morir contra el cesped y parecia un
 * "puente invisible". */
static void puente_destruir_bajo_avion(void) {
    if (!puente_borrar_entero()) return;

    puente_flash = PUENTE_FLASH_FRAMES;
    puente_flash_on();
}

/* PUENTE <-> BALA: un disparo se lleva el puente ENTERO, de orilla a orilla
 * y de la primera fila de viga a la ultima.
 *
 * Es el comportamiento del original: el puente no se esquiva ni se marca a
 * trozos, se revienta de un tiro. Devuelve 1 si la bala i ha dado al puente. */
static uint8_t puente_toca_bala(uint8_t i) {
    uint8_t cr = FILA_CINTA(bullet_y[i]);
    uint8_t c  = (uint8_t)(BULLET_HIT_X(i) >> 3);

    if (!puente_bit[cr]) return 0;
    /* Solo si la bala cae sobre el cauce (la calle de los lados es terreno). */
    if (c <= river_l[cr] || c >= river_r[cr]) return 0;

    /* Se lleva la viga ENTERA con la MISMA funcion que el choque, para que las
     * dos rutas no se puedan desincronizar (antes esta usaba un margen fijo de
     * BRIDGE_ROWS filas alrededor del impacto, y el choque otro distinto). */
    (void)puente_borrar_entero();

    puente_flash = PUENTE_FLASH_FRAMES;
    puente_flash_on();
    return 1;
}

/* Caben los 16x16 px de un sprite con origen en (x, y)?
 *
 * OJO con las DOS dimensiones: con VC_SPR_SCALE2X el sprite ocupa 16 px, o sea
 * DOS celdas de ancho y DOS filas de cinta de alto. Comprobar solo la celda de
 * x >> 3 deja medio sprite sobre la orilla, y comprobar solo una fila deja la
 * mitad de abajo sobre la tierra cuando el cauce cambia de ancho justo ahi. */
static uint8_t sprite_cabe(uint16_t x, uint8_t y) {
    uint8_t r0, r1;

    if (x > (uint16_t)((VC_MAP_COLS - 2) * 8)) return 0;
    r0 = FILA_CINTA(y);
    r1 = FILA_CINTA((uint8_t)(y + 15));

    /* TODAS las celdas del ancho (2 o 3), no solo los extremos: con el sprite
     * en x=100..115 las celdas son 12, 13 y 14; mirar solo 12 y 14 dejaba la 13
     * (justo donde cae la costa) sin comprobar. */
    {
        uint8_t c0 = (uint8_t)(x >> 3);
        uint8_t c1 = (uint8_t)((x + 15) >> 3);
        uint8_t c;
        for (c = c0; c <= c1; c++) {
            if (!celda_agua(r0, c)) return 0;
            if (!celda_agua(r1, c)) return 0;
        }
    }
    return 1;
}

static uint8_t plane_on_bank(uint16_t x, uint8_t y) {
    /* La fila de cinta se indexa como la de VIDRIO: cinta = (-n) & 31. La fila
     * de mundo visible arriba es n = scroll_total >> 3, luego
     *     cinta = (32 - (scroll_total>>3)) & 31  ==  (-(scroll_total>>3)) & 31
     * OJO: antes se usaba (scroll_val + y) >> 3, que es el orden CONTRARIO al
     * que pinta el rio, asi que la colision leia el cauce de otra fila (verde
     * donde hay agua). El avion moria solo cuando las dos formas no coincidian
     * y el game over apagaba el HUD en mitad de la partida. */
    uint8_t cinta_row = FILA_CINTA(y);

    /* Las DOS orillas del sprite (16 px) en la MISMA fila de cinta: antes solo
     * se miraba el centro, asi que medio avion podia quedar sobre el cesped
     * sin que contara. OJO: el borde derecho NO se evalua en su propia fila
     * (plane_y + PLANE_H - 1); con 1 px de scroll por frame eso leia la orilla
     * de la fila siguiente, o sea una linea diagonal que el jugador veia como
     * una "V" de collision imposible. */
    /* Celdas que ocupa el DIBUJO del sprite, no la caja de 16x16.
     *
     * El patron del avion mide 8x8 con el borde transparente: al pintarlo a
     * escala 2x el dibujo real ocupa 14x14 y quedan 2 px invisibles alrededor.
     * Colisionando con la caja entera, el avion moria con el borde transparente
     * aun separado de la orilla: el jugador lo vive como chocar con algo que no
     * ve. Se recorta 1 px por lado (PLANE_HIT_INSET). */
    {
        uint16_t x0 = (uint16_t)(x + PLANE_HIT_INSET);
        uint16_t x1 = (uint16_t)(x + PLANE_W - PLANE_HIT_INSET - 1);
        uint8_t c0 = (uint8_t)(x0 >> 3);
        uint8_t c1 = (uint8_t)(x1 >> 3);
        uint8_t c;
        for (c = c0; c <= c1; c++) {
            if (celda_en_tierra(cinta_row, c)) {
                return 1;
            }
        }
    }
    return 0;
}

/* Muerte del avion: resta una vida, dibuja la EXPLOSION en el sitio del
 * choque y, cuando el fuego se apaga, deja el avion listo para reaparecer. */
static void plane_hit(void) {
    /* Con `plane_dead` basta: se pone a 1 aqui y solo vuelve a 0 cuando el
     * jugador pulsa FIRE. El otro instante en que el avion esta en pantalla
     * sin jugar (esperando FIRE) lo cubre la guarda de check_plane_collisions(). */
    if (plane_dead) return;
    plane_dead = 1;
    snd_explode();

    /* La explosion se ancla DONDE esta el avion ahora: si se dibujara en
     * plane_x/plane_y tras llamar a plane_place_safe() saldria en el punto de
     * reaparicion, que es justo donde el jugador no ha chocado. */
    plane_blast_x = plane_x;
    plane_blast_y = plane_y;
    plane_blast = PLANE_BLAST_FRAMES;

    /* La recarga en curso se cancela: el bidon no queda "a medias". */
    fuel_touching = 0;
    fuel_refuel_tick = 0;

    /* La vida se descuenta al momento del impacto. */
    if (lives) lives--;

    if (lives == 0) {
        game_over = 1;
        snd_engine_off();
        snd_gameover();
        return;
    }

    /* El avion vuelve al punto de partida y espera. La explosion se sigue
     * viendo durante PLANE_BLAST_FRAMES porque draw_plane() dibuja el fuego
     * mientras plane_blast no llegue a 0.
     *
     * OJO: NO se pone esperando_fire aqui. El río sigue andando durante el
     * fuego (el mundo no se congela en seco) y es AL APAGARSE cuando el avion
     * queda quieto a la espera de FIRE; ver el bucle principal. */
    gen_restore_checkpoint();
    plane_place_safe();
    esperando_fire = 0;
    snd_engine_off();
}

/* Mueve el avion al centro-abajo del rio tras la muerte. Si no quedan vidas, se
 * marca game over. */
/* Coloca el avion en un punto donde el sprite entero quepa en agua.
 *
 * OJO CON LA FILA: antes se leia el perfil en la cinta PLANE_ROW, un valor
 * CONSTANTE (23) que no tiene nada que ver con la cinta que se ve en la fila
 * del avion. Como la cinta tiene 32 filas y el scroll la va rotando, la 23
 * contiene un trozo ARBITRARIO del mapa: el avion se colocaba "en agua" segun
 * esa fila y aparecia sobre la isla o contra la orilla. Moverse un pixel a un
 * lado mataba al instante (el "choque con algo invisible" del arranque). Ahora
 * se usa FILA_CINTA(plane_y), la misma cuenta que hace la colision. */
static void plane_place_safe(void) {
    uint8_t k, dy;
    uint8_t mejor = 0xFF;
    uint8_t mejor_dist = 0xFF;
    uint8_t centro = (uint8_t)((GEN_ZONE_L + GEN_ZONE_R + 1) / 2);
    uint8_t fallo = 0xFF;

    plane_y = PLANE_Y;

    /* Un candidato es valido solo si TODAS las celdas del dibujo (las mismas
     * que mira plane_on_bank, recortadas por PLANE_HIT_INSET) son agua en
     * TODAS las filas de cinta que ocupa el sprite. Comprobar una sola fila
     * deja el sprite a caballo de la orilla cuando la costa cambia de fila. */
    for (k = GEN_ZONE_L; k + 1 <= GEN_ZONE_R; k++) {
        uint16_t x = (uint16_t)((uint16_t)k * 8 + 8) - (PLANE_W / 2);
        uint8_t cabe = 1;
        uint8_t dist;

        if (x < PLANE_MIN_X || x > PLANE_MAX_X) continue;
        for (dy = PLANE_HIT_INSET; dy < PLANE_H - PLANE_HIT_INSET && cabe; dy++) {
            uint8_t r = FILA_CINTA((uint8_t)(plane_y + dy));
            uint8_t c0 = (uint8_t)((x + PLANE_HIT_INSET) >> 3);
            uint8_t c1 = (uint8_t)((x + PLANE_W - PLANE_HIT_INSET - 1) >> 3);
            uint8_t c;
            for (c = c0; c <= c1; c++) {
                if (!celda_agua(r, c)) { cabe = 0; break; }
            }
        }
        if (!cabe) continue;
        if (fallo == 0xFF) fallo = (uint8_t)k;
        dist = (uint8_t)((k > centro) ? (k - centro) : (centro - k));
        if (dist < mejor_dist) { mejor_dist = dist; mejor = (uint8_t)k; }
    }

    if (mejor != 0xFF) {
        plane_x = (uint8_t)((uint8_t)(mejor * 8 + 8) - (PLANE_W / 2));
    } else if (fallo != 0xFF) {
        plane_x = (uint8_t)((uint8_t)(fallo * 8 + 8) - (PLANE_W / 2));
    } else {
        /* Nada cabe (embudo junto al puente): al centro del agua de la fila
         * central. Es preferible a dejarlo clavado contra la orilla. */
        for (k = GEN_ZONE_L; k <= GEN_ZONE_R; k++) {
            if (celda_agua(FILA_CINTA(plane_y), k)) {
                plane_x = (uint8_t)(k * 8 - 4);
                break;
            }
        }
    }

    /* Limites de pantalla: nunca fuera del rango visible. */
    if (plane_x < PLANE_MIN_X) plane_x = PLANE_MIN_X;
    if (plane_x > PLANE_MAX_X) plane_x = PLANE_MAX_X;
}

/* Colision bala <-> barco y bala <-> isla/orilla (por celda). */
static void check_bullet_collisions(void) {
    uint8_t i;

    for (i = 0; i < NBULLETS; i++) {
        if (!bullet_alive[i]) continue;

        /* La bala sale por arriba -> apagar. */
        if (bullet_y[i] < 4) {
            bullet_alive[i] = 0;
            continue;
        }

        /* PUENTE <-> BALA: solo DETECTA aqui; la explosion y el consumo de la
         * bala se comparten con la de enemigos/bidon saltando a `explota`. */
        if (puente_toca_bala(i)) {
            boom_x = (uint8_t)(BULLET_HIT_X(i) & 0xF8);
            boom_y = (uint8_t)(bullet_y[i] - 4);
            goto explota;
        }

        /* Bala <-> ENEMIGOS y <-> BIDON (caja AABB).
         *
         * El bidon TAMBIEN se destruye de un disparo, como en el River Raid de
         * verdad: no se recoge, se revienta. El CONTACTO del avion no lo
         * consume (ver check_plane_collisions), asi que el jugador elige entre
         * pasar por encima para cargar o quitarlo de en medio a tiros. Los
         * bidones no dan puntos, pero la explosion es la misma, asi que se pone
         * al final y se salta a ella con goto (es lo que menos bytes ocupa en
         * cc65; repetirla costaba 74 de los que no hay). */
        {
            uint8_t k;
            for (k = 0; k < NENEMIGOS; k++) {
                if (!ene_active[k]) continue;
                if (!fv_box_toca(BULLET_HIT_X(i), bullet_y[i], BULLET_W, BULLET_H,
                                 ene_x[k], ene_y[k], ENEMIGO_W, ENEMIGO_H)) continue;
                ene_active[k] = 0;
                score += SCORE_SHIP;
                boom_x = ene_x[k];
                boom_y = ene_y[k];
                goto explota;
            }
            if (!fuel_active ||
                !fv_box_toca(BULLET_HIT_X(i), bullet_y[i], BULLET_W, BULLET_H,
                             fuel_x, fuel_y, 8, 8)) continue;
            fuel_active = 0;
            boom_x = fuel_x;
            boom_y = fuel_y;
        explota:
            bullet_alive[i] = 0;
            boom_active = 1;
            boom_frames = 12;
            snd_explode();
            continue;
        }

        /* Bala <-> orilla (por X; la bala viaja en linea recta asi que si su X
         * cae fuera del canal, la apagamos). */
        if (BULLET_HIT_X(i) < PLANE_MIN_X ||
            (BULLET_HIT_X(i) + BULLET_W - 1) >= ((RIVER_RIGHT + 1) * 8)) {
            bullet_alive[i] = 0;
            continue;
        }
    }
}

/* Colision avion <-> barco / isla / orilla.
 *
 * Rendimiento: la comprobacion contra el canal (plane_on_bank) solo se repite
 * cuando el avion CAMBIA de posicion; entre medias el resultado no puede
 * cambiar (mismo x, mismas filas de cinta). Se recuerda el resultado. Las
 * colisiones con objetos son cajas AABB y son baratas. */
static void check_plane_collisions(void) {
    uint8_t last_bank;

    /* El avion NO recibe golpes mientras esta muerto o esperando FIRE.
     *
     * OJO CON esperando_fire: tras la explosion el avion REAPARECE en pantalla
     * y el rio queda parado hasta que el jugador pulse FIRE. Antes se seguia
     * comprobando la colision en ese estado y, como gen_restore_checkpoint()
     * rebobina el trazado, el avion podia quedar encima de la orilla y morir
     * solo: el segundo petardazo. */
    if (plane_dead || plane_blast || esperando_fire) return;

    /* AVION <-> ORILLA / ISLA.
     *
     * Se miran TODAS las filas de cinta que ocupa el sprite (16 px = 3), no
     * solo arriba/centro/abajo: con 1 px de scroll por frame la orilla entra
     * por una fila intermedia y el avion se metia medio sprite en la arena sin
     * que saltara la colision. NO se puede cachear por (x, y): la respuesta
     * depende de la fila de cinta que pasa debajo, que cambia con el scroll. */
    {
        /* Las TRES filas de cinta que ocupa un sprite de 16 px, cada una por su
         * banda: 0-7, 8-15 y 16-23 desde plane_y. Mirar solo y, y+7, y+8 y
         * y+15 dejaba la fila INTERMEDIA (y+8..y+15) sin cubrir cuando el
         * sprite empezaba en mitad de una celda: el avion se metia en la orilla
         * por la fila de en medio sin que saltara la colision. */
        uint8_t k;
        last_bank = 0;
        for (k = PLANE_HIT_INSET; k < PLANE_H - PLANE_HIT_INSET; k++) {
            if (plane_on_bank(plane_x, (uint8_t)(plane_y + k))) {
                last_bank = 1;
                break;
            }
        }
    }
    if (last_bank) {
        DBG_MUERTE = 1;
        /* Si lo que habia debajo era la VIGA del puente, el puente se destruye
         * con el avion: es lo que pasa en el original al chocar sin haberlo
         * volado antes. Se hace ANTES de plane_hit() porque esa funcion
         * restaura el checkpoint (o sea, vuelve a poner el puente). */
        puente_destruir_bajo_avion();
        plane_hit();
        return;
    }

    /* Nota: la colision con cesped e isla central la resuelve plane_on_bank()
     * mas arriba, consultando el perfil de la fila. */

    /* Avion <-> enemigos (caja AABB). */
    {
        uint8_t k;
        for (k = 0; k < NENEMIGOS; k++) {
            if (!ene_active[k]) continue;
            if (!fv_box_toca(plane_x, plane_y, PLANE_W, PLANE_H,
                             ene_x[k], ene_y[k], ENEMIGO_W, ENEMIGO_H)) continue;
            ene_active[k] = 0;   /* choque destruye ambos */
            DBG_MUERTE = 2;
            plane_hit();
            return;
        }
    }

    /* Avion <-> deposito de combustible: EL BIDON NO SE CONSUME.
     *
     * El bidon es una ENTIDAD del rio, sin nivel propio: mientras el avion
     * este en CONTACTO con el, el estanque del avion se va llenando. Por eso
     * lo que cargas es proporcional al TIEMPO de contacto: rozarlo de lado
     * suma poco, sobrevolarlo entero llena el deposito.
     *
     * Al soltarlo el bidon SIGUE EN EL RIO (antes se retiraba con
     * fuel_active = 0, y eso hacia imposible notar la proporcionalidad: no
     * habia forma de estar encima cargando). Solo desaparece cuando su caja
     * sale de pantalla (update_fuel) o cuando lo recoge el propio avion ya
     * con el deposito a tope.
     *
     * El sonido de repostaje dura lo que dura el contacto (snd_fuel_touch se
     * refresca en cada frame de solape). */
    if (fuel_active) {
        /* La caja del avion es de 16 px (avion a escala 2x); la del bidon se
         * toma a su tamano real de 8 px para que el contacto sea sincero. */
        if (fv_box_toca(plane_x, plane_y, PLANE_W, PLANE_H,
                     fuel_x, fuel_y, 8, 8)) {
            fuel_touching = 1;

            /* Con el estanque a tope no se suma nada ni se pinguea (no hay
             * pulso, asi que no hay sonido) pero el contacto se sigue
             * reconociendo. */
            if (fuel < FUEL_MAX) {
                fuel_refuel_tick++;
                if (fuel_refuel_tick >= FUEL_REFUEL_EVERY) {
                    fuel_refuel_tick = 0;
                    if (fuel <= (uint8_t)(FUEL_MAX - FUEL_REFUEL_STEP)) {
                        fuel = (uint8_t)(fuel + FUEL_REFUEL_STEP);
                    } else {
                        fuel = FUEL_MAX;
                    }
                    /* UN PING POR PULSO: mas frames de contacto = mas pings.
                     * Rozar el bidon da uno o dos; sobrevolarlo da una rafaga,
                     * que es la proporcion que el jugador percibe. */
                    snd_fuel_ping();
                }
            } else {
                fuel_refuel_tick = 0;
            }
        } else if (fuel_touching) {
            /* Ha dejado de tocarlo: lo cargado SE QUEDA y el bidon SIGUE en el
             * rio. Solo se cierra la racha de contacto, no se puntua ni se
             * retira el bidon. */
            fuel_touching = 0;
            fuel_refuel_tick = 0;
        }
    } else {
        fuel_touching = 0;
        fuel_refuel_tick = 0;
    }
}

/* ===========================================================================
 * ACTUALIZACION DE LOS ENEMIGOS
 * ===========================================================================
 * Dos enemigos independientes (barco y helicoptero). Cada uno lleva su propio
 * temporizador de aparicion y su posicion; la unica restriccion que comparten
 * es que NUNCA pueden estar a la misma altura (ENEMIGO_SEP_Y).
 * =========================================================================== */

/* Devuelve 1 si el que nace en la fila `y` no queda a la MISMA ALTURA que otro
 * objeto del rio.
 *
 * Se aplica a los tres: barco, helicoptero y BIDON (el bidon antes no lo
 * comprobaba y podia nacer encima de un barco, con lo que se confundian).
 * `i` = indice del enemigo que nace, o NENEMIGOS si es el bidon; ese es
 * justamente el que se salta la comprobacion consigo mismo. */

/* 1 si las dos filas estan a menos de ENEMIGO_SEP_Y px. */
static uint8_t cerca(uint8_t a, uint8_t b) {
    return (uint8_t)(((a >= b) ? (uint8_t)(a - b) : (uint8_t)(b - a)) < ENEMIGO_SEP_Y);
}

static uint8_t enemigo_libre(uint8_t i, uint8_t y) {
    uint8_t k;

    /* k llega hasta NENEMIGOS: esa ultima "posicion" es el BIDON, para que
     * tambien respete la altura (antes podia nacer encima de un barco y se
     * confundian). i == NENEMIGOS significa que el que nace es el bidon. */
    for (k = 0; k <= NENEMIGOS; k++) {
        uint8_t oy;

        if (k == i) continue;
        if (k == NENEMIGOS) {
            if (!fuel_active) continue;
            oy = fuel_y;
        } else {
            if (!ene_active[k]) continue;
            oy = ene_y[k];
        }
        if (cerca(oy, y)) return 0;
    }
    return 1;
}

/* Columna X al azar (de las de GEN_ZONE_L..R) donde cabe un sprite de 2
 * celdas en la fila de entrada y=0. Devuelve 0xFF si no cabe en ninguna.
 * Compartida por enemigos y bidon: antes cada uno tenia su copia. */
static uint8_t columna_libre(void) {
    uint8_t k, n = 0, pick, total = 0;

    for (k = GEN_ZONE_L; k + 1 <= GEN_ZONE_R; k++) {
        if (sprite_cabe((uint16_t)(k * 8), 0)) total++;
    }
    if (total == 0) return 0xFF;

    pick = (uint8_t)(rng_next() % total);
    for (k = GEN_ZONE_L; k + 1 <= GEN_ZONE_R; k++) {
        if (!sprite_cabe((uint16_t)(k * 8), 0)) continue;
        if (n == pick) break;
        n++;
    }
    return (uint8_t)(k * 8);
}

/* Si el sprite dejo de estar sobre agua, lo mueve 1 px por frame hacia el agua
 * mas cercana de su fila. Devuelve 0 si en esa fila ya no hay agua. */
static uint8_t reubicar_al_agua(uint8_t *x, uint8_t y) {
    uint8_t c0 = (uint8_t)(*x >> 3);
    uint8_t target = 0xFF;
    uint8_t d;
    uint8_t tx;

    for (d = 1; d < VC_MAP_COLS && target == 0xFF; d++) {
        if (c0 >= d && sprite_cabe((uint16_t)((c0 - d) * 8), y)) target = (uint8_t)(c0 - d);
        else if (sprite_cabe((uint16_t)((c0 + d) * 8), y))          target = (uint8_t)(c0 + d);
    }
    if (target == 0xFF) return 0;

    tx = (uint8_t)(target * 8);
    if (*x < tx) (*x)++;
    else if (*x > tx) (*x)--;
    return 1;
}

static void update_enemy(uint8_t i) {
    if (ene_active[i]) {
        ene_y[i] = (uint8_t)(ene_y[i] + scroll_step);
        if (ene_y[i] >= VC_SCREEN_H) { ene_active[i] = 0; return; }

        /* Los dos bordes del sprite (16 px = 2 celdas) tienen que caer en agua.
         * Se pregunta CELDA A CELDA con celda_agua(), la misma que usa el
         * avion: asi el enemigo rebota en la orilla, en la isla y en la viga del
         * puente sin listas de limites que se desincronicen del rio. */

        /* Si el cauce se cerro a su lado, se desplaza AL AGUA MAS CERCANA,
         * pero **1 px por frame**: moviendolo de golpe saltaba de una celda a
         * otra y el jugador lo veia teletransportarse. Si se sale de la
         * pantalla por arriba/abajo no se mueve de lado.
         *
         * OJO: SOLO si de verdad se sale del agua. Llamarlo en cada frame le
         * ganaba al rebote: el sprite de 16 px casi nunca cae en columnas
         * exactas de 8, asi que el "objetivo" oscilaba entre la celda de la
         * izquierda y la de la derecha y el enemigo se movia solo, sin llegar
         * nunca a la orilla (medido: 0 rebotes en 600 frames, 23 saltos). */
        if (!sprite_cabe(ene_x[i], ene_y[i]) &&
            !reubicar_al_agua(&ene_x[i], ene_y[i])) { ene_active[i] = 0; return; }

        /* Deriva lateral con rebote: 1 px cada N frames, con N segun el nivel
         * (prog_tab). N = 0 -> el enemigo NO se mueve de lado: sigue bajando
         * con el scroll, asi que se le puede disparar y chocar, pero no se
         * cruza. Asi empiezan los dos primeros tramos.
         *
         * OJO con el 0: `++ene_drift >= 0` SIEMPRE es cierto, asi que sin el
         * `if` el enemigo derivaria a 1 px por frame con cadencia 0. */
        {
            uint8_t cada = nivel_val(i == ENEMIGO_HELI ? PROG_HELI : PROG_BARCO);
            uint8_t nx;

            if (!cada) return;
            if (++ene_drift[i] < cada) return;
            ene_drift[i] = 0;
            nx = (uint8_t)((ene_dir[i] == 0) ? ene_x[i] + 1 : ene_x[i] - 1);

            /* Rebota si el sprite se sale del agua por cualquier lado. */
            if (!sprite_cabe(nx, ene_y[i])) {
                ene_dir[i] = (uint8_t)!ene_dir[i];
            } else {
                ene_x[i] = nx;
            }
        }
        return;
    }

    /* El HELICOPTERO no existe en los niveles donde prog_tab dice 0: los dos
     * primeros tramos son solo de barcos. Se sale ANTES de tocar ene_next, para
     * que no se acumule un "nacimiento pendiente" que aparezca de golpe en
     * cuanto se suba de nivel. */
    if (i == ENEMIGO_HELI && !nivel_val(PROG_HELI)) return;

    if (world_row < ene_next[i]) return;

    /* Nace en y=0 solo si el otro no esta a esa altura (nunca a la misma
     * altura). Si no, reintenta unos frames despues. */
    if (!enemigo_libre(i, 0)) { ene_next[i] = world_row + 2; return; }

    {
        /* Nace SIEMPRE en agua navegable de la fila por la que entra. */
        uint8_t x = columna_libre();
        if (x == 0xFF) { ene_next[i] = world_row + 4; return; }
        ene_x[i] = x;
        ene_dir[i] = (uint8_t)(rng_next() & 1);
        ene_y[i] = 0;
        ene_drift[i] = 0;
        ene_active[i] = 1;
        ene_next[i] = world_row + 16;
    }
}

static void update_boat(void) {
    update_enemy(ENEMIGO_BOAT);
    update_enemy(ENEMIGO_HELI);
}

static void update_fuel(void) {
    if (fuel_active) {
        fuel_y = (uint8_t)(fuel_y + scroll_step);
        if (fuel_y >= VC_SCREEN_H) {
            fuel_active = 0;
            return;
        }
        /* El rio cambia de ancho y de sitio: si la fila por la que pasa el
         * bidon deja de ser agua donde esta, SE MUEVE LATERALMENTE al agua mas
         * cercana en vez de desaparecer. Antes se apagaba de golpe y el jugador
         * veia bidones que parpadeaban a media pantalla.
         *
         * OJO: solo si NO cabe. Llamarlo siempre hacia temblar el bidon: el
         * agua mas cercana puede estar en la celda de al lado y se movia 1 px
         * hacia alla en cada frame (medido: 5-8 saltos por partida). */
        if (!sprite_cabe(fuel_x, fuel_y)) {
            if (!reubicar_al_agua(&fuel_x, fuel_y)) fuel_active = 0;
        }
        return;
    }

    /* Sin bidon en pantalla no hay contacto que contar. Se pone aqui y no en
     * cada sitio que retira el bidon (fin de pantalla, barrido de la bala): asi
     * el estado de la recarga se limpia solo en el siguiente frame. */
    fuel_touching = 0;
    fuel_refuel_tick = 0;

    if (world_row >= fuel_next) {
        uint8_t x;

        /* Aparece en AGUA navegable de la fila por la que entra, y NUNCA a la
         * altura de un enemigo. Si coincide, se espera un poco: antes el bidon
         * podia nacer encima de un barco y se confundian. */
        if (!enemigo_libre(NENEMIGOS, 0)) { fuel_next = world_row + 2; return; }

        x = columna_libre();
        if (x == 0xFF) { fuel_next = world_row + 4; return; }
        fuel_x = x;
        fuel_y = 0;
        fuel_active = 1;
        /* Cada cuanto aparece el siguiente bidon: sale de prog_tab[]. Al
         * principio caen a menudo y segun sube la dificultad se espacian (18
         * filas en el primer tramo, 34 en el ultimo). */
        fuel_next = world_row + nivel_val(PROG_BIDON);
    }
}

/* ===========================================================================
 * DRENAJE DE COMBUSTIBLE
 * ===========================================================================
 * Cada FUEL_DRAIN_EVERY frames se pierde 1 unidad de combustible. Si llega a
 * 0, se pierde una vida (o game over).
 * =========================================================================== */
/* Vuelca a las direcciones de diagnostico ($03E5-$03EA) el estado de la
 * recarga. Se llama una vez por frame desde el bucle principal, ANTES de
 * cualquier salida temprana (esperando_fire, game over), para que /ram.bin
 * nunca devuelva valores viejos de un frame o de una partida anterior. */

static void update_fuel_gauge(void) {
    if (game_over) return;

    fuel_tick++;
    if (fuel_tick < FUEL_DRAIN_EVERY) return;
    fuel_tick = 0;

    if (fuel > 0) {
        fuel--;
    }

    if (fuel == 0 && !plane_dead && !esperando_fire) {
        /* Sin combustible: muere el avion (pierde una vida).
         * OJO con esperando_fire: ahi el rio esta PARADO y el jugador aun no ha
         * pulsado FIRE; si el deposito se habia quedado a 0, sin esta guarda el
         * avion volvia a explotar sin culpa del jugador. */
        DBG_MUERTE = 3;
        plane_hit();
    }
}

/* ===========================================================================
 * ANIMACION DEL OLEAJE
 * ===========================================================================
 * DESACTIVADA (a proposito). Repintaba agua de filas VISIBLES y, medido con
 * capturas, dejaba HUECOS: columnas con el tile anterior (panel de GAME OVER,
 * agua suelta) en mitad del cauce, y manchas en el cesped. Ademas borraba la
 * tierra central.
 *
 * El oleaje hay que hacerlo cambiando el PATRON del tile de agua (o con dos
 * tiles alternando por bloques), NUNCA repintando celdas que el jugador esta
 * viendo. Cuando se haga, que sea asi.
 *
 * La antigua version queda abajo en #if 0 como referencia.
 * =========================================================================== */

#if 0
static void update_water_anim_old(void) {
    static uint8_t anim_row, anim_phase, anim_tick;
    uint8_t col;
    uint16_t cell;

    if (++anim_tick < 3) return;     /* cada 3 frames una fila */
    anim_tick = 0;

    {
        uint8_t l  = river_l[anim_row];
        uint8_t r  = river_r[anim_row];
        uint8_t wave = (uint8_t)(((anim_row + anim_phase) & 1)
                                 ? TILE_WATER2 : TILE_WATER);

        /* Solo el AGUA real de la fila, y NUNCA una celda que sea isla.
         *
         * OJO: no basta con consultar island_l/r de esta fila. El perfil se
         * reescribe cuando la fila se regenera, y la animacion puede llegar
         * justo despues de que su mitad superior se haya pintado: en ese caso
         * el perfil ya no tiene isla pero el TILE si, y repintar agua borraria
         * media isla (la "isla fantasma"). Por eso se mira el tile guardado
         * en RAM antes de sobreescribirlo. */
        cell = (uint16_t)((uint16_t)anim_row << 6);
        for (col = (uint8_t)(l + 1); col < r; col++) {
            uint16_t c = (uint16_t)(cell + col);
            if (ISLA_GET(anim_row, col)) continue;
            VID_ADDR_LO = (uint8_t)c;
            VID_ADDR_HI = (uint8_t)((c >> 8) & 0x07);
            VID_DATA    = wave;
            VID_ADDR_LO = (uint8_t)c;
            VID_ADDR_HI = (uint8_t)(VC_AREA_ATTR | ((c >> 8) & 0x07));
            VID_DATA    = PAL_WATER;
        }
    }

    anim_row = (uint8_t)((anim_row + 1) & 31);
    if (anim_row == 0) anim_phase ^= 1;   /* al dar la vuelta, cambia fase */
}
#endif

/* ===========================================================================
 * AVANCE DEL SCROLL + REGENERACION (solo el canal)
 * ===========================================================================
 * CUANDO SE REPINTA LA FILA NUEVA (el punto delicado de todo esto):
 *
 * En pantalla se ven 30 filas de cinta (40x30) y la cinta tiene 32, asi que
 * hay exactamente DOS filas fuera de la vista: la de arriba (que entra en
 * cuanto se mueva el scroll) y la de abajo (que ya salio).
 *
 * La fila nueva tiene que pintarse en la cinta que esta POR DEBAJO de la
 * pantalla: ahi nadie la ve y el jugador solo ve el resultado ya terminado
 * cuando, 30 filas despues, llega a la vista. Al principio se pintaba demasiado
 * pronto (con 8 filas de ventaja), o sea SINCRONIZADA con la que se veia: el
 * cauce nuevo se dibujaba encima del que aun estaba en pantalla y la forma
 * "se iba componiendo" fila a fila. Y al reves (con la camara 6 filas por
 * detras de lo generado) el perfil llegaba a pantalla con el scroll ya
 * encima: las anchuras se deslizaban mal.
 *
 * OJO con el ULTIMO PIXEL del paso: la fila que entra por arriba se pinta 1
 * paso ANTES de quedar totalmente fuera. Si con la fila ya en el borde se
 * pinta su sustituta, esa sustituta queda a UN paso de la vista y el pixel que
 * asoma por arriba sale con la forma compuesta. Por eso el nivel que se pinta
 * es el de la fila que entra en el SIGUIENTE paso de scroll (fila_visible + 2),
 * que solo puede verse dentro de 30 filas: la sustituta no asoma nunca.
 * =========================================================================== */
/* Calcula cuantos px se mueve el mundo ESTE frame.
 *
 * Va aparte y al PRINCIPIO del frame a proposito: barco, bidon y explosion se
 * desplazan con scroll_step, y tienen que usar el mismo valor con el que luego
 * se mueve el rio. Si se calcula dentro de advance_scroll() (al final del
 * frame), esos objetos se movian con el paso del frame ANTERIOR y quedaban
 * desfasados una fila respecto al agua: aparecian sobre la orilla.
 * advance_scroll() usa el valor ya calculado y no lo recalcula. */
static void step_scroll(void) {
    uint8_t spd = scroll_speed;
    if (spd == 0) spd = SCROLL_NORM;
    scroll_acc = (uint8_t)(scroll_acc + spd);
    scroll_step = (uint8_t)(scroll_acc >> 3);
    scroll_acc = (uint8_t)(scroll_acc & 0x07);
}

static void advance_scroll(void) {
    /* Fila (y px) que se veran en el borde superior DESPUES del paso.
     * La fila se deriva de un total, no de un contador aparte: asi no puede
     * desfasarse del scroll. */
    {
        uint32_t fila_nueva = (uint32_t)((scroll_total + scroll_step) >> 3) + 2;

        /* OJO: hay que generar TODAS las filas que falten, no solo la ultima.
         *
         * Al acelerar el scroll avanza mas de 8 px en un frame (scroll_speed
         * llega a SCROLL_MAX = 16, o sea scroll_step = 2), asi que fila_nueva
         * salta DOS filas. Generando solo una, la intermedia se quedaba con el
         * contenido del barrido anterior: aparecia una FRANJA HORIZONTAL de
         * agua vieja cruzando el cesped. Con velocidad baja no se veia nunca
         * porque scroll_step es 0 o 1. */
        while (world_row < fila_nueva) {
            world_row++;
            generate_world_row(world_row);
        }
    }

    scroll_val = (uint16_t)((scroll_val - scroll_step) & 0xFF);
    scroll_total += scroll_step;      /* px recorridos, sin envolver   */
    vid_set_scroll_y(scroll_val);
}

/* ===========================================================================
 * REINICIO DE PARTIDA
 * =========================================================================== */
static void reset_game(void) {
    uint8_t row;


    /* Al reiniciar vuelven a dibujarse los sprites y el HUD. Hay que invalidar
     * las caches de dibujo: si no, creen que el OAM ya tiene el patron y en
     * hardware se sigue viendo la basura inicial (un '7' o un '8'). */
    sprites_off = 0;
    oam_cache_invalida = 1;

    /* Estado de juego. */
    lives = MAX_LIVES;
    fuel  = FUEL_MAX;
    score = 0;
    fuel_tick = 0;
    fuel_touching = 0;
    fuel_refuel_tick = 0;
    game_over = 0;

    /* Avion: se coloca DESPUES de regenerar el rio (mas abajo), porque
     * plane_place_safe() lee el perfil river_l/river_r de la fila. */
    plane_dead = 0;
    plane_blast = 0;
    esperando_fire = 1;       /* avion fijo al reiniciar; arranca con FIRE */

    /* Balas, barco, deposito. */
    for (row = 0; row < NBULLETS; row++) bullet_alive[row] = 0;
    fire_cooldown = 0;
    ene_active[ENEMIGO_BOAT] = 0;
    ene_active[ENEMIGO_HELI] = 0;
    ene_next[ENEMIGO_BOAT]   = 40;
    ene_next[ENEMIGO_HELI]   = 56;
    ene_dir[ENEMIGO_BOAT]    = 0;
    ene_dir[ENEMIGO_HELI]    = 0;
    boom_active = 0;
    boom_frames = 0;
    boom_x = 0;
    boom_y = 0;
    /* El destello del puente cambia la PALETA del agua: si se reinicia en mitad
     * del fogonazo, el agua se quedaria clara toda la partida. */
    puente_flash = 0;
    fuel_active = 0;
    fuel_next   = 2;
    /* Reinicia el generador del rio (LFSR del original).
     *
     * OJO: hay que reiniciar el estado del core de video ANTES de pintar. El
     * programa puede recargarse encima de si mismo (R sin reset): el tilemap
     * conserva entonces lo que habia y la cola de scroll de la animacion del
     * agua sigue donde estaba. Si no se limpia, algunas filas no se repintan
     * y quedan con la decoracion de la partida anterior (agua encima del
     * cesped, cauce de dos anchos). */
    vid_clear_vram();
    vid_set_scroll_y(0);
    gen_reset();

    /* vid_clear_vram() expande SOLO la fuente: los patrones propios (rio,
     * orillas, isla, puente y TODOS los de sprite) se borran. Sin recargarlos
     * el rio sale en blanco y los sprites como bloques solidos. */
    load_patterns();

    /* Re-genera el rio (misma semilla => mismo layout) y vuelve a poner el
     * puente inicial delante del avion. */
    rng_state = RIVER_SEED;
    llenar_cinta_inicial();

    /* Ahora que el perfil esta regenerado, colocar el avion en agua libre. */
    plane_place_safe();

    /* hud_setup() reescribe POSICION y patrones: en GAME OVER se apagaron los
     * 32 sprites, asi que hud_draw() solo no basta (no repondria el HUD). */
    hud_setup();
    hud_draw();

    /* El motor NO se enciende aqui: el avion esta parado esperando FIRE (lo
     * mismo que al arrancar). Se enciende en main() al salir de esperando_fire. */
}

/* ===========================================================================
 * GAME OVER
 * ===========================================================================
 * Ya NO hay panel con texto. Se quito para liberar ~500 bytes de codigo y de
 * RODATA que hacian falta para guardar island_r[] (32 bytes), el arreglo del
 * "choque con algo invisible" (ver el comentario de island_l/island_r).
 *
 * Ahora el mundo se queda congelado con los sprites apagados y se espera a
 * FIRE/R para reiniciar o Q para salir. Es la misma mecanica que la pantalla
 * anterior, sin el cartel: la consola del monitor sigue mostrando el estado.
 * =========================================================================== */
static void game_over_screen(void) {
    uint8_t j = joy_read();
    char c;

    if (j & JOY_A_FIRE) {
        reset_game();
        return;
    }

    while (rom_uart_rx_ready()) {
        c = rom_uart_getc();
        if (c == 'r' || c == 'R' || c == ' ') {
            reset_game();
            return;
        }
        if (c == 'q' || c == 'Q') {
            quit_flag = 1;
            return;
        }
    }
}

/* ===========================================================================
 * PROGRAMA PRINCIPAL
 * =========================================================================== */
int main(void) {
    setup_video();

    quit_flag = 0;
    sprites_off = 0;
    oam_cache_invalida = 1;   /* el OAM se limpio: reescribir patrones */

    while (!quit_flag) {
        vid_wait_vblank();
        perf_begin();

        /* 0) Esperando FIRE (inicio de partida o tras un choque): el avion esta
         * fijo en el punto de partida y el rio NO avanza. Se arranca al disparar. */
        if (esperando_fire) {
            /* La explosion del avion sigue corriendo aunque el rio este parado:
             * es lo que se ve antes de que el jugador pueda volver a empezar. */
            if (plane_blast) plane_blast--;

            /* MIENTRAS ARDE LA EXPLOSION NO SE ATIENDE AL BOTON. Antes se podia
             * pulsar FIRE en mitad del fuego y el avion reaparecia encima de la
             * animacion; ahora el disparo se ignora hasta que el fuego termina
             * (plane_blast llega a 0). Se sigue llamando a fire_pressed() para
             * DRENAR el UART: si no, los ' ' que lleguen durante la animacion se
             * acumulan y el juego arrancaria solo al acabar. */
            if (plane_blast) {
                (void)fire_pressed();       /* consumir y descartar */
            } else if (fire_pressed()) {
                esperando_fire = 0;
                /* OJO: sin esto el avion queda INVULNERABLE. plane_hit() pone
                 * plane_dead=1 y solo reset_game() lo volvia a 0, asi que al
                 * reanudar con FIRE las colisiones seguian saltandose por el
                 * "if (plane_dead) return" y el avion atravesaba orillas,
                 * islas y enemigos sin perder una vida. */
                plane_dead = 0;
                scroll_speed = SCROLL_NORM;
                scroll_acc   = 0;
                snd_engine_on();
            }
            snd_update();
            draw_plane();
            hud_draw();
            vid_wait_vblank_end();
            perf_end();
            continue;
        }

        /* 1) Leer entrada y mover el avion / disparar (solo si no hay game over
         * NI explosion en curso: mientras arde, el avion no existe). */
        if (!game_over && !plane_blast) {
            read_input();
            /* El rugido del motor sigue la velocidad: al acelerar (arriba) se
             * oye mas agudo y ligero, al frenar (abajo) mas grave y pesado. */
            snd_engine_speed(scroll_speed);
        }
        if (plane_blast) {
            plane_blast--;
            /* Al apagarse el fuego el avion reaparece QUIETO y el rio se para:
             * queda listo para arrancar con FIRE, igual que al empezar la
             * partida. Durante la animacion el mundo SI sigue avanzando (la
             * explosion se mueve con el scroll, como en el arcade); el avion no.
             *
             * OJO: SE RECOLOCA EL AVION AQUI, y no solo en plane_hit().
             * plane_place_safe() busca agua leyendo el perfil de la cinta, y
             * entre el choque y este momento el rio ha avanzado
             * PLANE_BLAST_FRAMES px (36, o sea 4,5 filas de cinta). La cinta
             * rota, asi que las filas que habia debajo del avion YA NO SON LAS
             * MISMAS: si se deja la x calculada al chocar, el avion reaparece
             * sobre una isla o sobre la orilla (bug reportado: "al morir inicio
             * sobre una isla"). Recalcularlo aqui lo deja en agua de verdad.
             *
             * El scroll_acc se pone a 0 para que el primer pase tras el FIRE no
             * salga a medio pixel y de un tiron. */
            if (!plane_blast && !game_over) {
                plane_dead = 0;
                plane_place_safe();
                esperando_fire = 1;
                scroll_acc = 0;
            }
        }

        /* 1b) EL RIO SE MUEVE PRIMERO.
         *
         * Antes el scroll se aplicaba al final del frame, despues de dibujar
         * los sprites: el barco se colocaba contra el perfil de una fila que en
         * pantalla ya no era la suya, y quedaba medio sprite sobre la orilla un
         * frame (y al frame siguiente el rio volvia a moverse debajo). Moverlo
         * aqui deja los dos en el mismo instante: lo que consulta la colision
         * es exactamente lo que se ve.
         *
         * Va antes de los sprites porque la regeneracion de la fila de cinta es
         * la parte mas pesada del frame. */
        if (!game_over) {
            step_scroll();
            advance_scroll();
        }

        /* 2) Logica: balas, colisiones, barco, combustible. */
        if (!game_over) {
            update_bullets();
            check_bullet_collisions();
            check_plane_collisions();
            update_boat();
            update_fuel();
            update_boom();
            update_puente_flash();
            update_fuel_gauge();
        }
        perf_mark(0);

        /* 3) Dibujo: sprites PRIMERO (barato y critico). En GAME OVER solo se
         * deja el HUD: el avion y los objetos deben desaparecer. */
        /* 3) Dibujo. En GAME OVER se apaga TODO (avión, objetos y HUD) para
         * que el texto del panel quede legible: los sprites del HUD tapaban
         * parte del mensaje. Excepcion: la EXPLOSION final del avion se dibuja
         * mientras dura, porque si no la ultima vida se pierde sin que se vea
         * el choque. */
        if (!game_over || plane_blast) {
            draw_plane();
            draw_bullets();
            draw_boat();
            draw_boom();
            draw_fuel();
            oam_cache_invalida = 0;   /* ya reescritos patron y flags */
        } else if (!sprites_off) {
            uint8_t k;
            sprites_off = 1;
            /* Los 32 sprites: cubre objetos y HUD sin enumerar cada slot. */
            for (k = 0; k < 32; k++) fv_sprite_disable(k);
        }

        /* 4) HUD (solo en partida). */
        if (!game_over) {
            hud_draw();
        }

        perf_mark(1);

        /* 5) Sonido. */
        snd_update();

        /* 6) Si hay game over, mostrar pantalla y permitir reiniciar/salir. */
        if (game_over) {
            game_over_screen();
        } else {
            /* El scroll ya se aplico en 1b. */
        }
        perf_mark(2);

        vid_wait_vblank_end();
        perf_end();
    }

    vid_wait_vblank();
    fv_clear_oam();
    vid_set_scroll_y(0);
    vid_wait_vblank_end();

    return 0;
}
