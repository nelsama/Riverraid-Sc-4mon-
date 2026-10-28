"""lfsr_original.py - LFSR de River Raid (Atari 2600) TAL CUAL el ASM.

Rutina original (Carol Shaw):

    NextRandomBlock:
        LDA SEED_HI
        LSR             ; C = bit0 de SEED_HI, SEED_HI >>= 1
        BCC NoXor
        EOR #$B4        ; SEED_HI ^= $B4  (solo si el bit salido era 1)
    NoXor:
        STA SEED_HI
        ROR SEED_LO     ; rota SEED_LO con el CARRY (el bit que salio del LSR)
        RTS

OJO: el Python del documento .docx NO reproduce esto (usa el LSB original
para el carry del byte bajo). Aqui se hace exactamente lo que hace el 6502.
"""
import sys


class RiverRaidLFSR:
    def __init__(self, hi=0xA8, lo=0x14):
        self.hi = hi
        self.lo = lo

    def step(self):
        """Un paso del LFSR. Devuelve el estado de 16 bits (hi<<8 | lo)."""
        carry = self.hi & 1          # LSR: el bit 0 pasa al carry
        self.hi >>= 1
        if carry:
            self.hi ^= 0xB4          # EOR #$B4
        self.lo = ((self.lo >> 1) | (carry << 7)) & 0xFF   # ROR SEED_LO
        return (self.hi << 8) | self.lo

    def period(self, limit=70000):
        """Cuenta cuantos pasos tarda en repetir el estado inicial."""
        h0, l0 = self.hi, self.lo
        for n in range(1, limit + 1):
            self.step()
            if self.hi == h0 and self.lo == l0:
                return n
        return None


def decode(val):
    """Campo de bits del bloque (segun el documento)."""
    return {
        "width": (val & 0x0F) + 4,
        "split": ((val >> 4) & 0x03) == 0,
        "bits45": (val >> 4) & 0x03,
        "entity": (val >> 6) & 0x07,
    }


ENTIDADES = {
    0: "fuel",
    1: "barco",
    2: "helicoptero",
    3: "jet",
    4: "edificio",
    5: "-",
    6: "-",
    7: "-",
}


def main():
    g = RiverRaidLFSR()
    print("PERIODO:", g.period(), "pasos")
    g = RiverRaidLFSR()
    print("\nprimeros 40 bloques:")
    print(" n   val    ancho split ent entidad")
    for n in range(40):
        v = g.step()
        d = decode(v)
        print("%3d  %04X  %3d    %s   %d  %s" % (
            n, v, d["width"], "SI " if d["split"] else "no ",
            d["entity"], ENTIDADES[d["entity"]]))

    # estadisticas de los primeros 128 bloques (un "nivel")
    g = RiverRaidLFSR()
    stats = {}
    splits = 0
    widths = []
    for n in range(128):
        d = decode(g.step())
        stats[ENTIDADES[d["entity"]]] = stats.get(ENTIDADES[d["entity"]], 0) + 1
        splits += d["split"]
        widths.append(d["width"])
    print("\n128 bloques -> entidades:", stats)
    print("bloques con partida del cauce:", splits)
    print("ancho: min %d  max %d  media %.1f" % (
        min(widths), max(widths), sum(widths) / len(widths)))


if __name__ == "__main__":
    sys.exit(main())
