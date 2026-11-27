#include "world_test.h"
#include "../game/actors.h"
#include "../game/chests.h"
#include "../game/interaction.h"
#include <math.h>
#include <stdio.h>

enum { Y=156 };
#define CHECK(c) do { if (!(c)) { printf("CAT TEST failed line %d: %s\n",__LINE__,#c); ++bad; } } while (0)
static Vec3 at(float x,float z) { return (Vec3){x,Y+.01f,z}; }
static void player_at(float x,float z) { px=x; py=Y+.01f; pz=z; }
static void aim(Vec3 p,float height) {
    float dx=p.x-px,dz=p.z-pz;
    yaw=atan2f(dx,-dz); pitch=atan2f(p.y+height-py-1.55f,hypotf(dx,dz));
}
static void ticks(int n) { while (n-->0) actors_step(.05f); }
static int floor_stage(void) {
    for (int x=4;x<=38;++x) for (int z=3;z<=21;++z)
        if (!set_block(x,Y-1,z,STONE)) return 0;
    return 1;
}
static int tame_source(void) {
    int bad=0,packs=0;
    CHECK(world_begin_generated(992382978,GENERATOR_GOLEMS));
    generate(); player_reset_rules(CREATIVE); player_rules.day_tick=1000;
    const ActorRecord *a=actors_in(OVERWORLD);
    for (int x=-2000;x<2000 && !packs;x+=43) for (int z=-2000;z<2000 && !packs;z+=43) {
        if (terrain_biome(x,z)!=BIOME_JUNGLE || terrain_biome(x-12,z)!=BIOME_JUNGLE ||
            terrain_biome(x+12,z)!=BIOME_JUNGLE || terrain_biome(x,z-12)!=BIOME_JUNGLE ||
            terrain_biome(x,z+12)!=BIOME_JUNGLE) continue;
        py=terrain_height(x,z)+1.01f;
        if (py<63.f) continue;
        actors_reset();
        px=x+.5f; pz=z+.5f;
        for (int i=-2;i<=2;++i) for (int j=-2;j<=2;++j) (void)block(x+i*CHUNK_SIZE,0,z+j*CHUNK_SIZE);
        for (int k=0;k<50 && !packs;++k) {
            ticks(40);
            for (int j=0;j<ACTOR_CAPACITY;++j) if (a[j].type==ACTOR_OCELOT) {
                ++packs;
                CHECK(!a[j].traits && a[j].health==CAT_HEALTH && a[j].feet.y>=62.9f);
            }
        }
        if (packs) printf("CAT TEST natural jungle origin=(%d,%d), wild=%d\n",x,z,packs);
    }
    CHECK(packs>=1 && packs<=3);
    return bad;
}
int cat_selftest(void) {
    int bad=0;
    world_begin(18431); generate(); player_reset_rules(SURVIVAL); inventory_reset();
    CHECK(floor_stage());
    const ActorRecord *a=actors_in(OVERWORLD);
    const ActorCare *care=actors_care_in(OVERWORLD);
    selected_slot=0;
    CHECK(!actors_restore(OVERWORLD,(ActorRecord){at(8.5f,8.5f),0,ACTOR_OCELOT,10,CAT_SIAMESE}));
    CHECK(!actors_restore(OVERWORLD,(ActorRecord){at(8.5f,8.5f),0,ACTOR_OCELOT,10,PET_TAMED}));
    CHECK(actors_restore(OVERWORLD,(ActorRecord){at(8.5f,8.5f),0,ACTOR_OCELOT,10,0}));
    player_at(8.5f,10.8f); aim(a[0].feet,.36f);
    inventory[0]=(ItemStack){PUFFERFISH,32,0,0,0};
    CHECK(game_tap().message && inventory[0].count==32 && !a[0].traits);
    ticks(1);
    player_at(8.5f,10.4f); ticks(4); aim(a[0].feet,.36f);
    CHECK(a[0].feet.z<8.35f);
    (void)game_tap();
    CHECK(!a[0].traits && inventory[0].count==32);
    for (int i=0;i<40 && !a[0].traits;++i) {
        ticks(1);
        aim(a[0].feet,.36f);
        if (i>=12) (void)game_tap();
    }
    CHECK((a[0].traits&(PET_TAMED|PET_SITTING))==(PET_TAMED|PET_SITTING) &&
        (a[0].traits&CAT_COAT_MASK) && inventory[0].count<32 && care[0].managed);
    inventory[0]=(ItemStack){0}; aim(a[0].feet,.35f);
    CHECK(game_tap().message && !(a[0].traits&PET_SITTING));
    player_at(28.5f,8.5f); ticks(2);
    CHECK(a[0].type==ACTOR_OCELOT && hypotf(a[0].feet.x-px,a[0].feet.z-pz)<4.5f);

    actors_reset();
    CHECK(actors_restore(OVERWORLD,(ActorRecord){at(7.5f,8.5f),0,ACTOR_OCELOT,10,0}) &&
        actors_restore(OVERWORLD,(ActorRecord){at(10.5f,8.5f),0,ACTOR_CHICKEN,4,0}));
    player_at(18.5f,16.5f);
    for (int i=0;i<40 && a[1].health==4;++i) ticks(1);
    CHECK(a[1].health<4 || !a[1].type);

    actors_reset();
    player_at(10.5f,10.5f);
    CHECK(set_block(10,Y-1,8,LIMESTONE_ROCK) &&
        actors_restore(OVERWORLD,(ActorRecord){at(10.5f,8.5f),0,ACTOR_SHELLBACK,SHELLBACK_HEALTH,0}) &&
        actors_restore(OVERWORLD,(ActorRecord){at(12.5f,7.5f),0,ACTOR_OCELOT,10,PET_TAMED|PET_SITTING|CAT_SIAMESE}));
    ticks(260);
    CHECK(a[0].type==ACTOR_SHELLBACK && a[0].traits==SHELLBACK_RIPE && a[0].health==SHELLBACK_HEALTH &&
        a[1].type==ACTOR_OCELOT && actors_cat_sitting(OVERWORLD,1));

    actors_reset();
    CHECK(actors_restore(OVERWORLD,(ActorRecord){at(8.5f,8.5f),0,ACTOR_OCELOT,10,PET_TAMED|CAT_TUXEDO}) &&
        actors_restore(OVERWORLD,(ActorRecord){at(10.1f,8.5f),0,ACTOR_OCELOT,10,PET_TAMED|CAT_SIAMESE}));
    inventory[0]=(ItemStack){CLOWNFISH,1,0,0,0}; player_at(8.5f,10.5f);
    aim(a[0].feet,.36f); CHECK(game_tap().message && care[0].love && !inventory[0].count);
    inventory[0]=(ItemStack){RAW_SALMON,1,0,0,0}; player_at(10.1f,10.5f);
    aim(a[1].feet,.36f); CHECK(game_tap().message && care[1].love && !inventory[0].count);
    for (int i=0;i<110 && !a[2].type;++i) ticks(1);
    CHECK(a[2].type==ACTOR_OCELOT && (a[2].traits== (PET_TAMED|CAT_TUXEDO) ||
        a[2].traits==(PET_TAMED|CAT_SIAMESE)) && care[2].grow && care[2].managed && care[0].cooldown);

    actors_reset();
    player_at(10.5f,11.7f);
    CHECK(set_block(10,Y,8,CHEST) && set_block(11,Y,8,CHEST) &&
        actors_restore(OVERWORLD,(ActorRecord){at(9.5f,8.5f),0,ACTOR_OCELOT,10,PET_TAMED|CAT_TABBY}));
    for (int i=0;i<35 && !actors_cat_sitting(OVERWORLD,0);++i) ticks(1);
    CHECK(actors_cat_sitting(OVERWORLD,0) && !(a[0].traits&PET_SITTING));
    ChestView view;
    CHECK(chest_open((ChestPos){11,Y,8},&view)==CHEST_CAT &&
        chest_automation_open((ChestPos){10,Y,8},&view) && view.count==2);
    player_at(a[0].feet.x,a[0].feet.z+2.3f); aim(a[0].feet,.30f);
    CHECK(game_tap().message && !actors_cat_sitting(OVERWORLD,0) &&
        chest_open((ChestPos){11,Y,8},&view)==CHEST_OK);
    bad+=tame_source();
    printf("CAT TEST raw fish/patience/coat/prey/coexistence/perch/chest/breeding failures=%d\n",bad);
    return bad;
}
