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
PIER_L, PIER_R = 13, 26
FUNNEL_BLK = 14
W_MIN, W_MAX = 5, 20
CURVE_MAX = 3
ARM_MIN = 5
ISLAND_W = 5


class Gen:
    def __init__(self):
        self.hi, self.lo = 0xA8, 0x14
        mid = (ZONE_L + ZONE_R + 1) // 2
        self.left = mid - 4
        self.right = mid + 3
        self.t_left, self.t_right = self.left, self.right
        self.split_w = 0
        self.split_on = 0
        self.split_target = 0
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

        w = (self.lo & 0x0F) + 4
        w = max(W_MIN, min(W_MAX, w))
        c = (self.hi >> 4) & 0x07
        if c > 6:
            c = 5
        c = (c - 3) if c >= 3 else (3 - c)
        c = min(c, CURVE_MAX)
        if self.hi & 0x40:
            center = mid + c
        else:
            center = mid - c
        l = center - w // 2
        r = l + w - 1
        if l < ZONE_L:
            l = ZONE_L
            r = l + w - 1
        if r > ZONE_R:
            r = ZONE_R
            l = r - w + 1
        self.t_left, self.t_right = l, r
        if (self.lo & 0x80) == 0 and (w - ISLAND_W) >= 2 * ARM_MIN:
            self.split_target = ISLAND_W // 2
            self.split_on = 1
        else:
            self.split_target = 0
            self.split_on = 0

    def toward(self, cur, tgt):
        return cur + 1 if cur < tgt else (cur - 1 if cur > tgt else cur)

    def row(self):
        if self.block_row == 0:
            self.new_block()
        self.block_row = (self.block_row + 1) % GEN_BLOCK

        self.left = self.toward(self.left, self.t_left)
        self.right = self.toward(self.right, self.t_right)
        if self.left + 3 > self.right:
            self.right = self.left + 3
        half = (self.right - self.left + 1) // 2

        goal = self.split_target if self.split_on else 0
        if self.split_w < goal:
            self.split_w += 1
        elif self.split_w > goal:
            self.split_w -= 1
        if self.split_w and (self.right - self.left + 1) < (ISLAND_W + 2 * ARM_MIN):
            self.split_w -= 1

        mid = (ZONE_L + ZONE_R + 1) // 2
        l, r = mid - half, mid + half - 1
        il = ir = None
        if self.split_w:
            il, ir = mid - self.split_w, mid + self.split_w - 1
        return l, r, il, ir, self.bridge, self.funnel, self.block_idx, self.sector


def main():
    filas = int(sys.argv[1]) if len(sys.argv) > 1 else 120
    desde = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    g = Gen()
    for n in range(filas):
        l, r, il, ir, br, fu, bi, sec = g.row()
        if n < desde:
            continue
        isla = "-" if il is None else "%d-%d" % (il, ir)
        marca = "PUENTE" if br else ("embudo" if fu else "")
        print("%4d agua %2d..%2d ancho %2d  isla %-5s  bloque %2d sector %d %s"
              % (n, l, r, r - l + 1, isla, bi, sec, marca))


if __name__ == "__main__":
    main()
