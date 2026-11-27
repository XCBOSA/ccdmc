#include "../game/actors.h"
#include "../game/horses.h"
#include "../world/space.h"
#include <math.h>
#include <stdio.h>

#define CHECK(test) do { if (!(test)) { printf("MOB CONTACT TEST failed line %d: %s\n",__LINE__,#test); ++failures; } } while (0)

static int mob_separate(int i,int j) {
    BlockBox a,b;
    return actors_body_at(OVERWORLD,i,&a) && actors_body_at(OVERWORLD,j,&b) && !box_overlap(a,b);
}

static void step_contact(void) {
    for (int i=0;i<24;++i) actors_step(.05f);
}

int mob_contact_selftest(void) {
    int failures=0;
    CHECK(world_begin_options(882,GENERATOR_CURRENT,WORLD_SUPERFLAT,1));
    generate();
    player_reset_rules(CREATIVE);
    px=.5f; py=4.01f; pz=3.05f;
    const Vec3 center={.5f,4.01f,.5f};
    const ActorRecord black={center,3.14159f,ACTOR_OCELOT,CAT_HEALTH,PET_TAMED|CAT_TUXEDO};
    const ActorRecord orange={center,3.14159f,ACTOR_OCELOT,CAT_HEALTH,PET_TAMED|CAT_TABBY};
    ActorCare managed={.managed=1};
    CHECK(actors_restore_with_care(OVERWORLD,black,managed) && actors_restore_with_care(OVERWORLD,orange,managed));
    CHECK(!mob_separate(0,1));
    step_contact();
    CHECK(mob_separate(0,1) && !actors_cat_sitting(OVERWORLD,0) && !actors_cat_sitting(OVERWORLD,1));

    actors_reset();
    ActorRecord seated=black, other=orange;
    seated.traits|=PET_SITTING;
    other.feet.x+=.05f;
    CHECK(actors_restore_with_care(OVERWORLD,seated,managed) && actors_restore_with_care(OVERWORLD,other,managed));
    step_contact();
    const ActorRecord *m=actors_in(OVERWORLD);
    CHECK(mob_separate(0,1) && actors_cat_sitting(OVERWORLD,0) && m[0].feet.x==center.x && m[0].feet.z==center.z);

    actors_reset();
    /* Narrow X, clear Z, and a pair starting at the same coordinates.
       No camera-facing “draw offset” or relocation through the walls. */
    for (int z=-3;z<=3;++z) {
        CHECK(set_block(-1,4,z,STONE) && set_block(1,4,z,STONE) &&
             set_block(-1,5,z,STONE) && set_block(1,5,z,STONE));
    }
    other=orange; other.traits|=PET_SITTING;
    CHECK(actors_restore_with_care(OVERWORLD,seated,managed) && actors_restore_with_care(OVERWORLD,other,managed));
    step_contact();
    CHECK(mob_separate(0,1) && actors_cat_sitting(OVERWORLD,0) && actors_cat_sitting(OVERWORLD,1));
    CHECK(m[0].feet.x>=.295f && m[0].feet.x<=.705f && m[1].feet.x>=.295f && m[1].feet.x<=.705f &&
        fabsf(m[0].feet.z-m[1].feet.z)>.58f);

    actors_reset();
    Vec3 pen={7.5f,4.01f,.5f};
    CHECK(actors_restore(OVERWORLD,(ActorRecord){pen,0,ACTOR_PIG,10,0}) &&
        actors_restore(OVERWORLD,(ActorRecord){pen,0,ACTOR_COW,10,0}));
    step_contact();
    CHECK(mob_separate(0,1) && m[0].type==ACTOR_PIG && m[1].type==ACTOR_COW);

    actors_reset();
    HorseRecord horse={.feet=pen,.type=EQUINE_HORSE,.max_health=20,.health=20,.gait=45,.spring=46,.tamed=1,.saddle=1};
    px=pen.x; py=pen.y+1.04f; pz=pen.z;
    CHECK(horse_restore(OVERWORLD,horse) && horse_restore_rider(0));
    CHECK(actors_restore(OVERWORLD,(ActorRecord){pen,0,ACTOR_PIG,10,0}));
    step_contact();
    BlockBox horse_box,pig_box;
    CHECK(horse_body_at(OVERWORLD,0,&horse_box) && actors_body_at(OVERWORLD,0,&pig_box) &&
        !box_overlap(horse_box,pig_box) && horses_in(OVERWORLD)[0].feet.x==pen.x &&
        horses_in(OVERWORLD)[0].feet.z==pen.z && horse_rider()==0);
    horses_reset();
    px=.5f; py=4.01f; pz=3.05f;

    actors_reset();
    CHECK(set_block(8,7,0,STONE));
    CHECK(actors_restore(OVERWORLD,(ActorRecord){{8.5f,4.01f,.5f},0,ACTOR_CHICKEN,4,0}) &&
        actors_restore(OVERWORLD,(ActorRecord){{8.5f,8.01f,.5f},0,ACTOR_PIG,10,0}));
    actors_step(.05f);
    CHECK(m[0].feet.x==8.5f && m[0].feet.z==.5f && m[1].feet.x==8.5f && m[1].feet.z==.5f &&
        mob_separate(0,1));
    actors_reset();
    actors_reset();
    printf("MOB CONTACT TEST standing/sitting/walls/passive/mounted/height failures=%d\n",failures);
    return failures;
}
