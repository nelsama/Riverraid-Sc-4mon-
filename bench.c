/**
 * bench.c - Banco de pruebas de coste de las operaciones del Core de Video.
 *
 *   D:/cc65/bin/cl65.exe -c -t none -O --cpu 6502 -I include -DPERF -o build/bench.o bench.c
 *   D:/cc65/bin/cl65.exe -c -t none -O --cpu 6502 -I include -DPERF -o build/perf.o perf.c
 *   D:/cc65/bin/ld65.exe -C config/programa.cfg -o output/bench.bin build/bench.o build/perf.o build/startup.o lib/vc.lib D:/cc65/lib/none.lib
 *
 * Mide con el RTC el coste de lo que mas pesa en el bucle del juego, para saber
 * que se puede permitir el CPU: VBLANK = 1430 us (45 de 525 lineas a 27 MHz),
 * frame = 16683 us.
 */
#include <stdint.h>
#include "video.h"
#include "romapi.h"
#include "perf.h"

static void put_u32(uint32_t v) {
    char b[11];
    uint8_t i = 0;
    if (v == 0) { rom_uart_putc('0'); return; }
    while (v && i < 10) { b[i++] = (char)('0' + (uint8_t)(v % 10u)); v /= 10u; }
    while (i) rom_uart_putc(b[--i]);
}

static void report(const char *name, uint32_t us, uint16_t n) {
    rom_uart_puts(name);
    rom_uart_putc(' ');
    put_u32(us);
    rom_uart_puts(" us / ");
    put_u32(n);
    rom_uart_puts(" = ");
    put_u32(us * 100u / n);
    rom_uart_puts(" centi-us/op\n");
}

int main(void) {
    uint32_t t0, dt;
    uint16_t i;
    uint16_t cell;

    vc_wait_ready();
    vc_wait_vblank();
    vc_clear_vram();

    /* vc_sprite_move: es lo que se hace 15 veces por frame con el HUD. */
    t0 = perf_micros();
    for (i = 0; i < 500; i++) vc_sprite_move(0, (uint16_t)(i & 0x0F), 100, 0);
    dt = perf_micros() - t0;
    report("sprite_move", dt, 500);

    /* vc_oam_put: una escritura OAM. */
    t0 = perf_micros();
    for (i = 0; i < 500; i++) vc_oam_put(0, VC_OAM_TILE, 5);
    dt = perf_micros() - t0;
    report("oam_put", dt, 500);

    /* vc_put_cell + vc_put_attr: como estaba antes la regeneracion. */
    t0 = perf_micros();
    for (i = 0; i < 250; i++) {
        vc_put_cell((uint8_t)(i & 63), 5, 3);
        vc_put_attr((uint8_t)(i & 63), 5, 1);
    }
    dt = perf_micros() - t0;
    report("cell+attr(par)", dt, 250);

    /* Escritura directa al puerto, avanzando la direccion. */
    t0 = perf_micros();
    cell = VC_CELL(0, 5);
    for (i = 0; i < 250; i++, cell++) {
        VID_ADDR_LO = (uint8_t)cell;
        VID_ADDR_HI = (uint8_t)((cell >> 8) & 0x07);
        VID_DATA = 3;
        VID_ADDR_LO = (uint8_t)cell;
        VID_ADDR_HI = (uint8_t)(VC_AREA_ATTR | ((cell >> 8) & 0x07));
        VID_DATA = 1;
    }
    dt = perf_micros() - t0;
    report("directo(par)", dt, 250);

    /* vc_wait_vblank_end: cuanto cuesta el trabajo del frame con el vblank. */
    t0 = perf_micros();
    for (i = 0; i < 20; i++) {
        vc_wait_vblank();
        vc_wait_vblank_end();
    }
    dt = perf_micros() - t0;
    report("frame_vacio", dt, 20);

    rom_uart_puts("FIN\n");
    for (;;) {}
}
