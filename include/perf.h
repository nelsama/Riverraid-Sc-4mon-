#ifndef PERF_H
#define PERF_H

#include <stdint.h>

/* Medidor de coste de frame (solo con `make PERF=1`). Con PERF apagado, las
 * llamadas desaparecen y no entra codigo en el binario.
 *
 *   perf_begin()        inicio del frame (justo tras vc_wait_vblank)
 *   perf_mark(0..3)     cierran las fases de arriba (ver perf.c)
 *   perf_end()          final del frame (antes de vc_wait_vblank_end)
 *
 * Fases que se acumulan por separado:
 *   0 = logica (balas, colisiones, enemigos, combustible)
 *   1 = dibujo de sprites + HUD
 *   2 = scroll + regeneracion de fila + animacion del agua
 */
#ifdef PERF
extern uint32_t perf_t0;
extern uint32_t perf_ph[3];
extern uint32_t perf_last;

uint32_t perf_micros(void);
void perf_begin(void);
void perf_mark(uint8_t fase);
void perf_end(void);
void perf_put_u32(uint32_t v);
#else
#define perf_begin()      ((void)0)
#define perf_mark(f)      ((void)0)
#define perf_end()        ((void)0)
#endif

#endif
