#!/usr/bin/env python3
"""simular_cauce.py - Reproduce en Python el generador de `game.c` y saca el
perfil que DEBERIA verse, fila a fila.

  Uso: python tools/simular_cauce.py [filas] [desde]

Sirve para depurar el generador sin emulador: si lo que sale aqui no coincide
con lo que se mide en /screen.png, el fallo esta en el pintado o en el scroll;
si coincide, el generador es el que esta mal.

Mantener en sincronia con gen_new_block()/generate_world_row() de game.c.
"""
import sys

GEN_BLOCK = 16
GEN_SECTOR = 16
ZONE_L, ZONE_R = 7, 33
PIER_L, PIER_R = 12, 27
FUNNEL_BLK = 14
W_MIN, W_HARD_MIN = 7, 5
W_MAX = 20
WIDTH_TAB = [6, 7, 8, 9, 10, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20]
CURVE_MAX = 3
ARM_EASY, ARM_MIN_HARD = 7, 5
ISLAND_W = 9
SPLIT_W = ISLAND_W + 2 * ARM_EASY          # 23
RAMP_SECTORS = 3
ISLE_MIN = 5


class Gen:
    def __init__(self):
        self.hi, self.lo = 0xA8, 0x14
        mid = (ZONE_L + ZONE_R + 1) // 2
        self.left, self.right = ZONE_L + 1, ZONE_R - 1
        self.t_left, self.t_right = self.left, self.right
        self.split_w = 0
        self.split_on = 0
        self.split_target = 0
        self.w_tras_isla = 0
        self.block_row = 0
        self.block_idx = 1
        self.sector = 1
        self.funnel = 0
        self.bridge = 0
        self.lfsr_step()          # consume el bloque 0
        self.new_block()

    def lfsr_step(self):
        carry = self.hi & 1
        self.hi >>= 1
        if carry:
            self.hi ^= 0xB4
        self.lo = ((self.lo >> 1) | (carry << 7)) & 0xFF

    # --- progresion: 7->6->5 celdas de minimo y brazos 7->6->5 ---
    def step(self):
        s = self.sector - 1
        return s if s < RAMP_SECTORS else RAMP_SECTORS

    def w_min_now(self):
        return W_MIN - min(self.step(), 2)

    def arm_now(self):
        return ARM_EASY - min(self.step(), 2)

    def new_block(self):
        self.lfsr_step()
        self.block_idx += 1
        self.funnel = self.bridge = 0
        mid = (ZONE_L + ZONE_R + 1) // 2

        if self.block_idx >= GEN_SECTOR:
            self.bridge = 1
            self.sector += 1
            self.block_idx = 0
            self.t_left, self.t_right = PIER_L, PIER_R
            self.split_on = self.split_target = 0
            return
        if self.block_idx >= FUNNEL_BLK:
            self.funnel = 1
            self.t_left, self.t_right = PIER_L, PIER_R
            self.split_target = 0
            self.split_on = 1 if self.split_w else 0
            return

        w = max(WIDTH_TAB[self.lo & 0x0F], self.w_min_now())
        # 1 de cada 4 bloques: bit 7 a cero Y bloque par (ver game.c)
        split = (self.lo & 0x80) == 0 and (self.block_idx & 1)

        c = (self.hi >> 4) & 0x07
        if c > 6:
            c = 5
        c = (c - 3) if c >= 3 else (3 - c)
        c = min(c, CURVE_MAX)
        center = mid + c if self.hi & 0x40 else mid - c
        l = center - w // 2
        r = l + w - 1
        if l < ZONE_L:
            l = ZONE_L
            r = l + w - 1
        if r > ZONE_R:
            r = ZONE_R
            l = r - w + 1
        self.t_left, self.t_right = l, r
        if split:
            self.split_target = ISLAND_W
            self.split_on = 1
        else:
            self.split_target = 0
            self.split_on = 0

    def toward(self, cur, tgt):
        return cur + 1 if cur < tgt else (cur - 1 if cur > tgt else cur)

    def row(self):
        if self.block_row == 0:
            self.new_block()
        es_viga = 1 if (self.bridge and self.block_row < 3) else 0
        self.block_row = (self.block_row + 1) % GEN_BLOCK

        self.left = self.toward(self.left, self.t_left)
        self.right = self.toward(self.right, self.t_right)
        if self.left + 3 > self.right:
            self.right = self.left + 3

        if self.split_w >= ISLE_MIN:
            self.w_tras_isla = self.right - self.left + 1

        # tierra central: crece/encoge 1 celda por fila
        goal = self.split_target if self.split_on else 0
        if self.split_w < goal:
            self.split_w += 1
        elif self.split_w > goal:
            self.split_w -= 1

        mid = (ZONE_L + ZONE_R + 1) // 2
        w = self.right - self.left + 1
        if self.split_w >= ISLE_MIN:
            w = self.split_w + 2 * self.arm_now() + 2
        elif w > W_MAX:
            w = W_MAX
        elif self.w_tras_isla and w < self.w_tras_isla:
            w = self.w_tras_isla
        else:
                w = max(w, self.split_w + 2 * self.arm_now() + 2)
        if (not self.split_w) and self.w_tras_isla and w >= self.w_tras_isla:
            self.w_tras_isla = 0

        l = mid - w // 2
        r = l + w - 1
        if w < self.w_min_now():
            w = self.w_min_now()
            l = mid - w // 2
            r = l + w - 1
        self.left, self.right = l, r

        il = ir = None
        if self.split_w >= ISLE_MIN:
            ww = min(self.split_w, ISLAND_W)
            il = l + ((r - l + 1) - ww) // 2
            ir = il + ww - 1
        return l, r, il, ir, es_viga, self.funnel, self.block_idx, self.sector


def main():
    filas = int(sys.argv[1]) if len(sys.argv) > 1 else 120
    desde = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    g = Gen()
    for n in range(filas):
        l, r, il, ir, viga, fu, bi, sec = g.row()
        if n < desde:
            continue
        isla = "-" if il is None else "%d-%d" % (il, ir)
        brazo = "-" if il is None else "%d/%d" % (il - l, r - ir)
        marca = "PUENTE" if viga else ("embudo" if fu else "")
        print("%4d agua %2d..%2d ancho %2d  isla %-5s brazos %-5s sector %d %s"
              % (n, l, r, r - l + 1, isla, brazo, sec, marca))


if __name__ == "__main__":
    main()
