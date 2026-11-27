#include "world_test.h"
#include "../game/field_supplies.h"
#include "../game/cistern.h"
#include "../game/weather.h"
#include "../game/item_icons.h"
#include "../game/armor.h"
#include "../world/redstone.h"
#include <stdio.h>
#include <string.h>

static void probe_ticks(int x,int z) {
    for (int i=0;i<24;++i) redstone_step(x,z);
}
static int cistern_probe_selftest(void) {
    int failures=0;
#define CHECK_PROBE(c) do { if (!(c)) { fprintf(stderr,"CISTERN PROBE TEST line %d: %s\n",__LINE__,#c); ++failures; } } while (0)
    const int x=-17,y=70,z=0; /* Tank and probe straddle a chunk seam. */
    CHECK_PROBE(world_begin_profile(1801,GENERATOR_CURRENT,WORLD_SUPERFLAT));
    memset(inventory,0,sizeof inventory);
    selected_slot=0;
    inventory[0]=(ItemStack){BRICK_ITEM,1,0,0,0};
    inventory[1]=(ItemStack){STRIATED_GLASS,1,0,0,0};
    inventory[2]=(ItemStack){IRON_INGOT,1,0,0,0};
    inventory[3]=(ItemStack){TIDE_GRIT,1,0,0,0};
    const int recipe[]={0,1,2,3};
    CHECK_PROBE(craft_take(recipe,2) && inventory[0].type==CISTERN_PROBE_ITEM && inventory[0].count==2);
    ItemIcon icon;
    const int ready=CISTERN_PROBE_BASE+EAST, empty=ready+CISTERN_PROBE_EMPTY;
    CHECK_PROBE(item_icon(CISTERN_PROBE_ITEM,&icon) && icon.path[3] && mining_drop(empty,0)==CISTERN_PROBE_ITEM);
    const BlockEdit parts[]={{x,y,z,CISTERN_EMPTY},{x+1,y,z,(uint16_t)ready},
        {x+2,y-1,z,STONE},{x+3,y,z,LAMP_OFF},{x+1,y,z+1,LAMP_OFF}};
    CHECK_PROBE(world_set_blocks(parts,5) && redstone_place_wire(x+2,y,z));
    probe_ticks(x,z);
    CHECK_PROBE(!redstone_wire_power(x+2,y,z) && world_cell(x+3,y,z)==LAMP_OFF);
    CHECK_PROBE(weather_restore((WeatherSave){1901,1200,0,8,WEATHER_RAIN}));
    weather_step(x,z,4*CISTERN_RAIN_PERIOD);
    probe_ticks(x,z);
    CHECK_PROBE(redstone_cistern_probe_level(x+1,y,z,ready)==3 &&
        redstone_wire_power(x+2,y,z)==15 && world_cell(x+3,y,z)==LAMP_ON &&
        world_cell(x+1,y,z+1)==LAMP_OFF && !redstone_timing_count(OVERWORLD));
    Hit tank={0}; tank.hit=1; tank.x=x;tank.y=y;tank.z=z;
    inventory[0]=(ItemStack){FIELD_FLASK_EMPTY,1,0,0,0};
    CHECK_PROBE(cistern_use(tank) && inventory[0].type==FIELD_FLASK_FULL);
    probe_ticks(x,z);
    CHECK_PROBE(!redstone_wire_power(x+2,y,z) && world_cell(x+3,y,z)==LAMP_OFF);
    int raw=0;
    CHECK_PROBE(redstone_toggle_cistern_probe(x+1,y,z,&raw) && raw==empty);
    probe_ticks(x,z);
    CHECK_PROBE(redstone_wire_power(x+2,y,z)==15 && world_cell(x+3,y,z)==LAMP_ON);
    CHECK_PROBE(world_set_cistern_level(x,y,z,CISTERN_EMPTY,CISTERN_EMPTY+1));
    probe_ticks(x,z);
    CHECK_PROBE(!redstone_wire_power(x+2,y,z) && world_cell(x+3,y,z)==LAMP_OFF);
    CHECK_PROBE(set_block(x,y,z,AIR) && redstone_cistern_probe_level(x+1,y,z,raw)==-1);
    probe_ticks(x,z);
    CHECK_PROBE(!redstone_wire_power(x+2,y,z) && world_cell(x+3,y,z)==LAMP_OFF);
    CHECK_PROBE(set_block(x,y,z,CISTERN_EMPTY));
    probe_ticks(x,z);
    CHECK_PROBE(world_cell(x+3,y,z)==LAMP_ON);
    /* Recorded storage remains legible to a sensor without bringing it into
       the streaming cache. Missing storage must not simulate a dry tank. */
    const int far=-17409;
    CHECK_PROBE(world_restore_edit(far,y,z,CISTERN_FULL));
    CHECK_PROBE(!world_resident_cell(far,y,z,&raw) && redstone_cistern_probe_level(far+1,y,z,ready)==3 &&
        !world_resident_cell(far,y,z,&raw));
    CHECK_PROBE(world_select_dimension(END) && redstone_cistern_probe_level(far+1,y,z,ready)==-1);
    printf("CISTERN PROBE TEST craft/rain/cross-seam/output/drain/empty/fault/nonresident failures=%d\n",failures);
#undef CHECK_PROBE
    return failures;
}
int field_supplies_selftest(void) {
    int failures=0;
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FIELD SUPPLIES TEST line %d: %s\n",__LINE__,#c); ++failures; } } while (0)
    const GameDifficulty previous=game_selected_difficulty();
    CHECK(world_begin_profile(1801,GENERATOR_CURRENT,WORLD_SUPERFLAT) && game_set_difficulty(NORMAL));
    player_reset_rules(SURVIVAL);
    field_reset();
    for (int i=0;i<55;++i) field_apply_exposure(20,1,0,1,0);
    int exposed=field_record().water;
    CHECK(exposed<90 && field_in_sun() && field_record().heat==FIELD_HEAT_MAX);
    field_reset();
    for (int i=0;i<55;++i) field_apply_exposure(20,1,0,1,1);
    int capped=field_record().water;
    CHECK(capped>exposed);
    for (int i=0;i<4;++i) field_apply_exposure(20,0,0,0,1);
    FieldSave sheltered=field_record();
    CHECK(!sheltered.heat && !field_in_sun());
    for (int i=0;i<10;++i) field_apply_exposure(20,0,0,0,1);
    CHECK(field_record().water==sheltered.water && field_record().drain==sheltered.drain);
    CHECK(field_restore((FieldSave){7,0,111}) && field_stride_scale()<.9f && !field_allows_recovery());
    CHECK(!field_restore((FieldSave){101,0,0}) && !field_restore((FieldSave){7,81,0}) &&
        !field_restore((FieldSave){0,0,1}) && !field_restore((FieldSave){7,0,400}));
    ItemStack flask={FIELD_FLASK_EMPTY,1,0,0,0};
    CHECK(field_use_water(&flask,0) && flask.type==FIELD_FLASK_EMPTY);
    CHECK(field_use_water(&flask,1) && flask.type==FIELD_FLASK_FULL && field_record().water==7);
    CHECK(field_use_water(&flask,1) && flask.type==FIELD_FLASK_FULL && field_record().water==7);
    CHECK(field_use_water(&flask,0) && flask.type==FIELD_FLASK_TWO && field_record().water==33 &&
        field_stride_scale()==1.f && field_allows_recovery());
    CHECK(field_use_water(&flask,0) && flask.type==FIELD_FLASK_ONE && field_record().water==59);
    CHECK(field_use_water(&flask,0) && flask.type==FIELD_FLASK_EMPTY && field_record().water==85);
    ItemStack bottle={WATER_BOTTLE,1,0,0,0};
    CHECK(field_use_water(&bottle,0) && bottle.type==GLASS_BOTTLE && field_record().water==99);
    field_reset();
    CHECK(field_use_water(&flask,1) && field_use_water(&flask,0) && flask.type==FIELD_FLASK_FULL);
    CHECK(field_restore((FieldSave){12,0,390}) && game_set_difficulty(PEACEFUL));
    field_apply_exposure(20,1,0,1,0);
    CHECK(field_record().water==12 && field_stride_scale()==1.f && !field_in_sun());
    CHECK(field_use_water(&flask,0) && flask.type==FIELD_FLASK_FULL && field_record().water==12);
    CHECK(game_set_difficulty(NORMAL));
    memset(inventory,0,sizeof inventory);
    inventory[0]=(ItemStack){LEATHER,1,0,0,0};
    inventory[1]=(ItemStack){STRING,1,0,0,0};
    inventory[2]=(ItemStack){GLASS_BOTTLE,1,0,0,0};
    const int craft[]={0,1,2,-1};
    CHECK(craft_preview(craft,2).type==FIELD_FLASK_EMPTY && craft_take(craft,2));
    for (int item=FIELD_FLASK_EMPTY;item<=FIELD_FLASK_FULL;++item) {
        ItemIcon icon;
        CHECK(item_icon(item,&icon) && icon.path[0] && icon.path[3] &&
            (icon.rgb[3]==0x87d6e5)==(item!=FIELD_FLASK_EMPTY));
    }
    CHECK(world_begin_profile(1801,GENERATOR_CURRENT,WORLD_SUPERFLAT));
    memset(inventory,0,sizeof inventory);
    selected_slot=0;
    inventory[0]=(ItemStack){LAMINATE_ITEM,1,0,0,0};
    inventory[1]=(ItemStack){STRIATED_GLASS,1,0,0,0};
    inventory[2]=(ItemStack){CLAY_BALL,1,0,0,0};
    inventory[3]=(ItemStack){PUMP_GASKET,1,0,0,0};
    const int camp[]={0,1,2,3};
    ItemIcon cistern_icon;
    CHECK(craft_preview(camp,2).type==CISTERN_ITEM && craft_take(camp,2) &&
        item_icon(CISTERN_ITEM,&cistern_icon) && cistern_icon.path[3] &&
        item_def(CISTERN_ITEM)->placed_block==CISTERN_EMPTY && mining_drop(CISTERN_EMPTY,0)==CISTERN_ITEM);
    CHECK(set_block(-1,70,1,CISTERN_EMPTY) && set_block(-2,70,1,CISTERN_EMPTY) &&
        set_block(-2,72,1,GLASS));
    for (int i=2;i<2+CISTERN_RAIN_BATCH;++i) CHECK(set_block(-1,70,i,CISTERN_EMPTY));
    CHECK(world_restore_edit(20,70,-8,CISTERN_EMPTY) && world_has_cisterns());
    BlockEdit batch[1];
    CHECK(!world_cistern_batch(1,-1,batch,1));
    weather_step(0,0,4*CISTERN_RAIN_PERIOD);
    CHECK(world_recorded_cell(OVERWORLD,-1,70,1)==CISTERN_EMPTY);
    CHECK(weather_restore((WeatherSave){1901,1200,0,8,WEATHER_RAIN}));
    weather_step(0,0,5*CISTERN_RAIN_PERIOD);
    for (int i=1;i<2+CISTERN_RAIN_BATCH;++i)
        CHECK(world_recorded_cell(OVERWORLD,-1,70,i)==CISTERN_FULL);
    CHECK(world_recorded_cell(OVERWORLD,-2,70,1)==CISTERN_EMPTY &&
        world_recorded_cell(OVERWORLD,20,70,-8)==CISTERN_EMPTY &&
        !world_cistern_batch(1,-1,batch,1));
    CHECK(!world_set_cistern_level(-1,70,1,CISTERN_EMPTY,CISTERN_FULL) &&
        !world_set_cistern_level(20,70,-8,CISTERN_EMPTY,CISTERN_FULL));
    Hit at={0}; at.hit=1; at.x=-1; at.y=70; at.z=1;
    Hit sheltered_hit=at; sheltered_hit.x=-2;
    inventory[0]=(ItemStack){0};
    const char *covered=cistern_use(sheltered_hit);
    CHECK(covered && strstr(covered,"顶部被遮住")!=NULL);
    inventory[0]=(ItemStack){FIELD_FLASK_TWO,1,0,0,0};
    CHECK(cistern_use(at) && inventory[0].type==FIELD_FLASK_FULL &&
        world_recorded_cell(OVERWORLD,-1,70,1)==CISTERN_EMPTY+2);
    CHECK(cistern_use(at) && world_recorded_cell(OVERWORLD,-1,70,1)==CISTERN_EMPTY+2);
    inventory[0]=(ItemStack){FIELD_FLASK_EMPTY,1,0,0,0};
    CHECK(cistern_use(at) && inventory[0].type==FIELD_FLASK_TWO &&
        world_recorded_cell(OVERWORLD,-1,70,1)==CISTERN_EMPTY);
    CHECK(cistern_use(sheltered_hit) && inventory[0].type==FIELD_FLASK_TWO);
    CHECK(set_block(-1,70,1,AIR) && !cistern_use(at) &&
        world_select_dimension(END) && !world_has_cisterns());
    printf("FIELD SUPPLIES TEST sun=%d hat=%d, shade, thirst, flask, recipe and peaceful failures=%d\n",exposed,capped,failures);
    failures+=cistern_probe_selftest();
    (void)game_set_difficulty(previous);
    field_reset();
    world_reset();
#undef CHECK
    return failures;
}
