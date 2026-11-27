#include "renderer.h"
#include "../world/world.h"
#include "../world/build_blocks.h"
#include "../game/game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Procedural surface atlas. UI icons have their own vector silhouettes. */
int tile_for_block(int type, int side) {
    if (cistern_probe_cell(type)) {
        static const int front[]={4,1,5,0};
        int dir=cistern_probe_facing(type);
        return side==front[dir] ? PRESSURE_CLOSED_TILE :
            side==front[(dir+2)&3] ? CONDUIT_TILE :
            side==2 ? (cistern_probe_empty(type) ? CISTERN_DRY_ATLAS : CISTERN_WET_ATLAS) : SINTERED_MATERIAL_TILE;
    }
    if (air_relay_cell(type)) {
        static const int front[]={4,1,5,0};
        int f=air_relay_facing(type);
        return side==front[f] ? PRESSURE_CLOSED_TILE : side==front[(f+2)&3] ? CONDUIT_TILE :
            side==2 ? LAMINATE_TILE : SINTERED_MATERIAL_TILE;
    }
    if (cistern_cell(type))
        return side!=2 ? CISTERN_CASING_ATLAS : type==CISTERN_EMPTY ? CISTERN_DRY_ATLAS :
            type==CISTERN_FULL ? CISTERN_FULL_ATLAS : CISTERN_WET_ATLAS;
    if (heat_bench_cell(type)) return side==3 ? SINTERED_MATERIAL_TILE :
        type==HEAT_BENCH_EMPTY ? PRESSURE_CLOSED_TILE : VENT_TILE;
    if (build_double(type)) type=build_material_block(build_material(type));
    if (type>=BIRCH_PLANK_CELL && type<=DARK_OAK_PLANK_CELL) return BUILD_PLANK_TILE+type-BIRCH_PLANK_CELL;
    if (type==BRICK_CELL || type==QUARTZ_BLOCK_CELL) return type==BRICK_CELL ? BUILD_BRICK_TILE : BUILD_QUARTZ_TILE;
    if (stained_clay_cell(type)) return STAINED_CLAY_TILE+type-STAINED_CLAY_BASE;
    if (type==PUMPKIN || pumpkin_cell(type)) {
        static const int front[]={4,1,5,0};
        int facing=pumpkin_cell(type) ? type-PUMPKIN_FACE_BASE : 2;
        return side==2 || side==3 ? PUMPKIN_TOP_TILE : side==front[facing] ? PUMPKIN_FACE_TILE : PUMPKIN_SIDE_TILE;
    }
    if (piston_cell(type) || piston_base_type(type)) {
        static const int front[]={4,1,5,0,3,2};
        int dir=piston_facing(type),facing=dir<0 ? 2 : front[dir];
        if (side==facing && !piston_extended(type)) return type==STICKY_PISTON || piston_sticky(type) ? 75 : 74;
        return side==(facing^1) || (side==facing && piston_extended(type)) ? 73 : 72;
    }
    if (ejector_cell(type) || ejector_type(type)) {
        static const int front[]={4,1,5,0,3,2};
        int dir=ejector_face(type),facing=dir<0 ? 5 : front[dir];
        return side==facing ? (type==DISPENSER || dispenser_cell(type) ? 68 : 66) : 67;
    }
    switch (type) {
    case SLATE_ROCK: return SLATE_TILE;
    case LIMESTONE_ROCK: return LIMESTONE_TILE;
    case STRATA_SAMPLE_SHALE: return side==3 ? SLATE_TILE : BRINE_TILE;
    case STRATA_SAMPLE_LIME: return side==3 ? LIMESTONE_TILE : VENT_TILE;
    case LAMINATED_MASONRY: return LAMINATE_TILE;
    case AQUIFER_WINCH: return WINCH_TILE;
    case AQUIFER_DOCK: return DOCK_TILE;
    case AQUIFER_BRINE: return BRINE_TILE;
    case AQUIFER_VENT: return VENT_TILE;
    case AQUIFER_LIGHT: return CONDUIT_TILE;
    case SEEP_BED: return SEEP_BED_ATLAS;
    case SEEP_TILE: return SEEP_SOFT_ATLAS;
    case SEEP_TILE_BRIGHT: return SEEP_BRIGHT_ATLAS;
    case SETTLING_MOLD: return SETTLING_FRAME_ATLAS;
    case JOINT_BASALT: return STONE_ALT_B;
    case BASALT_PAVING: return side==2 ? SINTERED_MATERIAL_TILE : STONE_ALT_A;
    case HYDRAULIC_SORTER: return side==2 ? SETTLING_FRAME_ATLAS :
        side==3 ? SINTERED_MATERIAL_TILE : SEEP_BED_ATLAS;
    case FALL_SIEVE: return side==2 ? CISTERN_WET_ATLAS :
        side==3 ? SINTERED_MATERIAL_TILE : DOCK_TILE;
    case SETTLING_LIME: return side==2 ? LIMESTONE_TILE : SETTLING_FRAME_ATLAS;
    case SETTLING_PACKED: return side==2 ? LAMINATE_TILE : SETTLING_FRAME_ATLAS;
    case PRESSURE_HATCH: case PRESSURE_LIFT: return PRESSURE_LIFT_TILE;
    case PRESSURE_CLOSED: return PRESSURE_CLOSED_TILE;
    case PRESSURE_OPEN: return CONDUIT_TILE;
    case PRESSURE_KILN: return VENT_TILE;
    case SINTERED_TILE: return SINTERED_MATERIAL_TILE;
    case TNT_BLOCK: return side==2 ? 70 : side==3 ? 71 : 69;
    case REDSTONE_LAMP: case LAMP_OFF: return 64;
    case LAMP_ON: return 65;
    case REDSTONE_ORE: return 57;
    case REDSTONE_BLOCK: return 58;
    case CHEST: case SADDLE_CHEST: case SMITH_CHEST: case TRAPPED_CHEST: case TEMPLE_CHEST: case DUNGEON_CHEST:
    case DESERT_PYRAMID_CHEST:
        return side==2 || side==3 ? 59 : 60;
    case EMERALD_ORE: return EMERALD_ORE_TILE;
    case EMERALD_BLOCK: return EMERALD_BLOCK_TILE;
    case LAPIS_ORE: return LAPIS_ORE_TILE;
    case LAPIS_BLOCK: return LAPIS_BLOCK_TILE;
    case WOOL: return 61;
    case BOOKSHELF: return side==2 || side==3 ? 19 : 62;
    case IRON_BLOCK: return 63;
    case GOLD_BLOCK: return GOLD_BLOCK_TILE;
    case DIAMOND_BLOCK: return DIAMOND_BLOCK_TILE;
    case GRASS: return side == 2 ? 0 : side == 3 ? 2 : 1;
    case DIRT: case FARMLAND: return 2;
    case STONE: return 3;
    case WOOD: return side == 2 || side == 3 ? 5 : 4;
    case LEAF: return 6;
    case BEDROCK: return 7;
    case SAND: return 8;
    case GRAVEL: return 9;
    case COAL_ORE: return 10;
    case IRON_ORE: return 11;
    case SNOW: return 12;
    case WATER: return 13;
    case CLAY: return 14;
    case FIBROUS_CLAY: return side==2 ? LIMESTONE_TILE : 14;
    case SANDSTONE: return 15;
    case GOLD_ORE: return 16;
    case DIAMOND_ORE: return 17;
    case MOSS_STONE: return 18;
    case PLANKS: return 19;
    case COBBLESTONE: return 20;
    case CRAFTING_TABLE: return side == 2 ? 21 : side == 3 ? 19 : 22;
    case FURNACE: return side == 4 ? 23 : 20;
    case PODZOL: return 24;
    case RED_SAND: return 25;
    case TERRACOTTA: return 26;
    case MYCELIUM: return 27;
    case ICE: return 28;
    case PACKED_ICE: return 29;
    case BIRCH_LOG: return side == 2 || side == 3 ? 31 : 30;
    case SPRUCE_LOG: return side == 2 || side == 3 ? 33 : 32;
    case JUNGLE_LOG: return side == 2 || side == 3 ? 35 : 34;
    case ACACIA_LOG: return side == 2 || side == 3 ? 37 : 36;
    case DARK_OAK_LOG: return side == 2 || side == 3 ? 39 : 38;
    case MUSHROOM_STEM: return 40;
    case RED_MUSHROOM_BLOCK: return 41;
    case BROWN_MUSHROOM_BLOCK: return BROWN_MUSHROOM_TILE;
    case OBSIDIAN: return 42;
    case NETHERRACK: return 43;
    case ASH_SAND: return 44;
    case GLOWSTONE: return 45;
    case NETHER_QUARTZ_ORE: return LIMESTONE_TILE;
    case RESONANCE_MARKER: return SHELL_MARKER_TILE;
    case LAVA: return 47;
    case STONE_BRICKS: return 54;
    case NETHER_BRICKS: return 55;
    default: return 7;
    }
}
static Uint8 clamp_byte(int value) {
    return (Uint8)(value < 0 ? 0 : value > 255 ? 255 : value);
}

int tile_variant(int tile,int x,int z) {
    if (tile!=0 && tile!=2 && tile!=3) return tile;
    /* Independent of live world state: chunk workers and negative coordinates
       always pick the same variant for adjacent faces of one block. */
    unsigned h=(unsigned)x*0x9e3779b1u ^ (unsigned)z*0x85ebca77u;
    h^=h>>16; h*=0x7feb352du; h^=h>>15;
    unsigned v=h&3u;
    if (!v) return tile;
    if (tile==0) return v==1 ? TURF_ALT_A : v==2 ? TURF_ALT_B : 0;
    if (tile==3) return v==1 ? STONE_ALT_A : v==2 ? STONE_ALT_B : 3;
    return v==2 ? SOIL_ALT : 2;
}

static void legacy_texel(int tile,int x,int y,Uint8 *pixel) {
    unsigned r = hash2(x + tile * 23, y + tile * 47);
    unsigned coarse = hash2(x / 3 + tile * 11, y / 3 + tile * 19);
    int n = (int)(r % 25) - 12, q = (int)(coarse % 19) - 9;
    int red = 0, green = 0, blue = 0;
    if (tile>=BUILD_PLANK_TILE && tile<=BUILD_QUARTZ_TILE) {
        if (tile<BUILD_BRICK_TILE) {
            static const int palette[5][3]={{217,189,129},{130,93,56},{170,118,83},{186,104,67},{97,69,45}};
            const int *c=palette[tile-BUILD_PLANK_TILE];
            int joint=y%4==0 || (x+4*(y/4))%13==0;
            int grain=n/3+q/2-joint*23;
            red=c[0]+grain; green=c[1]+grain; blue=c[2]+grain;
        } else if (tile==BUILD_BRICK_TILE) {
            int joint=y%4==0 || (x+4*(y/4))%8==0;
            red=joint ? 159+q : 169+n+q;
            green=joint ? 149+q : 84+n/2+q;
            blue=joint ? 135+q : 69+n/3+q;
        } else {
            int edge=x==0 || y==0 || x==15 || y==15;
            red=231+q/2-edge*19; green=222+q/2-edge*19; blue=210+q/2-edge*19;
        }
        pixel[0]=clamp_byte(red); pixel[1]=clamp_byte(green); pixel[2]=clamp_byte(blue); pixel[3]=255;
        return;
    }
    switch (tile) {
    case PUMPKIN_SIDE_TILE: case PUMPKIN_FACE_TILE: case PUMPKIN_TOP_TILE: {
        int rib=(tile==PUMPKIN_TOP_TILE ? (abs(x-7)+abs(y-7))%4 : (x+y/7)%4)==0;
        red=222+n+q-26*rib; green=111+n+q-22*rib; blue=26+n/2-7*rib;
        if (tile==PUMPKIN_TOP_TILE && abs(x-7)<=1 && abs(y-7)<=1) { red=91; green=91; blue=34; }
        if (tile==PUMPKIN_FACE_TILE &&
            ((y>=5 && y<=7 && ((x>=3&&x<=5)||(x>=10&&x<=12))) ||
             (y==8 && (x==7 || x==8)) || (y>=10 && y<=12 && x>=3 && x<=12 && (y<12 || x%3==1)))) {
            red=56+n/3; green=34+n/3; blue=19;
        }
        break;
    }
    case EMERALD_ORE_TILE: {
        int base=126+n+q;
        red=base; green=base+2; blue=base+5;
        int a=abs(x-4)+abs(y-4),b=abs(x-10)+abs(y-10),c=abs(x-11)+abs(y-3);
        if (a<=2 || b<=3 || c<=1) {
            red=21+n; green=161+n+q; blue=82+n/2;
            if ((x<=4 && y<=4) || (x<=10 && y<=10 && b<=2) || c==0) {
                red+=41; green+=57; blue+=30;
            }
        }
        break;
    }
    case EMERALD_BLOCK_TILE: {
        int edge=x==0 || y==0 || x==15 || y==15;
        int bevel=x<=2 || y<=2 || x>=13 || y>=13;
        int highlight=y<2 || x<2 || ((x+y>=11 && x+y<=13) && !bevel);
        int rel=edge ? -30 : bevel ? -19 : highlight ? 40 : 0;
        red=30+rel/2+n/4; green=168+rel+n/2+q/3; blue=86+rel/2+n/2;
        break;
    }
    case GOLD_BLOCK_TILE: case DIAMOND_BLOCK_TILE: {
        int gold=tile==GOLD_BLOCK_TILE;
        int rim=x==0 || y==0 || x==15 || y==15;
        int glint=x==1 || y==1 || (x+y>=10 && x+y<=12);
        int relief=rim ? -32 : glint ? 26 : 0;
        red=(gold ? 225 : 66)+relief+n/4;
        green=(gold ? 174 : 203)+relief+n/3;
        blue=(gold ? 36 : 192)+relief+n/3;
        break;
    }
    case LAPIS_ORE_TILE: {
        int base=124+n+q;
        red=base; green=base+2; blue=base+6;
        if (abs(x-3)+abs(y-5)<=3 || abs(x-11)+abs(y-11)<=3 || abs(x-11)+abs(y-3)<=2) {
            red=31+n/2; green=70+n+q; blue=165+2*n+q;
        }
        break;
    }
    case LAPIS_BLOCK_TILE: {
        int relief=(x==0 || y==0) ? 20 : (x==15 || y==15) ? -20 : 0;
        red=36+relief/3+n/3; green=76+relief/2+n/2+q; blue=172+relief+n+q;
        break;
    }
    case 72: case 73: {
        int edge=x<2 || x>13 || y<2 || y>13;
        int slot=tile==72 && y>=6 && y<=9;
        red=(slot ? 99 : edge ? 100 : 143)+n+q;
        green=(slot ? 72 : edge ? 98 : 139)+n+q;
        blue=(slot ? 43 : edge ? 96 : 128)+n+q;
        break;
    }
    case 74: case 75: {
        int metal=x<2 || x>13 || y<2 || y>13;
        int groove=y%5==0 || x==7+(y/5%2)*4;
        int sticky=tile==75 && !metal && ((x>=4 && x<=11) || (y>=4 && y<=11));
        red=(metal ? 135 : sticky ? 101 : groove ? 149 : 187)+n+q;
        green=(metal ? 133 : sticky ? 155 : groove ? 103 : 138)+n+q;
        blue=(metal ? 126 : sticky ? 71 : groove ? 57 : 84)+n+q;
        break;
    }
    case 76:
        red=154+x*3+n/2; green=red-3; blue=red-9;
        break;
    case 69: case 70: case 71: {
        int bundle=x%4==0 || (tile!=69 && y%4==0);
        red=bundle ? 148+n : 221+n; green=bundle ? 44+n/2 : 77+n; blue=bundle ? 34+n/3 : 56+n/3;
        if (tile==69 && y>=6 && y<=10) {
            red=218+n; green=200+n; blue=174+n;
            if (x>1 && x<14 && ((y==7 && x%5<4) || (y>=8 && y<=9 && (x%5==2 || x==6 || x==9))))
                { red=79+n; green=44+n/2; blue=43+n/2; }
        }
        if (tile==70 && x>=7 && x<=8 && y>=4 && y<=8) { red=66; green=48; blue=37; }
        break;
    }
    case 66: case 67: case 68: {
        int seam=x==0 || y==15 || ((y==6 || y==11) && (x<4 || x>11));
        int rim=tile!=67 && x>=4 && x<=11 && y>=4 && y<=11;
        int mouth=rim && x>=5 && x<=10 && y>=5 && y<=10;
        red=green=blue=mouth ? 33+n/3 : rim ? (y<=5 ? 85 : 158)+n/2 : (seam ? 99 : 139)+n+q;
        if (tile==68) {
            if (y==3 && (x==4 || x==11)) { red=49; green=81; blue=105; }
            else if (!mouth) { red-=8; blue+=9; }
        }
        break;
    }
    case 64: case 65: {
        int edge=x==0 || x==15 || y==0 || y==15;
        int rib=x==7 || x==8 || y==7 || y==8;
        if (edge || rib) {
            red=(tile==65 ? 114 : 62)+n/4; green=(tile==65 ? 63 : 38)+n/4; blue=29+n/4;
        } else if (tile==65) {
            red=245+n; green=181+n+q; blue=97+n+2*q;
        } else {
            red=132+n+q; green=83+n+q; blue=45+n/3;
        }
        break;
    }
    case 63: {
        int trim=x==0 || y==15 ? -25 : y==0 || x==15 ? 15 : 0;
        red=210+trim+n/4; green=214+trim+n/4; blue=217+trim+n/4;
        break;
    }
    case 62: {
        static const int spines[5][3]={{164,80,53},{77,119,108},{184,142,68},{143,94,153},{91,110,154}};
        if (y==0 || y==7 || y==8 || y==15 || !x || x==15) {
            red=159+n; green=103+n; blue=54+n/2;
        } else {
            const int *color=spines[((x/3)+(y>8 ? 2 : 0))%5];
            int edge=x%3==0 ? -23 : 0;
            red=color[0]+edge+n/2; green=color[1]+edge+n/2; blue=color[2]+edge+n/3;
            if (y==3 || y==11) { red+=25; green+=25; blue+=18; }
        }
        break;
    }
    case 61:
        red=233+n; green=227+n; blue=217+n;
        break;
    case 56:
        red = green = blue = 255;
        break;
    case 57:
        red=112+n; green=111+n; blue=104+n;
        if ((y+2*(x/3)+(int)(hash2(x/4,y/4)%5u))%9<2)
            { red=192+n; green=143+n; blue=78+n; }
        break;
    case 58:
        red=150+n*2; green=103+n; blue=63+n/2;
        if (x<=1 || x>=14 || y<=1 || y>=14) { red-=26; green-=21; blue-=14; }
        if (x>=4 && x<=11 && (y==4 || y==11)) { red+=33; green+=21; blue+=7; }
        break;
    case 59: case 60:
        red=160+n; green=103+n; blue=52+n/2;
        if (x<=1 || x>=14 || y<=1 || y>=14 || (tile==60 && y==5))
            { red=91+n; green=56+n; blue=30+n; }
        if (tile==60 && x>=7 && x<=8 && y>=4 && y<=7)
            { red=217; green=187; blue=94; }
        break;
    case 0:
        red = 91 + n + q; green = 151 + n + q; blue = 53 + n / 2;
        if (r % 23 == 0) { red += 18; green += 16; blue += 8; }
        break;
    case 1:
        if (y < 3 + (int)(hash2(x, 9) % 4)) {
            red = 82 + n; green = 145 + n + q; blue = 47 + n / 2;
        } else {
            red = 124 + n + q; green = 83 + n + q; blue = 53 + n / 2;
            if (r % 17 == 0) { red -= 25; green -= 20; blue -= 13; }
        }
        break;
    case 2:
        red = 128 + n + q; green = 88 + n + q; blue = 57 + n / 2;
        if (r % 19 == 0) { red -= 22; green -= 19; blue -= 13; }
        break;
    case 3:
        red = 130 + n + q; green = 133 + n + q; blue = 132 + n + q;
        if (r % 15 == 0) { red -= 26; green -= 26; blue -= 26; }
        break;
    case 4: {
        int band = (int)(hash2(x / 2, 81) % 30) - 15;
        red = 119 + n / 2 + band; green = 87 + n / 2 + band;
        blue = 53 + n / 3 + band / 2;
        if (x % 5 == 0) { red -= 27; green -= 20; blue -= 13; }
        break;
    }
    case 5: {
        int d = (int)fmaxf(fabsf(x - 7.5f), fabsf(y - 7.5f));
        int ring = d % 3 == 0 ? -23 : 0;
        red = 174 + n / 2 + ring; green = 135 + n / 2 + ring;
        blue = 84 + n / 2 + ring;
        if (d >= 7) { red -= 23; green -= 20; blue -= 10; }
        break;
    }
    case 6:
        red = 51 + n + q; green = 116 + n + q; blue = 47 + n / 2;
        if (r % 8 == 0) { red += 22; green += 25; blue += 10; }
        if (coarse % 7 == 0) { red -= 15; green -= 18; blue -= 10; }
        break;
    case 8:
        red = 215 + n / 2 + q; green = 200 + n / 2 + q; blue = 143 + n / 2 + q;
        if (r % 13 == 0) { red -= 17; green -= 16; blue -= 11; }
        break;
    case 9:
        red = 132 + n + q; green = 124 + n + q; blue = 113 + n + q;
        if (coarse % 4 == 0) { red -= 23; green -= 21; blue -= 19; }
        break;
    case 10: case 11:
        red = 132 + n + q; green = 133 + n + q; blue = 132 + n + q;
        if (coarse % 5 == 0 && r % 5 != 0) {
            if (tile == 10) { red = 40 + n; green = 41 + n; blue = 43 + n; }
            else { red = 194 + n; green = 139 + n; blue = 104 + n; }
        }
        break;
    case 12:
        red = 229 + n / 2; green = 240 + n / 2; blue = 246 + n / 2;
        break;
    case 13:
        red = 39 + n / 2; green = 111 + n + q; blue = 183 + n + q;
        if ((y + x / 3) % 7 == 0) { red += 18; green += 20; blue += 18; }
        break;
    case 14:
        red = 160 + n / 2 + q; green = 171 + n / 2 + q; blue = 183 + n / 2 + q;
        break;
    case 15:
        red = 209 + n / 2; green = 191 + n / 2; blue = 139 + n / 2;
        if (y % 5 == 0 || (x + (y / 5 % 2) * 5) % 10 == 0) {
            red -= 23; green -= 21; blue -= 15;
        }
        break;
    case 16: case 17:
        red = 130 + n + q; green = 132 + n + q; blue = 133 + n + q;
        if (coarse % 5 == 0 && r % 5 != 0) {
            if (tile == 16) { red = 230 + n; green = 191 + n; blue = 59 + n; }
            else { red = 65 + n; green = 210 + n; blue = 200 + n; }
        }
        break;
    case 18:
        red = 115 + n + q; green = 125 + n + q; blue = 112 + n + q;
        if (coarse % 3 == 0) { red = 68 + n; green = 111 + n; blue = 54 + n; }
        break;
    case 19: case 21: case 22: {
        int seam = y % 4 == 0 || (x + 4 * (y / 4 % 2)) % 11 == 0;
        if (tile == 21) seam = y % 5 == 0 || x % 5 == 0;
        if (tile == 22) seam = seam || x < 2 || x > 13 || y < 3;
        int shade = seam ? -38 : n + q;
        red = 183 + shade; green = 131 + shade; blue = 77 + shade;
        break;
    }
    case 20: case 23: {
        int joint = y % 5 == 0 || (x + 3 * (y / 5 % 2)) % 6 == 0;
        int mouth = tile == 23 && x >= 4 && x <= 11 && (y > 8 || (y > 3 && y < 7));
        red = (mouth ? 27 : joint ? 77 : 135) + n / 2 + q;
        green = red; blue = red - 3;
        break;
    }
    case 24: case 25: case 26: case 27: case 28: case 29: {
        static const int tones[][3] = {
            {123, 82, 45}, {203, 101, 45}, {158, 97, 68},
            {130, 103, 125}, {165, 204, 235}, {115, 160, 218}
        };
        int accent = tile == 28 || tile == 29 ? ((x + y * 2) % 8 == 0 ? 18 : q) :
            (r % 7 == 0 ? q - 14 : q);
        red = tones[tile - 24][0] + n / 2 + accent;
        green = tones[tile - 24][1] + n / 2 + accent;
        blue = tones[tile - 24][2] + n / 2 + accent;
        break;
    }
    case 30: case 32: case 34: case 36: case 38: {
        static const int bark[][3] = {{208, 208, 192}, {103, 72, 50}, {147, 124, 78}, {124, 107, 98}, {79, 54, 38}};
        int accent = (int)(hash2(x / 2, tile) % 26) - 13;
        if (tile == 30) accent -= hash2(x / 3, y / 2) % 9 == 0 ? 116 : 0;
        else accent -= x % 5 == 0 ? 19 : 0;
        int k = (tile - 30) / 2;
        red = bark[k][0] + n / 2 + accent; green = bark[k][1] + n / 2 + accent;
        blue = bark[k][2] + n / 2 + accent;
        break;
    }
    case 31: case 33: case 35: case 37: case 39: {
        static const int ends[][3] = {{220, 203, 150}, {170, 123, 73}, {178, 122, 92}, {204, 105, 57}, {109, 77, 54}};
        int ring = (int)fmaxf(fabsf(x - 7.5f), fabsf(y - 7.5f));
        int accent = ring >= 7 ? -36 : ring % 3 == 0 ? -17 : 0;
        int k = (tile - 31) / 2;
        red = ends[k][0] + n / 2 + accent; green = ends[k][1] + n / 2 + accent;
        blue = ends[k][2] + n / 2 + accent;
        break;
    }
    case 40:
        red = 213 + q + n / 2; green = 195 + q + n / 2; blue = 156 + q + n / 2;
        break;
    case 41: {
        int speck = coarse % 8 == 0;
        red = (speck ? 237 : 188) + n / 2; green = (speck ? 226 : 46) + n / 2;
        blue = (speck ? 192 : 36) + n / 2;
        break;
    }
    case BROWN_MUSHROOM_TILE:
        red=164+n/2+q; green=122+n/3+q; blue=93+n/3+q;
        break;
    case 42: red = 31 + n/2 + q; green = 22 + n/3 + q/2; blue = 45 + n + q; break;
    case 43:
        red = 126 + n + q; green = 55 + n/2 + q; blue = 52 + n/2 + q;
        break;
    case 44: red=110+n+q; green=121+n+q; blue=114+n+q; break;
    case 45: {
        int edge = r % 13 < 2 ? -50 : q;
        red = 237 + n + edge; green = 183 + n + edge; blue = 109 + n + edge;
        break;
    }
    case 47:
        red = 246 + n/2; green = (x+y/2)%6 < 2 ? 162 + n + q : 79 + n + q;
        blue = 15 + n/3;
        break;
    case 54: case 55: {
        int seam = y%5 == 0 || (x+(y/5%2)*4)%8 == 0;
        red = (tile == 54 ? 133 : 98) + n/2 - 32*seam;
        green = (tile == 54 ? 135 : 55) + n/2 - 26*seam;
        blue = (tile == 54 ? 137 : 62) + n/2 - 27*seam;
        break;
    }
    default:
        if (tile>=STAINED_CLAY_TILE && tile<=STAINED_CLAY_LAST_TILE) {
            const ItemDef *def=item_def(BLACK_CLAY_ITEM+tile-STAINED_CLAY_TILE);
            unsigned pigment=def ? def->color : 0xb47452u;
            int grain=n/3+q/3;
            red=(int)((pigment>>16)&255u)+grain;
            green=(int)((pigment>>8)&255u)+grain;
            blue=(int)(pigment&255u)+grain;
        } else {
            red = 66 + n + q; green = 67 + n + q; blue = 69 + n + q;
            if (r % 7 == 0) { red += 35; green += 35; blue += 35; }
        }
        break;
    }
    pixel[0] = clamp_byte(red);
    pixel[1] = clamp_byte(green);
    pixel[2] = clamp_byte(blue);
    pixel[3] = 255;
}

static float mix_value(float a,float b,float t) { return a+(b-a)*t; }
static float taper(float x) { return x*x*(3.f-2.f*x); }
static unsigned seed_texel(int x,int y,int period,int salt) {
    int sx=(x%period+period)%period,sy=(y%period+period)%period;
    return hash2(sx+salt*23,sy+salt*47);
}
static float roughness(float u,float v,int frequency,int salt) {
    float x=u*frequency,y=v*frequency;
    int ix=(int)floorf(x),iy=(int)floorf(y);
    float fx=taper(x-ix),fy=taper(y-iy);
    float a=(float)(seed_texel(ix,iy,frequency,salt)&255u);
    float b=(float)(seed_texel(ix+1,iy,frequency,salt)&255u);
    float c=(float)(seed_texel(ix,iy+1,frequency,salt)&255u);
    float d=(float)(seed_texel(ix+1,iy+1,frequency,salt)&255u);
    return mix_value(mix_value(a,b,fx),mix_value(c,d,fx),fy)/255.f;
}

static float interior(int x,int y) {
    int a=x<TILE_EDGE-1-x ? x : TILE_EDGE-1-x;
    int b=y<TILE_EDGE-1-y ? y : TILE_EDGE-1-y;
    return taper(fminf(1.f,(a<b ? a : b)*.25f));
}

static void rgba(Uint8 *pixel,float r,float g,float b) {
    pixel[0]=clamp_byte((int)r); pixel[1]=clamp_byte((int)g); pixel[2]=clamp_byte((int)b); pixel[3]=255;
}

/* Native-size materials. A soil/grass variant changes the interior only,
   so adjacent blocks agree at their borders without repeating the same patch. */
static int ground_surface(int tile,int x,int y,Uint8 *pixel) {
    int variant=tile==TURF_ALT_A ? 1 : tile==TURF_ALT_B ? 2 : tile==SOIL_ALT ? 1 : 0;
    int turf=tile==0 || tile==TURF_ALT_A || tile==TURF_ALT_B;
    int edge=tile==1,soil=tile==2 || tile==SOIL_ALT,leaf=tile==6;
    int sand=tile==8 || tile==25,gravel=tile==9;
    if (!(turf || edge || soil || leaf || sand || gravel)) return 0;
    float u=(x+.5f)/TILE_EDGE,v=(y+.5f)/TILE_EDGE;
    float fine=roughness(u,v,24,47), grains=roughness(u,v,11,91), patch=roughness(u,v,4,13);
    if (variant) {
        float weight=interior(x,y);
        fine=mix_value(fine,roughness(u,v,24,47+variant*7),weight);
        grains=mix_value(grains,roughness(u,v,11,91+variant*9),weight);
        patch=mix_value(patch,roughness(u,v,4,13+variant*13),weight*.85f);
    }
    float detail=(fine-.5f)*21.f+(grains-.5f)*18.f+(patch-.5f)*13.f;
    if (turf || edge) {
        /* Thin blade highlights over mixed olive, moss and dry straw. */
        float stalk=roughness(3.f*u,v,13,109);
        float straw=fmaxf(0.f,patch+fine-.99f)*32.f;
        float glint=fmaxf(0.f,stalk-.62f)*29.f;
        float r=87+detail+straw+glint*.55f,g=111+detail+glint,b=61+detail*.55f-straw*.25f;
        if (edge) {
            float line=.16f+.21f*roughness(u,0,7,19)+.08f*roughness(u,0,13,21);
            float w=taper(fmaxf(0.f,fminf(1.f,(line-v)*14.f+.5f)));
            r=mix_value(104+detail,r,w); g=mix_value(81+detail*.85f,g,w); b=mix_value(61+detail*.63f,b,w);
        }
        rgba(pixel,r,g,b);
    } else if (leaf) {
        float clusters=roughness(u,v,7,207), tips=roughness(u,v,19,211);
        float lobes=fmaxf(0.f,clusters-.35f)*37.f+fmaxf(0.f,tips-.54f)*18.f;
        float recess=fmaxf(0.f,.42f-clusters)*57.f;
        rgba(pixel,45+detail*.62f+lobes-recess,71+detail*.7f+1.26f*lobes-recess,43+detail*.4f+lobes*.4f-recess*.7f);
    } else if (soil) {
        float flint=fmaxf(0.f,fine+grains-1.43f)*44.f;
        rgba(pixel,104+detail+flint,81+detail*.85f+flint,61+detail*.63f+flint*.88f);
    } else if (sand) {
        float wind=7.f*sinf(6.283185307f*(3.f*v+u+(patch-.5f)*.24f));
        if (tile==25) rgba(pixel,176+detail*.64f+wind,117+detail*.59f+wind,74+detail*.52f+wind);
        else rgba(pixel,197+detail*.69f+wind,181+detail*.63f+wind,143+detail*.52f+wind);
    } else {
        float pebbles=fmaxf(0.f,grains-.5f)*56.f-fmaxf(0.f,.39f-grains)*46.f;
        rgba(pixel,116+detail*.76f+pebbles,112+detail*.71f+pebbles,105+detail*.66f+pebbles);
    }
    return 1;
}

static int timber_surface(int tile,int x,int y,Uint8 *pixel) {
    const int plank=tile==19 || (tile>=BUILD_PLANK_TILE && tile<BUILD_BRICK_TILE);
    const int log=tile==4 || tile==5 || (tile>=30 && tile<=39);
    if (!plank && !log) return 0;
    int species=tile>=BUILD_PLANK_TILE ? tile-BUILD_PLANK_TILE+1 : tile>=30 && tile<=39 ? (tile-30)/2+1 : 0;
    static const float bark[6][3]={{103,86,60},{166,160,139},{79,71,56},{114,89,66},{114,100,83},{64,55,45}};
    static const float wood[6][3]={{161,127,88},{199,179,134},{141,103,68},{170,123,87},{173,122,79},{111,76,50}};
    float u=(x+.5f)/TILE_EDGE,v=(y+.5f)/TILE_EDGE;
    float warp=roughness(u,v,4,119+species), fleck=roughness(u,v,25,157+species);
    const int cut=log && (tile==5 || (tile>=30 && (tile&1)));
    float d=0;
    if (cut) {
        float bu=u-.49f,bv=v-.53f;
        float rings=hypotf(bu,bv*.92f)*13.f+(warp-.5f)*.46f;
        rings-=floorf(rings);
        d=(fleck-.5f)*17.f+(warp-.5f)*23.f-21.f*fmaxf(0.f,1.f-fabsf(rings-.76f)*8.f);
        if (fmaxf(fabsf(bu),fabsf(bv))>.46f) d-=25.f;
    } else {
        float p=plank ? v : u,q=plank ? u : v;
        float grain=roughness(p*3.f+(warp-.5f)*.06f,q,9,169+species);
        float channels=fmaxf(0.f,.39f-grain);
        d=(grain-.5f)*(plank ? 26.f : 49.f)+(fleck-.5f)*17.f-channels*(plank ? 17.f : 46.f);
        if (plank) {
            int row=y/(TILE_EDGE/4),course=y%(TILE_EDGE/4);
            int splice=(x+(row&1)*(TILE_EDGE/2))%TILE_EDGE;
            d+=(row-1)*3.f-(course==0 ? 34.f : course==1 ? -9.f : 0.f)-(splice==0 ? 20.f : 0.f);
        } else if (species==1 && grain<.37f && roughness(v*3.f,u,5,127)>.55f) d-=36.f;
    }
    const float *c=(cut || plank) ? wood[species] : bark[species];
    rgba(pixel,c[0]+d,c[1]+d*.86f,c[2]+d*.73f);
    return 1;
}

static int mineral_surface(int tile,int x,int y,Uint8 *pixel) {
    int variant=tile==STONE_ALT_A ? 1 : tile==STONE_ALT_B ? 2 : 0;
    if (variant) tile=3;
    int base[3];
    switch (tile) {
    case 2: base[0]=95; base[1]=76; base[2]=60; break;
    case 3: base[0]=120; base[1]=129; base[2]=134; break;
    case 7: base[0]=67; base[1]=75; base[2]=84; break;
    case 15: base[0]=182; base[1]=155; base[2]=111; break;
    case 26: base[0]=151; base[1]=88; base[2]=66; break;
    case 42: base[0]=55; base[1]=56; base[2]=69; break;
    case SLATE_TILE: base[0]=75; base[1]=89; base[2]=111; break;
    case LIMESTONE_TILE: base[0]=193; base[1]=178; base[2]=146; break;
    case LAMINATE_TILE: base[0]=138; base[1]=157; base[2]=150; break;
    default: return 0;
    }
    const float u=(x+.5f)/TILE_EDGE,v=(y+.5f)/TILE_EDGE;
    float grain=roughness(u,v,18,tile+11), mottles=roughness(u,v,7,tile+2);
    float deposits=roughness(u,v,3,tile+67);
    if (variant) {
        float weight=interior(x,y);
        grain=mix_value(grain,roughness(u,v,18,231+variant*17),weight);
        mottles=mix_value(mottles,roughness(u,v,7,238+variant*13),weight);
        deposits=mix_value(deposits,roughness(u,v,3,251+variant*11),weight);
    }
    /* Warped but periodic sediment bands. Wide variation and fine inclusions
       are generated independently at atlas resolution, not upsampled. */
    float band=v*(tile==15 ? 7.f : 5.f)+u+(deposits-.5f)*.75f;
    band-=floorf(band);
    float bedding=fmaxf(0.f,1.f-fabsf(band-.42f)*8.f);
    float seam=fmaxf(0.f,1.f-fabsf(band-.12f)*27.f);
    float fine=(grain-.5f)*17.f+(mottles-.5f)*25.f+(deposits-.5f)*21.f;
    float shade=tile==15 || tile==26 ? 15.f*bedding-28.f*seam : 7.f*bedding-13.f*seam;
    if (tile==SLATE_TILE) shade+=17.f*bedding-29.f*seam;
    if (tile==LIMESTONE_TILE)
        shade+=20.f*fmaxf(0.f,mottles-.43f)-48.f*fmaxf(0.f,grain-.63f);
    if (tile==LAMINATE_TILE) {
        int seam_x=(x+((y/(TILE_EDGE/2))&1)*TILE_EDGE/2)%TILE_EDGE;
        int seam_y=y%(TILE_EDGE/2);
        shade=seam_x==0 || seam_y==0 ? -31.f : seam_x==1 || seam_y==1 ? 17.f : 4.f*bedding;
    }
    if (tile==42) shade+=22.f*fmaxf(0.f,mottles-.52f);
    if (tile==2) shade=7.f*bedding-15.f*(grain>.77f);
    float mineral=(deposits-.5f)*(tile==3 || tile==42 ? 15.f : 7.f);
    pixel[0]=clamp_byte((int)(base[0]+fine+shade-mineral));
    pixel[1]=clamp_byte((int)(base[1]+fine+shade));
    pixel[2]=clamp_byte((int)(base[2]+fine+shade+mineral));
    pixel[3]=255;
    return 1;
}

void texel(int tile,int x,int y,Uint8 *pixel) {
    if (tile==SHELL_MARKER_TILE) {
        int grain=(int)(hash2(x+633,y-23)%13u)-6;
        const int dx=abs(x-15),dy=abs(y-15),r=dx*dx+dy*dy;
        int light=(r>=94 && r<=152) || dx<=2 || dy<=1;
        int inlay=dx<=12 && dy<=12 && light;
        pixel[0]=clamp_byte((inlay ? 232 : 72)+grain);
        pixel[1]=clamp_byte((inlay ? 171 : 79)+grain);
        pixel[2]=clamp_byte((inlay ? 82 : 89)+grain);
        pixel[3]=255;
        return;
    }
    if (tile==PRESSURE_LIFT_TILE || tile==PRESSURE_CLOSED_TILE || tile==SINTERED_MATERIAL_TILE) {
        int grain=(int)(hash2(x+tile*37,y+tile*61)%19u)-9;
        if (tile==SINTERED_MATERIAL_TILE) {
            const int joint=y%11==0 || (x+(y/11)*9)%16==0;
            const int tone=joint ? 65 : 117+((x+2*y)%23==0)*31+grain;
            pixel[0]=clamp_byte(tone+12); pixel[1]=clamp_byte(tone+15); pixel[2]=clamp_byte(tone+6);
        } else {
            const int dx=x-16,dy=y-16,r=dx*dx+dy*dy;
            const int rim=x<2 || y<2 || x>=30 || y>=30;
            const int brass=r>=76 && r<=139;
            const int mechanism=tile==PRESSURE_LIFT_TILE ? abs(dx)<=2 && abs(dy)<=7 : abs(dx*2+dy)<4 && r<=72;
            const int plate=(rim ? 27 : r<145 ? 53 : 82)+grain;
            pixel[0]=clamp_byte(brass || mechanism ? 201+grain : plate);
            pixel[1]=clamp_byte(brass || mechanism ? 143+grain : plate+8);
            pixel[2]=clamp_byte(brass || mechanism ? 63+grain : plate+17);
        }
        pixel[3]=255;
        return;
    }
    if (tile>=WINCH_TILE && tile<=CONDUIT_TILE) {
        static const int bands[5][3]={{82,158,131},{133,189,159},{70,167,211},{220,131,63},{170,234,199}};
        const int *c=bands[tile-WINCH_TILE];
        int border=x<3 || y<3 || x>=TILE_EDGE-3 || y>=TILE_EDGE-3;
        int screw=(abs(x-4)<=1 || abs(x-(TILE_EDGE-5))<=1) &&
            (abs(y-4)<=1 || abs(y-(TILE_EDGE-5))<=1);
        int window=abs(x-16)<=8 && (tile==CONDUIT_TILE ? y>=9 && y<=23 :
            abs(y-15)<=6 && abs(x-16)+abs(y-15)<13);
        int stripe=window && (y%5==0 || (tile==CONDUIT_TILE && x%6==0));
        int grain=(int)(hash2(x+tile*31,y+tile*67)%13u)-6;
        for (int i=0;i<3;++i)
            pixel[i]=clamp_byte((screw ? 134 : border ? 29 : window ? c[i]-stripe*22 :
                37+c[i]/4+(y/4)%2*9)+grain);
        pixel[3]=255;
        return;
    }
    if (tile>=SEEP_BED_ATLAS && tile<=SEEP_BRIGHT_ATLAS) {
        int border=x<2 || y<2 || x>=30 || y>=30;
        float u=(x+.5f)/TILE_EDGE,v=(y+.5f)/TILE_EDGE;
        float d=(roughness(u,v,17,tile)-.5f)*14.f;
        int bed=tile==SEEP_BED_ATLAS,bright=tile==SEEP_BRIGHT_ATLAS;
        int channel=x>=4 && x<28 && y>=5 && y<27 && ((x+y/5)%8<2);
        int fleck=hash2(x+317,y*17+tile)%13u<2u;
        if (border) rgba(pixel,47+d,64+d,66+d);
        else if (bed) rgba(pixel,channel ? 51+d : 120+d,
            channel ? 142+d+fleck*21 : 101+d,channel ? 147+d : 78+d);
        else {
            float band=channel ? 29.f : fleck ? 12.f : 0.f;
            rgba(pixel,(bright ? 135 : 56)+band+d,(bright ? 188 : 142)+band+d,
                (bright ? 112 : 137)+band+d);
        }
        return;
    }
    if (tile==SETTLING_FRAME_ATLAS) {
        int lip=x<4 || x>=28 || y<4 || y>=28;
        int drain=!lip && ((x+3)%8<2) && y>=6 && y<26;
        int rivet=(x==1 || x==30) && (y==2 || y==29);
        int fiber=(int)(hash2(x*4+y/3,y)%15u)-7;
        if (rivet) rgba(pixel,183,158,117);
        else if (lip) rgba(pixel,164+fiber,115+fiber,73+fiber);
        else if (drain) rgba(pixel,46+fiber,33+fiber,26+fiber);
        else rgba(pixel,112+fiber,80+fiber,56+fiber);
        return;
    }
    if (tile>=CISTERN_CASING_ATLAS && tile<=CISTERN_FULL_ATLAS) {
        int d=(int)(hash2(x+411,y-37)%11u)-5;
        int edge=x<3 || y<3 || x>=29 || y>=29;
        int bolts=(x==2 || x==29) && (y==2 || y==29);
        if (bolts) rgba(pixel,214+d,181+d,125+d);
        else if (tile==CISTERN_CASING_ATLAS) {
            int seam=y==6 || y==26 || x==5 || x==26;
            int tap=x>=13 && x<=18 && y>=13 && y<=20;
            if (tap) rgba(pixel,64+d,119+d,109+d);
            else if (seam || edge) rgba(pixel,76+d,80+d,71+d);
            else rgba(pixel,146+d,132+d,112+d);
        } else if (edge || x<5 || y<5 || x>=27 || y>=27)
            rgba(pixel,edge ? 148+d : 90+d,edge ? 127+d : 92+d,edge ? 100+d : 83+d);
        else if (tile==CISTERN_DRY_ATLAS) {
            int slat=(x+1)%6<2 || (y+2)%6<2;
            rgba(pixel,slat ? 82+d : 39+d,slat ? 126+d : 50+d,slat ? 116+d : 50+d);
        } else {
            int glint=(x+3*y+(int)(hash2(y,x)%5u))%17==0;
            int full=tile==CISTERN_FULL_ATLAS;
            rgba(pixel,40+d+15*glint,91+d+31*full+23*glint,112+d+40*full+16*glint);
        }
        return;
    }
    if (!ground_surface(tile,x,y,pixel) && !timber_surface(tile,x,y,pixel) && !mineral_surface(tile,x,y,pixel))
        legacy_texel(tile,x*16/TILE_EDGE,y*16/TILE_EDGE,pixel);
}

/* Keep the ordering in both Metal material address helpers in sync. */
static const int detail_tiles[ATLAS_DETAIL_COUNT]={3,15,26,42,STONE_ALT_A,STONE_ALT_B,SLATE_TILE,LIMESTONE_TILE};
static int detail_slot(int tile) {
    for (int slot=0;slot<ATLAS_DETAIL_COUNT;++slot) if (detail_tiles[slot]==tile) return slot;
    return -1;
}
static float positive(float v) { return fmaxf(0.f,v); }

/* Independent native samples: wide faults, material-specific beds and fine
   inclusions. This RGB relief stays subtle under the real shadow pass. */
static void detail_rock(int tile,int x,int y,Uint8 *out) {
    int variant=tile==STONE_ALT_A ? 1 : tile==STONE_ALT_B ? 2 : 0;
    if (variant) tile=3;
    const float u=(x+.5f)/ATLAS_DETAIL_EDGE,v=(y+.5f)/ATLAS_DETAIL_EDGE;
    const float shape=roughness(u,v,4,271+tile),warp=roughness(u,v,8,197+tile);
    float fragments=roughness(u,v,17,359+tile),sand=roughness(u,v,47,519+tile);
    if (variant) {
        int border=x<ATLAS_DETAIL_EDGE-1-x ? x : ATLAS_DETAIL_EDGE-1-x;
        int vz=y<ATLAS_DETAIL_EDGE-1-y ? y : ATLAS_DETAIL_EDGE-1-y;
        float weight=taper(fminf(1.f,positive((border<vz ? border : vz)-1)*.14285715f));
        if (weight>0.f) {
            fragments=mix_value(fragments,roughness(u,v,17,645+variant*11),weight);
            sand=mix_value(sand,roughness(u,v,47,733+variant*13),weight);
        }
    }
    float r,g,b,d,tint;
    const float mineral=(shape-.5f)*19.f+(warp-.5f)*15.f;
    if (tile==15 || tile==26) {
        const float f=7.f*v+u+.42f*(shape-.5f)+.15f*(warp-.5f);
        const float layer=f-floorf(f);
        const float seams=positive(1.f-fabsf(layer-.18f)*15.f);
        const float streak=positive(1.f-fabsf(layer-.42f)*4.f);
        d=mineral+(fragments-.5f)*15.f+(sand-.5f)*18.f-24.f*seams+13.f*streak;
        tint=9.f*(layer-.5f);
        r=tile==15 ? 185.f : 152.f; g=tile==15 ? 158.f : 94.f; b=tile==15 ? 115.f : 71.f;
    } else if (tile==SLATE_TILE) {
        const float f=9.f*v+2.f*u+.48f*(shape-.5f)+.27f*(warp-.5f)+.09f*(fragments-.5f);
        const float layer=f-floorf(f);
        const float split=positive(1.f-fabsf(layer-.17f)*18.f);
        const float edge=positive(1.f-fabsf(layer-.27f)*11.f);
        d=mineral+(fragments-.5f)*14.f+(sand-.5f)*12.f+20.f*edge-34.f*split;
        tint=(warp-.5f)*10.f; r=78.f; g=92.f; b=113.f;
    } else if (tile==LIMESTONE_TILE) {
        const float pores=positive((fragments*.7f+sand*.3f)-.59f);
        const float pale=positive(.51f-fragments)*16.f;
        d=mineral+(sand-.5f)*18.f-95.f*pores+pale;
        tint=(shape-.5f)*17.f; r=193.f; g=178.f; b=146.f;
    } else if (tile==42) {
        /* Fractured volcanic glass: quiet faces, highlights confined to the
           fracture shoulders rather than bright repeated mineral patches. */
        const float f=warp*.76f+fragments*.24f;
        const float fracture=positive(1.f-fabsf(f-.42f)*26.f);
        const float lip=positive(1.f-fabsf(f-.46f)*19.f);
        d=mineral*.42f+(fragments-.5f)*11.f+(sand-.5f)*10.f-13.f*fracture+13.f*lip;
        tint=(shape-.5f)*6.f; r=51.f; g=55.f; b=67.f;
    } else {
        const float field=warp*.69f+fragments*.31f;
        const float seam=positive(1.f-fabsf(field-.48f)*15.f);
        const float edge=positive(1.f-fabsf(field-.57f)*10.f);
        d=mineral+(fragments-.5f)*26.f+(sand-.5f)*14.f-17.f*seam+11.f*edge;
        tint=(warp-.5f)*14.f;
        r=122.f; g=130.f; b=134.f;
    }
    rgba(out,r+d+tint,g+d,b+d-tint*.75f);
}

static Uint8 *atlas_row(Uint8 *p,int level,int tile,int y) {
    static const int tops[]={0,64,96,104,108};
    const int edge=TILE_EDGE>>level;
    const int bank_width=level ? ATLAS_WIDTH-ATLAS_DETAIL_WIDTH : ATLAS_WIDTH;
    const int columns=bank_width/edge;
    const int row=tops[level]+(tile/columns)*edge+y;
    const int x=(level ? ATLAS_DETAIL_WIDTH : 0)+(tile%columns)*edge;
    return p+((size_t)row*ATLAS_WIDTH+x)*4u;
}
_Static_assert(ATLAS_TILES<=ATLAS_WIDTH/TILE_EDGE*2 &&
    ATLAS_TILES<=(ATLAS_WIDTH-ATLAS_DETAIL_WIDTH)/(TILE_EDGE/2)*2 &&
    ATLAS_TILES<=(ATLAS_WIDTH-ATLAS_DETAIL_WIDTH)/(TILE_EDGE/4) &&
    ATLAS_DETAIL_Y+ATLAS_DETAIL_EDGE<=ATLAS_HEIGHT,
    "Every detail and filtered tile must fit inside its bank");
static Uint8 *detail_row(Uint8 *p,int slot,int y) {
    return p+((size_t)(ATLAS_DETAIL_Y+y)*ATLAS_WIDTH+slot*ATLAS_DETAIL_EDGE)*4u;
}
static Uint8 blended_channel(const Uint8 *a,const Uint8 *b,int x,int component) {
    int k=8*x+component;
    return (Uint8)((a[k]+a[k+4]+b[k]+b[k+4]+2)/4);
}
static void half_row(Uint8 *row,const Uint8 *a,const Uint8 *b,int edge) {
    for (int x=0;x<edge;++x) for (int c=0;c<4;++c)
        row[4*x+c]=blended_channel(a,b,x,c);
}
static int reserve_test_row(Uint8 *marks,const Uint8 *atlas,const Uint8 *row,int edge) {
    size_t first=(size_t)(row-atlas)/4u;
    if (first>=ATLAS_WIDTH*ATLAS_HEIGHT || first%ATLAS_WIDTH+(size_t)edge>ATLAS_WIDTH) return 1;
    int failures=0;
    for (int x=0;x<edge;++x) {
        failures+=marks[first+(size_t)x]!=0 || row[x*4+3]!=255;
        marks[first+(size_t)x]=1;
    }
    return failures;
}
int material_atlas_build(Uint8 *pixels,Uint32 byte_count) {
    const Uint32 required=ATLAS_WIDTH*ATLAS_HEIGHT*4u;
    if (!pixels || byte_count<required) return 0;
    memset(pixels,0,required);
    for (int slot=0;slot<ATLAS_DETAIL_COUNT;++slot) for (int y=0;y<ATLAS_DETAIL_EDGE;++y) {
        Uint8 *row=detail_row(pixels,slot,y);
        for (int x=0;x<ATLAS_DETAIL_EDGE;++x) detail_rock(detail_tiles[slot],x,y,row+x*4);
    }
    for (int tile=0;tile<ATLAS_TILES;++tile) {
        const int slot=detail_slot(tile);
        for (int y=0;y<TILE_EDGE;++y) {
            Uint8 *row=atlas_row(pixels,0,tile,y);
            if (slot>=0)
                half_row(row,detail_row(pixels,slot,2*y),detail_row(pixels,slot,2*y+1),TILE_EDGE);
            else
                for (int x=0;x<TILE_EDGE;++x) texel(tile,x,y,row+x*4);
        }
    }
    /* Pack the downsampled tiles at their true widths. Shader addresses are
       clamped to half-texel centers, so filtering cannot enter adjacent tiles. */
    for (int m=1;m<ATLAS_LEVELS;++m) {
        int edge=TILE_EDGE>>m;
        for (int y=0;y<edge;++y) for (int tile=0;tile<ATLAS_TILES;++tile) {
            const Uint8 *a=atlas_row(pixels,m-1,tile,2*y),*b=atlas_row(pixels,m-1,tile,2*y+1);
            Uint8 *row=atlas_row(pixels,m,tile,y);
            half_row(row,a,b,edge);
        }
    }
    return 1;
}

int material_atlas_selftest(void) {
    const Uint32 bytes=ATLAS_WIDTH*ATLAS_HEIGHT*4u;
    Uint8 *p=(Uint8 *)malloc(bytes);
    Uint8 *marks=(Uint8 *)calloc(ATLAS_WIDTH*ATLAS_HEIGHT,1);
    if (!p || !marks) { free(p); free(marks); return 1; }
    int failures=!!material_atlas_build(p,bytes-1) || !material_atlas_build(p,bytes);
    for (int m=0;m<ATLAS_LEVELS;++m) {
        int edge=TILE_EDGE>>m;
        for (int tile=0;tile<ATLAS_TILES;++tile) for (int y=0;y<edge;++y)
            failures+=reserve_test_row(marks,p,atlas_row(p,m,tile,y),edge);
    }
    for (int i=0;i<ATLAS_DETAIL_COUNT;++i) for (int y=0;y<ATLAS_DETAIL_EDGE;++y)
        failures+=reserve_test_row(marks,p,detail_row(p,i,y),ATLAS_DETAIL_EDGE);
    free(marks);
    for (int m=1;m<ATLAS_LEVELS;++m) {
        int edge=TILE_EDGE>>m;
        for (int y=0;y<edge;++y) for (int tile=0;tile<ATLAS_TILES;++tile) {
            const Uint8 *a=atlas_row(p,m-1,tile,2*y),*b=atlas_row(p,m-1,tile,2*y+1);
            const Uint8 *row=atlas_row(p,m,tile,y);
            for (int x=0;x<edge;++x) for (int i=0;i<4;++i) {
                int k=8*x+i;
                failures+=row[4*x+i]!=(a[k]+a[k+4]+b[k]+b[k+4]+2)/4;
            }
        }
    }
    int detail_groups=0;
    for (int slot=0;slot<ATLAS_DETAIL_COUNT;++slot) {
        int distinct=0;
        for (int y=0;y<TILE_EDGE;++y) {
            const Uint8 *a=detail_row(p,slot,2*y),*b=detail_row(p,slot,2*y+1);
            const Uint8 *base=atlas_row(p,0,detail_tiles[slot],y);
            for (int x=0;x<TILE_EDGE;++x) {
                distinct+=memcmp(a+8*x,a+8*x+4,4)!=0 || memcmp(a+8*x,b+8*x,4)!=0;
                for (int c=0;c<4;++c) failures+=base[4*x+c]!=blended_channel(a,b,x,c);
            }
        }
        detail_groups+=distinct;
        failures+=distinct<TILE_EDGE*TILE_EDGE*3/4;
    }
    static const int pairs[][2]={{0,TURF_ALT_A},{0,TURF_ALT_B},{2,SOIL_ALT},{3,STONE_ALT_A},{3,STONE_ALT_B}};
    for (unsigned j=0;j<sizeof pairs/sizeof pairs[0];++j) {
        int changed=0;
        for (int y=0;y<TILE_EDGE;++y) for (int x=0;x<TILE_EDGE;++x) {
            const Uint8 *a=atlas_row(p,0,pairs[j][0],y)+x*4;
            const Uint8 *b=atlas_row(p,0,pairs[j][1],y)+x*4;
            int distinct=memcmp(a,b,4)!=0;
            changed+=distinct;
            if (x==0 || y==0 || x==TILE_EDGE-1 || y==TILE_EDGE-1) failures+=distinct;
        }
        failures+=changed<300;
    }
    free(p);
    printf("MATERIAL ATLAS TEST packed levels=%d bytes=%u native-64 materials=%d distinct 2x2 groups=%d failures=%d\n",
        ATLAS_LEVELS,bytes,ATLAS_DETAIL_COUNT,detail_groups,failures);
    return failures!=0;
}
