#ifndef FASTSOUND_H
#define FASTSOUND_H

/* ===========================================================================
 * fastsound.h - driver del SID 6581 EN LINEA (antes sound.c)
 * ===========================================================================
 *
 * Es el MISMO codigo que sound.c, movido a un header y marcado static para que
 * el enlazador no tenga que reservar un objeto aparate ni la tabla de imports
 * de funciones publicas. El programa debe caber en $0800-$3DFF (13.824 bytes)
 * y cada byte cuenta: 835 de ellos eran este driver.
 *
 * Modelo: efectos "percutivos" no bloqueantes. Cada efecto dispara su voz con
 * GATE=1 y un temporizador; snd_update() lo decrementa por frame y al llegar a
 * 0 baja el GATE y deja que el release apague la voz.
 *
 * Voces: 1 motor del avion (ruido), 2 disparo/explosion, 3 combustible/open
 * over (su oscilador de ruido $D41B es ademas la fuente de azar).
 * =========================================================================== */

#include <stdint.h>
/* OJO: NO incluir sound.h: declara las funciones como externas y chocaria con
 * las definiciones static de abajo. */

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
#define GAMEOVER_FRAMES 60  /* melodina de game over       */

/* PING de repostaje: "golpe en un vaso". Ver snd_fuel_ping().
 *
 *   TINK_GATE  = frames con el GATE alto (lo que dura el impulso audible).
 *   TINK_SPACE = frames MINIMOS entre dos pings. Es mayor que TINK_GATE para
 *                que el sonido se apague del todo antes de empezar el
 *                siguiente: si se solapasen sonaria un tono continuo, que es
 *                justo lo que hay que evitar.
 *
 *  (3 + 2) frames a 60 Hz = ~83 ms por tink: el punto medio. Con 5 frames
 *  el golpe era mas largo pero cabian menos en una pasada por el bidon; con 2
 *  solo pasaba el transitorio y sonaba a crujido. 3 deja oir el TONO y caben
 *  bastantes por sobrevuelo. */
#define TINK_GATE   3
#define TINK_SPACE  5
#define TINK_RATE   ((uint8_t)((TINK_SPACE > TINK_GATE) ? (TINK_SPACE - TINK_GATE) : 0))

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
static uint8_t  t_v3;           /* tink de repostaje / game over */
static uint8_t  tink_space;     /* frames que faltan para el siguiente tink */
static uint8_t  engine_on;      /* 1 = motor sonando (voz 1) */
static uint16_t engine_freq;    /* frecuencia actual del motor */
static uint16_t shoot_freq;     /* frecuencia actual del sweep del disparo */
static uint8_t  v2_is_explode;  /* 1 = la voz 2 suena explosion (no disparo) */
static uint8_t  v3_is_gameover; /* 1 = la voz 3 suena game over */

/* Frecuencia del ruido del motor para la velocidad MINIMA y MAXIMA del juego
 * (las SCROLL_MIN/SCROLL_MAX de game.c). La diferencia es la que se oye como
 * acelerar. Se pasan como 0..15 normalizado para no depender de esas macros. */
#define SND_ENGINE_SLOW  0x0400
#define SND_ENGINE_FAST  0x0E00
#define SND_SPEED_MIN    4
#define SND_SPEED_MAX    16

/* ===========================================================================
 * API
 * =========================================================================== */

static void snd_init(void) {
    sid_wr(SID_V1 + 4, 0);
    sid_wr(SID_V2 + 4, 0);
    sid_wr(SID_V3 + 4, 0);
    sid_wr(SID_V1 + 5, 0); sid_wr(SID_V1 + 6, 0);
    sid_wr(SID_V2 + 5, 0); sid_wr(SID_V2 + 6, 0);
    sid_wr(SID_V3 + 5, 0); sid_wr(SID_V3 + 6, 0);
    SID_MODE_VOL = 0x0F;       /* filtro off + volumen master maximo */
    t_v2 = t_v3 = 0;
    tink_space = 0;
    shoot_freq = 0;
    engine_on = 0;
    engine_freq = 0;
    v2_is_explode = 0;
    v3_is_gameover = 0;
}

/* (snd_silence y snd_random se eliminaron: no las llamaba nadie y el programa
 * va justo de RAM; el enlazador ya avisaba de que no se usaban.) */

/* Motor del avion: ruido grave continuo en la voz 1.
 *
 * VOLUMEN: el SID tiene un unico registro de volumen global ($D418), asi que
 * bajar solo el motor es imposible... salvo por la ENVOLVENTE: con
 * sustain < 15 la voz suena mas floja sin tocar el volumen maestro de los
 * efectos. Se usa sustain 5 (de 15) y un decay largo, que da un zumbido de
 * fondo en vez de un rugido que tapa el disparo y las explosiones. */
static void snd_engine_on(void) {
    sid_wr(SID_V1 + 5, 0x09);      /* ataque 0, decay 9 (baja despacio) */
    sid_wr(SID_V1 + 6, 0x57);      /* sustain 5/15, release 7           */
    sid_freq(SID_V1, SND_ENGINE_SLOW);   /* velocidad normal de aqui */
    sid_wr(SID_V1 + 4, WF_NOISE | GATE);
    engine_on = 1;
    engine_freq = SND_ENGINE_SLOW;
}

/* Ajusta el rugido del motor a la VELOCIDAD del avión.
 *
 * El motor es ruido en la voz 1: el ruido no tiene "tono", pero el filtro de
 * salida del SID y el propio muestreo del LFSR de ruido hacen que una
 * frecuencia MAS ALTA suene mas AGUDA y mas "soplada", y una mas BAJA mas
 * grave y arrastrada. Es exactamente la sensacion que se busca: acelerar se
 * oye como un avion que va mas rapido.
 *
 * `speed` es scroll_speed (SCROLL_MIN..SCROLL_MAX). El registro se reescribe
 * solo cuando la frecuencia cambia, porque escribir en el SID fuera del VBLANK
 * No es gratis. */
static void snd_engine_speed(uint8_t speed) {
    uint16_t f;

    if (!engine_on) return;

    /* Mapeo lineal: SCROLL_MIN -> SND_ENGINE_SLOW, SCROLL_MAX -> SND_ENGINE_FAST */
    if (speed <= SND_SPEED_MIN) {
        f = SND_ENGINE_SLOW;
    } else if (speed >= SND_SPEED_MAX) {
        f = SND_ENGINE_FAST;
    } else {
        f = (uint16_t)(SND_ENGINE_SLOW +
                       (uint16_t)(speed - SND_SPEED_MIN) *
                       (uint16_t)((SND_ENGINE_FAST - SND_ENGINE_SLOW) /
                                  (SND_SPEED_MAX - SND_SPEED_MIN)));
    }
    if (f != engine_freq) {
        engine_freq = f;
        sid_freq(SID_V1, f);
    }
}

static void snd_engine_off(void) {
    sid_wr(SID_V1 + 4, 0);
    engine_on = 0;
}

/* Disparo: blip agudo con sweep descendente (voz 2). */
static void snd_shoot(void) {
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
static void snd_explode(void) {
    sid_wr(SID_V2 + 5, 0x08);
    sid_wr(SID_V2 + 6, 0x00);
    sid_freq(SID_V2, 0x1800);
    sid_wr(SID_V2 + 4, WF_NOISE | GATE);
    v2_is_explode = 1;
    t_v2 = EXPLODE_FRAMES;
}

/* PING de repostaje: golpe seco en un vaso (voz 3).
 *
 * Se llama UNA VEZ POR PULSO de recarga, no por frame: cuantos mas frames de
 * contacto, mas golpes seguidos se oyen.
 *
 * ENVOLVENTE "tink": ataque 0 (sube al instante) y decay rapido, y el gate
 * se baja a los TINK_GATE frames. Ataque 0 + impulso corto da el golpe seco;
 * el decay NO es 0 porque con caida instantanea la voz se queda sin nivel
 * audible y lo que se oye es el transitorio del gate, que suena a CRUJIDO.
 * Con decay 1 la caida es rapidisima pero sigue siendo un tono definido.
 *
 * VOLUMEN: el SID solo tiene un volumen GLOBAL ($D418), asi que el volumen de
 * cada voz se regula con la envolvente. Antes el tink tenia sustain 0: el
 * ataque lo ponia a tope y el decay lo desplomaba en el mismo frame, asi que
 * se oia muy flojo (solo quedaba el pico del ataque). Con sustain 8/15 el
 * tono se mantiene audible durante todo el impulso y suena claramente mas
 * fuerte, sin tocar el volumen de los demas efectos.
 *
 * TONO: 0x7E00 es casi el techo del rango de frecuencia del SID (16 bits). Se
 * busca el tic agudo de un cristal: cuanto mas alto, mas "tink" y menos
 * "bip". Si SUENA demasiado fino, bajar a 0x7000 o 0x6800.
 *
 * La onda es TRIANGULAR porque en el SID es la mas limpia de las tres: sin
 * armonicos impares duros del pulso y sin el ruido del noise. Da un tink
 * brillante, no un chasquido.
 *
 * Si el tink anterior AUN no ha acabado (TINK_RATE frames de espera) se
 * ignora: asi los golpes no se solapan y cada uno se oye separado. */
static void snd_fuel_ping(void) {
    if (v3_is_gameover) return;          /* no pisar la melodia de game over */
    if (t_v3) return;                    /* el anterior aun suena: esperar */

    sid_wr(SID_V3 + 5, 0x18);            /* ataque 1, decay 8 (no se apaga)  */
    sid_wr(SID_V3 + 6, 0x80);            /* sustain 8/15 (volumen audible)   */
    sid_freq(SID_V3, 0x8F00);            /* casi el techo: tic de cristal    */
    sid_wr(SID_V3 + 4, WF_TRI | GATE);   /* dispara el tink                  */
    t_v3 = TINK_GATE;                    /* gate alto TINK_GATE frames       */
    tink_space = TINK_RATE;              /* y espera antes del siguiente     */
}

/* Game over: tono triangular descendente largo (voz 3). */
static void snd_gameover(void) {
    sid_wr(SID_V3 + 5, 0x09);
    sid_wr(SID_V3 + 6, 0x00);
    sid_freq(SID_V3, 0x4000);
    sid_wr(SID_V3 + 4, WF_TRI | GATE);
    v3_is_gameover = 1;
    t_v3 = GAMEOVER_FRAMES;
}

/* Tick por frame. */
static void snd_update(void) {
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

    /* Voz 3: un TINK de repostaje, o la melodia de game over. */
    if (t_v3) {
        t_v3--;
        if (t_v3 == 0) {
            if (v3_is_gameover) {
                sid_wr(SID_V3 + 4, 0);
                v3_is_gameover = 0;
            } else {
                /* Fin del impulso: bajar el GATE. Con sustain 8 el sonido NO se
                 * apaga solo, asi que hace falta un RELEASE corto (sustain 8,
                 * release 4 = 3 frames) para que caiga limpio y no de un
                 * click. El release sigue corriendo aunque el gate ya este a
                 * 0: es lo que deja la cola corta del golpe. */
                sid_wr(SID_V3 + 6, 0x84);   /* sustain 8, release 4 */
                sid_wr(SID_V3 + 4, WF_TRI);
            }
        } else if (v3_is_gameover) {
            sid_freq(SID_V3, (uint16_t)(0x4000 - (GAMEOVER_FRAMES - t_v3) * 0x0100));
        }
    }

    /* Separacion entre tinks: mientras no llegue a 0 no empieza el siguiente. */
    if (tink_space) tink_space--;
}

#endif /* FASTSOUND_H */
