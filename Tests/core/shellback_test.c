#include "../game/actors.h"
#include "../game/interaction.h"
#include "../game/fireworks.h"
#include "../game/spawn_eggs.h"
#include "../world/light.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("SHELLBACK TEST failed line %d: %s\n",__LINE__,#c); ++failures; } } while (0)
int shellback_selftest(void) {
    int failures=0;
    const GameDifficulty before=game_selected_difficulty();
    CHECK(world_begin_profile(30217,GENERATOR_CURRENT,WORLD_SUPERFLAT));
    generate(); player_reset_rules(SURVIVAL); game_set_difficulty(PEACEFUL);
    px=.5f; py=4.01f; pz=1.5f; yaw=0; pitch=-.42f;
    memset(inventory,0,sizeof inventory);
    inventory[0]=(ItemStack){SURVEY_TAPPER,1,0,0,0};
    selected_slot=0;
    CHECK(set_block(0,3,-2,LIMESTONE_ROCK));
    const Vec3 bed={.5f,4.01f,-1.5f};
    CHECK(spawn_egg_at(EGG_SHELLBACK,bed) && !spawn_egg_at(EGG_SHELLBACK,bed));
    const ActorRecord *a=actors_in(OVERWORLD);
    CHECK(a[0].health==SHELLBACK_HEALTH && !a[0].traits);
    const char *growing=game_tap().message;
    CHECK(growing && strstr(growing,"还在生长"));
    for (int i=0;i<100;++i) actors_step(.05f);
    CHECK(!a[0].traits);
    for (int i=0;i<150;++i) actors_step(.05f);
    CHECK(a[0].traits==SHELLBACK_RIPE && a[0].health==SHELLBACK_HEALTH && !actors_hostile_near(bed,4,4));
    for (int i=1;i<SLOT_COUNT;++i) inventory[i]=(ItemStack){DIRT,64,0,0,0};
    const char *full=game_tap().message;
    CHECK(full && strstr(full,"背包已满") && a[0].traits==SHELLBACK_RIPE);
    inventory[1]=(ItemStack){0};
    CHECK(set_block(0,5,0,STONE) && !actors_collect_shell(target()) && a[0].traits==SHELLBACK_RIPE);
    CHECK(set_block(0,5,0,AIR));
    const char *picked=game_tap().message;
    CHECK(picked && strstr(picked,"未受伤") && !a[0].traits && a[0].health==SHELLBACK_HEALTH &&
        inventory[1].type==SHELL_FLAKE && inventory[1].count==1);
    CHECK(game_tap().message && inventory[1].count==1);
    inventory[2]=(ItemStack){SLATE_ITEM,1,0,0,0};
    const int marker[]={1,2,-1,-1};
    CHECK(craft_preview(marker,2).type==RESONANCE_MARKER_ITEM && craft_take(marker,2));
    CHECK(inventory[1].type==RESONANCE_MARKER_ITEM && inventory[1].count==2 &&
          light_emission(RESONANCE_MARKER)==11 && mining_drop(RESONANCE_MARKER,1)==RESONANCE_MARKER_ITEM);
    CHECK(!a[0].traits);
    for (int i=0;i<250;++i) actors_step(.05f);
    CHECK(a[0].traits==SHELLBACK_RIPE);
    const ItemStack spiral[]={{SHELL_FLAKE,1,0,0,0},{ROSE_RED,1,0,0,0},{GUNPOWDER,1,0,0,0},{0}};
    FireworkSpec spec={0};
    CHECK(firework_recipe(spiral,4,&spec).type==FIREWORK_STAR && spec.star[0].shape==FW_SPIRAL);
    const FireworkFlight f={{0},spec,0,1.f,.5f,0,419u};
    FireworkSpark sparks[FW_SPARKS];
    unsigned n=firework_sparks(&f,sparks);
    float elevation=0;
    for (unsigned i=0;i<n;++i) elevation=fmaxf(elevation,fabsf(sparks[i].point.z));
    CHECK(n==54 && elevation>.05f);

    /* A player can establish habitat without eggs; spawn only in a resident ring. */
    actors_reset();
    for (int x=-11;x<=11;++x) for (int z=-11;z<=11;++z)
        if (x*x+z*z>=36) CHECK(set_block(x,3,z,LIMESTONE_ROCK));
    for (int i=0;i<675;++i) actors_step(.10f);
    int shells=0;
    for (int i=0;i<ACTOR_CAPACITY;++i) if (a[i].type==ACTOR_SHELLBACK) {
        ++shells;
        CHECK(block((int)floorf(a[i].feet.x),3,(int)floorf(a[i].feet.z))==LIMESTONE_ROCK);
    }
    CHECK(shells>=1 && shells<=3);
    game_set_difficulty(before);
    world_reset();
    printf("SHELLBACK TEST habitat/growth/blocked harvest/inventory/output/renewal/spiral and natural spawn=%d failures=%d\n",shells,failures);
    return failures;
}
