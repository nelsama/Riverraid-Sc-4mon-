# **Especificación Técnica Profunda: Renderizado, Hardware y Geometría Procedural de River Raid**

---

Este documento complementario proporciona una especificación técnica de nivel de ingeniería sobre el funcionamiento interno de **River Raid** en el microprocesador MOS 6502 y el chip gráfico TIA (Television Interface Adaptor) del Atari 2600\. Incluye las fórmulas exactas de interpolación de scanlines, sincronización de registros, geometría de bifurcaciones e implementación completa del generador de terreno en Python y C\# para motores de videojuegos modernos.

## **1\. Arquitectura de Hardware del Atari 2600 y el TIA**

### **1.1. Restricciones del Hardware Original**

> * **CPU:** MOS Technology 6507 ejecutando a \~1.19 MHz (NTSC).  
> * **RAM:** 128 bytes compartidos (variables globales, stack, estado de objetos).  
> * **ROM:** Cartucho de 4,096 bytes (4 KB). Sin framebuffers ni memoria de vídeo VRAM.  
> * **Raster Kernel (Dibujo en Tiempo Real):** La imagen debe calcularse y enviarse al chip TIA (Television Interface Adaptor) línea por línea de televisión (scanline) mientras el haz de electrones recorre la pantalla CRT.

### **1.2. Mapeo de Objetos del Chip TIA**

| Objeto TIA | Ancho / Resolución | Uso en River Raid   |
| :---- | :---- | :---- |
| **Playfield (PF0, PF1, PF2)** | 40 bloques gruesos horizontales | Orillas verdes del río (tierra) e islas centrales. |
| **Player 0 (P0)** | 8 píxeles de ancho (monocromo) | Avión del jugador (B-1 StratoWing). |
| **Player 1 (P1)** | 8 píxeles de ancho (monocromo) | Enemigo activo en pantalla (Barco, Helicóptero, Jet, Fuel, Casa). |
| **Missile 0 (M0)** | 1, 2, 4 o 8 píxeles | Disparo / Proyectil del jugador. |
| **Ball (BL)** | 1, 2, 4 o 8 píxeles | Línea divisoria del puente / Marcador de UI. |

## **2\. Anatomía de una Sección de Río y Ciclo de 16 Bloques**

A diferencia de los tilemaps tradicionales de cuadrícula estática, el mapa de River Raid se genera en bloques verticales de altura fija organizados en ciclos estrictos de 16 bloques entre cada puente.

### **2.1. Estructura de un Tramo de Puente a Puente**

`+-------------------------------------------------------+`  
`| [Bloque 00]  PUENTE (Checkpoint / Zona Destruible)   | -> Marcador de inicio de sección`  
`+-------------------------------------------------------+`  
`| [Bloque 01]  Río Normal / Curva / Isla                |`  
`| [Bloque 02]  Río Normal / Curva / Isla                |`  
`| [Bloque 03]  Río Normal / Curva / Isla                |`  
`|   ...        ...                                      | -> 14 Bloques de Navegación Procedural`  
`| [Bloque 13]  Río Normal / Curva / Isla                |`  
`| [Bloque 14]  Río Normal / Curva / Isla                |`  
`+-------------------------------------------------------+`  
`| [Bloque 15]  Bloque de Transición / Embudo de Cierre  | -> Fuerza la alineación para el puente`  
`+-------------------------------------------------------+`  
`| [Bloque 16]  SIGUIENTE PUENTE (Reinicia ciclo a 00)   |`  
`+-------------------------------------------------------+`

## **3\. Geometría Procedural, Interpolación de Scanlines e Islas**

### **3.1. Transformación de Ancho y Curva (${X}_{1}$ y ${X}_{2}$)**

Cada bloque de terreno define la forma de las orillas para una altura de 16 a 32 scanlines. El ancho base del cauce $W$ y el desplazamiento horizontal $C$ se extraen del valor de 16 bits del LFSR:

`W = (LFSR_Value & 0x0F) + AnchoMinimo`  
`C = ((LFSR_Value >> 4) & 0x07) - DesplazamientoCentral`

Las coordenadas de la orilla izquierda (${X}_{1}$) y orilla derecha (${X}_{2}$) para una resolución estándar de 100 unidades horizontales se calculan como:

`X1 = (50 + C) - (W / 2)`  
`X2 = (50 + C) + (W / 2)`

### **3.2. Interpolación Lineal Scanline por Scanline**

Para evitar cambios abruptos y escalonados entre el bloque actual (con orilla $X{1}_{actual}$) y el siguiente bloque objetivo ($X{1}_{target}$), la CPU ejecuta una interpolación lineal paso a paso por cada scanline vertical $y$:

`X1(y) = X1_actual + ( (X1_target - X1_actual) * (y / ScanlinesPorBloque) )`  
`X2(y) = X2_actual + ( (X2_target - X2_actual) * (y / ScanlinesPorBloque) )`

### **3.3. Algoritmo de Bifurcación e Islas Centrales**

Cuando los bits 4 y 5 del registro de estado son 00, se inserta una isla en el centro exacto del cauce actual:

`X_centro = (X1 + X2) / 2`  
`Isla_Izq = X_centro - (AnchoIsla / 2)`  
`Isla_Der = X_centro + (AnchoIsla / 2)`

`SubCanal_Izquierdo = [X1, Isla_Izq]`  
`SubCanal_Derecho   = [Isla_Der, X2]`  
`Ancho_Pasada       = ( (X2 - X1) - AnchoIsla ) / 2`

## **4\. Escalado Progresivo de Dificultad**

La dificultad se escala dinámicamente según el número del puente actual (${N}_{puente}$), sin alterar la secuencia determinista del algoritmo generador:

| Parámetro | Fórmula / Lógica de Ajuste | Efecto en la Jugabilidad   |
| :---- | :---- | :---- |
| **Compresión de Cauce (${W}_{max}$)** | W\_max \= Max(30, W\_base \- (N\_puente \* 0.8)) | Reduce el ancho máximo del río a medida que se avanza. |
| **Ancho de Subcanales** | Canal \= (W\_comprimido \- AnchoIsla) / 2 | En puentes altos (+30), los subcanales se reducen al ancho exacto del jet. |
| **Velocidad de Enemigos** | Vel\_Enemigo \= Vel\_Base \* (1.0 \+ (N\_puente \* 0.02)) | Jets y helicópteros cruzan la pantalla más rápido. |
| **Substitución de Entidades** | If (N\_puente \> 15 and Entity \== VACIO) \-\> JET | Reemplaza espacios vacíos por jets de alta velocidad en fases avanzadas. |

## **5\. Código de Implementación Completo para Motores Modernos**

### **5.1. Implementación Completa en Python**

`class RiverRaidEngine:`  
    `def __init__(self):`  
        `self.seed_hi = 0xA8`  
        `self.seed_lo = 0x14`  
        `self.puente_actual = 1`  
        `self.bloque_en_tramo = 0`  
        `self.scanlines_por_bloque = 16`  
        `self.ancho_isla_fijo = 16`

    `def reset_to_checkpoint(self, seed_hi, seed_lo, puente):`  
        `self.seed_hi = seed_hi`  
        `self.seed_lo = seed_lo`  
        `self.puente_actual = puente`  
        `self.bloque_en_tramo = 0`

    `def step_lfsr(self):`  
        `lsb_hi = self.seed_hi & 1`  
        `self.seed_hi >>= 1`  
        `if lsb_hi:`  
            `self.seed_hi ^= 0xB4`  
          
        `carry = 0x80 if lsb_hi else 0x00`  
        `self.seed_lo = (self.seed_lo >> 1) | carry`  
        `return (self.seed_hi << 8) | self.seed_lo`

    `def generar_siguiente_bloque(self):`  
        `self.bloque_en_tramo += 1`  
          
        `# Bloque 16: Puente / Checkpoint`  
        `if self.bloque_en_tramo == 16:`  
            `self.bloque_en_tramo = 0`  
            `self.puente_actual += 1`  
            `return {`  
                `"tipo": "PUENTE",`  
                `"orilla_izq": 10,`  
                `"orilla_der": 90,`  
                `"tiene_isla": False,`  
                `"enemigo": "NINGUNO",`  
                `"numero_puente": self.puente_actual`  
            `}`

        `# Generar terreno procedural con LFSR`  
        `val = self.step_lfsr()`  
          
        `# Extracción de características`  
        `ancho_raw = (val & 0x0F) + 20`  
        `curva_shift = ((val >> 4) & 0x07) - 3`  
        `tiene_isla = ((val >> 7) & 0x03) == 0`  
        `entity_raw = (val >> 9) & 0x07`

        `# Ajuste por dificultad del puente`  
        `dificultad = min(1.0, self.puente_actual / 50.0)`  
        `ancho_maximo = 80 - (dificultad * 40)  # De 80px a 40px`  
        `ancho_final = min(ancho_raw, ancho_maximo)`

        `centro = 50 + (curva_shift * 3)`  
        `orilla_izq = max(5, centro - (ancho_final / 2))`  
        `orilla_der = min(95, centro + (ancho_final / 2))`

        `# Cancelar isla si el canal resultante es impracticable`  
        `if tiene_isla and (orilla_der - orilla_izq - self.ancho_isla_fijo) < 12:`  
            `tiene_isla = False`

        `return {`  
            `"tipo": "RIO",`  
            `"orilla_izq": orilla_izq,`  
            `"orilla_der": orilla_der,`  
            `"tiene_isla": tiene_isla,`  
            `"ancho_isla": self.ancho_isla_fijo if tiene_isla else 0,`  
            `"enemigo_id": entity_raw,`  
            `"numero_puente": self.puente_actual`  
        `}`

    `def obtener_scanline_interpolado(self, bloque_actual, bloque_siguiente, scanline_y):`  
        `"""Calcula las posiciones X exactas para una scanline específica."""`  
        `factor = scanline_y / float(self.scanlines_por_bloque)`  
          
        `izq = bloque_actual["orilla_izq"] + (bloque_siguiente["orilla_izq"] - bloque_actual["orilla_izq"]) * factor`  
        `der = bloque_actual["orilla_der"] + (bloque_siguiente["orilla_der"] - bloque_actual["orilla_der"]) * factor`  
          
        `return {`  
            `"scanline_y": scanline_y,`  
            `"orilla_izq_x": izq,`  
            `"orilla_der_x": der`  
        `}`

### **5.2. Implementación Completa en C\# (Godot / Unity Engine)**

`using System;`

`public class RiverRaidEngineCS`  
`{`  
    `private byte seedHi = 0xA8;`  
    `private byte seedLo = 0x14;`  
    `public int PuenteActual { get; private set; } = 1;`  
    `public int BloqueEnTramo { get; private set; } = 0;`  
    `private const int ScanlinesPorBloque = 16;`  
    `private const float AnchoIslaFijo = 16.0f;`

    `public struct BloqueTerreno`  
    `{`  
        `public string Tipo;`  
        `public float OrillaIzq;`  
        `public float OrillaDer;`  
        `public bool TieneIsla;`  
        `public float AnchoIsla;`  
        `public int EnemigoID;`  
        `public int NumeroPuente;`  
    `}`

    `public void CargarCheckpoint(byte hi, byte lo, int puente)`  
    `{`  
        `seedHi = hi;`  
        `seedLo = lo;`  
        `PuenteActual = puente;`  
        `BloqueEnTramo = 0;`  
    `}`

    `private ushort StepLFSR()`  
    `{`  
        `int lsbHi = seedHi & 1;`  
        `seedHi >>= 1;`  
        `if (lsbHi == 1) seedHi ^= 0xB4;`

        `int carry = (lsbHi == 1) ? 0x80 : 0x00;`  
        `seedLo = (byte)((seedLo >> 1) | carry);`

        `return (ushort)((seedHi << 8) | seedLo);`  
    `}`

    `public BloqueTerreno GenerarSiguienteBloque()`  
    `{`  
        `BloqueEnTramo++;`

        `if (BloqueEnTramo == 16)`  
        `{`  
            `BloqueEnTramo = 0;`  
            `PuenteActual++;`  
            `return new BloqueTerreno`  
            `{`  
                `Tipo = "PUENTE",`  
                `OrillaIzq = 10.0f,`  
                `OrillaDer = 90.0f,`  
                `TieneIsla = false,`  
                `AnchoIsla = 0.0f,`  
                `EnemigoID = 0,`  
                `NumeroPuente = PuenteActual`  
            `};`  
        `}`

        `ushort val = StepLFSR();`  
        `float anchoRaw = (val & 0x0F) + 20.0f;`  
        `int curvaShift = ((val >> 4) & 0x07) - 3;`  
        `bool tieneIsla = ((val >> 7) & 0x03) == 0;`  
        `int entityRaw = (val >> 9) & 0x07;`

        `float dificultad = Math.Min(1.0f, PuenteActual / 50.0f);`  
        `float anchoMaximo = 80.0f - (dificultad * 40.0f);`  
        `float anchoFinal = Math.Min(anchoRaw, anchoMaximo);`

        `float centro = 50.0f + (curvaShift * 3.0f);`  
        `float orillaIzq = Math.Max(5.0f, centro - (anchoFinal / 2.0f));`  
        `float orillaDer = Math.Min(95.0f, centro + (anchoFinal / 2.0f));`

        `if (tieneIsla && (orillaDer - orillaIzq - AnchoIslaFijo) < 12.0f)`  
        `{`  
            `tieneIsla = false;`  
        `}`

        `return new BloqueTerreno`  
        `{`  
            `Tipo = "RIO",`  
            `OrillaIzq = orillaIzq,`  
            `OrillaDer = orillaDer,`  
            `TieneIsla = tieneIsla,`  
            `AnchoIsla = tieneIsla ? AnchoIslaFijo : 0.0f,`  
            `EnemigoID = entityRaw,`  
            `NumeroPuente = PuenteActual`  
        `};`  
    `}`

    `public (float izq, float der) ObtenerScanlineInterpolado(BloqueTerreno bActual, BloqueTerreno bSiguiente, int scanlineY)`  
    `{`  
        `float factor = scanlineY / (float)ScanlinesPorBloque;`  
        `float izq = bActual.OrillaIzq + (bSiguiente.OrillaIzq - bActual.OrillaIzq) * factor;`  
        `float der = bActual.OrillaDer + (bSiguiente.OrillaDer - bActual.OrillaDer) * factor;`  
        `return (izq, der);`  
    `}`  
`}`  
