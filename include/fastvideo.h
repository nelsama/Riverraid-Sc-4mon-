#ifndef FASTVIDEO_H
#define FASTVIDEO_H

/* ===========================================================================
 * fastvideo.h - primitivas de video EN LINEA, sin dependencia de lib/vc.lib
 * ===========================================================================
 *
 * Por que existe: el juego se quedaba sin RAM de programa ($0800-$3DFF, 13.824
 * bytes) y la libreria ocupa 2.152. Lo que el juego realmente usa son unas
 * pocas operaciones triviales sobre el puerto indirecto del core:
 *
 *     $D800 = direccion baja
 *     $D801 = area | bits 10:8 de la direccion
 *     $D802 = dato  (escribirlo dispara la escritura)
 *
 * No hay auto-incremento: cada byte cuesta esos 3 stores. Escribirlo aqui evita
 * la llamada a funcion y, sobre todo, deja que el enlazador no tenga que traer
 * las ~30 rutinas de la libreria con sus ayudantes (pushax, incsp2, ...).
 *
 * IMPORTANTE: todas estas escrituras deben hacerse DENTRO del VBLANK, igual que
 * con las funciones de la libreria.
 * =========================================================================== */

#include <stdint.h>
#include "video.h"

/* --- Escritura cruda al puerto indirecto --- */
static void fv_write(uint8_t area, uint16_t addr, uint8_t data) {
    VID_ADDR_LO = (uint8_t)addr;
    VID_ADDR_HI = (uint8_t)(area | (uint8_t)((addr >> 8) & 0x07));
    VID_DATA    = data;
}

/* ===========================================================================
 * SINCRONIZACION Y REGISTROS DIRECTOS
 * ===========================================================================
 * Mismo protocolo que la libreria, comprobado alli: VIDEO_READY en STATUS al
 * arrancar, VBLANK para entrar en la zona segura y VID_SETUP_ST para esperar al
 * setup de VRAM por hardware.
 */

static void vid_wait_ready(void);
static void vid_wait_setup(void);
static void vid_clear_vram(void);

static void vid_wait_vblank(void) {
    while ((VID_STATUS & VC_STATUS_VBLANK) == 0) { }
}

static void vid_wait_vblank_end(void) {
    while (VID_STATUS & VC_STATUS_VBLANK) { }
}

/* Vuelca el tilemap y los atributos a 0 con el setup por hardware del core:
 * limpia tilemap, atributos, patrones y OAM, y re-expande la fuente $20-$7F.
 * Consecuencia: hay que recargar los patrones DESPUES de llamarla. */
static void vid_clear_vram(void) {
    /* El setup por hardware del core: limpia tilemap, atributos, patrones y
     * OAM y re-expande la fuente $20-$7F (~120-275 us). Se dispara escribiendo
     * $D816 y se espera a que $D817 deje de marcar BUSY. */
    VID_SETUP = 0x01;
    while (VID_SETUP_ST & VC_SETUP_BUSY) { }
}

static void vid_wait_ready(void) {
    while ((VID_STATUS & VC_STATUS_VIDEO_READY) == 0) { }
}

static void vid_wait_setup(void) {
    while (VID_SETUP_ST & VC_SETUP_BUSY) { }
}

/* --- Scroll (16 bits: bits 2:0 = fila de 32, resto = pixel) --- */
static void vid_set_scroll_y(uint16_t y) {
    VID_SCROLL_Y_LO = (uint8_t)y;
    VID_SCROLL_Y_HI = (uint8_t)((y >> 8) & 0x07);
}

static void vid_set_raster(uint8_t line0, uint8_t line1) {
    VID_RASTER_LINE0 = line0;
    VID_RASTER_LINE1 = line1;
}

static void vid_set_band2_scroll(uint16_t x, uint16_t y) {
    VID_BAND2_X_LO = (uint8_t)x;
    VID_BAND2_X_HI = (uint8_t)((x >> 8) & 0x07);
    VID_BAND2_Y_LO = (uint8_t)y;
    VID_BAND2_Y_HI = (uint8_t)((y >> 8) & 0x07);
}

static void vid_set_band3_scroll(uint16_t x, uint16_t y) {
    VID_BAND3_X_LO = (uint8_t)x;
    VID_BAND3_X_HI = (uint8_t)((x >> 8) & 0x07);
    VID_BAND3_Y_LO = (uint8_t)y;
    VID_BAND3_Y_HI = (uint8_t)((y >> 8) & 0x07);
}

/* --- Paletas --- */
static void vid_pal_set(uint8_t entrada, uint16_t rgb444) {
    VID_PAL_PTR = entrada;
    VID_PAL_LO  = (uint8_t)rgb444;
    VID_PAL_HI  = (uint8_t)((rgb444 >> 8) & 0x0F);
}

/* Entrada de fondo = paleta*4 + color; entrada de sprite = 16 + paleta*4+color */
static void vid_pal_set_bg(uint8_t paleta, uint8_t color, uint16_t rgb444) {
    vid_pal_set((uint8_t)((paleta << 2) | (color & 3)), rgb444);
}

static void vid_pal_set_spr(uint8_t paleta, uint8_t color, uint16_t rgb444) {
    vid_pal_set((uint8_t)(VC_PAL_SPR_BASE + (paleta << 2) | (color & 3)), rgb444);
}

/* BG_COLOR es la entrada de paleta 15, color 3 (ver TIPS-Y-TRAMPA §2.5). */
static void vid_set_bgcolor(uint16_t rgb444) {
    vid_pal_set(VC_PAL_BGCOLOR, rgb444);
}

static void fv_oam(uint8_t spr, uint8_t field, uint8_t data);

/* --- OAM completa a Y=deshabilitado --- */
static void fv_clear_oam(void) {
    uint8_t s;
    for (s = 0; s < VC_NUM_SPRITES; s++) fv_oam(s, VC_OAM_Y, VC_SPR_DISABLED);
}

/* --- Celda del tilemap (area distinta de la de atributos) --- */
static void fv_put_cell(uint8_t col, uint8_t row, uint8_t tile) {
    fv_write(VC_AREA_TILEMAP, VC_CELL(col, row), tile);
}

static void fv_put_attr(uint8_t col, uint8_t row, uint8_t attr) {
    fv_write(VC_AREA_ATTR, VC_CELL(col, row), attr);
}

/* --- Atributo por indice de celda ya calculado (fila*64+col) --- */
static void fv_attr_cell(uint16_t cell, uint8_t attr) {
    fv_write(VC_AREA_ATTR, cell, attr);
}

static void fv_cell(uint16_t cell, uint8_t tile) {
    fv_write(VC_AREA_TILEMAP, cell, tile);
}

/* --- Patrones (8 bytes por plano) --- */
static void fv_load_bg_pattern(uint8_t tile, const uint8_t *p0, const uint8_t *p1) {
    uint8_t i;
    for (i = 0; i < 8; i++) {
        fv_write(VC_AREA_PAT_BG0, VC_BG_PAT_ADDR(tile, i), p0[i]);
        fv_write(VC_AREA_PAT_BG1, VC_BG_PAT_ADDR(tile, i), p1[i]);
    }
}

/* spr = indice 0..63; el area de patrones de sprite usa 2bpp, o sea un plano
 * por banco. El indice 0x80 separa los dos bancos dentro del area $C8. */
static void fv_load_spr_pattern(uint8_t spr, const uint8_t *p0, const uint8_t *p1) {
    uint8_t i;
    uint16_t a = VC_SPR_PAT_ADDR(spr, 0);
    for (i = 0; i < 8; i++) {
        fv_write(VC_AREA_PAT_SPR0, (uint16_t)(a + i), p0[i]);
        fv_write(VC_AREA_PAT_SPR1, (uint16_t)(a + i), p1[i]);
    }
}

/* --- Colision AABB con sprites de 8x8 escalados ---
 * hw = ancho en pantalla (16 con SCALE2X, 8 sin el); si se usa el ancho logico
 * el solape llega 8 px antes de que se toquen. */
static uint8_t fv_box_toca(uint16_t ax, uint16_t ay, uint8_t aw, uint8_t ah,
                           uint16_t bx, uint16_t by, uint8_t bw, uint8_t bh) {
    if ((uint16_t)(ax + aw) <= bx) return 0;
    if (ax >= (uint16_t)(bx + bw)) return 0;
    if ((uint16_t)(ay + ah) <= by) return 0;
    if (ay >= (uint16_t)(by + bh)) return 0;
    return 1;
}

/* --- Volcados masivos (versiones reducidas: solo lo que usa el juego) --- */

/* Rellena el tilemap entero (64x32 = 2048) con un tile. */
static void fv_fill(uint8_t tile) {
    uint16_t a;
    VID_ADDR_LO = 0;
    VID_ADDR_HI = VC_AREA_TILEMAP;
    for (a = 0; a < (uint16_t)(VC_MAP_COLS * VC_MAP_ROWS); a++) VID_DATA = tile;
}

/* Atributos a 0: tile transparente = BG_COLOR. */
static void fv_clear_attr(void) {
    uint16_t a;
    VID_ADDR_LO = 0;
    VID_ADDR_HI = VC_AREA_ATTR;
    for (a = 0; a < (uint16_t)(VC_MAP_COLS * VC_MAP_ROWS); a++) VID_DATA = 0;
}

/* --- OAM: spr = indice 0..31 --- */
static void fv_oam(uint8_t spr, uint8_t field, uint8_t data) {
    fv_write(VC_AREA_OAM, VC_OAM_ADDR(spr, field), data);
}


static void fv_oam_tile(uint8_t spr, uint8_t tile) {
    fv_oam(spr, VC_OAM_TILE, tile);
}

/* Posicion + flags de un sprite: X de 9 bits (bit 8 dentro de FLAGS).
 * base_flags NO debe traer VC_SPR_XBIT8: aqui se pone segun x. */
static void fv_sprite_move(uint8_t spr, uint16_t x, uint8_t y, uint8_t base_flags) {
    uint8_t flags = (uint8_t)(base_flags & (uint8_t)~VC_SPR_XBIT8);
    if (x & 0x100) flags |= VC_SPR_XBIT8;

    /* Cada campo REESCRIBE la direccion: el puerto indirecto del core NO
     * auto-incrementa (ver video.h). Con los 5 VID_DATA seguidos solo entraba
     * el primero y el sprite quedaba con la basura del OAM, asi que no se veia
     * ninguno: los campos que se tocan son X, Y y FLAGS; TILE lo pone cada
     * draw_* y COLL se queda como este. */
    VID_ADDR_LO = (uint8_t)VC_OAM_ADDR(spr, VC_OAM_X);
    VID_ADDR_HI = VC_AREA_OAM;
    VID_DATA    = (uint8_t)x;

    VID_ADDR_LO = (uint8_t)VC_OAM_ADDR(spr, VC_OAM_Y);
    VID_DATA    = y;

    VID_ADDR_LO = (uint8_t)VC_OAM_ADDR(spr, VC_OAM_FLAGS);
    VID_DATA    = flags;
}

static void fv_sprite_disable(uint8_t spr) {
    fv_oam(spr, VC_OAM_Y, VC_SPR_DISABLED);
}

#endif /* FASTVIDEO_H */
