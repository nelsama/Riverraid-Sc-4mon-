/**
 * ============================================================================
 * sound.h - Driver de sonido para el SID 6581 (compatible C64)
 * ============================================================================
 * El SID esta mapeado en $D400-$D41F, igual que en el Commodore 64.
 *
 * Mapa de registros por voz (offset 0, 7, 14):
 *   +0 FREQ_LO      +1 FREQ_HI      +2 PW_LO    +3 PW_HI
 *   +4 CTRL (gate/sync/ring/test/waveform)
 *   +5 ATTACK/DECAY (+6 SUSTAIN/RELEASE)
 * $D418 = MODE/VOL (bits 3:0 = volumen master)
 *
 * Voces asignadas para RIVER RAID:
 *   Voz 1: motmotor del avion (ruido grave continuo).
 *   Voz 2: disparo del jugador (sweep descendente) y explosion (ruido).
 *   Voz 3: recoger combustible (blip) y game over. Ademas, su oscilador de
 *          ruido ($D41B) se usa como generador de numeros aleatorios.
 * ============================================================================
 */

#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

/* Inicializa el SID: silencia las 3 voces, volumen master al maximo. */
void snd_init(void);

/* Efectos. Todos son no bloqueantes: disparan la voz y suenan solos. */
void snd_shoot(void);      /* disparo del jugador (sweep)                */
void snd_explode(void);    /* explosion / choque (ruido)                  */
void snd_fuel(void);       /* recoger deposito de combustible (blip)      */
void snd_gameover(void);   /* melodina breve de game over                 */

/* Motor del avion: ruido grave continuo en la voz 1. Se mantiene con
 * snd_update(); se apaga con snd_engine_off(). */
void snd_engine_on(void);
void snd_engine_off(void);

/* Apaga todas las voces (al salir o cambiar de estado). */
void snd_silence(void);

/* Devuelve un byte pseudoaleatorio del SID (oscilador de ruido de la voz 3). */
uint8_t snd_random(void);

/* Tick por frame: gestiona las envolventes de los efectos y el motor. */
void snd_update(void);

#endif /* SOUND_H */
