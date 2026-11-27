#include "world_test.h"
#include "../game/actors.h"
#include "../game/drops.h"
#include "../game/farming.h"
#include "../game/golems.h"
#include "../game/interaction.h"
#include "../game/item_icons.h"
#include "../world/internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { Y=150 };
#define CHECK(c) do { if (!(c)) { printf("GOLEM TEST failed line %d: %s\n",__LINE__,#c); ++bad; } } while (0)
static void aim(float x,float y,float z) {
    float dx=x-px,dz=z-pz;
    yaw=atan2f(dx,-dz); pitch=atan2f(y-py-1.55f,hypotf(dx,dz));
}
static int frame(int x,int z,int along_x) {
    BlockEdit e[]={{x,Y-1,z,STONE},{x,Y,z,IRON_BLOCK},{x,Y+1,z,IRON_BLOCK},
        {x-along_x,Y+1,z-!along_x,IRON_BLOCK},{x+along_x,Y+1,z+!along_x,IRON_BLOCK}};
    return world_set_blocks(e,5);
}
static int pumpkins(void) {
    static TerrainResult old,current;
    const uint32_t seed=992382978;
    int bad=0,count=0,checked=0;
    for (int x=-70;x<70 && count<12;x+=3) for (int z=-70;z<70 && count<12;z+=3) {
        Biome b=terrain_biome(x*CHUNK_SIZE+8,z*CHUNK_SIZE+8);
        if (b!=BIOME_PLAINS && b!=BIOME_FOREST && b!=BIOME_SAVANNA) continue;
        terrain_prepare(TERRAIN_KEY(seed,GENERATOR_FORESTRY,OVERWORLD,WORLD_STANDARD,0),x,z,&old);
        terrain_prepare(TERRAIN_KEY(seed,GENERATOR_GOLEMS,OVERWORLD,WORLD_STANDARD,0),x,z,&current);
        ++checked;
        CHECK(!memcmp(old.cells,current.cells,sizeof old.cells));
        for (unsigned i=0;i<current.wide.count;++i) if (pumpkin_cell(current.wide.cell[i].cell)) {
            int k=current.wide.cell[i].index;
            CHECK(k%WY && old.cells[k]==AIR && old.cells[k-1]==GRASS);
            ++count;
        }
    }
    CHECK(count>=12 && checked>0);
    printf("PUMPKIN GENERATION checked=%d cells=%d\n",checked,count);
    return bad;
}
int golem_selftest(void) {
    int bad=0;
    CHECK(world_begin_generated(992382978,GENERATOR_GOLEMS));
    bad+=pumpkins();
    generate(); player_reset_rules(SURVIVAL); player_rules.day_tick=18000;
    selected_slot=0; inventory[0]=(ItemStack){PUMPKIN_ITEM,1,0,0,0};
    for (int x=4;x<=18;++x) for (int z=4;z<=13;++z) CHECK(set_block(x,Y-1,z,STONE));
    /* Placing fruit now leaves both the fruit and the frame as blocks. */
    CHECK(frame(8,8,1));
    px=8.5f; py=Y+2.01f; pz=10.5f;
    aim(8.5f,Y+1.95f,8.5f);
    const ActorRecord *a=actors_in(OVERWORLD);
    CHECK(game_tap().placed && !inventory[0].count && world_cell(8,Y+1,8)==IRON_BLOCK &&
        block(8,Y+2,8)==PUMPKIN && !a[0].type);
    CHECK(set_block(8,Y+2,8,AIR) && set_block(8,Y+1,8,AIR) && set_block(8,Y,8,AIR) &&
        set_block(7,Y+1,8,AIR) && set_block(9,Y+1,8,AIR));
    inventory[0]=(ItemStack){LAMINATE_ITEM,1,0,0,0};
    inventory[1]=(ItemStack){IRON_INGOT,2,0,0,0};
    inventory[2]=(ItemStack){PUMP_GASKET,1,0,0,0};
    CHECK(craft_take((int[]){0,1,1,2},2) && inventory[0].type==CRAWLER_KIT);
    px=8.5f; py=Y+.01f; pz=11.5f;
    aim(8.5f,Y-.03f,8.5f);
    CHECK(set_block(8,Y+1,8,STONE));
    CHECK(camp_machine_deploy((Hit){1,8,Y-1,8,8,Y,8}) && inventory[0].count==1 && !a[0].type);
    CHECK(set_block(8,Y+1,8,AIR));
    CHECK(game_tap().message && !inventory[0].count && world_cell(8,Y,8)==AIR &&
        a[0].type==ACTOR_IRON_GOLEM && a[0].health==CRAWLER_FACTORY_HEALTH);
    for (int i=0;i<20;++i) actors_step(.05f);
    CHECK(a[0].feet.x==8.5f && a[0].feet.z==8.5f);
    CHECK(actors_restore(OVERWORLD,(ActorRecord){{14.5f,Y+.01f,8.5f},0,ACTOR_VILLAGER,20,0}));
    CHECK(actors_restore(OVERWORLD,(ActorRecord){{11.5f,Y+.01f,8.5f},0,ACTOR_ZOMBIE,20,0}));
    px=17.5f; py=Y+.01f; pz=12.5f;
    int swing=0;
    for (int i=0;i<135 && a[2].type==ACTOR_ZOMBIE;++i) {
        actors_step(.05f);
        swing|=actors_golem_swing(OVERWORLD,0)>0.f;
    }
    CHECK(a[1].type==ACTOR_VILLAGER && a[1].health>0 && a[2].type!=ACTOR_ZOMBIE && swing);
    CHECK(a[0].type==ACTOR_IRON_GOLEM && player_rules.health==MAX_HEALTH);
    px=130.5f;
    actors_step(.05f);
    CHECK(a[0].type==ACTOR_IRON_GOLEM); /* constructed mobs never despawn from distance */

    actors_reset();
    CHECK(actors_restore(OVERWORLD,(ActorRecord){{8.3f,Y+.01f,8.5f},0,ACTOR_IRON_GOLEM,100,GOLEM_PLAYER_BUILT}) &&
        actors_restore(OVERWORLD,(ActorRecord){{10.35f,Y+.01f,8.5f},0,ACTOR_ZOMBIE,20,0}));
    for (int y=Y;y<Y+4;++y) for (int z=5;z<=12;++z) CHECK(set_block(9,y,z,STONE));
    px=16.5f; py=Y+.01f; pz=12.5f;
    for (int i=0;i<20;++i) actors_step(.05f);
    CHECK(a[0].health==100 && a[1].health==20 && actors_golem_swing(OVERWORLD,0)==0.f);
    for (int y=Y;y<Y+4;++y) for (int z=5;z<=12;++z) CHECK(set_block(9,y,z,AIR));

    actors_reset();
    px=8.5f; py=Y+.01f; pz=11.5f;
    inventory[0]=(ItemStack){CRAWLER_KIT,1,0,0,0};
    for (int i=0;i<ACTOR_CAPACITY;++i)
        CHECK(actors_restore(OVERWORLD,(ActorRecord){{25.5f+i,Y+.01f,24.5f},0,ACTOR_PIG,10,0}));
    Hit site={1,8,Y-1,8,8,Y,8};
    CHECK(camp_machine_deploy(site) && inventory[0].count==1 && world_cell(8,Y,8)==AIR);
    actors_reset();
    CHECK(camp_machine_deploy(site) && !inventory[0].count && a[0].type==ACTOR_IRON_GOLEM);
    drops_reset();
    Vec3 center=a[0].feet; center.y+=.45f;
    CHECK(actors_hit_arrow(center,100) && !a[0].type && drops[0].item.type==IRON_INGOT && drops[0].item.count==1);

    inventory[0]=(ItemStack){STRIATED_GLASS,1,0,0,0};
    inventory[1]=(ItemStack){IRON_INGOT,1,0,0,0};
    inventory[2]=(ItemStack){SEEP_TILE_ITEM,1,0,0,0};
    inventory[3]=(ItemStack){PUMP_GASKET,1,0,0,0};
    CHECK(craft_take((int[]){0,1,2,3},2) && inventory[0].type==PULSE_KIT);
    CHECK(set_block(8,Y-1,8,LAMINATED_MASONRY));
    CHECK(camp_machine_deploy(site) && inventory[0].count && !camp_machine_cooled(8,Y,8));
    CHECK(set_block(7,Y-1,8,WATER) && camp_machine_cooled(8,Y,8));
    CHECK(camp_machine_deploy(site) && !inventory[0].count && a[0].type==ACTOR_SNOW_GOLEM);
    actors_reset();
    float dry_advance=0.f;
    for (int wet=0;wet<=1;++wet) for (int wall=0;wall<=1;++wall) {
        CHECK(set_block(7,Y-1,8,wet ? WATER : STONE));
        CHECK(set_block(10,Y,8,wall ? STONE : AIR));
        CHECK(actors_restore(OVERWORLD,(ActorRecord){{8.5f,Y+.01f,8.5f},0,ACTOR_SNOW_GOLEM,16,0}) &&
            actors_restore(OVERWORLD,(ActorRecord){{12.5f,Y+.01f,8.5f},0,ACTOR_ZOMBIE,20,0}));
        actors_step(.05f);
        CHECK((actors_golem_swing(OVERWORLD,0)>0.f)==(wet && !wall));
        CHECK(a[0].feet.x==8.5f && a[0].feet.z==8.5f && a[1].health==20);
        if (!wet && !wall) dry_advance=12.5f-a[1].feet.x;
        if (wet && !wall) {
            CHECK(dry_advance>.08f && 12.5f-a[1].feet.x<dry_advance*.50f);
            aim(8.5f,Y+.48f,8.5f);
            inventory[0]=(ItemStack){PUMP_GASKET,2,0,0,0};
            CHECK(game_tap().message && a[0].health==SNOW_GOLEM_HEALTH && inventory[0].count==1);
            CHECK(game_tap().message && inventory[0].count==1);
        }
        actors_reset();
    }
    CHECK(set_block(10,Y,8,AIR));
    CHECK(actors_restore(OVERWORLD,(ActorRecord){{8.5f,Y+.01f,8.5f},0,ACTOR_SNOW_GOLEM,SNOW_GOLEM_HEALTH,0}) &&
        actors_restore(OVERWORLD,(ActorRecord){{8.5f,Y+.01f,8.5f},0,ACTOR_PIG,10,0}));
    for (int i=0;i<20;++i) actors_step(.05f);
    CHECK(a[0].feet.x==8.5f && a[0].feet.z==8.5f &&
        hypotf(a[1].feet.x-a[0].feet.x,a[1].feet.z-a[0].feet.z)>.38f);
    actors_reset();

    player_rules.day_tick=0; px=13.5f; py=Y+.01f; pz=8.5f;
    inventory[0]=(ItemStack){PUMPKIN_ITEM,1,0,0,0};
    CHECK(craft_take((int[]){0,-1,-1,-1},2) && inventory[0].type==PUMPKIN_SEEDS && inventory[0].count==4);
    BlockEdit soil={13,Y-1,6,FARM_BASE+7};
    CHECK(world_set_blocks(&soil,1) && set_block(14,Y-1,6,DIRT) && set_block(12,Y-1,6,WATER));
    aim(13.5f,Y-.15f,6.5f);
    CHECK(game_tap().message && world_cell(13,Y,6)==PUMPKIN_STEM_BASE && inventory[0].count==3);
    for (int i=0;i<6 && world_cell(13,Y,6)<PUMPKIN_STEM_END-1;++i) CHECK(farm_bonemeal(13,Y,6)>0);
    CHECK(world_cell(13,Y,6)==PUMPKIN_STEM_END-1);
    for (int i=0;i<4000 && block(14,Y,6)!=PUMPKIN;++i) farm_step(.075f);
    CHECK(block(14,Y,6)==PUMPKIN && block(13,Y,6)==PUMPKIN_STEM);
    ItemIcon icon;
    CHECK(item_icon(PUMPKIN_ITEM,&icon) && icon.path[0] && item_icon(PUMPKIN_SEEDS,&icon) &&
        item_icon(POPPY_ITEM,&icon));
    const int kits[]={CRAWLER_KIT,PULSE_KIT};
    for (int n=0;n<2;++n) {
        CHECK(item_icon(kits[n],&icon));
        for (int i=0;i<ITEM_ICON_LAYERS;++i)
            CHECK(icon.path[i] && *icon.path[i] &&
                strspn(icon.path[i],"0123456789ABCDEF;")==strlen(icon.path[i]));
    }
    printf("CAMP TEST no old activation, recipes, intercept, wet sightline, repair, capacity failures=%d\n",bad);
    return bad;
}
