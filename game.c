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
/* Recarga por CONTACTO con el bidon: no es instantanea. Se suman
 * FUEL_REFUEL_STEP unidades cada FUEL_REFUEL_EVERY frames que el avion siga
 * tocando el bidon, asi lo que cargas depende del tiempo de contacto (pasar
 * rapido por el borde carga poco; quedarse encima carga mas). */
#define FUEL_REFUEL_STEP  8    /* unidades por pulso de recarga */
#define FUEL_REFUEL_EVERY 3    /* frames entre pulsos            */

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

/* Sprite del deposito de combustible. */
#define SPR_FUEL       4
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
/* Slots de OAM. Solo hay 32 y el HUD ocupa 14 (4 puntos + 8 barra + icono y
 * numero de vidas). El resto: 0 avion, 5 explosion, 6 heli, 8 barco, 30-31
 * balas. MOVIMIENTO: si el HUD se lleva 14 slots no queda margen para enemigos
 * extra; para ampliar el juego hay que soltar algun sprite o pasar el HUD a
 * tiles con split de raster. */
#define SPR_HUD_BASE   10      /* puntos: 10..13                     */
#define SPR_HUD_FUEL   14      /* barra: 14..21                      */
#define SPR_HUD_LIVES  22      /* icono de vida (22) y numero (23)   */
/* Y=10: la fila 1 del tilemap (pixeles 8..15). La fila 0 (0..7) la recorta
 * el overscan de muchos monitores (manual del core, 1.9 y 5): con Y=2 el HUD
 * quedaba medio fuera de pantalla. Aqui sobra 2 px de margen por arriba. */
#define HUD_Y          10      /* Y de la fila de HUD (px). Ocupa 10..17 */
#define HUD_SCORE_X    24      /* puntos (4 digitos x 8 px)          */
#define HUD_FUEL_X     120     /* barra de combustible               */
#define HUD_FUEL_W     8       /* nº de celdas de la barra (8x8 px)  */
/* Avance de la celda de la barra de combustible: 14 px. Las celdas del HUD se
 * pintan a escala 2 (16 px), asi que avanzar 14 px las solapa y la barra se ve
 * continua en vez de con huecos. HUD_FUEL_STEP va en unidades de HUD_SCALE. */
#define HUD_FUEL_STEP  14
#define HUD_LIVES_X    208     /* contador de vidas                  */

/* Patrones de sprite del HUD. */
#define HUD_PAT_DIGIT0 42       /* 10 digitos = 42..51                */
#define HUD_PAT_LIVES  52       /* icono de vida (avion pequeno)      */
#define HUD_PAT_BAR_ON 53       /* celda de barra llena               */
#define HUD_PAT_BAR_OFF 54      /* celda de barra vacia               */

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
#define TILE_BRIDGE   0x1D   /* viga del puente (arena maciza)            */

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
/* Panel de GAME OVER: paleta 0, que queda libre porque el cesped ya usa
 * PAL_EDGE. 1 = negro (fondo opaco), 3 = blanco (texto de la fuente). */
#define PAL_MENU     VC_BGPAL_0
/* ===========================================================================
 * SPRITES
 * ===========================================================================
 * Slots bajos: objetos del juego. 10..15: HUD (fijo en la banda superior).
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
#define SPR_PLANE      0     /* sprite unico del avion (escala 2x) */
#define SPR_PLANE_PAT  12
#define SPR_BOAT       8
#define SPR_BOAT_PAT   13
#define SPR_HELI       6
#define SPR_HELI_PAT   9
#define SPR_BULLET     30    /* 30 y 31 (pool de 2 balas), libre de HUD */
#define SPR_BULLET_PAT 14
#define SPR_BOOM       5
#define SPR_BOOM_PAT   10     /* 10 y 11 = 2 frames de explosion    */

/* Patrones del mosaico del avion (16x16). */
#define PAT_PLANE_TL 0     /* el avion vuelve a ser un solo sprite (patron 0) */
#define PAT_PLANE_TR 0
#define PAT_PLANE_BL 0
#define PAT_PLANE_BR 0

#define SPR_PLANE_PAL  VC_SPPAL_1
#define SPR_BOAT_PAL   VC_SPPAL_2
#define SPR_HELI_PAL   VC_SPPAL_0
#define SPR_BULLET_PAL VC_SPPAL_1
#define SPR_BOOM_PAL   VC_SPPAL_3

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

/* El avion se dibuja con su esquina superior en plane_y. Para que este CENTRADO
 * en las filas de cinta que ocupa, el centro (plane_y + PLANE_H/2) debe caer en
 * mitad de una fila de cinta (celda) => plane_y = 4 + 8k.
 * PLANE_Y es la posicion fija tras reaparecer; PLANE_ROW su fila de cinta. */
#define PLANE_Y    (4 + 8 * 22)          /* = 180: altura fija, abajo */
#define PLANE_ROW  ((PLANE_Y + (PLANE_H / 2)) >> 3)

static uint16_t plane_x;
static uint8_t  plane_y;
static uint16_t scroll_val;   /* valor del registro de scroll (0..255) */
static uint8_t  scroll_speed; /* 1/8 px por frame: SCROLL_MIN..SCROLL_MAX */
static uint8_t  scroll_acc;   /* acumulador con los 3 bits fraccionarios  */
static uint32_t world_row;    /* ultima fila de mundo generada         */
static uint32_t scroll_total; /* pixeles recorridos desde el inicio    */
static uint8_t  quit_flag;
static uint8_t  sprites_off;   /* 1 = ya se ocultaron avion/objetos (game over) */
static uint8_t  oam_cache_invalida;  /* 1 = redibujar TODO el OAM (patron incluido) */

static uint8_t  boat_active;
static uint8_t  boat_x;
static uint8_t  boat_y;
static uint8_t  boat_dir;       /* 0 = derecha, 1 = izquierda (deriva)      */
static uint8_t  boat_drift;     /* contador de frames para el paso lateral  */
static uint8_t  boat_is_heli;   /* 1 = el enemigo actual es un helicoptero  */
static uint32_t boat_next;

/* Explosion (sprite efimero al destruir un barco). */
static uint8_t  boom_active;
static uint8_t  boom_x;
static uint8_t  boom_y;
static uint8_t  boom_frames;

/* Columna de la isla de la fila anterior (para islas de 2 filas). */


/* Balas del jugador. */
static uint8_t   bullet_alive[NBULLETS];
static uint16_t  bullet_x[NBULLETS];
static uint8_t   bullet_y[NBULLETS];
static uint8_t   fire_cooldown;

/* Flag de muerte del avion (parpadeo breve antes de reubicar). */
static uint8_t   plane_dead;
static uint8_t   plane_dead_frames;
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
 *   island_l/r  = isla central que parte el cauce (0xFF = sin division) */
static uint8_t   river_l[VC_MAP_ROWS];
static uint8_t   river_r[VC_MAP_ROWS];
static uint8_t   island_l[VC_MAP_ROWS];
static uint8_t   island_r[VC_MAP_ROWS];

/* PUENTE: 1 si la fila de cinta es la viga (mata salvo el hueco). Se lleva en
 * RAM porque gen_bridge solo vale para la fila que se esta generando. */
static uint8_t   puente_bit[VC_MAP_ROWS];

/* MAPA DE BITS: 1 = la celda es cuadrante de isla (64x32 = 256 bytes).
 *
 * El core no deja leer la VRAM, y la animacion del agua necesita saber si una
 * celda es isla antes de repintarla: el perfil (island_l/r) cambia cuando la
 * fila se regenera, asi que no sirve como fuente de verdad. Un bit por celda
 * cabe de sobra en RAM. */
static uint8_t   isla_bit[VC_MAP_ROWS][8];   /* 64 celdas = 8 bytes por fila */

#define ISLA_SET(row, col)  (isla_bit[(row)][(col) >> 3] |= (uint8_t)(1u << ((col) & 7)))
#define ISLA_CLR(row, col)  (isla_bit[(row)][(col) >> 3] &= (uint8_t)~(1u << ((col) & 7)))
#define ISLA_GET(row, col)  ((isla_bit[(row)][(col) >> 3] >> ((col) & 7)) & 1)

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

/* --- GENERADO por tools/gen_tiles.py --- */
static const uint8_t tile_island_tl_p0[8] = { 0xFF, 0xFF, 0xF1, 0xE0, 0xC0, 0xC0, 0xC0, 0xE0 };
static const uint8_t tile_island_tl_p1[8] = { 0xF0, 0xC0, 0x8E, 0x9F, 0x3F, 0x3F, 0x3F, 0x1F };
static const uint8_t tile_island_tr_p0[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x7F, 0x7F, 0xFF };
static const uint8_t tile_island_tr_p1[8] = { 0xFF, 0x3F, 0x1F, 0x1F, 0x8F, 0x8F, 0x8F, 0x0F };
static const uint8_t tile_island_bl_p0[8] = { 0xF1, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static const uint8_t tile_island_bl_p1[8] = { 0x8E, 0x80, 0xC0, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF };
static const uint8_t tile_island_br_p0[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static const uint8_t tile_island_br_p1[8] = { 0x1F, 0x1F, 0x3F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
/* MEDIDO en el emulador (no fiarse del byte):
 *   0x17 con p0=0x0F,p1=0xF0 se ve   aaaagggg  = arena IZQ / cesped DER
 *   0x19 con p0=0xF0,p1=0x0F se ve   ggggaaaa  = cesped IZQ / arena DER
 * El bit 7 del patron es el pixel IZQUIERDO, asi que el primer byte "1" sale a
 * la izquierda; leer el hex a ojo lleva a cruzarlos. Por eso 0x19 es el BASE. */
static const uint8_t tile_edge_l0_p0[8] = { 0xF0, 0xF0, 0xF0, 0xF0, 0xF8, 0xF8, 0xF8, 0xF8 };
static const uint8_t tile_edge_l0_p1[8] = { 0x0F, 0x0F, 0x0F, 0x0F, 0x07, 0x07, 0x07, 0x07 };
/* Mismo dibujo ESPEJADO: el core no voltea tiles de fondo, asi que el lado del
 * agua necesita su propio patron. */
static const uint8_t tile_edge_r0_p0[8] = { 0x0F, 0x0F, 0x0F, 0x0F, 0x1F, 0x1F, 0x1F, 0x1F };
static const uint8_t tile_edge_r0_p1[8] = { 0xF0, 0xF0, 0xF0, 0xF0, 0xE0, 0xE0, 0xE0, 0xE0 };
/* --- FIN GENERADO --- */

/* Arbol/vegetacion para el interior del islote. La forma debe seguir el
 * cuadrante que sustituye (arriba-derecha), para no romper el circulo:
 *   3 = agua, 1 = arena, 2 = vegetacion */
static const uint8_t tile_tree_p0[8] = { 0xFF,0xFF,0x3B,0x0B,0x05,0x0D,0x03,0x03 };
static const uint8_t tile_tree_p1[8] = { 0x1F,0x07,0xC3,0xF1,0xF9,0xF8,0xFC,0xFC };

/* VIGA DEL PUENTE: madera maciza en ARENA. En PAL_ISLAND (1=arena, 2=veg,
 * 3=azul) el indice 2 es vegetacion, asi que la viga va en el INDICE 1:
 * plan0=1 y plan1=0 en toda la celda -> indice (0<<1)|1 = 1 = arena. */
static const uint8_t tile_bridge_p0[8] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };
static const uint8_t tile_bridge_p1[8] = { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };

/* Fondo del panel de GAME OVER: opaco y en INDICE 1, que en PAL_MENU es el
 * NEGRO. La fuente siempre pinta el indice 3 (BLANCO en PAL_MENU), asi que
 * cada pieza usa un indice distinto. Con un tile transparente (0) se veria el rio. */
static const uint8_t tile_panel_p0[8] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };
static const uint8_t tile_panel_p1[8] = { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };

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
 * Regla: 0 = transparente. Para dar volumen se usan dos tonos
 * (3 = claro, 1 = oscuro) de la paleta de sprite del objeto.
 * =========================================================================== */

/* AVION del jugador (8x8 a ESCALA 2x => 16x16 en pantalla).
 * Caza amarillo visto desde arriba, morro arriba, como el arcade.
 * 3 = amarillo, 1 = amarillo oscuro. */
/* Explosion: dos frames (estrella abierta y cerrada). Indice 3. */
static const uint8_t spr_boom_p0[8]  = { 0x81,0x42,0x24,0x18,0x18,0x24,0x42,0x81 };
static const uint8_t spr_boom2_p0[8] = { 0x00,0x6C,0xFE,0xFE,0xFE,0xFE,0x6C,0x00 };

/* --- DIGITOS 0..9 (8x8) para el HUD. Un solo color (indice 3). --- */
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

/* Icono de vida: mini-avion. */
static const uint8_t hud_lives_pat[8] = { 0x18,0x3C,0x7E,0xFF,0x18,0x18,0x18,0x00 };

/* Celdas de la barra de combustible (llena / vacia). */
static const uint8_t hud_bar_on[8]  = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };
static const uint8_t hud_bar_off[8] = { 0x81,0x81,0x81,0x81,0x81,0x81,0x81,0x81 };

/* --- NAVES (GENERADO por tools/gen_ships.py) --- */
/* --- Sprites nuevos (export de Piskel, tools/piskel_a_sprite.py) ---
 *
 * Cada frame es 8x8 (64 valores 0xAARRGGBB): alfa 0 = transparente. El core
 * admite 3 colores utiles por sprite (indices 1,2,3; el 0 es transparente),
 * asi que los tres caben. La paleta es POR SPRITE, de ahi que cada uno tenga
 * sus entradas con sus colores.
 */

/* AVION del jugador (hacia arriba): 1 = cian, 2 = azul. */
static const uint8_t spr_plane_p0[8] = { 0x10,0x38,0x6C,0xC6,0x92,0x38,0x6C,0x00 };
static const uint8_t spr_plane_p1[8] = { 0x00,0x00,0x10,0x10,0x00,0x00,0x10,0x54 };

/* BARCO (hacia la izquierda): 1 = negro, 2 = azul, 3 = rojo. */
static const uint8_t spr_boat_p0[8] = { 0x00,0x04,0x0C,0x3E,0x00,0x00,0x3E,0x00 };
static const uint8_t spr_boat_p1[8] = { 0x00,0x00,0x00,0x00,0xFF,0x7F,0x3E,0x00 };

/* HELICOPTERO (hacia la izquierda): 1 = cian, 2 = negro, 3 = rojo. */
static const uint8_t spr_heli_p0[8] = { 0xFE,0x10,0x00,0x00,0xFF,0x00,0x00,0x00 };
static const uint8_t spr_heli_p1[8] = { 0x00,0x00,0x39,0xFF,0xFF,0xFC,0x10,0x7C };
/* --- FIN NAVES --- */
/* Bala del jugador: 2x6 vertical, color claro (indice 3). */
static const uint8_t spr_bullet_p0[8] = { 0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00 };
static const uint8_t spr_bullet_p1[8] = { 0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00 };

/* Deposito de combustible: bidon con asa y "F", como el arcade.
 *    .####.
 *    #....#
 *    #.FF.#
 *    #.F..#
 *    #.FF.#
 *    #....#
 *    .####.
 * 3 = contorno, 1 = relleno. */
static const uint8_t spr_fuel_p0[8] = { 0x7E,0x5A,0x7E,0x7E,0x7E,0x7E,0x7E,0x00 };
static const uint8_t spr_fuel_p1[8] = { 0x7E,0x5A,0x4E,0x5E,0x4E,0x5E,0x7E,0x00 };

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
 * secundarios (no dispara): es el "arrancar" de la espera inicial. */
static uint8_t fire_pressed(void) {
    uint8_t j = joy_read();

    if (j & JOY_A_FIRE) return 1;
    while (rom_uart_rx_ready()) {
        char c = rom_uart_getc();
        if (c == ' ' || c == 'f' || c == 'F') return 1;
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
                if (plane_x > PLANE_MIN_X) plane_x = (uint16_t)(plane_x - PLANE_SPEED); break;
            case 'd': case 'D': case 'l': case 'L': case '6': case 0x13:
                if (plane_x < PLANE_MAX_X) plane_x = (uint16_t)(plane_x + PLANE_SPEED); break;
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
static uint8_t  hud_last_digit[4];
static uint8_t  hud_last_filled = 0xFF;   /* 0xFF = aun no dibujada */
static uint8_t  hud_last_slot_on[8];      /* estado de cada pieza de la barra */
static uint8_t  hud_last_lives = 0xFF;

/* Fija posicion y paleta de todos los sprites del HUD (una sola vez). */
static void hud_setup(void) {
    uint8_t i;
    for (i = 0; i < 4; i++)
        fv_sprite_move((uint8_t)(SPR_HUD_BASE + i), (uint16_t)(HUD_SCORE_X + i * 8),
                       HUD_Y, VC_SPPAL_1 | HUD_SCALE);
    for (i = 0; i < HUD_FUEL_W; i++)
        fv_sprite_move((uint8_t)(SPR_HUD_FUEL + i),
                       (uint16_t)(HUD_FUEL_X + i * HUD_FUEL_STEP),
                       HUD_Y, VC_SPPAL_1 | HUD_SCALE);
    for (i = 0; i < 8; i++) hud_last_slot_on[i] = 0;
    fv_sprite_move((uint8_t)(SPR_HUD_LIVES), HUD_LIVES_X, HUD_Y,
                   VC_SPPAL_1 | HUD_SCALE);
    fv_sprite_move((uint8_t)(SPR_HUD_LIVES + 1),
                   (uint16_t)(HUD_LIVES_X + 8), HUD_Y, VC_SPPAL_1 | HUD_SCALE);
    hud_last_digit[0] = hud_last_digit[1] = hud_last_digit[2] = hud_last_digit[3] = 0xFF;
    hud_last_filled = 0xFF;
    hud_last_lives = 0xFF;
}

/* Dibuja el HUD completo en sprites (solo lo que cambia). */
static void hud_draw(void) {
    uint8_t i;
    uint16_t v = (uint16_t)(score % 10000u);

    /* Puntos: 4 digitos, de izquierda a derecha. */
    for (i = 0; i < 4; i++) {
        uint8_t div = 1;
        uint8_t k;
        uint8_t d;
        for (k = (uint8_t)(3 - i); k; k--) div = (uint8_t)(div * 10);
        d = (uint8_t)((v / div) % 10u);
        if (d != hud_last_digit[i]) {
            hud_last_digit[i] = d;
            fv_oam((uint8_t)(SPR_HUD_BASE + i), VC_OAM_TILE,
                       (uint8_t)(HUD_PAT_DIGIT0 + d));
        }
    }

    /* Barra de combustible.
     *
     * Las celdas se pintan a escala 2 (16 px) con paso HUD_FUEL_STEP, asi que
     * se solapan y una misma celda de la barra cae dentro de dos piezas. Por
     * eso cada pieza se escribe con el patron de la celda que tiene CENTRADA
     * (pieza x+8 en pantalla), y se reescribe cuando cambia el valor de esa
     * celda o cuando el relleno cruza justo por su centro. Escribir siempre las
     * 8 piezas costaba varios cientos de us por frame en el VBLANK y dar por
     * sentado que la barra se actualizaba ocultaba bugs. */
    {
        uint8_t filled = (uint8_t)(((uint16_t)fuel * HUD_FUEL_W) / FUEL_MAX);
        for (i = 0; i < HUD_FUEL_W; i++) {
            uint16_t center = (uint16_t)(HUD_FUEL_X + i * HUD_FUEL_STEP + 8);
            uint8_t cell = (uint8_t)((center - HUD_FUEL_X) / HUD_FUEL_STEP);
            uint8_t on   = (uint8_t)(cell < filled);

            if (on != hud_last_slot_on[i]) {
                hud_last_slot_on[i] = on;
                fv_oam((uint8_t)(SPR_HUD_FUEL + i), VC_OAM_TILE,
                           (uint8_t)(on ? HUD_PAT_BAR_ON : HUD_PAT_BAR_OFF));
            }
        }
        hud_last_filled = filled;
    }

    /* Icono de vida + numero de vidas. */
    if (lives != hud_last_lives) {
        if (hud_last_lives == 0xFF)
            fv_oam(SPR_HUD_LIVES, VC_OAM_TILE, HUD_PAT_LIVES);
        fv_oam((uint8_t)(SPR_HUD_LIVES + 1), VC_OAM_TILE,
                   (uint8_t)(HUD_PAT_DIGIT0 + (lives % 10)));
        hud_last_lives = lives;
    }
}



/* ===========================================================================
 * PERFIL DEL RIO POR FILA
 * ===========================================================================
 * Para que el cauce no sea una banda fija (el rio se abre en dos y vuelve a
 * juntarse), cada fila de la cinta guarda su propio perfil:
 *
 *   river_l[r] / river_r[r]   : columnas de AGUA utiles (bordes del cauce)
 *   island_l[r]/island_r[r]   : tramo de ISLA CENTRAL que parte el cauce
 *                               (0xFF = el cauce es uno solo)
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
#define GEN_SECTOR     16     /* bloques entre puentes (1 puente cada uno)*/
#define GEN_AHEAD      2      /* filas de cinta fuera de la vista (ver
                               * advance_scroll): 32 de cinta - 30 visibles */
#define GEN_W_MIN      7      /* ancho minimo del cauce en el primer tramo */
#define GEN_W_HARD_MIN 5      /* ... y en el ultimo (dificultad maxima)    */
#define GEN_W_MAX      20     /* ancho maximo sin isla (cabe en la cinta) */
#define GEN_CURVE_MAX  3      /* desplazamiento maximo del centro         */
/* ISLA: GEN_ISLAND_W es el HUECO central (suelo + las dos orillas), o sea las
 * columnas de tierra que se ven. Con 9 columnas y dos brazos de 5 el cauce se
 * abre a GEN_SPLIT_W = 19 celdas. */
#define GEN_ARM_MIN    5      /* ancho MINIMO de cada brazo (tramo duro)  */
#define GEN_ARM_EASY   7      /* ancho de cada brazo en el primer tramo    */
#define GEN_ISLAND_W   9      /* isla visible = 9 columnas de tierra      */
/* ISLA MINIMA: por debajo de esto la tierra central NO se pinta ni cuenta para
 * la colision. Una isla de 1-2 celdas es una tira de arena de 8-16 px que
 * aparece y desaparece con el scroll y mata sin que se vea: el jugador lo vive
 * como "choque con algo invisible". 3 celdas es el minimo legible. */
#define ISLE_MIN_COLS  3
/* Con isla el cauce se abre a GEN_SPLIT_W: el rio se ensancha para que quepan
 * la tierra central y los dos brazos MAS ANCHOS (los del arranque). */
#define GEN_SPLIT_W    (GEN_ISLAND_W + 2 * GEN_ARM_EASY)  /* 23 */

/* PROGRESION: cada puente (sector) estrecha el rio. Con GEN_SECTOR bloques por
 * tramo y 4 tramos, es decir 4 puentes, el ancho llega al minimo duro:
 *
 *   tramo 1: brazos de 7 y cauce de 7..20   (facil)
 *   tramo 2: brazos de 6
 *   tramo 3: brazos de 5, cauce desde 6
 *   tramo 4: brazos de 5, cauce desde 5     (dificil)  ← se queda aqui
 *
 * GEN_RAMP_SECTORS = 3 significa "3 puentes para pasar de facil a dificil". */
#define GEN_RAMP_SECTORS 3

/* PUENTE: coordenadas FIJAS (documento de puentes, X1=34 X2=66 sobre 100).
 * Sobre 100 de ancho de pantalla -> 13,6..26,4; en nuestras 40 columnas
 * visibles eso es 13..26 en el CAUCE, o sea 12..27 contando las orillas. El
 * embudo converge aqui para que el puente encaje igual todos los tramos y el
 * cauce se vea ANGOSTARSE al llegar y ENSANCHARSE despues. */
#define GEN_PIER_L     12     /* borde izquierdo del cauce en el puente   */
#define GEN_PIER_R     27     /* borde derecho (= 12..27 con orillas)     */
#define GEN_FUNNEL_BLK 14     /* primer bloque de embudo (14 y 15)        */
/* HUECO DEL PUENTE: dos celdas de agua en el centro de la viga. GEN_PIER_L..R
 * es 12..27, o sea centro 19/20. El avion (16 px = 2 celdas) cabe justo. */
#define BRIDGE_GAP_L   19
#define BRIDGE_GAP_R   20
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
static uint8_t  gen_half;             /* semiancho del agua (sin tierra)   */
static uint8_t  gen_total_half;       /* semiancho del cauce dibujado      */
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
static uint8_t  gen_entity;           /* entidad del bloque (bits 9-11)   */
static uint8_t  gen_funnel;           /* 1 = bloque de embudo (pre-puente)*/
static uint8_t  gen_bridge;           /* 1 = esta fila es del PUENTE      */
static uint8_t  gen_block_row;        /* filas desde el ultimo bloque     */
static uint8_t  gen_block_idx;        /* bloque dentro del sector (0..15) */
static uint16_t gen_sector;           /* numero de sector (= puente)      */

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
 * pasaba casi siempre por los mismos dos anchos. */
static const uint8_t gen_width_tab[16] = {
    6, 7, 8, 9, 10, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20
};

static uint8_t gen_width_from(bits) {
    return gen_width_tab[bits & 0x0F];
}

/* --- PROGRESION: de facil (tramo 1) a dificil (a partir del tramo 4) ---
 *
 * `paso` = cuantos tramos se ha avanzado desde el arranque, con TOPE: el juego
 * se estabiliza en el nivel mas duro en vez de estrecharse sin fin.
 *
 * Se aplica SOLO aqui, al generar un bloque nuevo. Nunca se resta nada a
 * gen_left/gen_right ya calculados: como la curva se interpola 1 celda por
 * fila, cualquier resta se deshace en la fila siguiente y el ajuste no llega a
 * verse. Por eso el minimo se aplica sobre el ANCHO OBJETIVO del bloque. */
#define GEN_STEP()  ((uint8_t)(((gen_sector - 1) < GEN_RAMP_SECTORS) ? \
                               ((gen_sector - 1) & 0xFF) : GEN_RAMP_SECTORS))

/* Ancho minimo del cauce segun el tramo: 7 -> 6 -> 5. */
static uint8_t gen_w_min_now(void) {
    uint8_t paso = GEN_STEP();
    return (uint8_t)(GEN_W_MIN - ((paso > 2) ? 2 : paso));
}

/* Ancho minimo de cada brazo junto a la isla: 7 -> 6 -> 5. */
static uint8_t gen_arm_now(void) {
    uint8_t paso = GEN_STEP();
    return (uint8_t)(GEN_ARM_EASY - ((paso > 2) ? 2 : paso));
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
        gen_entity = 0;
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
        gen_entity = 0;
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
     * que deja tramos de cauce libre entre isla e isla. */
    if ((gen_seed_lo & 0x80) == 0 && (gen_block_idx & 1)) {
        /* El ensanche del cauce para que quepa la isla se hace al generar la
         * fila (generate_world_row), donde ya se conoce el ancho real. El
         * objetivo es el ANCHO de la isla en columnas (9), no su semiancho. */
        gen_split_target = GEN_ISLAND_W;
        gen_split_on = 1;
    } else {
        gen_split_target = 0;
        gen_split_on = 0;
    }

    /* --- Entidad del bloque: bits 9-11 (0 fuel, 1 barco, 2 heli, 3 jet...) - */
    gen_entity = (uint8_t)((gen_seed_hi >> 1) & 0x07);
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
    gen_left    = GEN_ZONE_L + 1;
    gen_right   = GEN_ZONE_R - 1;
    gen_t_left  = gen_left;
    gen_t_right = gen_right;
    gen_half    = (uint8_t)((gen_right - gen_left + 1) / 2);
    gen_total_half = gen_half;
    gen_split_on = 0;
    gen_split_w = 0;
    gen_split_target = 0;
    gen_entity  = 0;
    gen_funnel  = 0;
    gen_bridge  = 0;
    /* Se entra en el bloque 1 del tramo 1: el bloque 0 es el puente y al
     * arrancar ya se ha cruzado (si no, la partida empieza dentro del puente).
     * OJO: aqui NO se avanza el LFSR; el valor del bloque 0 lo consume
     * gen_new_block(), igual que en todos los demas bloques. */
    gen_block_row = 0;
    gen_block_idx = 1;
    gen_sector    = 1;
    gen_new_block();

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
    uint8_t knee_l, knee_r;

    /* --- Bloque del LFSR ---
     * Cada GEN_BLOCK filas se precalcula el siguiente bloque (ancho, curva,
     * isla, entidad y si es embudo o puente). gen_block_row cuenta las filas
     * ya generadas con el bloque actual. */
    if (gen_block_row == 0) { gen_new_block(); }

    /* --- VIGA: solo las primeras filas del bloque de puente ---
     * `gen_bridge` marca el BLOQUE entero (para el checkpoint), pero la viga
     * visible dura BRIDGE_ROWS filas. Se decide ANTES de incrementar
     * gen_block_row, que vale 0 en la primera fila del bloque. */
    {
        uint8_t es_viga = (uint8_t)(gen_bridge && gen_block_row < BRIDGE_ROWS);
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

    /* EL CAUCE NO SE ESTRECHA JUSTO TRAS UNA ISLA: al desvanecerse la tierra
     * el cauce volvia al ancho del bloque (12 en el tramo 1) y se quedaba asi
     * unas filas, o sea un estrechamiento feo pegado al ensanche. Se recuerda
     * el ancho de la ultima fila CON isla y, mientras la curva lo pida, no se
     * baja de ahi. */
    if (gen_split_w >= ISLE_MIN_COLS) {
        gen_w_tras_isla = (uint8_t)(gen_right - gen_left + 1);
    }
    gen_half = (uint8_t)((gen_right - gen_left + 1) / 2);

    /* --- Tierra central: crece y se encoge 1 celda por fila --- */
    {
        uint8_t goal = (uint8_t)(gen_split_on ? gen_split_target : 0);
        if (gen_split_w < goal) gen_split_w++;
        else if (gen_split_w > goal) gen_split_w--;
    }
    /* El cauce se reparte alrededor del CENTRO de la zona de agua.
     * Si en esta fila hay isla, el cauce se ENSANCHA para que quepan la
     * tierra y los dos brazos (el original tambien abre el rio). Se recalcula
     * DESPUES de saber si hay isla, que es lo que fallaba antes. */
    {
        uint8_t center = (uint8_t)((GEN_ZONE_L + GEN_ZONE_R + 1) / 2);
        uint8_t l = gen_left, r = gen_right;
        uint8_t w = (uint8_t)(r - l + 1);

        if (gen_split_w >= ISLE_MIN_COLS) {
            /* Al empezar la isla el cauce se abre DE GOLPE hasta el ancho que
             * necesita (isla + dos brazos). Interpolar no vale: la isla dura
             * pocas filas y el borde solo avanza 1 celda por fila, asi que el
             * ensanche no llegaba nunca y los brazos quedaban en 4. Al salir
             * de la isla si se estrecha progresivamente.
             * Los brazos son MAS ANCHOS al principio (7) y se van apretando
             * hasta 5 con los puentes. OJO: el ancho con isla NO usa GEN_SPLIT_W
             * fijo (23): si se usa, los brazos se quedan en 7 SIEMPRE y la
             * progresion no se nota (medido con simular_cauce.py). */
            /* OJO con las DOS costas: gen_left..gen_right incluye la celda de
             * costa en cada extremo, y la isla tambien se lleva las suyas. El
             * agua navegable de cada brazo es (ancho - isla - 2 costas)/2, asi
             * que para pedir N celdas de brazo el ancho es
             *     isla + 2*N + 2 costas
             * (antes faltaban las 2 costas y los brazos salian 1 celda mas
             * estrechos de lo pedido: 4 en vez de 5 en el tramo dificil). */
            uint8_t need = (uint8_t)(gen_split_w + 2 * gen_arm_now() + 2);
            w = need;
        } else if (w > GEN_W_MAX) {
            /* Sin isla el cauce no puede ser tan ancho que parezca un mar. */
            w = GEN_W_MAX;
        } else if (gen_w_tras_isla && w < gen_w_tras_isla) {
            /* Acaba de desaparecer una isla: no se encoge por debajo del ancho
             * que tenia con ella, para no dejar un bache justo despues. */
            w = gen_w_tras_isla;
        } else {
            /* EL RESTO DE LA ISLA: hay tierra saliendo o entrando y el cauce
             * ya no puede ser mas estrecho de lo que pide esa tierra. La
             * curva del bloque se interpola 1 celda por fila y llega antes
             * que el desvanecido de la isla: sin esto quedaban filas de 5
             * celdas CON la isla aun dentro (medido: 5, 6, 8... en el tramo 1,
             * donde el minimo es 7). Aqui no se abre de golpe: solo se pone
             * el suelo que la tierra necesita. */
            if (gen_split_w >= ISLE_MIN_COLS) {
                uint8_t need = (uint8_t)(gen_split_w + 2 * gen_arm_now() + 2);
                if (w < need) w = need;
            }
        }
        /* El ancho ya paso el bache: se suelta la referencia. */
        if (!gen_split_w && gen_w_tras_isla && w >= gen_w_tras_isla) {
            gen_w_tras_isla = 0;
        }

        /* OJO: se reconstruye por BORDES, no con semiancho. Con semiancho entero
         * las anchuras impares perdian una celda (17 -> 16) y el ensanche para
         * la isla no llegaba nunca a la pantalla. */
        l = (uint8_t)(center - (w / 2));
        r = (uint8_t)(l + w - 1);

        /* Suelo del tramo en TODAS las filas: la curva interpola 1 celda por
         * fila y el objetivo del bloque puede ser mas ancho que el minimo, asi
         * que sin esto quedaban filas sueltas por debajo del minimo del tramo
         * mientras el borde bajaba. */
        if (w < gen_w_min_now()) {
            l = (uint8_t)(center - (gen_w_min_now() / 2));
            r = (uint8_t)(l + gen_w_min_now() - 1);
            w = gen_w_min_now();
        }
        gen_left  = l;
        gen_right = r;
        gen_half  = (uint8_t)(w / 2);
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
        /* La isla crece de 1 a GEN_ISLAND_W columnas VISIBLES (9). gen_split_w
         * guarda ese ancho en curso, no un semiancho: asi los pasos son 1..9 y
         * el estado estable es exactamente 9 columnas de tierra. Se deja una
         * celda mas a la izquierda para que el cauce quede CENTRADO (con un
         * ancho par el centro cae entre dos celdas). */
        uint8_t w = gen_split_w;
        if (w > GEN_ISLAND_W) w = GEN_ISLAND_W;
        il = (uint8_t)(gen_left + (((gen_right - gen_left + 1) - w) / 2));
        ir = (uint8_t)(il + w - 1);
    }

    /* Guarda el perfil de esta fila ANTES de pintar. Es lo que consulta la
     * colision (plane_on_bank): si no se actualiza, la colision usa datos de
     * otra fila y el avion muere sin obstaculo. */
    river_l[cinta_row]  = gen_left;
    river_r[cinta_row]  = gen_right;
    island_l[cinta_row] = il;
    island_r[cinta_row] = ir;
    (void)knee_l; (void)knee_r;

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

            /* --- PUENTE: viga maciza con un HUECO en el centro ---
             * La viga cruza todo el cauce (gen_left..gen_right) salvo las dos
             * celdas del hueco, que es por donde se pasa. Antes se tapaba el
             * cauce ENTERO y el avion volaba sobre la viga: no habia puente que
             * cruzar, solo una franja de arena. El hueco esta en el centro del
             * cauce, que es a donde converge el embudo (GEN_PIER_L..R), asi que
             * siempre cae en la misma columna y el jugador puede anticiparlo. */
            if (puente_bit[cinta_row] && col >= gen_left && col <= gen_right &&
                (col < BRIDGE_GAP_L || col > BRIDGE_GAP_R)) {
                tile = TILE_BRIDGE;
                attr = PAL_ISLAND;         /* arena maciza */
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
    fv_load_bg_pattern(TILE_ISLAND,    tile_island_tl_p0, tile_island_tl_p1);
    fv_load_bg_pattern(TILE_ISLAND_R,  tile_island_tr_p0, tile_island_tr_p1);
    fv_load_bg_pattern(TILE_ISLAND_L2, tile_island_bl_p0, tile_island_bl_p1);
    fv_load_bg_pattern(TILE_ISLAND_R2, tile_island_br_p0, tile_island_br_p1);

    /* Transicion tierra->agua: arena y agua. */
    fv_load_bg_pattern(TILE_COAST, tile_edge_l0_p0, tile_edge_l0_p1);
    fv_load_bg_pattern(TILE_COAST_M, tile_edge_r0_p0, tile_edge_r0_p1);
    fv_load_bg_pattern(TILE_TREE,    tile_tree_p0,    tile_tree_p1);
    fv_load_bg_pattern(TILE_BRIDGE,  tile_bridge_p0,  tile_bridge_p1);
    /* Fondo opaco del panel de GAME OVER (TILE_PANEL usa el indice 1). */
    fv_load_bg_pattern(TILE_PANEL,   tile_panel_p0,   tile_panel_p1);

    fv_load_spr_pattern(SPR_PLANE_PAT, spr_plane_p0, spr_plane_p1);
    fv_load_spr_pattern(SPR_BOAT_PAT,  spr_boat_p0,  spr_boat_p1);
    fv_load_spr_pattern(SPR_HELI_PAT,  spr_heli_p0,  spr_heli_p1);
    fv_load_spr_pattern(SPR_BULLET_PAT, spr_bullet_p0, spr_bullet_p1);
    fv_load_spr_pattern(SPR_FUEL_PAT,   spr_fuel_p0,   spr_fuel_p1);
    fv_load_spr_pattern(SPR_BOOM_PAT,      spr_boom_p0,  spr_boom_p0);
    fv_load_spr_pattern((uint8_t)(SPR_BOOM_PAT + 1), spr_boom2_p0, spr_boom2_p0);

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
    vid_wait_vblank();
    vid_clear_vram();

    vid_set_bgcolor(VC_RGB444(0x2, 0x6, 0xF));

    /* Paleta 0 (PAL_MENU): solo la usa el panel de GAME OVER.
     * 1 = NEGRO (fondo opaco del panel), 3 = BLANCO (texto de la fuente). */
    vid_pal_set_bg(PAL_MENU, 1, VC_RGB444(0x0, 0x0, 0x0));
    vid_pal_set_bg(PAL_MENU, 2, VC_RGB444(0xF, 0xF, 0xF));
    vid_pal_set_bg(PAL_MENU, 3, VC_RGB444(0xF, 0xF, 0xF));

    /* Paleta 1: agua. 1 = azul, 2 = oleaje, 3 = azul claro. */
    vid_pal_set_bg(PAL_WATER, 1, VC_RGB444(0x0, 0x2, 0xA));
    vid_pal_set_bg(PAL_WATER, 2, VC_RGB444(0x1, 0x4, 0xE));   /* oleaje */
    vid_pal_set_bg(PAL_WATER, 3, VC_RGB444(0x2, 0x6, 0xF));

    /* Paleta 2 (ISLA y tierra): 1 = arena, 2 = vegetacion, 3 = agua. El 3 debe
     * ser el MISMO azul del cauce para que la isla no tenga borde negro. */
    vid_pal_set_bg(PAL_ISLAND, 1, VC_RGB444(0xD, 0x8, 0x3));
    vid_pal_set_bg(PAL_ISLAND, 2, VC_RGB444(0x0, 0x7, 0x0));
    vid_pal_set_bg(PAL_ISLAND, 3, VC_RGB444(0x2, 0x6, 0xF));

    /* Paleta 3 (cesped y orillas): 1 = verde claro (continuidad con el cesped),
     * 2 = arena, 3 = verde oscuro. El agua de la orilla es transparente
     * (indice 0) y deja ver el BG_COLOR, que es el mismo azul del rio. */
    vid_pal_set_bg(PAL_EDGE, 1, VC_RGB444(0x0, 0xA, 0x0));
    vid_pal_set_bg(PAL_EDGE, 2, VC_RGB444(0xD, 0x8, 0x3));
    vid_pal_set_bg(PAL_EDGE, 3, VC_RGB444(0x0, 0x6, 0x0));

    /* La franja de arena usa la misma paleta (indice 1 = arena). */
    /* Avion: amarillo vivo (3) y amarillo oscuro (1). */
    vid_pal_set_spr(SPR_PLANE_PAL, 1, VC_RGB444(0x0, 0xF, 0xF));  /* cian */
    vid_pal_set_spr(SPR_PLANE_PAL, 2, VC_RGB444(0x2, 0xA, 0xE));  /* azul */

    /* Barco: casco azul (1), cubierta roja (2), gris claro (3). */
    vid_pal_set_spr(SPR_BOAT_PAL,  1, VC_RGB444(0x0, 0x0, 0x0));  /* negro */
    vid_pal_set_spr(SPR_BOAT_PAL,  2, VC_RGB444(0x0, 0x0, 0xF));  /* azul  */
    vid_pal_set_spr(SPR_BOAT_PAL,  3, VC_RGB444(0xB, 0x0, 0x1));  /* rojo  */

    /* Helicoptero: cuerpo azul (1), rotor beige (2), azul claro (3). */
    vid_pal_set_spr(SPR_HELI_PAL,  1, VC_RGB444(0x2, 0xF, 0xF));  /* cian  */
    vid_pal_set_spr(SPR_HELI_PAL,  2, VC_RGB444(0x0, 0x0, 0x0));  /* negro */
    vid_pal_set_spr(SPR_HELI_PAL,  3, VC_RGB444(0xB, 0x1, 0x0));  /* rojo  */

    vid_pal_set_spr(SPR_FUEL_PAL,  3, VC_RGB444(0xD, 0x1, 0x1));  /* rojo  */
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
    gen_reset();

    /* Rellena la cinta: la fila de mundo n va a la fila de cinta (-n) & 31, asi
     * que la 31 es la de arriba (negativa) y las 0..30 quedan por delante. */
    for (row = 0; row < VC_MAP_ROWS; row++) {
        generate_world_row((uint32_t)row);
    }
    world_row = VC_MAP_ROWS - 2;

    /* Camara: muestra SANA el tramo ya pintado y deja UNA fila entera por
     * delante (la 30 del mundo). Ver advance_scroll(). */
    scroll_total = (uint32_t)((VC_MAP_ROWS - 1) * 8);
    scroll_val   = (uint16_t)((0 - scroll_total) & 0xFF);
    scroll_speed = SCROLL_NORM;
    scroll_acc   = 0;
    scroll_step  = 1;
    vid_set_scroll_y(scroll_val);

    plane_place_safe();

    boat_active = 0;
    boat_next   = 40;

    /* Estado de las balas y muerte. */
    for (row = 0; row < NBULLETS; row++) bullet_alive[row] = 0;
    fire_cooldown = 0;
    plane_dead = 0;
    plane_dead_frames = 0;
    esperando_fire = 1;       /* avion fijo al empezar; arranca con FIRE */

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
    uint8_t visible = 1;
    uint8_t flags = (uint8_t)(SPR_PLANE_PAL | VC_SPR_SCALE2X);

    if (oam_cache_invalida) {
        last_x = 0xFFFF; last_y = 0xFF; last_flags = 0xFF;
    }

    /* Si esta muerto, PARPADEA (se dibuja/oculta alternando) y no se mueve. */
    if (plane_dead && (plane_dead_frames & 4)) visible = 0;

    if (!visible) {
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

static void draw_boat(void) {
    /* Igual que el avion: solo se escribe el OAM si cambia. fv_sprite_move y
     * fv_oam cuestan ~7-15 us cada uno. */
    static uint8_t last_on = 0xFF;   /* 0 = oculto, 1 = barco, 2 = heli */
    static uint8_t last_x;
    static uint8_t last_y;
    static uint8_t last_dir = 0xFF;
    uint8_t mode = (uint8_t)(boat_active ? (boat_is_heli ? 2 : 1) : 0);
    /* Los dos sprites estan dibujados mirando a la IZQUIERDA. Si el enemigo se
     * mueve hacia la derecha (boat_dir == 0) se voltea con VC_SPR_FLIP_X. */
    uint8_t flip = (uint8_t)((boat_dir == 0) ? VC_SPR_FLIP_X : 0);

    if (oam_cache_invalida) {
        last_on = 0xFF; last_dir = 0xFF;
    }

    if (mode == last_on && (!boat_active ||
                            (boat_x == last_x && boat_y == last_y && flip == last_dir)))
        return;

    if (mode == 0) {
        fv_sprite_disable(SPR_BOAT);
        fv_sprite_disable(SPR_HELI);
        last_on = 0;
        return;
    }

    if (mode != last_on) {
        /* Cambio de tipo: oculta el que sobra y prepara el patron. */
        if (mode == 2) {
            fv_sprite_disable(SPR_BOAT);
            fv_sprite_move(SPR_HELI, boat_x, boat_y,
                           (uint8_t)(SPR_HELI_PAL | VC_SPR_SCALE2X | flip));
            fv_oam(SPR_HELI, VC_OAM_TILE, SPR_HELI_PAT);
        } else {
            fv_sprite_disable(SPR_HELI);
            fv_sprite_move(SPR_BOAT, boat_x, boat_y,
                           (uint8_t)(SPR_BOAT_PAL | VC_SPR_SCALE2X | flip));
            fv_oam(SPR_BOAT, VC_OAM_TILE, SPR_BOAT_PAT);
        }
    } else if (mode == 2) {
        fv_sprite_move(SPR_HELI, boat_x, boat_y,
                       (uint8_t)(SPR_HELI_PAL | VC_SPR_SCALE2X | flip));
        /* El FLIP va en las FLAGS: si solo cambia el sentido de marcha (misma
         * posicion), sin esto el enemigo se quedaba mirando al lado contrario. */
        fv_oam(SPR_HELI, VC_OAM_FLAGS,
               (uint8_t)(SPR_HELI_PAL | VC_SPR_SCALE2X | flip));
    } else {
        fv_sprite_move(SPR_BOAT, boat_x, boat_y,
                       (uint8_t)(SPR_BOAT_PAL | VC_SPR_SCALE2X | flip));
        fv_oam(SPR_BOAT, VC_OAM_FLAGS,
               (uint8_t)(SPR_BOAT_PAL | VC_SPR_SCALE2X | flip));
    }
    last_on = mode;
    last_x = boat_x;
    last_y = boat_y;
    last_dir = flip;
}

static void draw_fuel(void) {
    if (!fuel_active) {
        fv_sprite_disable(SPR_FUEL);
        return;
    }
    fv_sprite_move(SPR_FUEL, fuel_x, fuel_y, SPR_FUEL_PAL | VC_SPR_SCALE2X);
    fv_oam(SPR_FUEL, VC_OAM_TILE, SPR_FUEL_PAT);
}

/* Explosion: parpadea entre sus 2 frames mientras dura. */
static void draw_boom(void) {
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

    bullet_x[i] = (uint16_t)(plane_x + (PLANE_W / 2) - 1);  /* centrada en el morro */
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
 * El scroll de este juego DECRECE (scroll_val baja para que el terreno baje):
 *     fila_mundo = scroll_total >> 3   (la que esta arriba)
 *     cinta      = (32 - fila_mundo) & 31
 * Es la MISMA formula que usa generate_world_row() al pintar. Con
 * (scroll_val + y) >> 3 se lee el orden INVERSO y la colision mira el cauce de
 * otra fila (verde donde hay agua). */
#define FILA_CINTA(y)  ((uint8_t)((VC_MAP_ROWS - (uint8_t)((scroll_total + (y)) >> 3)) \
                                  & (VC_MAP_ROWS - 1)))

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
    uint8_t ir = island_r[cinta_row];

    /* Los bordes son tiles de ARENA, no agua: el agua va de l+1 a r-1. */
    if (c <= l || c >= r) return 0;
    if (il != 0xFF && c >= il && c <= ir) return 0;

    /* PUENTE: la viga es solida salvo por el hueco. */
    if (puente_bit[cinta_row] && (c < BRIDGE_GAP_L || c > BRIDGE_GAP_R)) return 0;
    return 1;
}

/* Lo contrario: cesped, orilla, isla o viga. */
static uint8_t celda_en_tierra(uint8_t cinta_row, uint8_t c) {
    return (uint8_t)(!celda_agua(cinta_row, c));
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
    /* TODAS las celdas que ocupa el sprite, no solo las dos de los extremos.
     * Con el avion en x=100..115 eso es 12, 13 y 14: mirar solo 12 y 14 dejaba
     * la 13 sin comprobar, y ahi es justo donde cae la costa cuando el rio se
     * estrecha. El avion se metia medio sprite en la orilla sin morir. */
    {
        uint8_t c0 = (uint8_t)(x >> 3);
        uint8_t c1 = (uint8_t)((x + PLANE_W - 1) >> 3);
        uint8_t c;
        for (c = c0; c <= c1; c++) {
            if (celda_en_tierra(cinta_row, c)) return 1;
        }
    }
    return 0;
}

/* Muerte del avion: resta una vida, reproduce la explosion y activa el
 * parpadeo breve. Tras el parpadeo se reubica (o termina la partida). */
static void plane_hit(void) {
    if (plane_dead) return;
    plane_dead = 1;
    plane_dead_frames = 0;
    snd_explode();

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

    /* El avion vuelve YA al punto de partida y el rio se para hasta que se
     * pulse FIRE: no sigue andando mientras se pierde una vida. */
    gen_restore_checkpoint();
    plane_place_safe();
    esperando_fire = 1;
    snd_engine_off();
}

/* Mueve el avion al centro-abajo del rio tras la muerte. Si no quedan vidas, se
 * marca game over. */
/* Coloca el avion en un punto navegable de su fila: el centro del agua libre
 * (a izquierda o derecha de la isla central si la hay). */
static void plane_place_safe(void) {
    /* Coloca el avion en el agua, CENTRADO en el hueco donde quepa ENTERO.
     *
     * OJO: no basta con centrarlo en el cauce o en un brazo. El sprite mide
     * 16 px (2 celdas), asi que si nace en un cauce de 3 celdas con la orilla
     * a un lado, medio sprite queda en tierra y la colision lo mata al
     * instante: el jugador pulsa FIRE y el avion vuelve a morir, una y otra
     * vez, sin poder jugar. Se busca una celda c que cumpla que caben c y c+1
     * en agua de la MISMA fila de cinta. */
    uint8_t row = PLANE_ROW;
    uint8_t k;
    uint8_t mejor = 0xFF;
    uint8_t mejor_dist = 0xFF;
    uint8_t centro = (uint8_t)((GEN_ZONE_L + GEN_ZONE_R + 1) / 2);

    for (k = GEN_ZONE_L; k + 1 <= GEN_ZONE_R; k++) {
        uint8_t dist;
        if (!celda_agua(row, k) || !celda_agua(row, (uint8_t)(k + 1))) continue;
        dist = (uint8_t)((k > centro) ? (k - centro) : (centro - k));
        if (dist < mejor_dist) { mejor_dist = dist; mejor = k; }
    }

    if (mejor != 0xFF) {
        /* Centro del hueco de 2 celdas. */
        plane_x = (uint16_t)((uint16_t)mejor * 8 + 8) - (PLANE_W / 2);
    } else {
        /* Sin hueco de 2 celdas (cauce de 1 celda): se pone donde haya agua,
         * aunque roce; es mejor que dejarlo clavado muriendo. */
        for (k = GEN_ZONE_L; k <= GEN_ZONE_R; k++) {
            if (celda_agua(row, k)) { plane_x = (uint16_t)(k * 8) - 4; break; }
        }
    }
    plane_y = PLANE_Y;

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

        /* Bala <-> barco (caja AABB). */
        if (boat_active) {
            if (fv_box_toca(bullet_x[i], bullet_y[i], 2, 6,
                         boat_x, boat_y, 16, 16)) {
                bullet_alive[i] = 0;
                boat_active = 0;    /* destruye el barco */
                boom_active = 1;    /* explosion en su sitio   */
                boom_x = boat_x;
                boom_y = boat_y;
                boom_frames = 12;
                score += SCORE_SHIP;
                snd_explode();
                continue;
            }
        }

        /* Bala <-> orilla (por X; la bala viaja en linea recta asi que si su X
         * cae fuera del canal, la apagamos). */
        if (bullet_x[i] < PLANE_MIN_X ||
            (bullet_x[i] + 1) >= ((RIVER_RIGHT + 1) * 8)) {
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

    if (plane_dead) return;

    /* AVION <-> ORILLA / ISLA.
     *
     * OJO: no se puede cachear el resultado por (x, y) del sprite. La respuesta
     * depende tambien de la fila de cinta que pasa por debajo, y con filas de
     * 8 px y 1 px de scroll por frame esa fila cambia en el 12% de los frames:
     * con cache, el avion moria solo en mitad del cauce. Son 3 consultas a RAM
     * por frame, coste despreciable. Se miran las TRES filas de cinta que ocupa
     * el sprite (arriba, centro y abajo); con un sprite de 16 px el borde del
     * canal puede cambiar a mitad del avion. */
    /* Se miran TODAS las filas de cinta que ocupa el sprite (16 px = 3 filas),
     * no solo arriba/centro/abajo: con 1 px de scroll por frame la orilla entra
     * por una fila intermedia y el avion se metia medio sprite en la arena sin
     * que saltara la colision. */
    {
        /* Las TRES filas de cinta que ocupa un sprite de 16 px, cada una por su
         * banda: 0-7, 8-15 y 16-23 desde plane_y. Mirar solo y, y+7, y+8 y
         * y+15 dejaba la fila INTERMEDIA (y+8..y+15) sin cubrir cuando el
         * sprite empezaba en mitad de una celda: el avion se metia en la orilla
         * por la fila de en medio sin que saltara la colision. */
        uint8_t k;
        last_bank = 0;
        for (k = 0; k < PLANE_H; k++) {
            if (plane_on_bank(plane_x, (uint8_t)(plane_y + k))) {
                last_bank = 1;
                break;
            }
        }
    }
    if (last_bank) {
        plane_hit();
        return;
    }

    /* Nota: la colision con cesped e isla central la resuelve plane_on_bank()
     * mas arriba, consultando el perfil de la fila. */

    /* Avion <-> barco (caja AABB). */
    if (boat_active) {
        if (fv_box_toca(plane_x, plane_y, PLANE_W, PLANE_H,
                     boat_x, boat_y, 16, 16)) {
            boat_active = 0;   /* choque destruye ambos */
            plane_hit();
            return;
        }
    }

    /* Avion <-> deposito de combustible: RECARGA POR CONTACTO.
     *
     * El bidon NO se consume en el primer frame de solape: mientras el avion
     * siga tocandolo se acumula combustible cada FUEL_REFUEL_EVERY frames, asi
     * que lo que cargas depende del TIEMPO de contacto (rozarlo de lado suma
     * poco; quedarse encima llena el deposito). El acumulador NO se descarta
     * al soltar el bidon: lo cargado se queda y el bidon se da por recogido.
     * Con el deposito a tope no se toca: el bidon pasa de largo. */
    if (fuel_active && fuel > 0) {
        /* La caja del avion es de 16 px (avion a escala 2x); la del bidon se
         * toma a su tamano real de 8 px para que el contacto sea sincero. */
        if (fv_box_toca(plane_x, plane_y, PLANE_W, PLANE_H,
                     fuel_x, fuel_y, 8, 8)) {
            fuel_touching = 1;
            fuel_refuel_tick++;
            if (fuel_refuel_tick >= FUEL_REFUEL_EVERY) {
                fuel_refuel_tick = 0;

                /* Un pulso de recarga mientras haya contacto: el tiempo total
                 * de contacto es lo que decide cuanta energia se carga. */
                if (fuel <= (uint8_t)(FUEL_MAX - FUEL_REFUEL_STEP)) {
                    fuel = (uint8_t)(fuel + FUEL_REFUEL_STEP);
                } else {
                    fuel = FUEL_MAX;
                }

                if (fuel == FUEL_MAX) {
                    /* Deposito lleno: el bidon se ha agotado. */
                    fuel_active = 0;
                    fuel_touching = 0;
                    fuel_refuel_tick = 0;
                    score += SCORE_FUEL;
                    snd_fuel();
                }
            }
        } else if (fuel_touching) {
            /* Ha dejado de tocarlo: lo cargado se queda y el bidon se da por
             * recogido (no se puede volver a sorber). */
            fuel_active = 0;
            fuel_touching = 0;
            fuel_refuel_tick = 0;
            score += SCORE_FUEL;
            snd_fuel();
        }
    } else if (!fuel_active) {
        fuel_touching = 0;
        fuel_refuel_tick = 0;
    }
}

/* ===========================================================================
 * ACTUALIZACION DEL BARCO ENEMIGO
 * =========================================================================== */
static void update_boat(void) {
    if (boat_active) {
        boat_y = (uint8_t)(boat_y + scroll_step);
        if (boat_y >= VC_SCREEN_H) { boat_active = 0; return; }

        /* Los dos bordes del sprite (16 px = 2 celdas) tienen que caer en agua.
         * Se pregunta CELDA A CELDA con celda_agua(), la misma que usa el
         * avion: asi el enemigo rebota en la orilla, en la isla y en la viga del
         * puente sin listas de limites que se desincronicen del rio. */

        /* Si el cauce se cerro a su lado, se desplaza AL AGUA MAS CERCANA,
         * pero **1 px por frame**: moviendolo de golpe saltaba de una celda a
         * otra y el jugador lo veia teletransportarse. Si se sale de la
         * pantalla por arriba/abajo no se mueve de lado. */
        if (!celda_agua(FILA_CINTA(boat_y), (uint8_t)(boat_x >> 3))) {
            uint8_t row = FILA_CINTA(boat_y);
            uint8_t c0  = (uint8_t)(boat_x >> 3);
            uint8_t target = 0xFF;
            uint8_t d;

            for (d = 1; d < VC_MAP_COLS && target == 0xFF; d++) {
                if (c0 >= d && celda_agua(row, (uint8_t)(c0 - d))) target = (uint8_t)(c0 - d);
                else if (celda_agua(row, (uint8_t)(c0 + d)))       target = (uint8_t)(c0 + d);
            }
            if (target == 0xFF) { boat_active = 0; return; }

            /* 1 px hacia el objetivo, sin pasarse. */
            {
                uint8_t tx = (uint8_t)(target * 8);
                if (boat_x < tx) boat_x++;
                else if (boat_x > tx) boat_x--;
            }
        }

        /* Deriva lateral con rebote: 1 px cada pocos frames. El HELICOPTERO
         * tambien queda sujeto al cauce; en el arcade vuela sobre el agua. */
        if (++boat_drift >= (boat_is_heli ? 2 : 4)) {
            uint8_t nx;
            boat_drift = 0;
            nx = (uint8_t)((boat_dir == 0) ? boat_x + 1 : boat_x - 1);

            /* Rebota si el sprite se sale del agua por cualquier lado. */
            if (!sprite_cabe(nx, boat_y)) {
                boat_dir = (uint8_t)!boat_dir;
            } else {
                boat_x = nx;
            }
        }
        return;
    }

    if (world_row >= boat_next) {
        /* Nace SIEMPRE en agua navegable de la fila por la que entra. */
        uint8_t r0 = FILA_CINTA(0);
        uint8_t cols = 0;
        uint8_t k;

        boat_is_heli = (uint8_t)(rng_next() & 1);   /* mitad barcos, mitad helis */

        /* Cuenta las celdas validas y elige una al azar: no hay brazo "mejor",
         * basta con que quepa el sprite (2 celdas). */
        for (k = GEN_ZONE_L; k + 1 <= GEN_ZONE_R; k++) {
            if (sprite_cabe((uint16_t)(k * 8), 0)) cols++;
        }
        if (cols == 0) { boat_next = world_row + 4; return; }

        {
            uint8_t pick = (uint8_t)(rng_next() % cols);
            for (k = GEN_ZONE_L; k + 1 <= GEN_ZONE_R; k++) {
                if (sprite_cabe((uint16_t)(k * 8), 0)) {
                    if (pick == 0) break;
                    pick--;
                }
            }
        }
        boat_x = (uint8_t)(k * 8);
        boat_dir = (uint8_t)(rng_next() & 1);
        boat_y = 0;
        boat_drift = 0;
        boat_active = 1;
        boat_next = world_row + 16;
    }
}

/* ===========================================================================
 * ACTUALIZACION DEL DEPOSITO DE COMBUSTIBLE
 * =========================================================================== */
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
         * veia bidones que parpadeaban a media pantalla. */
        if (!sprite_cabe(fuel_x, fuel_y)) {
            uint8_t c0 = (uint8_t)(fuel_x >> 3);
            uint8_t d;

            for (d = 1; d < VC_MAP_COLS; d++) {
                if (c0 >= d && sprite_cabe((uint16_t)((c0 - d) * 8), fuel_y)) {
                    break;
                }
                if (sprite_cabe((uint16_t)((c0 + d) * 8), fuel_y)) break;
            }
            /* Si en esta fila no hay NINGUN sitio valido (cauce cerrado por la
             * viga del puente), entonces si se retira. */
            if (d >= VC_MAP_COLS) fuel_active = 0;
            else {
                /* 1 px por frame hacia el agua: desplazarlo de golpe hacia que
                 * el bidon saltara de sitio de un frame a otro. */
                uint8_t tx = (uint8_t)((c0 < d ? (c0 + d) : (c0 - d)) * 8);
                if (fuel_x < tx) fuel_x++;
                else if (fuel_x > tx) fuel_x--;
            }
        }
        return;
    }

    if (world_row >= fuel_next) {
        /* Aparece en AGUA navegable de la fila por la que entra. */
        uint8_t r0 = FILA_CINTA(0);
        uint8_t cols = 0;
        uint8_t k;

        for (k = GEN_ZONE_L; k + 1 <= GEN_ZONE_R; k++) {
            if (sprite_cabe((uint16_t)(k * 8), 0)) cols++;
        }
        if (cols == 0) { fuel_next = world_row + 4; return; }

        {
            uint8_t pick = (uint8_t)(rng_next() % cols);
            for (k = GEN_ZONE_L; k + 1 <= GEN_ZONE_R; k++) {
                if (sprite_cabe((uint16_t)(k * 8), 0)) {
                    if (pick == 0) break;
                    pick--;
                }
            }
        }
        fuel_x = (uint8_t)(k * 8);
        fuel_y = 0;
        fuel_active = 1;
        fuel_next = world_row + 30;
    }
}

/* ===========================================================================
 * DRENAJE DE COMBUSTIBLE
 * ===========================================================================
 * Cada FUEL_DRAIN_EVERY frames se pierde 1 unidad de combustible. Si llega a
 * 0, se pierde una vida (o game over).
 * =========================================================================== */
static void update_fuel_gauge(void) {
    if (game_over) return;

    fuel_tick++;
    if (fuel_tick < FUEL_DRAIN_EVERY) return;
    fuel_tick = 0;

    if (fuel > 0) {
        fuel--;
    }

    if (fuel == 0 && !plane_dead) {
        /* Sin combustible: muere el avion (pierde una vida). */
        plane_hit();
    }
}

/* ===========================================================================
 * ANIMACION DEL OLEAJE (repartida entre frames)
 * ===========================================================================
 * Alterna los dos patrones de agua en UNA fila por frame (rotando por las 32
 * del mapa). Coste por frame: 24 celdas, cabe de sobra en el VBLANK.
 * =========================================================================== */
static void update_water_anim(void) {
    /* DESACTIVADA (a proposito): repinta agua de filas VISIBLES.
     *
     * Medido con capturas antes/despues de la escritura: mientras el juego
     * repinta la fila nueva, esta animacion puede pasar por la misma fila de
     * cinta y escribir en celdas a las que aun les falta el tile de la fila
     * nueva. El resultado son HUECOS: columnas con el tile que habia antes
     * (panel de GAME OVER, agua suelta) en mitad del cauce, y manchas en el
     * cesped. Tambien borra la tierra central si la celda no esta en isla_bit.
     *
     * El oleaje hay que hacerlo cambiando el PATRON del tile de agua (o con
     * dos tiles alternando por bloques), nunca repintando celdas que el
     * jugador esta viendo. */
    (void)0;
}

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

        if (fila_nueva > world_row) {
            world_row = fila_nueva;
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
    plane_dead_frames = 0;
    esperando_fire = 1;       /* avion fijo al reiniciar; arranca con FIRE */

    /* Balas, barco, deposito. */
    for (row = 0; row < NBULLETS; row++) bullet_alive[row] = 0;
    fire_cooldown = 0;
    boat_active = 0;
    boat_next   = 40;
    boat_dir    = 0;
    boom_active = 0;
    boom_frames = 0;
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

    /* Re-genera el rio (misma semilla => mismo layout). El rio se repinta
     * entero, asi que no hay que borrar el panel del GAME OVER a mano. */
    rng_state = RIVER_SEED;

    /* La camara se fija ANTES de pintar: la fila de mundo n se escribe en la
     * cinta (32 - n) & 31, asi que si la camara ya esta en su sitio el repintado
     * cae justo donde debe (si no, se pinta un layout y se ensena otro). */
    scroll_total = (uint32_t)((VC_MAP_ROWS - 1) * 8);   /* ver setup_video() */
    scroll_val = (uint16_t)((0 - scroll_total) & 0xFF);
    vid_set_scroll_y(scroll_val);

    for (row = 0; row < VC_MAP_ROWS; row++) {
        generate_world_row((uint32_t)row);
    }
    world_row  = VC_MAP_ROWS - 2;
    scroll_speed = SCROLL_NORM;
    scroll_acc   = 0;
    scroll_step  = 1;

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
 * PANTALLA DE GAME OVER
 * ===========================================================================
 * Muestra el mensaje y espera FIRE/R para reiniciar o Q para salir.
 *
 * El panel se pinta UNA VEZ: el mundo se congela mientras dure y no se vuelve
 * a tocar el tilemap. Repintarlo cada frame hacia que la captura lo pillara a
 * medio escribir (parpadeo y renglones que faltaban).
 *
 * El panel ocupa 6 filas de cinta, en la paleta del MENU (PAL_MENU):
 *   fondo  = TILE_PANEL opaco (indice 1 = negro)
 *   texto  = fuente (indice 3 = blanco)
 * =========================================================================== */
#define MENU_ROW0   11     /* primera fila del panel EN PANTALLA (celdas) */
#define MENU_ROWS    6     /* alto del panel en filas de cinta            */
#define MENU_COL0    2     /* primera columna del panel                   */
#define MENU_COL1   37     /* ultima columna del panel                    */

static void game_over_screen(void) {
    static uint8_t panel_drawn;
    uint8_t j = joy_read();
    char c;
    uint8_t ly;

    if (!panel_drawn) {
        uint8_t r, k;
        panel_drawn = 1;


        /* Pinta el panel en las filas de cinta que estan en pantalla AHORA y
         * congela el scroll: a partir de aqui nada mueve el mundo. */
        for (r = 0; r < MENU_ROWS; r++) {
            uint8_t row = (uint8_t)(((scroll_val >> 3) + MENU_ROW0 + r) & (VC_MAP_ROWS - 1));
            uint16_t cell = (uint16_t)((uint16_t)row << 6);
            for (k = MENU_COL0; k <= MENU_COL1; k++) {
                uint16_t addr = (uint16_t)(cell + k);
                VID_ADDR_LO = (uint8_t)addr;
                VID_ADDR_HI = (uint8_t)((addr >> 8) & 0x07);
                VID_DATA    = TILE_PANEL;
                VID_ADDR_LO = (uint8_t)addr;
                VID_ADDR_HI = (uint8_t)(VC_AREA_ATTR | ((addr >> 8) & 0x07));
                VID_DATA    = PAL_MENU;
            }
        }

        /* Texto: tres renglones, en las filas 1, 3 y 4 del panel. */
        for (ly = 0; ly < 3; ly++) {
            static const char *const lineas[3] = {
                "GAME OVER", "FIRE/R = reintentar", "Q = salir"
            };
            uint8_t fila = (uint8_t)(((scroll_val >> 3) + MENU_ROW0 +
                                      (ly == 2 ? 4 : (uint8_t)(ly + 1))) & (VC_MAP_ROWS - 1));
            const char *t = lineas[ly];
            uint8_t col = (uint8_t)((ly == 0) ? 14 : (ly == 1 ? 12 : 15));
            while (*t) {
                fv_put_cell(col, fila, (uint8_t)*t);
                fv_put_attr(col, fila, PAL_MENU);
                col++;
                t++;
            }
        }
    }

    if (j & JOY_A_FIRE) {
        panel_drawn = 0;
        reset_game();
        return;
    }

    while (rom_uart_rx_ready()) {
        c = rom_uart_getc();
        if (c == 'r' || c == 'R' || c == ' ') {
            panel_drawn = 0;
            reset_game();
            break;
        }
        if (c == 'q' || c == 'Q') {
            quit_flag = 1;
            break;
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
            if (fire_pressed()) {
                esperando_fire = 0;
                /* OJO: sin esto el avion queda INVULNERABLE. plane_hit() pone
                 * plane_dead=1 y solo reset_game() lo volvia a 0, asi que al
                 * reanudar con FIRE las colisiones seguian saltandose por el
                 * "if (plane_dead) return" y el avion atravesaba orillas,
                 * islas y enemigos sin perder una vida. */
                plane_dead = 0;
                plane_dead_frames = 0;
                scroll_speed = SCROLL_NORM;
                scroll_acc   = 0;
                snd_engine_on();
            }
            snd_update();
            draw_plane();
            hud_draw();
            update_water_anim();
            vid_wait_vblank_end();
            perf_end();
            continue;
        }

        /* 1) Leer entrada y mover el avion / disparar (solo si no hay game over). */
        if (!game_over) {
            read_input();
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
            update_fuel_gauge();
        }
        perf_mark(0);

        /* 3) Dibujo: sprites PRIMERO (barato y critico). En GAME OVER solo se
         * deja el HUD: el avion y los objetos deben desaparecer. */
        /* 3) Dibujo. En GAME OVER se apaga TODO (avión, objetos y HUD) para
         * que el texto del panel quede legible: los sprites del HUD tapaban
         * parte del mensaje. */
        if (!game_over) {
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
            update_water_anim();
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
