#include "spawn_eggs.h"
#include "actors.h"
#include "horses.h"
#include "sound.h"
#include <stdio.h>

typedef struct { ItemDef item; int actor; unsigned spots; int hostile; } Egg;
#define EGG(title,shell,pattern,mob,enemy) {{title "刷怪蛋",shell,STACK_MAX,AIR,0,0,0,0},mob,pattern,enemy}
static const Egg eggs[]={
    EGG("猪",0xe6979e,0xf9c0ba,ACTOR_PIG,0),
    EGG("牛",0x5b3b29,0xe0d4bf,ACTOR_COW,0),
    EGG("羊",0xecdfcb,0x9a8679,ACTOR_SHEEP,0),
    EGG("鸡",0xfff8e8,0xd55c3c,ACTOR_CHICKEN,0),
    EGG("狼",0xb6b3a9,0x6c6355,ACTOR_WOLF,0),
    EGG("豹猫",0xe8bc64,0x78492d,ACTOR_OCELOT,0),
    EGG("鱿鱼",0x31445e,0x8db4cb,ACTOR_SQUID,0),
    EGG("马",0xa26c43,0x543727,ACTOR_EMPTY,0),
    EGG("潮砾蟹",0x365d62,0xb87851,ACTOR_TIDE_CRAB,0),
    EGG("村民",0xb58569,0x694b44,ACTOR_VILLAGER,0),
    EGG("苔壳行者",0x514c3e,0xdea457,ACTOR_ZOMBIE,1),
    EGG("骷髅",0xd5d4c6,0x787775,ACTOR_SKELETON,1),
    EGG("鸣壳兽",0xcfa674,0x414b59,ACTOR_SHELLBACK,0),
    EGG("蜘蛛",0x41363b,0xb74c45,ACTOR_SPIDER,1),
    EGG("洞穴蜘蛛",0x2d5963,0xac5157,ACTOR_CAVE_SPIDER,1),
    EGG("潮帆鳐",0x448c95,0xe3b078,ACTOR_TIDERAY,0),
    EGG("泥脊兽",0x5f6961,0xc1ab73,ACTOR_SILTBACK,0),
    EGG("岩簧蜥",0xb77052,0x8cd1bd,ACTOR_REEDLIZARD,1), /* Legacy hot-region egg ID. */
    EGG("岩簧蜥",0xb77052,0x8cd1bd,ACTOR_REEDLIZARD,1),
    EGG("引光蛾",0x294a58,0xeaa65c,ACTOR_LANTERN_MOTH,0),
    EGG("灰窑鼹",0x52515c,0xe19963,ACTOR_KILN_MOLE,0),
    EGG("润脉甲",0x195766,0xd9ca83,ACTOR_SEEP_BEETLE,0)
};
static const Egg faultback_egg=EGG("裂岩甲兽",0x9e7059,0xf6b553,ACTOR_FAULTBACK,1);
#undef EGG
_Static_assert(sizeof eggs/sizeof eggs[0]==EGG_SEEP_BEETLE-EGG_PIG+1,"Egg IDs require a complete registry");
static const Egg *egg_for(int item) {
    if (item==EGG_FAULTBACK) return &faultback_egg;
    return spawn_egg_item(item) ? &eggs[item-EGG_PIG] : NULL;
}
const ItemDef *spawn_egg_def(int item) { const Egg *e=egg_for(item); return e ? &e->item : NULL; }
unsigned spawn_egg_spot(int item) { const Egg *e=egg_for(item); return e ? e->spots : 0; }
int spawn_egg_at(int item,Vec3 point) {
    const Egg *egg=egg_for(item);
    if (!egg) return 0;
    if (egg->hostile && game_effective_difficulty()==PEACEFUL) return 0;
    int ok=item==EGG_HORSE ? horse_spawn_egg(point,yaw) : actors_spawn_egg(egg->actor,point,yaw);
    if (ok) {
        if (item==EGG_HORSE) sound_play(SOUND_HORSE,.79f,0,point);
        else sound_creature(egg->actor,0,0,point,0);
        printf("SPAWN EGG item=%d actor=%d\n",item,egg->actor);
        fflush(stdout);
    }
    return ok;
}
const char *spawn_egg_use(Hit hit) {
    if ((unsigned)selected_slot>=HOTBAR_SIZE) return NULL;
    ItemStack *held=&inventory[selected_slot];
    if (!held->count || !spawn_egg_item(held->type)) return NULL;
    if (!hit.hit) return "对准方块使用刷怪蛋";
    if (egg_for(held->type)->hostile && game_effective_difficulty()==PEACEFUL) return "和平难度不能生成敌对生物";
    if (held->type==EGG_SQUID && !water_strength(world_cell(hit.ax,hit.ay,hit.az))) return "鱿鱼需要水才能生成";
    if (held->type==EGG_TIDERAY && !water_strength(world_cell(hit.ax,hit.ay,hit.az))) return "潮帆鳐需要水才能孵化";
    if (held->type==EGG_LANTERN_MOTH && world_dimension()!=OVERWORLD) return "引光蛾要在地表孵化";
    if (!spawn_egg_at(held->type,(Vec3){hit.ax+.5f,hit.ay+.025f,hit.az+.5f}))
        return "空间不足或当前维度不适合，刷怪蛋未消耗";
    if (player_rules.mode==SURVIVAL && !--held->count) *held=(ItemStack){0};
    return "已生成生物";
}
