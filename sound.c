/**
 * ============================================================================
 * sound.c - Driver de sonido para el SID 6581 (compatible C64)
 * ============================================================================
 * Acceso directo a los registros del SID en $D400-$D41F.
 *
 * FORMATO DE REGISTROS (por voz, base = $D400 + 7*voz):
 *   FREQ_LO  = base+0   frecuencia, bits 7:0
 *   FREQ_HI  = base+1   frecuencia, bits 15:8
 *   PW_LO    = base+2   ancho de pulso, bits 7:0
 *   PW_HI    = base+3   ancho de pulso, bits 11:8
 *   CTRL     = base+4   bit0 gate | bit1 sync | bit2 ring | bit3 test |
 *                       bits7:4 waveform (pulse/tri/saw/noise)
 *   AD       = base+5   ataque (nibble alto) / decay (nibble bajo)
 *   SR       = base+6   sustain (nibble alto) / release (nibble bajo)
 *
 *   $D418 MODE_VOL  bits3:0 = volumen master (0-15)
 *
 * MODELO DE SONIDO: los efectos son "percutivos" (ataque rapido + decay), asi
 * que en cada frame comprobamos si la nota sigue viva con un temporizador y, al
 * agotarse, bajamos el GATE (dejando que el release la apague). El motor es un
 * ruido grave continuo que se enciende/apaga sin bloqueo.
 * ============================================================================
 */

#include <stdint.h>
#include "sound.h"

/* Registros del SID (escritura directa). */
#define SID_V1      0xD400
#define SID_V2      0xD407
#define SID_V3      0xD40E
#define SID_MODE_VOL (*(volatile uint8_t *)0xD418)

/* Registro de LECTURA del oscilador de la voz 3 ($D41B). Con la voz 3 en modo
 * RUIDO es el generador pseudoaleatorio del SID. */
#define SID_V3_OSC  (*(volatile uint8_t *)0xD41B)

/* Formas de onda (bits 7:4 de CTRL). */
#define WF_TRI      0x10
#define WF_SAW      0x20
#define WF_PULSE    0x40
#define WF_NOISE    0x80
#define GATE        0x01

/* Duraciones (en frames) de los efectos con temporizador. */
#define SHOOT_FRAMES    6   /* disparo (sweep descendente) */
#define EXPLODE_FRAMES 20   /* explosion / choque          */
#define FUEL_FRAMES    10   /* recoger combustible         */
#define GAMEOVER_FRAMES 60  /* melodina de game over       */

/* ===========================================================================
 * ESCRITURA DE REGISTROS
 * =========================================================================== */

static void sid_wr(uint16_t addr, uint8_t v) {
    *(volatile uint8_t *)addr = v;
}

static void sid_freq(uint16_t base, uint16_t f) {
    sid_wr(base + 0, (uint8_t)(f & 0xFF));
    sid_wr(base + 1, (uint8_t)(f >> 8));
}

static void sid_pw(uint16_t base, uint16_t pw) {
    sid_wr(base + 2, (uint8_t)(pw & 0xFF));
    sid_wr(base + 3, (uint8_t)(pw >> 8));
}

/* ===========================================================================
 * ESTADO
 * =========================================================================== */
static uint8_t  t_v2;           /* disparo / explosion */
static uint8_t  t_v3;           /* combustible / game over */
static uint8_t  engine_on;      /* 1 = motor sonando (voz 1) */
static uint16_t shoot_freq;     /* frecuencia actual del sweep del disparo */
static uint8_t  v2_is_explode;  /* 1 = la voz 2 suena explosion (no disparo) */
static uint8_t  v3_is_gameover; /* 1 = la voz 3 suena game over */

/* ===========================================================================
 * API
 * =========================================================================== */

void snd_init(void) {
    sid_wr(SID_V1 + 4, 0);
    sid_wr(SID_V2 + 4, 0);
    sid_wr(SID_V3 + 4, 0);
    sid_wr(SID_V1 + 5, 0); sid_wr(SID_V1 + 6, 0);
    sid_wr(SID_V2 + 5, 0); sid_wr(SID_V2 + 6, 0);
    sid_wr(SID_V3 + 5, 0); sid_wr(SID_V3 + 6, 0);
    SID_MODE_VOL = 0x0F;       /* filtro off + volumen master maximo */
    t_v2 = t_v3 = 0;
    shoot_freq = 0;
    engine_on = 0;
    v2_is_explode = 0;
    v3_is_gameover = 0;
}

void snd_silence(void) {
    sid_wr(SID_V1 + 4, 0);
    sid_wr(SID_V2 + 4, 0);
    sid_wr(SID_V3 + 4, 0);
    t_v2 = t_v3 = 0;
    engine_on = 0;
}

/* Motor del avion: ruido grave continuo en la voz 1. */
void snd_engine_on(void) {
    sid_wr(SID_V1 + 5, 0x08);      /* ataque 0, decay 8 */
    sid_wr(SID_V1 + 6, 0xF0);      /* sustain 15, release 0 */
    sid_freq(SID_V1, 0x0800);      /* ruido grave */
    sid_wr(SID_V1 + 4, WF_NOISE | GATE);
    engine_on = 1;
}

void snd_engine_off(void) {
    sid_wr(SID_V1 + 4, 0);
    engine_on = 0;
}

/* Disparo: blip agudo con sweep descendente (voz 2). */
void snd_shoot(void) {
    if (v2_is_explode && t_v2) return;   /* no pisar la explosion */

    sid_wr(SID_V2 + 5, 0x04);
    sid_wr(SID_V2 + 6, 0x00);
    sid_pw(SID_V2, 0x0800);
    sid_freq(SID_V2, 0x7000);
    sid_wr(SID_V2 + 4, WF_PULSE | GATE);

    shoot_freq = 0x7000;
    v2_is_explode = 0;
    t_v2 = SHOOT_FRAMES;
}

/* Explosion / choque: ruido grave largo (voz 2). */
void snd_explode(void) {
    sid_wr(SID_V2 + 5, 0x08);
    sid_wr(SID_V2 + 6, 0x00);
    sid_freq(SID_V2, 0x1800);
    sid_wr(SID_V2 + 4, WF_NOISE | GATE);
    v2_is_explode = 1;
    t_v2 = EXPLODE_FRAMES;
}

/* Recoger combustible: blip triangular corto (voz 3). */
void snd_fuel(void) {
    if (v3_is_gameover && t_v3) return;
    sid_wr(SID_V3 + 5, 0x09);
    sid_wr(SID_V3 + 6, 0x00);
    sid_freq(SID_V3, 0x5800);
    sid_wr(SID_V3 + 4, WF_TRI | GATE);
    v3_is_gameover = 0;
    t_v3 = FUEL_FRAMES;
}

/* Game over: tono triangular descendente largo (voz 3). */
void snd_gameover(void) {
    sid_wr(SID_V3 + 5, 0x09);
    sid_wr(SID_V3 + 6, 0x00);
    sid_freq(SID_V3, 0x4000);
    sid_wr(SID_V3 + 4, WF_TRI | GATE);
    v3_is_gameover = 1;
    t_v3 = GAMEOVER_FRAMES;
}

/* Byte pseudoaleatorio a partir del oscilador de ruido de la voz 3 ($D41B).
 * Se pone la voz 3 en modo RUIDO SIN gate (no suena) y se lee su registro. */
uint8_t snd_random(void) {
    uint8_t out;

    sid_freq(SID_V3, 0xFFFF);
    sid_wr(SID_V3 + 4, WF_NOISE);
    out = SID_V3_OSC;
    sid_wr(SID_V3 + 4, 0);
    return out;
}

/* Tick por frame. */
void snd_update(void) {
    (void)engine_on;

    /* Voz 2: disparo (sweep) o explosion. */
    if (t_v2) {
        t_v2--;
        if (t_v2 == 0) {
            sid_wr(SID_V2 + 4, 0);
            v2_is_explode = 0;
        } else if (!v2_is_explode) {
            if (shoot_freq > 0x0A00) shoot_freq = (uint16_t)(shoot_freq - 0x0700);
            sid_freq(SID_V2, shoot_freq);
        }
    }

    /* Voz 3: combustible (blip) o game over. */
    if (t_v3) {
        t_v3--;
        if (t_v3 == 0) {
            sid_wr(SID_V3 + 4, 0);
            v3_is_gameover = 0;
        } else if (v3_is_gameover) {
            sid_freq(SID_V3, (uint16_t)(0x4000 - (GAMEOVER_FRAMES - t_v3) * 0x0100));
        }
    }
}
