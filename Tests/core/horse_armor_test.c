#include "world_test.h"
#include "../game/chests.h"
#include "../game/drops.h"
#include "../game/horses.h"
#include "../game/interaction.h"
#include "../game/item_custom.h"
#include "../game/item_icons.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("HORSE ARMOR TEST failed line %d: %s\n",__LINE__,#c); ++bad; } } while (0)

static int armor_loot(int generation) {
    if (!world_begin_generated(992382978u,generation)) return -1;
    generate();
    int seen=0;
    for (int i=0;i<384 && seen!=7;++i) {
        ChestPos p={(i%24)*2,150,(i/24)*2};
        int cell=i%2 ? SMITH_CHEST_CELL : SADDLE_CHEST;
        if (!world_restore_edit(p.x,p.y,p.z,cell)) return -1;
        ItemStack loot[CHEST_SLOTS+2];
        int n=chest_blast_contents(p,loot);
        if (n<1 || n>CHEST_SLOTS) return -1;
        for (int s=0;s<n;++s) if (horse_armor_points(loot[s].type)) {
            if (!item_stack_valid(loot[s]) || loot[s].count!=1) return -1;
            seen|=1<<(loot[s].type-IRON_HORSE_ARMOR);
        }
        if (world_cell(p.x,p.y,p.z)!=cell) return -1;
    }
    return seen;
}
static void aim(const HorseRecord *h) {
    px=h->feet.x; py=h->feet.y; pz=h->feet.z+2.f; vy=0;
    yaw=0; pitch=atan2f(.85f-1.55f,2.f);
}
static HorseRecord horse(int type,float x,int tamed,int age) {
    return (HorseRecord){.feet={x,150.01f,.5f},.age=(uint16_t)age,.type=(uint8_t)type,
        .max_health=25,.health=25,.tamed=(uint8_t)tamed};
}
int horse_armor_selftest(void) {
    int bad=0;
    CHECK(armor_loot(GENERATOR_TREASURE)==0);
    CHECK(armor_loot(GENERATOR_HORSE_ARMOR)==7);
    world_begin(994); generate();
    player_reset_rules(SURVIVAL);
    for (int x=-2;x<=16;++x) for (int z=-2;z<=4;++z)
        CHECK(set_block(x,149,z,STONE));
    const int gear[]={IRON_HORSE_ARMOR,GOLD_HORSE_ARMOR,DIAMOND_HORSE_ARMOR};
    const int points[]={5,7,11};
    ItemIcon icon[3];
    for (int i=0;i<3;++i) {
        const ItemDef *type=item_def(gear[i]);
        CHECK(type && type->max_stack==1 && !type->durability && horse_armor_points(gear[i])==points[i]);
        CHECK(item_icon(gear[i],&icon[i]) && icon[i].path[0] && strlen(icon[i].path[0])>=16);
        if (i) CHECK(icon[i-1].rgb[0]!=icon[i].rgb[0]);
    }
    CHECK(horse_restore(OVERWORLD,horse(EQUINE_HORSE,.5f,0,0)));
    CHECK(horse_restore(OVERWORLD,horse(EQUINE_DONKEY,3.5f,1,0)));
    CHECK(horse_restore(OVERWORLD,horse(EQUINE_MULE,6.5f,1,0)));
    CHECK(horse_restore(OVERWORLD,horse(EQUINE_HORSE,9.5f,1,100)));
    HorseRecord mount=horse(EQUINE_HORSE,12.5f,1,0); mount.saddle=1;
    CHECK(horse_restore(OVERWORLD,mount));
    const HorseRecord *list=horses_in(OVERWORLD);
    memset(inventory,0,sizeof inventory); selected_slot=0;
    inventory[0]=(ItemStack){IRON_HORSE_ARMOR,1,0,0,0};
    CHECK(item_set_custom(&inventory[0],0,"铁甲·旅途"));
    ItemStack named=inventory[0];
    for (int i=0;i<4;++i) {
        aim(&list[i]);
        CHECK(game_tap().message && horse_rider()==-1 && !list[i].armor.count &&
            inventory[0].count==1 && item_same_kind(inventory[0],named));
    }
    aim(&list[4]);
    CHECK(game_tap().message && !inventory[0].count && item_same_kind(list[4].armor,named));
    Vec3 body=mount.feet; body.y+=.8f;
    CHECK(horse_hit_arrow(body,5) && list[4].health==21 && list[4].health_debt==0);
    inventory[0]=(ItemStack){GOLD_HORSE_ARMOR,1,0,0,0};
    CHECK(game_tap().message && item_same_kind(inventory[0],named) && list[4].armor.type==GOLD_HORSE_ARMOR);
    CHECK(horse_hit_arrow(body,1) && list[4].health==21 && list[4].health_debt==18);
    selected_slot=1;
    CHECK(game_tap().message && horse_rider()==4 && horse_can_equip_mounted());
    inventory[1]=(ItemStack){DIAMOND_HORSE_ARMOR,1,0,0,0};
    CHECK(item_set_custom(&inventory[1],0,"末地旅伴"));
    ItemStack diamond=inventory[1];
    CHECK(horse_equip_mounted(1) && inventory[1].type==GOLD_HORSE_ARMOR &&
        item_same_kind(horse_mounted_armor(),diamond));
    CHECK(horse_hit_hostile(body,1,DAMAGE_PROJECTILE) && list[4].health==20 && list[4].health_debt==7);
    for (int i=0;i<SLOT_COUNT;++i) inventory[i]=(ItemStack){DIRT,64,0,0,0};
    CHECK(!horse_unequip_mounted() && item_same_kind(horse_mounted_armor(),diamond));
    inventory[11]=(ItemStack){0};
    CHECK(horse_unequip_mounted() && !horse_mounted_armor().count && item_same_kind(inventory[11],diamond));
    CHECK(horse_equip_mounted(11) && !inventory[11].count && item_same_kind(horse_mounted_armor(),diamond));
    CHECK(game_dismount() && horse_rider()==-1);
    aim(&list[4]);
    inventory[1]=(ItemStack){LEAD,1,0,0,0};
    CHECK(game_tap().message && horses_lead_at(OVERWORLD,4)->kind==LEAD_PLAYER && !inventory[1].count);
    int explosion[HORSE_CAPACITY]={0};
    explosion[4]=5;
    horses_explosion_hits(explosion);
    CHECK(list[4].health==17 && list[4].health_debt==2);
    for (int i=0;i<MAXDROP-3;++i) drops[i].item=(ItemStack){STONE,1,0,0,0};
    CHECK(horse_hit_hostile(body,100,DAMAGE_PROJECTILE) && list[4].type && item_same_kind(list[4].armor,diamond));
    drops_reset();
    CHECK(horse_hit_hostile(body,100,DAMAGE_PROJECTILE) && !list[4].type);
    CHECK(drops[0].item.type==SADDLE && drops[1].item.type==LEATHER && drops[2].item.type==LEAD &&
        drops[3].item.count==1 && item_same_kind(drops[3].item,diamond) && !drops[4].item.count);
    printf("HORSE ARMOR TEST loot/equip/refusal/swap/remove/damage/death failures=%d\n",bad);
    return bad;
}
