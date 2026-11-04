# ============================================================================
# makefile - RIVER RAID para el Core de Video (vc) / Monitor 6502
# ============================================================================
# Genera output/game.bin (se carga en $0800 con el monitor).
#
# Proyecto autonomo: la biblioteca (output/vc.lib) y la config del linker
# (config/programa.cfg) viven DENTRO de este directorio, copiadas de
# videocore-6502-cc65. Asi no dependemos de rutas relativas fragiles.
#
# Uso:
#   make          - compila el juego
#   make run      - compila y ejecuta en el emulador (si tienes uno)
#   make info     - tamano del binario
#   make map      - mapa de memoria
#   make clean    - limpia generados
#
# Si actualizas la biblioteca, vuelve a copiar vc.lib:
#   cp ../videocore-6502-cc65/output/vc.lib lib/vc.lib
# ============================================================================

CC65_HOME ?= D:/cc65

CC  = $(CC65_HOME)/bin/cl65.exe
CA65 = $(CC65_HOME)/bin/ca65.exe
LD  = $(CC65_HOME)/bin/ld65.exe

# Proyecto autonomo: la lib y la config estan aqui dentro.
ROOT = .

BUILD_DIR  = build
OUTPUT_DIR = output

LD_CONFIG = $(ROOT)/config/programa.cfg
LIB       = $(ROOT)/lib/vc.lib

PROGRAM_NAME = game
PROGRAM = $(OUTPUT_DIR)/$(PROGRAM_NAME).bin
MAP_FILE = $(OUTPUT_DIR)/$(PROGRAM_NAME).map

# Fuentes del juego
APP_C   = game.c joy.c
APP_ASM = startup.s
APP_OBJECTS = $(BUILD_DIR)/game.o $(BUILD_DIR)/joy.o $(BUILD_DIR)/startup.o

# PERF=1 anade un medidor de coste de frame (volcado por UART).
#   make clean && make PERF=1
ifeq ($(PERF),1)
APP_C += perf.c
APP_OBJECTS += $(BUILD_DIR)/perf.o
PERF_FLAGS = -DPERF
endif

# DBG=0 quita las variables de DIAGNOSTICO ($03E0-$03EA) que leen los
# scripts de tools/. Solo hace falta cuando el binario no cabe en RAM.
ifeq ($(DBG),0)
DBG_FLAGS = -DDBG=0
endif

# Flags
CFLAGS  = -t none -O --cpu 6502 -I include $(PERF_FLAGS) $(DBG_FLAGS)
ASFLAGS = -t none --cpu 6502
LDFLAGS = -C $(LD_CONFIG) -m $(MAP_FILE)

# ============================================================================
# REGLAS
# ============================================================================

all: $(BUILD_DIR)/dirs-stamp $(PROGRAM)
	@echo "========================================"
	@echo "Juego generado: $(PROGRAM)"
	@ls -l $(PROGRAM) | awk '{print "Tamano: " $$5 " bytes"}'
	@echo "Carga con:  LOAD GAME 0800   /   R 0800"
	@echo "========================================"

$(BUILD_DIR)/dirs-stamp:
	@mkdir -p $(BUILD_DIR) $(OUTPUT_DIR)
	@touch $@

$(BUILD_DIR)/game.o: $(BUILD_DIR)/dirs-stamp game.c include/video.h include/fastvideo.h include/romapi.h include/fastsound.h include/joy.h include/perf.h
	$(CC) -c $(CFLAGS) -o $@ game.c

$(BUILD_DIR)/perf.o: $(BUILD_DIR)/dirs-stamp perf.c include/perf.h include/romapi.h
	$(CC) -c $(CFLAGS) -o $@ perf.c

$(BUILD_DIR)/sound.o: $(BUILD_DIR)/dirs-stamp sound.c include/sound.h
	$(CC) -c $(CFLAGS) -o $@ sound.c

$(BUILD_DIR)/joy.o: $(BUILD_DIR)/dirs-stamp joy.c include/joy.h
	$(CC) -c $(CFLAGS) -o $@ joy.c

$(BUILD_DIR)/startup.o: $(BUILD_DIR)/dirs-stamp $(APP_ASM)
	$(CA65) $(ASFLAGS) -o $@ $<

$(PROGRAM): $(BUILD_DIR)/dirs-stamp $(APP_OBJECTS) $(LIB)
	$(LD) $(LDFLAGS) -o $@ $(APP_OBJECTS) $(LIB) $(CC65_HOME)/lib/none.lib

# ============================================================================
# UTILIDADES
# ============================================================================

info:
	@if [ -f $(PROGRAM) ]; then \
		ls -l $(PROGRAM) | awk '{print "Tamano: " $$5 " bytes"}'; \
	else \
		echo "Error: juego no compilado"; \
	fi

map:
	@if [ -f $(MAP_FILE) ]; then cat $(MAP_FILE); else echo "Error: mapa no encontrado"; fi

clean:
	rm -rf $(BUILD_DIR) $(OUTPUT_DIR)
	@echo "Limpieza completa (lib/vc.lib se conserva)"

.PHONY: all dirs clean info map
