# **Especificación Técnica: Puentes, Métricas Espaciales, Embudo y Checkpoints en River Raid (Atari 2600\)**

---

Este documento constituye el módulo técnico especializado referente a la arquitectura de **Puentes, Cadencia Fija de Distancia, Transición en Embudo y Sistema de Checkpoints** de *River Raid* (Atari 2600, Carol Shaw / Activision, 1982). Proporciona los cálculos espaciales exactos, la temporización según la velocidad del jugador y el desensamblado del flujo de control de estados.

## **1\. Métricas de Cadencia Espacial y Estructura Fija de Sección**

### **1.1. Unidades de Medida en la Arquitectura Original**

> * **Scanline (Línea de Escaneo):** Unidad mínima de resolución vertical en el TIA (192 scanlines visibles en pantalla NTSC).  
> * **Subbloque de Terreno:** Equivalente a 16 o 32 scanlines consecutivas (según la tasa de refresco del kernel).  
> * **Bloque Estándar de Terreno:** Unidad discreta de avance del generador LFSR (32 scanlines / \~16 píxeles de alto relativo).  
> * **Tramo de Sección Completo:** Exactamente **16 Bloques de Terreno** (512 scanlines verticales acumuladas).

### **1.2. Mapeo del Ciclo Fijo de 16 Bloques**

| Rango de Bloque | Nombre de Fase | Comportamiento Geométrico y Lógica de LFSR | Restricciones de Generación   |
| :---- | :---- | :---- | :---- |
| Bloque 00 | **Puente / Checkpoint** | Ancho fijo y angosto (32 px). Sprite destruible en el centro. Salto de estado del LFSR. | Sin enemigos flotantes. Colisión impenetrable si no se destruye. |
| Bloques 01 a 13 | **Río Procedural** | El LFSR define el ancho, la dirección de curva y la presencia de islas centrales. | Generación libre de barcos, helicópteros, jets y depósitos de FUEL. |
| Bloques 14 y 15 | **Embudo de Transición (Funnel)** | Inactivación de la generación aleatoria de islas. Fuerza la alineación al ancho objetivo. | Orillas convergen linealmente hacia la coordenada fija del puente. |

## **2\. Temporización según la Velocidad de Vuelo (Física del Scroll)**

La aparición del puente es **estrictamente fija en distancia (512 scanlines)**, pero su tiempo de llegada varia en función del Joystick vertical del jugador:

| Estado del Joystick | Velocidad de Scroll (Scanlines/frame) | Duración Promedio por Tramo (60 Hz NTSC)   |
| :---- | :---- | :---- |
| **Frenando (Abajo)** | 0.33 scanlines / frame | \~25.6 segundos |
| **Velocidad Normal (Neutro)** | 0.50 scanlines / frame | \~17.0 segundos |
| **Acelerando (Arriba)** | 1.00 scanlines / frame | \~8.5 segundos |

## **3\. Algoritmo del Embudo de Cierre y Posicionamiento del Puente**

Para garantizar que el puente siempre se dibuje sobre un cauce angosto sin islas flotantes o bordes desalineados, el motor fuerza el siguiente cálculo geométrico en los bloques 14 y 15:

### **3.1. Fórmulas de Convergencia de Orillas**

`X1_puente_fijo = 34  // Coordenada X fija para el borde izquierdo del puente (resolución 0-100)`  
`X2_puente_fijo = 66  // Coordenada X fija para el borde derecho del puente (resolución 0-100)`

`// En los bloques 14 y 15, la orilla se calcula mediante interpolación lineal hacia la meta:`  
`X1(step) = X1_actual + ((X1_puente_fijo - X1_actual) * (step / total_steps))`  
`X2(step) = X2_actual + ((X2_puente_fijo - X2_actual) * (step / total_steps))`  
`Tiene_Isla = FALSE  // Forzado para evitar obstáculos en el cuello de botella`

## **4\. Sistema de Checkpoints, Reaparición y Memoria RAM**

### **4.1. Registro de Estado en RAM**

Al cruzar o destruir el puente (Bloque 00), la CPU actualiza las siguientes variables en la memoria RAM de 128 bytes:

`CURRENT_BRIDGE_NUM = CURRENT_BRIDGE_NUM + 1  // Incrementa el nivel/dificultad`  
`CHECKPOINT_SEED_HI = CURRENT_SEED_HI          // Guarda el estado del LFSR`  
`CHECKPOINT_SEED_LO = CURRENT_SEED_LO          // Guarda el estado del LFSR`

### **4.2. Algoritmo de Reaparición tras la Muerte (Respawn)**

Cuando el jugador choca o se queda sin combustible:

> 1. Se decrementa el contador de vidas (LIVES \= LIVES \- 1).  
> 2. Se restaura la semilla guardada: CURRENT\_SEED\_HI \= CHECKPOINT\_SEED\_HI y CURRENT\_SEED\_LO \= CHECKPOINT\_SEED\_LO.  
> 3. Se coloca la posición del avión en $Y=0$ (justo por encima de la línea del puente del checkpoint).  
> 4. El contador de bloque en el tramo se reinicia a Bloque 00\.

## **5\. Implementación Completa de la Lógica del Puente en C\#**

`using System;`

`public class RiverRaidBridgeController`  
`{`  
    `private const int BLOQUES_POR_TRAMO = 16;`  
    `private const float ORILLA_IZQ_PUENTE_FIJA = 34.0f;`  
    `private const float ORILLA_DER_PUENTE_FIJA = 66.0f;`  
      
    `private int bloqueActualEnTramo = 0;`  
    `public int ContadorPuentes { get; private set; } = 1;`

    `// Estado guardado para Checkpoint`  
    `private byte checkpointSeedHi = 0xA8;`  
    `private byte checkpointSeedLo = 0x14;`  
    `private int checkpointPuenteNum = 1;`

    `public struct DatosBloque`  
    `{`  
        `public string TipoBloque;     // "RIO", "EMBUDO", "PUENTE"`  
        `public float OrillaIzq;`  
        `public float OrillaDer;`  
        `public bool TieneIsla;`  
        `public bool EsCheckpoint;`  
        `public int NumeroPuente;`  
    `}`

    `public DatosBloque ProcesarSiguienteBloque(float orillaIzqActual, float orillaDerActual, ushort lfsrState)`  
    `{`  
        `bloqueActualEnTramo++;`

        `// FASE 1: BLOQUE 16 / 0 (EL PUENTE)`  
        `if (bloqueActualEnTramo >= BLOQUES_POR_TRAMO)`  
        `{`  
            `bloqueActualEnTramo = 0;`  
            `ContadorPuentes++;`

            `return new DatosBloque`  
            `{`  
                `TipoBloque = "PUENTE",`  
                `OrillaIzq = ORILLA_IZQ_PUENTE_FIJA,`  
                `OrillaDer = ORILLA_DER_PUENTE_FIJA,`  
                `TieneIsla = false,`  
                `EsCheckpoint = true,`  
                `NumeroPuente = ContadorPuentes`  
            `};`  
        `}`

        `// FASE 2: BLOQUES 14 Y 15 (EMBUDO DE CIERRE)`  
        `if (bloqueActualEnTramo >= 14)`  
        `{`  
            `float factor = (bloqueActualEnTramo - 13) / 2.0f; // Interpolación hacia el puente`  
            `float izqFunnel = orillaIzqActual + (ORILLA_IZQ_PUENTE_FIJA - orillaIzqActual) * factor;`  
            `float derFunnel = orillaDerActual + (ORILLA_DER_PUENTE_FIJA - orillaDerActual) * factor;`

            `return new DatosBloque`  
            `{`  
                `TipoBloque = "EMBUDO",`  
                `OrillaIzq = izqFunnel,`  
                `OrillaDer = derFunnel,`  
                `TieneIsla = false, // Desactivada obligatoriamente`  
                `EsCheckpoint = false,`  
                `NumeroPuente = ContadorPuentes`  
            `};`  
        `}`

        `// FASE 3: BLOQUES 1 A 13 (RÍO PROCEDURAL NORMAL)`  
        `float anchoRaw = (lfsrState & 0x0F) + 20.0f;`  
        `int curvaShift = ((lfsrState >> 4) & 0x07) - 3;`  
        `bool tieneIsla = ((lfsrState >> 7) & 0x03) == 0;`

        `float centro = 50.0f + (curvaShift * 3.0f);`  
        `float izq = Math.Max(5.0f, centro - (anchoRaw / 2.0f));`  
        `float der = Math.Min(95.0f, centro + (anchoRaw / 2.0f));`

        `return new DatosBloque`  
        `{`  
            `TipoBloque = "RIO",`  
            `OrillaIzq = izq,`  
            `OrillaDer = der,`  
            `TieneIsla = tieneIsla,`  
            `EsCheckpoint = false,`  
            `NumeroPuente = ContadorPuentes`  
        `};`  
    `}`

    `public void GuardarCheckpoint(byte seedHi, byte seedLo)`  
    `{`  
        `checkpointSeedHi = seedHi;`  
        `checkpointSeedLo = seedLo;`  
        `checkpointPuenteNum = ContadorPuentes;`  
    `}`

    `public (byte seedHi, byte seedLo, int puenteNum) CargarCheckpoint()`  
    `{`  
        `bloqueActualEnTramo = 0;`  
        `ContadorPuentes = checkpointPuenteNum;`  
        `return (checkpointSeedHi, checkpointSeedLo, checkpointPuenteNum);`  
    `}`  
`}`  
