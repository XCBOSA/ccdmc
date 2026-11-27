#include "../game/actors.h"
#include "../game/interaction.h"
#include "../game/silt_bed.h"
#include "../game/spawn_eggs.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(v) do { if (!(v)) { printf("SILT BED TEST failed line %d: %s\n",__LINE__,#v); ++failures; } } while (0)
static int said(const char *s,const char *part) { return s && strstr(s,part); }
static void steps(int count) { for (int i=0;i<count;++i) actors_step(.05f); }
static int siltbacks(void) {
    int n=0;
    for (int i=0;i<ACTOR_CAPACITY;++i) n+=actors_in(OVERWORLD)[i].type==ACTOR_SILTBACK;
    return n;
}
int silt_bed_selftest(void) {
    int failures=0,raw=0;
    enum { Y=90,Z=-2 };
    CHECK(world_begin_profile(91825,GENERATOR_CURRENT,WORLD_SUPERFLAT));
    generate(); player_reset_rules(SURVIVAL); CHECK(game_set_difficulty(PEACEFUL));
    px=.5f;py=Y+.014f;pz=Z+3.44f;yaw=0;pitch=-.54f;
    for (int x=-3;x<=3;++x) for (int z=-4;z<=4;++z) CHECK(set_block(x,Y-1,z,STONE));
    CHECK(set_block(0,Y-1,Z,CLAY) && !silt_bed_water(0,Y-1,Z));
    CHECK(set_block(1,Y-1,Z,WATER) && silt_bed_water(0,Y-1,Z));
    CHECK(set_block(0,Y,Z,STONE) && !silt_bed_water(0,Y-1,Z));
    CHECK(set_block(0,Y,Z,AIR));
    CHECK(set_block(0,Y,Z,WATER) && silt_bed_water(0,Y-1,Z));
    CHECK(set_block(0,Y+1,Z,WATER) && !silt_bed_water(0,Y-1,Z));
    CHECK(set_block(0,Y+1,Z,AIR) && set_block(0,Y,Z,AIR));
    const Vec3 at={.5f,Y+.014f,Z+.5f};
    CHECK(spawn_egg_at(EGG_SILTBACK,at) && siltbacks()==1);
    steps(80);
    CHECK(world_cell(0,Y-1,Z)==CLAY && actors_siltback_raking(OVERWORLD,0)>0.f);
    CHECK(set_block(1,Y-1,Z,STONE));
    steps(1);
    CHECK(!actors_siltback_raking(OVERWORLD,0));
    CHECK(set_block(1,Y-1,Z,WATER));
    steps(120);
    CHECK(world_cell(0,Y-1,Z)==CLAY); /* The old four seconds did not carry over. */
    steps(65);
    CHECK(world_cell(0,Y-1,Z)==FIBROUS_CLAY && siltbacks()==1 && player_rules.health==MAX_HEALTH);
    memset(inventory,0,sizeof inventory); selected_slot=0;
    const Hit bed={1,0,Y-1,Z,0,Y,Z};
    CHECK(said(silt_bed_tap(bed),"用锹轻点"));
    for (int i=0;i<SLOT_COUNT;++i) inventory[i]=(ItemStack){STONE,STACK_MAX,0,0,0};
    inventory[0]=(ItemStack){IRON_SHOVEL,1,6,0,0};
    CHECK(said(silt_bed_tap(bed),"背包已满") && inventory[0].damage==6 && world_cell(0,Y-1,Z)==FIBROUS_CLAY);
    inventory[1]=(ItemStack){0};
    CHECK(said(game_tap().message,"已收集泥纤胶") && inventory[0].damage==7 &&
        inventory[1].type==SILT_GEL && inventory[1].count==1 && world_cell(0,Y-1,Z)==CLAY);
    CHECK(said(silt_bed_tap(bed),"接水泥床") && inventory[0].damage==7);
    inventory[2]=(ItemStack){STRING,2,0,0,0}; inventory[3]=(ItemStack){LEATHER,1,0,0,0};
    const int recipe[]={2,2,3,1};
    CHECK(craft_preview(recipe,2).type==LEAD && craft_take(recipe,2));
    const ActorRecord animal=actors_in(OVERWORLD)[0];
    CHECK(actors_hit_arrow((Vec3){animal.feet.x,animal.feet.y+.25f,animal.feet.z},SILTBACK_HEALTH+1) &&
        siltbacks()==0);
    for (int i=0;i<MAXDROP;++i) CHECK(drops[i].item.type!=SILT_GEL);
    CHECK(!actors_spawn_egg(ACTOR_ARCHIVE_MAGMA,(Vec3){3.5f,Y+.014f,3.5f},0));

    CHECK(!world_resident_cell(64,Y,4,&raw) && !silt_bed_water(64,Y-1,4) &&
        !world_resident_cell(64,Y,4,&raw));
    /* Actual ecology scheduler, with an artificial clay bank and no egg. */
    for (int x=6;x<=12;++x) for (int z=-8;z<=8;++z)
        CHECK(set_block(x,Y-1,z,(x&1) ? CLAY : WATER));
    for (int i=0;i<600 && !siltbacks();++i) steps(1);
    CHECK(siltbacks()>=1 && siltbacks()<=3);
    world_reset();
    printf("SILT BED TEST wet/dry interruption/egg/worked surface/full bag/harvest/recipe/spawn/no split failures=%d\n",failures);
    return failures;
}
