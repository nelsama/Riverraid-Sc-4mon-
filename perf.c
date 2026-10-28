/**
 * ============================================================================
 * perf.c - Instrumentacion opcional para medir el coste del frame.
 * ============================================================================
 * Se compila SOLO con `make PERF=1`. Sin ese flag no entra en el binario.
 *
 * Reloj: se usa el timer RTC del core ($C030-$C03F). Escribir $C03C con el
 * bit 1 congela en $C038-$C03B el contador de microsegundos, que se incrementa
 * en el propio "core", asi que NO incluye el tiempo que el emulador pasa fuera
 * del juego. `rom_get_micros()` no sirve aqui: en este emulador no devuelve
 * microsegundos (se comprobo que da valores absurdos).
 *
 * Presupuesto real del hardware: el VBLANK dura ~1430 us (45 de las 525
 * lineas del barrido a 27 MHz, ver video_core.vhd: v_cnt >= 480); el frame
 * completo son 16683 us. Escribir VRAM/OAM fuera del VBLANK puede "partir" la
 * imagen, pero mientras todo el trabajo del frame quepa en el frame lo que se
 * ve no se rompe: el raster puede llevar ya escrito lo que estaba antes.
 *
 * Volcado: cada PERF_N frames escribe por UART tres lineas:
 *   |max,avg,last|   total del frame
 *   |a,b,c|          medias de logica / sprites+HUD / scroll+regeneracion
 *   |max,avg,last|   total (repeticion para no perder datos si se corta)
 * ============================================================================
 */

#include <stdint.h>
#include "romapi.h"
#include "perf.h"

#define PERF_N 60

#define TIMER_US_LATCH_CTRL (*(volatile uint8_t *)0xC03C)
#define TIMER_US0           (*(volatile uint8_t *)0xC038)
#define TIMER_US1           (*(volatile uint8_t *)0xC039)
#define TIMER_US2           (*(volatile uint8_t *)0xC03A)
#define TIMER_US3           (*(volatile uint8_t *)0xC03B)

uint32_t perf_t0;
uint32_t perf_ph[3];
uint32_t perf_last;

static uint32_t perf_max;
static uint32_t perf_sum;
static uint32_t perf_ph_sum[3];
static uint8_t  perf_cnt;

/* Lee los microsegundos del RTC (latch de 32 bits). */
uint32_t perf_micros(void) {
    uint32_t v;

    TIMER_US_LATCH_CTRL = 0x02;            /* latch del contador de us */
    v  = (uint32_t)TIMER_US0;
    v |= (uint32_t)TIMER_US1 << 8;
    v |= (uint32_t)TIMER_US2 << 16;
    v |= (uint32_t)TIMER_US3 << 24;
    return v;
}

/* Imprime un uint32 en decimal por el UART. */
void perf_put_u32(uint32_t v) {
    char buf[11];
    uint8_t i = 0;

    if (v == 0) {
        rom_uart_putc('0');
        return;
    }
    while (v && i < 10) {
        buf[i++] = (char)('0' + (uint8_t)(v % 10u));
        v /= 10u;
    }
    while (i) rom_uart_putc(buf[--i]);
}

void perf_begin(void) {
    perf_t0 = perf_micros();
}

void perf_mark(uint8_t fase) {
    uint32_t now = perf_micros();

    perf_ph[fase] = (uint32_t)(now - perf_t0);
    perf_t0 = now;
    perf_ph_sum[fase] += perf_ph[fase];
}

void perf_end(void) {
    uint32_t dt = (uint32_t)(perf_micros() - perf_t0) + perf_ph[0] +
                  perf_ph[1] + perf_ph[2];

    perf_last = dt;
    perf_sum += dt;
    if (dt > perf_max) perf_max = dt;

    if (++perf_cnt >= PERF_N) {
        rom_uart_putc('|');
        perf_put_u32(perf_max);
        rom_uart_putc(',');
        perf_put_u32((uint32_t)(perf_sum / PERF_N));
        rom_uart_putc(',');
        perf_put_u32(dt);
        rom_uart_puts("|\r\n");
        rom_uart_putc('<');
        perf_put_u32((uint32_t)(perf_ph_sum[0] / PERF_N));
        rom_uart_putc(',');
        perf_put_u32((uint32_t)(perf_ph_sum[1] / PERF_N));
        rom_uart_putc(',');
        perf_put_u32((uint32_t)(perf_ph_sum[2] / PERF_N));
        rom_uart_puts(">\r\n");
        perf_sum = 0;
        perf_max = 0;
        perf_cnt = 0;
        perf_ph_sum[0] = perf_ph_sum[1] = perf_ph_sum[2] = 0;
    }
}
