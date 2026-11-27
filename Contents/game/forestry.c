#include "forestry.h"
#include "actors.h"
#include "boats.h"
#include "drops.h"
#include "enchanting.h"
#include "experience.h"
#include "horses.h"
#include "minecarts.h"
#include "../world/light.h"
#include "../world/space.h"
#include <stdlib.h>

enum { TREE_EDGE=13, TREE_ORIGIN=5, TREE_HEIGHT=20, TREE_EDIT_LIMIT=512 };
enum { CROWN=1, TRUNK=2 };
typedef struct { uint8_t cells[TREE_HEIGHT][TREE_EDGE][TREE_EDGE]; } TreeShape;
typedef struct { int x,y,z,wide,species; } Planting;

int tree_soil(int type) { return type==DIRT || type==GRASS || type==PODZOL || type==FARMLAND; }
int tree_sapling_drop(int raw) { return sapling_cell(raw) ? OAK_SAPLING+sapling_species(raw) : AIR; }
static int leaf_item(int species) { return species==TREE_OAK ? LEAF : BIRCH_LEAF_ITEM+species-1; }
static int harvested_species(int raw,int x,int y,int z) {
    if (raw!=LEAF || world_generation()>=GENERATOR_FORESTRY) return leaf_species(raw);
    /* Old worlds used a generic leaf. Identify the nearest living trunk
       without mutating these maps or loading their neighboring chunks. */
    int nearest=100,species=TREE_OAK;
    for (int a=-4;a<=4;++a) for (int b=-4;b<=4;++b) for (int h=-5;h<=2;++h) {
        int score=a*a+b*b+h*h,v;
        if (score>=nearest || !world_resident_cell(x+a,y+h,z+b,&v)) continue;
        int id=tree_log_species(cell_type(v));
        if (id>=0) { nearest=score; species=id; }
    }
    return species;
}
int tree_leaf_drop(int raw,int x,int y,int z,ItemStack tool) {
    int species=harvested_species(raw,x,y,z);
    if (tool.type==SHEARS || enchant_level(tool.enchants,ENCH_SILK_TOUCH)) return leaf_item(species);
    unsigned result=hash2(x+31*y,z-29*y)^((unsigned)y*0x9e3779b9u);
    int fortune=enchant_level(tool.enchants,ENCH_FORTUNE);
    unsigned odds=species==TREE_JUNGLE ? 40u : 20u;
    if (fortune>0) {
        unsigned bonus=2u<<fortune;
        odds=odds>bonus+10u ? odds-bonus : 10u;
    }
    if (result%odds==0) return OAK_SAPLING+species;
    if ((species==TREE_OAK || species==TREE_DARK_OAK) && (result>>10)%200u==0) return APPLE;
    return AIR;
}
const char *tree_use(Hit hit) {
    if (!hit.hit || (unsigned)selected_slot>=HOTBAR_SIZE) return NULL;
    ItemStack *held=&inventory[selected_slot];
    if (!held->count || !sapling_item(held->type)) return NULL;
    int x=hit.x,y=hit.y,z=hit.z;
    if (hit.ay!=y+1 || y+1>=WY || !tree_soil(block(x,y,z)) || block(x,y+1,z)!=AIR)
        return "树苗需要种在无遮挡的泥土、草地、灰化土或耕地顶面";
    BlockEdit e={x,y+1,z,SAPLING_BASE+2*(held->type-OAK_SAPLING)};
    if (!world_set_blocks(&e,1)) return "暂时无法种植树苗";
    if (player_rules.mode==SURVIVAL && !--held->count) *held=(ItemStack){0,0,0,0,0};
    return "已种树苗";
}
static int plant_at(int x,int y,int z,int species) {
    int v,soil;
    return world_resident_cell(x,y,z,&v) && sapling_cell(v) && sapling_species(v)==species &&
        world_resident_cell(x,y-1,z,&soil) && tree_soil(cell_type(soil));
}
static int planting_at(int x,int y,int z,int species,Planting *p) {
    *p=(Planting){x,y,z,0,species};
    if (species!=TREE_DARK_OAK && species!=TREE_SPRUCE && species!=TREE_JUNGLE) return 1;
    for (int a=-1;a<=0;++a) for (int b=-1;b<=0;++b)
        if (plant_at(x+a,y,z+b,species) && plant_at(x+a+1,y,z+b,species) &&
            plant_at(x+a,y,z+b+1,species) && plant_at(x+a+1,y,z+b+1,species)) {
            p->x+=a; p->z+=b; p->wide=1;
            return 1;
        }
    return species!=TREE_DARK_OAK;
}
static void paint(TreeShape *s,int a,int h,int b,unsigned type) {
    a+=TREE_ORIGIN; b+=TREE_ORIGIN;
    if ((unsigned)a>=TREE_EDGE || (unsigned)b>=TREE_EDGE || (unsigned)h>=TREE_HEIGHT) return;
    if (s->cells[h][a][b]<type) s->cells[h][a][b]=(uint8_t)type;
}
static void crown(TreeShape *s,int x,int y,int z,int r,int width) {
    for (int a=-r;a<=r+width;++a) for (int b=-r;b<=r+width;++b) {
        int edge_x=a==-r || a==r+width,edge_z=b==-r || b==r+width;
        if (!(r>1 && edge_x && edge_z)) paint(s,x+a,y,z+b,CROWN);
    }
}
static void shape(TreeShape *s,Planting p) {
    unsigned key=hash2(p.x+19*p.y,p.z-13*p.y);
    static const int short_height[]={5,7,8,8,6,7},wide_height[]={5,7,14,14,6,7};
    int a=0,b=0,wide=p.wide,h=(wide ? wide_height : short_height)[p.species]+(int)(key&1u);
    int axis=(key>>1)&1u,step=(key&4u) ? 1 : -1,dx=axis ? step : 0,dz=axis ? 0 : step;
    for (int y=0;y<h;++y) {
        if ((p.species==TREE_ACACIA || p.species==TREE_DARK_OAK) && y==h-3) {
            a=dx; b=dz;
            for (int i=0;i<=wide;++i) for (int j=0;j<=wide;++j) paint(s,i,y,j,TRUNK);
        }
        for (int i=0;i<=wide;++i) for (int j=0;j<=wide;++j) paint(s,a+i,y,b+j,TRUNK);
    }
    switch (p.species) {
    case TREE_SPRUCE:
        for (int y=wide ? h-8 : h-6;y<h;++y)
            crown(s,0,y,0,y>=h-2 ? 1 : (wide && (h-y)%3==0 ? 3 : (h-y)%2+1),wide);
        paint(s,0,h,0,CROWN);
        break;
    case TREE_JUNGLE:
        if (wide) {
            for (int j=0;j<3;++j) paint(s,-dx*j,h-5+j,-dz*j,TRUNK);
            crown(s,-2*dx,h-3,-2*dz,2,0); crown(s,-2*dx,h-2,-2*dz,1,0);
        }
        crown(s,0,h-2,0,wide ? 3 : 2,wide);
        crown(s,0,h-1,0,wide ? 3 : 2,wide);
        crown(s,0,h,0,1,wide);
        break;
    case TREE_ACACIA:
        paint(s,-dx,h-3,-dz,TRUNK); paint(s,-2*dx,h-2,-2*dz,TRUNK);
        crown(s,-2*dx,h-2,-2*dz,2,0); crown(s,-2*dx,h-1,-2*dz,1,0);
        crown(s,a,h-1,b,3,0); crown(s,a,h,b,2,0);
        break;
    case TREE_DARK_OAK:
        paint(s,a-dx,h-3,b-dz,TRUNK); paint(s,a+1+dx,h-3,b+1+dz,TRUNK);
        crown(s,a,h-2,b,3,wide); crown(s,a,h-1,b,3,wide); crown(s,a,h,b,2,wide);
        break;
    default:
        crown(s,0,h-3,0,2,0); crown(s,0,h-2,0,2,0);
        crown(s,0,h-1,0,1,0); crown(s,0,h,0,p.species==TREE_BIRCH ? 1 : 0,0);
        break;
    }
}
static int lit(int x,int y,int z) {
    return (player_rules.day_tick<12000 && light_has_open_sky(x,y,z)) || block_light_at_resident(x,y,z)>=9;
}
static int grow_tree(Planting p,int fertilize) {
    TreeShape s={0};
    shape(&s,p);
    BlockEdit e[TREE_EDIT_LIMIT];
    BlockBox player=body_box(px,py,pz,.28f,1.72f);
    int n=0;
    for (int h=0;h<TREE_HEIGHT;++h) for (int a=0;a<TREE_EDGE;++a) for (int b=0;b<TREE_EDGE;++b) {
        unsigned part=s.cells[h][a][b];
        if (!part) continue;
        int x=p.x+a-TREE_ORIGIN,y=p.y+h,z=p.z+b-TREE_ORIGIN,raw;
        if (!inworld(x,y,z) || !world_resident_cell(x,y,z,&raw)) return 0;
        int root=h==0 && x>=p.x && x<=p.x+p.wide && z>=p.z && z<=p.z+p.wide &&
            sapling_cell(raw) && sapling_species(raw)==p.species;
        /* Keep adjoining foliage and player-placed leaf decorations intact. */
        if (part==CROWN && cell_type(raw)==LEAF) continue;
        if (!root && raw!=AIR && raw!=TALL_GRASS && raw!=SNOW_LAYER &&
            (cell_type(raw)!=LEAF || placed_leaf_cell(raw))) return 0;
        if (box_overlap(player,(BlockBox){(float)x,(float)y,(float)z,x+1.f,y+1.f,z+1.f}) ||
            actors_overlap_cell(x,y,z) || horse_overlaps_cell(x,y,z) ||
            boat_overlaps_cell(x,y,z) || cart_overlaps_cell(x,y,z)) return 0;
        if (n==TREE_EDIT_LIMIT) return 0;
        e[n++]=(BlockEdit){x,y,z,part==TRUNK ? tree_log_type(p.species) : natural_leaf(p.species)};
    }
    if (!n) return 0;
    if (fertilize && player_rules.mode==SURVIVAL && experience_roll(0,99)>=45u) return TREE_BONE_TRIED;
    return world_set_blocks(e,n) ? TREE_BONE_GROWN : 0;
}
/* 0 = not a plant, negative = unchanged/blocked, positive = fertilizer used. */
int tree_bonemeal(int x,int y,int z) {
    int raw,soil;
    if (!world_resident_cell(x,y,z,&raw) || !sapling_cell(raw)) return 0;
    if (!world_resident_cell(x,y-1,z,&soil) || !tree_soil(cell_type(soil)) || !lit(x,y,z)) return -1;
    Planting p;
    if (!planting_at(x,y,z,sapling_species(raw),&p)) return -1;
    if (!((raw-SAPLING_BASE)&1)) {
        if (player_rules.mode==SURVIVAL && experience_roll(0,99)>=45u) return TREE_BONE_TRIED;
        return world_set_farm_stage(x,y,z,raw+1) ? TREE_BONE_STAGE : -1;
    }
    int growth=grow_tree(p,1);
    return growth ? growth : -1;
}
void tree_tick(BlockEdit e,unsigned tick) {
    int raw,soil;
    if (!world_resident_cell(e.x,e.y,e.z,&raw) || !sapling_cell(raw) ||
        !world_resident_cell(e.x,e.y-1,e.z,&soil)) return;
    if (!tree_soil(cell_type(soil))) {
        int survival=player_rules.mode==SURVIVAL,i=0;
        if (survival) {
            while (i<MAXDROP && drops[i].item.count) ++i;
            if (i==MAXDROP) return;
        }
        BlockEdit edit={e.x,e.y,e.z,AIR};
        if (world_set_blocks(&edit,1) && survival) {
            ItemStack item={(uint16_t)tree_sapling_drop(raw),1,0,0,0};
            drops[i]=make_drop(e.x+.5f,e.y+.25f,e.z+.5f,item);
            drops_dirty=1;
        }
        return;
    }
    if ((hash2(e.x+e.y*23,e.z) ^ (tick*0x9e3779b9u))%339u || !lit(e.x,e.y,e.z)) return;
    Planting p;
    if (!planting_at(e.x,e.y,e.z,sapling_species(raw),&p)) return;
    if (!((raw-SAPLING_BASE)&1)) (void)world_set_farm_stage(e.x,e.y,e.z,raw+1);
    else (void)grow_tree(p,0);
}
