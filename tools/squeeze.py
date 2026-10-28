#!/usr/bin/env python3
"""squeeze.py - Reduce game.c en un subconjunto minimo que conserva el bucle
principal (scroll + regeneracion de fila + dibujo de sprites + HUD + sonido).

Sirve para medir cupo de frame: si el nucleo ya desborda el VBLANK, el bucle de
game.c no tiene arreglo sin reestructurar. Genera tools/game_sq.c.
"""
import re

src = open("game.c", encoding="latin-1").read()

# 1) Quita los bloques de comentarios /* ... */ (incluso multilinea) y // ...
src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
src = re.sub(r"//[^\n]*", "", src)

keep_names = set()

# 2) Elimina funciones que no tocan el camino caliente.
DROP = [
    "read_input", "update_bullets", "check_bullet_collisions", "update_boat",
    "update_fuel", "update_boom", "update_fuel_gauge", "game_over_screen",
    "plane_hit", "plane_respawn", "plane_place_safe", "plane_on_bank",
    "fire_bullet", "setup_video", "hud_number", "rng_next", "pause",
]

# Cada funcion static se define como "static <tipo> nombre(args) { ... }" con la
# llave de cierre en columna 0.
func_re = re.compile(r"^static [^\n{;]*?\b(\w+)\s*\([^)]*\)\s*\{", re.M)
out = []
pos = 0
for m in func_re.finditer(src):
    name = m.group(1)
    # localiza el cierre: primera "\n}" a partir del inicio
    end = src.find("\n}\n", m.end())
    end = len(src) if end < 0 else end + 3
    out.append(src[pos:m.start()])
    if name in DROP:
        out.append("/* %s eliminada */\n" % name)
        # Conserva una version vacia de las que se llaman desde el bucle.
        if name in ("read_input", "update_bullets", "check_bullet_collisions",
                    "update_boat", "update_fuel", "update_boom",
                    "update_fuel_gauge", "plane_place_safe", "game_over_screen"):
            sig = m.group(0).split("{")[0]
            out.append(sig + "{\n}\n")
        if name == "hud_number":
            out.append("static uint8_t hud_number(uint8_t s, uint16_t x, uint16_t v, uint8_t n) { (void)x; (void)v; (void)n; return s; }\n")
        if name == "setup_video":
            out.append("static void setup_video(void) { static uint8_t done; if (done) return; done = 1; vc_wait_ready(); vc_wait_vblank(); vc_clear_vram(); vc_set_bgcolor(VC_RGB444(0x0,0x0,0x0)); vc_load_bg_pattern(TILE_GRASS, tile_grass_p0, tile_grass_p1); vc_load_bg_pattern(TILE_WATER, tile_water_p0, tile_water_p1); vc_load_bg_pattern(TILE_WATER2, tile_water2_p0, tile_water2_p1); vc_pal_set_bg(PAL_TERRAIN,1,VC_RGB444(0x0,0xA,0x0)); vc_pal_set_bg(PAL_TERRAIN,2,VC_RGB444(0x0,0x6,0x0)); vc_pal_set_bg(PAL_WATER,1,VC_RGB444(0x0,0x2,0xA)); vc_pal_set_bg(PAL_WATER,2,VC_RGB444(0x1,0x4,0xE)); vc_pal_set_bg(PAL_WATER,3,VC_RGB444(0x2,0x6,0xF)); vc_pal_set_bg(PAL_ISLAND,1,VC_RGB444(0xD,0x8,0x3)); vc_pal_set_bg(PAL_ISLAND,2,VC_RGB444(0x0,0x7,0x0)); vc_pal_set_bg(PAL_ISLAND,3,VC_RGB444(0x2,0x6,0xF)); vc_load_bg_pattern(TILE_ISLAND, tile_island_tl_p0, tile_island_tl_p1); vc_load_bg_pattern(TILE_ISLAND_R, tile_island_tr_p0, tile_island_tr_p1); vc_load_bg_pattern(TILE_ISLAND_L2, tile_island_bl_p0, tile_island_bl_p1); vc_load_bg_pattern(TILE_ISLAND_R2, tile_island_br_p0, tile_island_br_p1); vc_load_bg_pattern(TILE_EDGE_L0, tile_edge_l0_p0, tile_edge_l0_p1); vc_load_bg_pattern(TILE_EDGE_L1, tile_edge_l1_p0, tile_edge_l1_p1); vc_load_bg_pattern(TILE_EDGE_R0, tile_edge_r0_p0, tile_edge_r0_p1); vc_load_bg_pattern(TILE_EDGE_R1, tile_edge_r1_p0, tile_edge_r1_p1); vc_load_spr_pattern(SPR_PLANE_PAT, spr_plane_p0, spr_plane_p1); vc_set_raster(0xFF, 0xFF); }\n")
        if name == "plane_on_bank":
            out.append("static uint8_t plane_on_bank(uint16_t x, uint8_t y) { (void)x; (void)y; return 0; }\n")
        if name == "plane_hit":
            out.append("static void plane_hit(void) {}\n")
        if name == "plane_respawn":
            out.append("static void plane_respawn(void) {}\n")
    else:
        out.append(src[m.start():end])
    pos = end
out.append(src[pos:])
code = "".join(out)

# 3) La colision y el disparo desaparecen del bucle; el avion se mueve fijo.
code = code.replace("read_input();", "plane_x = 100;")
code = code.replace("check_plane_collisions();", "")
code = code.replace("if (plane_dead) {\n                if (plane_dead_frames) plane_dead_frames--;\n                else plane_respawn();\n            }", "")

open("tools/game_sq.c", "w", encoding="latin-1").write(code)
print("funciones eliminadas:", len(DROP))
