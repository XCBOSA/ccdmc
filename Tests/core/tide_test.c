#include "../game/survey.h"
#include "../game/actors.h"
#include "../game/interaction.h"
#include "../game/spawn_eggs.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("TIDE TEST failed line %d: %s\n",__LINE__,#c); ++failures; } } while (0)
static void aim_at(Vec3 p) {
    float x=p.x-px,y=p.y-(py+1.55f),z=p.z-pz;
    yaw=atan2f(x,-z); pitch=atan2f(y,hypotf(x,z));
}
int tide_selftest(void) {
    int failures=0;
    GameDifficulty prior=game_selected_difficulty();
    CHECK(world_begin_profile(961729,GENERATOR_CURRENT,WORLD_SUPERFLAT));
    generate(); player_reset_rules(SURVIVAL); game_set_difficulty(PEACEFUL);
    for (int x=-1;x<=1;++x) for (int z=-1;z<=1;++z) (void)block(x,4,z);
    const Vec3 station={.5f,4.3f,.5f};
    CHECK(tide_read(station).state==TIDE_DRY);
    CHECK(tide_read((Vec3){1024.5f,4.3f,1024.5f}).state==TIDE_UNLOADED);
    CHECK(set_block(0,4,0,WATER));
    TideReading read=tide_read(station);
    CHECK(read.state==TIDE_EXIT && read.first==TIDE_UP && read.steps==1);
    CHECK(set_block(0,5,0,STONE));
    CHECK(set_block(2,4,0,WATER)); /* Cannot cross dry space to reach this pool. */
    CHECK(tide_read(station).state==TIDE_NO_EXIT);
    CHECK(set_block(1,4,0,WATER) && set_block(1,5,0,STONE));
    read=tide_read(station);
    CHECK(read.state==TIDE_EXIT && read.first==TIDE_EAST && read.steps==3);
    CHECK(set_block(2,5,0,STONE));
    CHECK(tide_read(station).state==TIDE_NO_EXIT);
    CHECK(set_block(2,5,0,AIR));

    memset(inventory,0,sizeof inventory);
    inventory[0]=(ItemStack){GLASS,1,0,0,0};
    inventory[1]=(ItemStack){LIMESTONE_ITEM,1,0,0,0};
    const int panes[]={0,1,-1,-1};
    CHECK(craft_preview(panes,2).type==STRIATED_GLASS && craft_take(panes,2));
    CHECK(inventory[0].type==STRIATED_GLASS && inventory[0].count==2);
    inventory[1]=(ItemStack){SLATE_ITEM,1,0,0,0};
    inventory[2]=(ItemStack){IRON_INGOT,1,0,0,0};
    const int guide[]={0,1,2,-1};
    CHECK(craft_preview(guide,2).type==TIDE_GUIDE && craft_take(guide,2));
    CHECK(inventory[0].type==STRIATED_GLASS && inventory[0].count==1 && inventory[1].type==TIDE_GUIDE);
    selected_slot=1; px=station.x; py=station.y-1.55f; pz=station.z; yaw=0;
    const char *msg=game_tap().message;
    CHECK(msg && strstr(msg,"向东") && strstr(msg,"3 格换气") && inventory[1].count==1);
    py=8.f;
    msg=game_tap().message;
    CHECK(msg && strstr(msg,"头部潜入") && inventory[1].count==1);

    actors_reset();
    CHECK(set_block(0,5,0,AIR) && set_block(1,5,0,AIR));
    for (int x=-4;x<=4;++x) for (int z=-6;z<=-1;++z) {
        CHECK(set_block(x,4,z,WATER));
        CHECK(set_block(x,5,z,WATER));
    }
    px=.5f; py=4.01f; pz=2.5f;
    const Vec3 fish={.5f,4.15f,-1.5f};
    CHECK(!spawn_egg_at(EGG_TIDERAY,(Vec3){1.5f,4.1f,2.5f}));
    CHECK(spawn_egg_at(EGG_TIDERAY,fish) && !spawn_egg_at(EGG_TIDERAY,fish));
    inventory[0]=(ItemStack){SURVEY_TAPPER,1,0,0,0};
    selected_slot=0; aim_at((Vec3){fish.x,fish.y+.21f,fish.z});
    msg=game_tap().message;
    CHECK(msg && strstr(msg,"从鳐所在处") && strstr(msg,"2 格换气"));
    CHECK(set_block(0,4,0,STONE) && set_block(0,5,0,STONE) && !actors_sound_ray(target()));
    CHECK(block(0,4,0)==STONE && block(0,5,0)==STONE);
    const ActorRecord *a=actors_in(OVERWORLD);
    CHECK(a[0].type==ACTOR_TIDERAY && a[0].health==TIDERAY_HEALTH);
    for (int i=0;i<100;++i) actors_step(.04f);
    CHECK(a[0].health==TIDERAY_HEALTH && player_rules.health==MAX_HEALTH &&
        !actors_hostile_near(fish,8.f,8.f) &&
        hypotf(a[0].feet.x-fish.x,a[0].feet.z-fish.z)>.2f &&
        a[0].feet.y<6.f && water_strength(world_cell((int)floorf(a[0].feet.x),
            (int)floorf(a[0].feet.y+.25f),(int)floorf(a[0].feet.z))));
    actors_reset();
    for (int x=-13;x<=13;++x) for (int z=-13;z<=13;++z) if (x*x+z*z>=36) {
        CHECK(set_block(x,4,z,WATER));
        CHECK(set_block(x,5,z,WATER));
    }
    for (int i=0;i<510;++i) actors_step(.10f);
    int rays=0;
    for (int i=0;i<ACTOR_CAPACITY;++i) rays+=a[i].type==ACTOR_TIDERAY;
    CHECK(rays>=1 && rays<=2);
    game_set_difficulty(prior); world_reset();
    printf("TIDE TEST resident routes walls/dry gaps/turn, reusable guide, neutral ray/occlusion/swim/wild=%d failures=%d\n",rays,failures);
    return failures;
}
