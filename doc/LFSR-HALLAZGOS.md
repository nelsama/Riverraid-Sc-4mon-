# LFSR de River Raid — hallazgos y verificación

Herramienta: `tools/lfsr_original.py` (reproduce el ASM del 2600, no el pseudo-C#
del `.docx`, que está mal). Documento de origen:
`docs/Especificación Técnica y Algoritmos para Replicar River Raid.docx`
(extracto en `doc/_reglas_docx.txt`).

## 1. El ASM es correcto; el pseudocódigo del .docx no

```
NextRandomBlock:
    LDA SEED_HI
    LSR             ; C = bit0 de SEED_HI ; SEED_HI >>= 1
    BCC NoXor
    EOR #$B4        ; SEED_HI ^= $B4   (solo si el bit que salió era 1)
NoXor:
    STA SEED_HI
    ROR SEED_LO     ; SEED_LO rota metiendo el CARRY del LSR por arriba
    RTS
```

Clave: **el bit que entra en `SEED_LO` es el Carry del `LSR`** (el bit 0 previo
de `SEED_HI`). El Python/C# del documento usa el LSB original de `SEED_LO`,
lo que da otra secuencia. **Se implementa el ASM.**

## 2. El "bucle de 128" del documento **no es exacto**

Medido con `tools/lfsr_original.py`:

| Dato medido | Valor |
|---|---|
| Período matemático del registro de 16 bits | pre-periodo 8 + ciclo **255** |
| Ciclo del byte alto (`SEED_HI`) solo | **255** exacto, todos los valores salvo 0 |
| Bloques con partida del cauce (bits 4–5 = `00`) en 128 bloques | 31 (≈ 1 de cada 4) |
| Reparto de bits 4–5 en el ciclo completo | 63/64/64/64 → uniforme |

Conclusión: el patrón **se repite cada 255 bloques**, no cada 128. El "128" del
documento no se sostiene; los "vectores de checkpoint" (`3C/8B`, `72/4F`,
`B4/6D`) **no aparecen** en la secuencia generada → son inventados o de otra
variante. Un puente "cada 128 bloques" es una decisión de diseño nuestra.

## 3. Ancho del río: `(val & 0x0F) + 4` no es suave

Valores del byte bajo (anchos) en bloques consecutivos:

```
8, 14, 9, 6, 5, 4, 4, 4, 12, 8, 14, 17, 18, 11, 7, 5, 12, 8, 6, 5, 12, 16, 18, 19...
```

**22 de 39** saltos entre bloques son de más de 3 unidades. Si cada bloque son
4 filas de tiles, la orilla daría saltos de hasta 10 celdas: el cauce "vibraría"
en cada bloque.

Conclusión: el ancho debe **interpolarse** (el original lo hace porque el río es
una curva de 1 posición por bloque, no un `+4` continuo) o tomarse del byte alto
con escalado propio. **Decisión pendiente**: es el único punto del documento que
choca con una regla ya validada en este proyecto (2.2: bordes sin vibración).

## 4. Lo que se adopta tal cual

- LFSR ASM (semillas `A8`/`14`, máscara `$B4`).
- Bits `0-3` = ancho, `4-5` = partida del cauce (`00`), `6-8` = entidad.
- Tabla de entidades: 0 fuel, 1 barco, 2 helicóptero, 3 jet, 4 edificio, 5-7 nada.
- Bucle de dificultad cuando el generador vuelve al estado inicial (**255**
  bloques medidos, no 128).
- Puntuación del documento: barco 30, heli 60, fuel 80, jet 100, puente 500.
- Vida extra cada 10.000 puntos (máx. 9 de reserva); kill screen a 1.000.000.
- Sirena al bajar del 25 % de combustible.
- El depósito **se puede destruir a tiros**; se puede cargar parcialmente.
