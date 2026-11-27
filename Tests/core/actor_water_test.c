#include "world_test.h"
#include "../game/actors.h"
#include "../game/game.h"
#include <math.h>
#include <stdio.h>

static int actor_crosses_water(int cliff) {
    enum { Y=24, BANK=6 };
    if (!world_begin_options(1712,GENERATOR_CURRENT,WORLD_SUPERFLAT,1)) return 1;
    generate(); player_reset_rules(SURVIVAL);
    player_rules.day_tick=18000;
    px=BANK+4.5f; py=Y+3.01f+cliff; pz=.5f;
    for (int x=-5;x<=12;++x) for (int z=-4;z<=4;++z) {
        if (!set_block(x,Y-1,z,STONE)) return 1;
        for (int y=Y;y<=Y+2;++y)
            if (!set_block(x,y,z,x<0 || x>=BANK ? STONE : WATER)) return 1;
        if (x>=BANK) for (int y=Y+3;y<Y+3+cliff;++y)
            if (!set_block(x,y,z,STONE)) return 1;
    }
    if (!actors_restore(OVERWORLD,(ActorRecord){{-1.5f,Y+3.01f,.5f},1.570796f,ACTOR_ZOMBIE,20,0})) return 1;
    const ActorRecord *a=actors_in(OVERWORLD);
    float submerged=0.f, farthest=-1.5f, surface=Y+2.f+water_surface(WATER,AIR);
    int finished=0;
    for (int n=0;n<240 && a[0].type==ACTOR_ZOMBIE;++n) {
        actors_step(.05f);
        Vec3 p=a[0].feet;
        farthest=fmaxf(farthest,p.x);
        if (p.x>=.75f && p.x<=BANK-.75f) submerged=fmaxf(submerged,surface-p.y);
        if (p.x>=BANK+.45f && p.y>=Y+2.95f) { finished=1; break; }
    }
    int failure=submerged<.70f || (cliff ? (farthest<BANK-.48f || farthest>=BANK) : !finished);
    printf("ACTOR RIVER TEST shore height=%d depth=%.2f furthest x=%.2f final y=%.2f failures=%d\n",
        cliff,submerged,farthest,a[0].feet.y,failure);
    return failure;
}

int actor_water_selftest(void) {
    enum { Y=154 };
    int failures=0;
    if (!world_begin_options(1712,GENERATOR_CURRENT,WORLD_SUPERFLAT,1)) return 1;
    generate(); player_reset_rules(CREATIVE);
    player_rules.day_tick=18000;
    px=6.5f; py=Y+3.f; pz=11.5f;
    for (int x=-7;x<=11;++x) for (int z=-7;z<=11;++z) {
        if (!set_block(x,Y-1,z,STONE)) return 1;
        if (x<=7) for (int y=Y;y<=Y+2;++y)
            if (!set_block(x,y,z,WATER)) return 1;
    }
    const ActorRecord cases[]={
        {{0.5f,Y+3.15f,0.5f},0,ACTOR_SKELETON,20,0},
        {{4.5f,Y+3.15f,0.5f},0,ACTOR_COW,10,0},
        {{0.5f,Y+3.15f,5.5f},0,ACTOR_IRON_GOLEM,GOLEM_HEALTH,0},
        {{3.5f,Y+.30f,6.5f},0,ACTOR_SQUID,SQUID_HEALTH,0},
        {{10.5f,Y+2.f,5.5f},0,ACTOR_PIG,10,0}
    };
    for (unsigned i=0;i<sizeof cases/sizeof *cases;++i)
        if (!actors_restore(OVERWORLD,cases[i])) return 1;
    const ActorRecord *a=actors_in(OVERWORLD);
    for (int n=0;n<43;++n) actors_step(.05f);
    float surface=Y+2.f+water_surface(WATER,AIR);
    for (int i=0;i<2;++i)
        failures+=a[i].type!=cases[i].type || a[i].feet.y>=surface-.39f || a[i].feet.y<Y+.65f;
    failures+=a[2].type!=ACTOR_IRON_GOLEM || fabsf(a[2].feet.y-Y)>.18f;
    failures+=a[3].type!=ACTOR_SQUID || a[3].feet.y>=surface-.60f ||
        hypotf(a[3].feet.x-cases[3].feet.x,a[3].feet.z-cases[3].feet.z)<.15f;
    failures+=a[4].type!=ACTOR_PIG || fabsf(a[4].feet.y-Y)>.10f;
    printf("ACTOR WATER TEST skeleton %.2f, cow %.2f, golem %.2f, squid %.2f, dry pig %.2f, surface %.2f failures=%d\n",
        a[0].feet.y,a[1].feet.y,a[2].feet.y,a[3].feet.y,a[4].feet.y,surface,failures);
    int bank=actor_crosses_water(0);
    int cliff=actor_crosses_water(2);
    return failures+bank+cliff;
}
