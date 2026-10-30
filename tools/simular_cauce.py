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
W_MIN, W_HARD_MIN = 9, 7
W_MAX = 22
WIDTH_TAB = [9, 10, 11, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22]
CURVE_MAX = 3
ARM_EASY, ARM_MIN_HARD = 8, 6
ISLAND_W = 9
SPLIT_W = ISLAND_W + 2 * ARM_EASY          # 23
RAMP_SECTORS = 3
ISLE_MIN = 5


class Gen:
    def __init__(self):
        self.hi, self.lo = 0xA8, 0x14
        mid = (ZONE_L + ZONE_R + 1) // 2
        self.left, self.right = ZONE_L, ZONE_R
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
            # Si el bloque lleva isla, el cauce se abre a SPLIT_W para que la
            # tierra quepa desde la primera fila (ver game.c).
            w = SPLIT_W
            l = center - w // 2
            r = l + w - 1
            if l < ZONE_L:
                l = ZONE_L
                r = l + w - 1
            if r > ZONE_R:
                r = ZONE_R
                l = r - w + 1
            self.t_left, self.t_right = l, r
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

        # tierra central: crece/encoge 1 celda por fila (ANTES de los bordes)
        goal = self.split_target if self.split_on else 0
        ancho_act = self.right - self.left + 1
        if self.split_w < goal:
            if ancho_act >= self.split_w + 1 + 2 * self.arm_now() + 2:
                self.split_w += 1
        elif self.split_w > goal:
            self.split_w -= 1

        if self.split_w >= ISLE_MIN:
            self.w_tras_isla = self.right - self.left + 1

        mid = (ZONE_L + ZONE_R + 1) // 2
        l, r = self.left, self.right
        w = r - l + 1
        suelo = self.w_min_now()
        if self.split_w >= ISLE_MIN:
            suelo = max(suelo, self.split_w + 2 * self.arm_now() + 2)

        if w < suelo:
            w = w + min(suelo - w, 2)
            l = mid - w // 2
            r = l + w - 1
        elif w > W_MAX:
            w = w - min(w - W_MAX, 2)
            l = mid - w // 2
            r = l + w - 1
        elif self.w_tras_isla and w < self.w_tras_isla and self.split_w < ISLE_MIN:
            w = min(w + 2, self.w_tras_isla)
            l = mid - w // 2
            r = l + w - 1

        if (not self.split_w) and self.w_tras_isla and w >= self.w_tras_isla:
            self.w_tras_isla = 0

        if l < ZONE_L:
            l = ZONE_L
            r = l + w - 1
        if r > ZONE_R:
            r = ZONE_R
            l = r - w + 1
        self.left, self.right = l, r

        il = ir = None
        if self.split_w >= ISLE_MIN:
            ww = min(self.split_w, ISLAND_W)
            ancho = r - l + 1
            if ancho >= ww + 4:
                il = l + (ancho - ww) // 2
                ir = il + ww - 1
        return l, r, il, ir, es_viga, self.funnel, self.block_idx, self.sector


def main():
    filas = int(sys.argv[1]) if len(sys.argv) > 1 else 120
    desde = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    g = Gen()
    prev = None
    saltos = 0
    brazos_malos = 0
    for n in range(filas):
        l, r, il, ir, viga, fu, bi, sec = g.row()

        # CONTINUIDAD: un sprite de 16 px ocupa 2 celdas y 2-3 filas de cinta,
        # asi que cada fila tiene que compartir al menos dos celdas de agua
        # con la anterior. Si no, el avion muere contra una pared invisible.
        if prev is not None and not viga:
            pl, pr, pil, pir, pviga = prev
            def agua(l, r, il, ir, c):
                if c <= l or c >= r:
                    return False
                if il is not None and il <= c <= ir:
                    return False
                return True
            comunes = [c for c in range(ZONE_L, ZONE_R + 1)
                       if agua(l, r, il, ir, c) and agua(pl, pr, pil, pir, c)]
            # hay que caber 2 celdas contiguas
            cabe = any(c + 1 in comunes for c in comunes)
            if not cabe:
                print("!! SALTO en fila %d: %d..%d isla %s  <- antes %d..%d isla %s"
                      % (n, l, r, (il, ir), pl, pr, (pil, pir)))
                saltos += 1

        # BRAZOS: si hay isla y no es embudo/puente, ambos brazos navegables
        if il is not None and not fu and not viga:
            izq = il - l - 2      # celdas de agua entre costa izq y costa isla
            der = r - ir - 2
            if izq < 1 or der < 1:
                print("!! BRAZO fila %d agua %d..%d isla %d-%d brazos %d/%d"
                      % (n, l, r, il, ir, izq, der))
                brazos_malos += 1

        prev = (l, r, il, ir, viga)
        if n < desde:
            continue
        isla = "-" if il is None else "%d-%d" % (il, ir)
        brazo = "-" if il is None else "%d/%d" % (il - l, r - ir)
        marca = "PUENTE" if viga else ("embudo" if fu else "")
        print("%4d agua %2d..%2d ancho %2d  isla %-5s brazos %-5s sector %d %s"
              % (n, l, r, r - l + 1, isla, brazo, sec, marca))

    print("\n%g filas: %d saltos de borde, %d filas con un brazo sin agua"
          % (filas, saltos, brazos_malos))


if __name__ == "__main__":
    main()
