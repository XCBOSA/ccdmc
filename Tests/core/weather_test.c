#include "../game/weather.h"
#include "../game/actors.h"
#include "../game/beds.h"
#include "../game/blast.h"
#include "../game/drops.h"
#include "../game/fire.h"
#include "../world/rails.h"
#include <stdio.h>

#define CHECK(c) do { if (!(c)) { printf("WEATHER TEST failed line %d: %s\n",__LINE__,#c); ++failures; } } while (0)
enum { Y=153 };
static unsigned prng(unsigned a) { a^=a<<13; a^=a>>17; return a^(a<<5); }
static void one_cover_at(int x,int z) {
    unsigned rng=0xbeef22u,r0=prng(rng),r1=prng(r0);
    weather_restore((WeatherSave){rng,500,0,1,WEATHER_RAIN});
    weather_step(x+24-(int)(r0%49u),z+24-(int)(r1%49u),1);
}
int weather_selftest(void) {
    int failures=0;
    world_begin(1318); generate();
    player_reset_rules(SURVIVAL);
    CHECK(weather_kind()==WEATHER_CLEAR && !weather_intensity());
    CHECK(weather_climate(BIOME_DESERT,Y)==PRECIP_NONE &&
        weather_climate(BIOME_FROZEN_RIVER,63)==PRECIP_SNOW &&
        weather_climate(BIOME_MOUNTAINS,95)==PRECIP_RAIN &&
        weather_climate(BIOME_MOUNTAINS,96)==PRECIP_SNOW);
    CHECK(!weather_restore((WeatherSave){1,500,0,8,WEATHER_THUNDER}));
    CHECK(!weather_restore((WeatherSave){0,500,300,8,WEATHER_THUNDER}));
    CHECK(weather_restore((WeatherSave){11,1,0,0,WEATHER_CLEAR}));
    weather_step(0,0,1);
    CHECK(weather_kind()!=WEATHER_CLEAR && weather_intensity()>0);
    CHECK(weather_restore((WeatherSave){17,1000,0,8,WEATHER_RAIN}));
    /* These assertions differentiate cached probing from normal world reads,
       which generate synchronously in this native test. */
    CHECK(!world_precipitation_surface(765432,-765432,NULL,NULL));
    CHECK(weather_column(765432,-765432,NULL)==PRECIP_NONE);
    CHECK(!world_precipitation_surface(765432,-765432,NULL,NULL));
    CHECK(set_block(-17,Y-1,0,STONE) && set_block(-16,Y-1,0,STONE) && set_block(-15,Y-1,0,NETHERRACK));
    CHECK(set_block(-16,Y+3,0,GLASS));
    CHECK(weather_wets(-17,Y,0) && !weather_wets(-16,Y,0) && weather_wets(-16,Y+4,0));
    CHECK(fire_ignite(-17,Y,0) && fire_ignite(-16,Y,0) && fire_ignite(-15,Y,0));
    fires_step(-17,0,42);
    CHECK(block(-17,Y,0)==AIR && block(-16,Y,0)==FIRE_CELL && block(-15,Y,0)==FIRE_CELL);
    px=-16.5f; py=Y+.01f; pz=.5f; vy=0;
    CHECK(player_restore_burn((PlayerBurn){80,20}));
    game_step(.05f,0,0,0,0);
    CHECK(!player_burn_state().remaining);
    px=-15.5f; py=Y+.01f; vy=0;
    CHECK(player_restore_burn((PlayerBurn){80,20}));
    game_step(.05f,0,0,0,0);
    CHECK(player_burn_state().remaining);
    CHECK(actors_restore(OVERWORLD,(ActorRecord){{-15.5f,Y+.01f,.5f},0,ACTOR_PIG,10,0}));
    actors_step(.05f);
    CHECK(actors_on_fire(OVERWORLD,0));
    CHECK(set_block(-16,Y+3,0,AIR));
    actors_step(.05f);
    CHECK(!actors_on_fire(OVERWORLD,0));
    WeatherSave rw=weather_record();
    CHECK(world_select_dimension(NETHER) && !weather_intensity() && weather_kind()==WEATHER_CLEAR);
    weather_step(0,0,500);
    CHECK(world_select_dimension(END) && !weather_intensity() && !weather_strike(0,0));
    weather_step(0,0,500);
    CHECK(world_select_dimension(OVERWORLD) && weather_record().remaining==rw.remaining);
    CHECK(weather_restore((WeatherSave){1,800,500,8,WEATHER_THUNDER}));
    CHECK(set_block(-19,Y-1,0,STONE) && set_block(-18,Y-1,0,STONE) && set_block(-18,Y+3,0,GLASS));
    actors_reset();
    CHECK(actors_restore(OVERWORLD,(ActorRecord){{-18.5f,Y+.01f,.5f},0,ACTOR_SHELLBACK,SHELLBACK_HEALTH,0}) &&
        actors_restore(OVERWORLD,(ActorRecord){{-17.5f,Y+.01f,.5f},0,ACTOR_SHELLBACK,SHELLBACK_HEALTH,0}));
    CHECK(weather_strike(-19,0) && weather_lightning().ticks==WEATHER_FLASH_TICKS);
    CHECK(actors_in(OVERWORLD)[0].health<SHELLBACK_HEALTH && !actors_in(OVERWORLD)[0].traits &&
        actors_in(OVERWORLD)[1].health==SHELLBACK_HEALTH && !actors_in(OVERWORLD)[1].traits);
    CHECK(block(-19,Y,0)==FIRE_CELL);
    weather_step(-19,0,4);
    CHECK(!weather_lightning().ticks);
    actors_reset();
    px=2.5f; py=Y+.01f; pz=4.5f; vy=0;
    CHECK(set_block(3,Y-1,4,STONE) && set_block(4,Y-1,4,STONE) && bed_place((BedPos){3,Y,4},EAST));
    world_set_dawn(7);
    player_rules.day_tick=DAY_TICKS-20;
    CHECK(bed_use((Hit){1,3,Y,4,0,0,0}) && bed_sleeping());
    for (int i=0;i<110 && bed_sleeping();++i) game_step(.05f,0,0,0,0);
    CHECK(!bed_sleeping() && weather_kind()==WEATHER_CLEAR && player_rules.day_tick==0 &&
        player_rules.world_day==8 && world_moon_phase()==0);
    px=11.5f; py=Y+.01f; pz=10.5f; vy=0;
    CHECK(set_block(11,Y-1,10,STONE));
    CHECK(blast_restore(OVERWORLD,(PrimedBlast){{.5f,Y,10.5f},{0,0,0},1,BLAST_TNT}));
    int health=player_rules.health;
    blasts_step(1);
    CHECK(player_rules.health==health);
    px=2.5f;
    CHECK(blast_restore(OVERWORLD,(PrimedBlast){{.5f,Y,10.5f},{0,0,0},1,BLAST_TNT}));
    blasts_step(1);
    CHECK(player_rules.health<health);

    CHECK(world_begin_generated(301397,GENERATOR_BOOKS));
    int x=0,z=0,found=0;
    for (int a=-1536;a<=1536 && !found;a+=32) for (int b=-1536;b<=1536;++b) {
        if (weather_climate(terrain_biome(a,b),Y)==PRECIP_SNOW &&
            weather_climate(terrain_biome(a+1,b),Y)==PRECIP_SNOW) { x=a; z=b; found=1; break; }
    }
    CHECK(found);
    if (found) {
        CHECK(set_block(x,Y, z,STONE) && set_block(x+1,Y,z,WATER));
        for (int dx=-CHUNK_SIZE;dx<=CHUNK_SIZE+1;dx+=CHUNK_SIZE)
            for (int dz=-CHUNK_SIZE;dz<=CHUNK_SIZE;dz+=CHUNK_SIZE) (void)block(x+dx,0,z+dz);
        one_cover_at(x,z); one_cover_at(x+1,z);
        CHECK(block(x,Y+1,z)==SNOW_LAYER && !solid(block(x,Y+1,z)) &&
            plant_height(world_cell(x,Y+1,z))==.125f && block(x+1,Y,z)==ICE);
        CHECK(!weather_wets(x,Y+5,z));
    }
    world_reset(); drops_reset();
    printf("WEATHER TEST cycle/dry/cover/cached/fire/mob/dimension/thunder/bed/charged-blast/snow/ice failures=%d\n",failures);
    return failures;
}
