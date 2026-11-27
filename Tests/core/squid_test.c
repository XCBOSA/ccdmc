#include "world_test.h"
#include "../game/actors.h"
#include "../game/drops.h"
#include "../game/enchanting.h"
#include "../game/interaction.h"
#include "../game/item_icons.h"
#include "../world/light.h"
#include "../world/space.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { Y=155 };
#define CHECK(c) do { if (!(c)) { printf("SQUID TEST failed line %d: %s\n",__LINE__,#c); ++bad; } } while (0)
static ItemStack stack(int item,int count) { return (ItemStack){(uint16_t)item,(uint8_t)count,0,0,0}; }
static void aim(float x,float y,float z) {
    float dx=x-px,dz=z-pz;
    yaw=atan2f(dx,-dz); pitch=atan2f(y-py-1.55f,hypotf(dx,dz));
}
static void ticks(int n) { while (n-->0) actors_step(.05f); }
static int count(int item) {
    int total=0;
    for (int i=0;i<SLOT_COUNT;++i) if (inventory[i].type==item) total+=inventory[i].count;
    return total;
}
static int ocean_spawn(void) {
    int bad=0,n=0;
    CHECK(world_begin_generated(992382978,GENERATOR_GOLEMS));
    generate(); player_reset_rules(CREATIVE);
    for (int x=-1200;x<=1200 && !n;x+=47) for (int z=-1200;z<=1200 && !n;z+=47) {
        Biome b=terrain_biome(x,z);
        if (b!=BIOME_DEEP_OCEAN) continue;
        for (int dx=-2;dx<=2;++dx) for (int dz=-2;dz<=2;++dz)
            (void)block(x+dx*CHUNK_SIZE,0,z+dz*CHUNK_SIZE);
        actors_reset();
        px=x+.5f; py=61.f; pz=z+.5f;
        const ActorRecord *a=actors_in(OVERWORLD);
        for (int k=0;k<25 && !n;++k) {
            ticks(40);
            for (int i=0;i<ACTOR_CAPACITY;++i) if (a[i].type==ACTOR_SQUID) {
                Vec3 p=a[i].feet;
                CHECK(a[i].health==SQUID_HEALTH && p.y>=SQUID_LOW_Y-.10f && p.y<=SQUID_HIGH_Y+.20f &&
                    block((int)floorf(p.x),(int)floorf(p.y+.62f),(int)floorf(p.z))==WATER);
                ++n;
            }
        }
        if (n) printf("SQUID TEST generated ocean origin=(%d,%d), school=%d\n",x,z,n);
    }
    CHECK(n>=1 && n<=3);
    return bad;
}
int squid_selftest(void) {
    int bad=0;
    world_begin(18281); generate(); player_reset_rules(SURVIVAL);
    const ActorRecord *a=actors_in(OVERWORLD);
    for (int x=-20;x<=-10;++x) for (int z=4;z<=12;++z) {
        CHECK(set_block(x,Y-1,z,STONE));
        if (z<10 && x< -11) {
            for (int y=Y;y<=Y+2;++y) CHECK(set_block(x,y,z,WATER));
        }
    }
    Vec3 start={-15.5f,Y+.10f,7.5f};
    CHECK(!actors_restore(NETHER,(ActorRecord){start,0,ACTOR_SQUID,10,0}));
    CHECK(actors_restore(OVERWORLD,(ActorRecord){start,0,ACTOR_SQUID,10,0}));
    px=start.x; py=Y+.03f; pz=9.7f;
    float pulse=actors_squid_pulse(OVERWORLD,0);
    ticks(10);
    CHECK(hypotf(a[0].feet.x-start.x,a[0].feet.z-start.z)>.1f &&
        actors_squid_pulse(OVERWORLD,0)!=pulse && a[0].health==10);
    Vec3 p=a[0].feet;
    px=p.x; py=p.y-.45f; pz=p.z+1.65f;
    aim(p.x,p.y+.43f,p.z);
    memset(inventory,0,sizeof inventory); selected_slot=0;
    inventory[0]=stack(DIAMOND_SWORD,1);
    CHECK(mine() && a[0].health<10 && mine() && !a[0].type);
    CHECK(drops[0].item.type==INK_SAC && drops[0].item.count>=1 && drops[0].item.count<=3);
    CHECK(inventory_collect(&drops[0].item) && count(INK_SAC)>=1);
    int ink=-1;
    for (int i=1;i<SLOT_COUNT && ink<0;++i) if (inventory[i].type==INK_SAC) ink=i;
    CHECK(ink>=0 && ink!=2);
    inventory[2]=stack(GLASS,8);
    int ring[]={2,2,2,2,ink,2,2,2,2};
    CHECK(craft_preview(ring,3).type==BLACK_GLASS_ITEM && craft_take(ring,3) &&
        count(BLACK_GLASS_ITEM)==8 && !count(GLASS));
    int black=-1;
    for (int i=1;i<HOTBAR_SIZE && black<0;++i) if (inventory[i].type==BLACK_GLASS_ITEM) black=i;
    CHECK(black>0);
    selected_slot=black>0 ? black : 0;
    px=-14.5f; py=Y+.01f; pz=12.4f;
    CHECK(set_block(-14,Y-1,11,STONE));
    aim(-13.5f,Y-.05f,11.5f);
    CHECK(game_tap().message && world_cell(-14,Y,11)==STAINED_GLASS_BASE &&
        block(-14,Y,11)==GLASS && count(BLACK_GLASS_ITEM)==7 &&
        light_opacity(STAINED_GLASS_BASE)==light_opacity(GLASS) && world_point_blocked(-13.5f,Y+.4f,11.5f,0));
    drops_reset(); selected_slot=0;
    aim(-13.5f,Y+.53f,11.5f);
    CHECK(mine() && block(-14,Y,11)==AIR && !drops[0].item.count);
    inventory[0]=stack(IRON_PICKAXE,1);
    inventory[0].enchants=enchant_code(ENCH_SILK_TOUCH,1);
    CHECK(set_block(-14,Y,11,STAINED_GLASS_BASE+2) && mine() && block(-14,Y,11)==AIR &&
        drops[0].item.type==LIGHT_GRAY_GLASS_ITEM && drops[0].item.count==1);
    memset(inventory,0,sizeof inventory);
    inventory[0]=stack(INK_SAC,3); inventory[1]=stack(BONE_MEAL,4);
    CHECK(craft_preview((int[]){1,-1,-1,0},2).type==GRAY_DYE &&
        craft_preview((int[]){1,1,-1,0},2).count==3 &&
        craft_take((int[]){1,-1,-1,0},2));
    int gray=-1;
    for (int i=2;i<SLOT_COUNT && gray<0;++i) if (inventory[i].type==GRAY_DYE) gray=i;
    CHECK(gray>=2 && craft_take((int[]){1,gray,-1,-1},2) && count(LIGHT_GRAY_DYE)==2);
    inventory[4]=stack(GLASS,7);
    CHECK(!craft_preview((int[]){4,4,4,4,1,4,4,4,-1},3).count);
    inventory[4]=stack(GLASS,8);
    CHECK(craft_preview((int[]){4,4,4,4,1,4,4,4,4},3).type==WHITE_GLASS_ITEM &&
        !craft_preview((int[]){1,4,4,4,4,4,4,4,4},3).count);
    ItemIcon icon;
    CHECK(item_icon(INK_SAC,&icon));
    for (int i=0;i<4;++i) CHECK(stained_glass_item(STAINED_GLASS_BASE+i)==BLACK_GLASS_ITEM+i &&
        item_icon(BLACK_GLASS_ITEM+i,&icon) && valid_cell(STAINED_GLASS_BASE+i));

    actors_reset(); drops_reset();
    CHECK(actors_restore(OVERWORLD,(ActorRecord){{-14.5f,Y+.01f,11.5f},0,ACTOR_SQUID,10,0}));
    ticks(200); CHECK(a[0].health==10);
    ticks(210); CHECK(!a[0].type && drops[0].item.type==INK_SAC);
    bad+=ocean_spawn();
    printf("SQUID TEST movement/dry/ink/four dyes/glass/silk/mapping failures=%d\n",bad);
    return bad;
}
