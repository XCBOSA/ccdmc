#include "world_test.h"
#include "../game/actors.h"
#include "../game/drops.h"
#include "../game/enchanting.h"
#include "../game/golems.h"
#include "../game/interaction.h"
#include "../game/item_icons.h"
#include "../game/weather.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { Y=150 };
#define CHECK(c) do { if (!(c)) { printf("SNOW TEST failed line %d: %s\n",__LINE__,#c); ++bad; } } while (0)
static ItemStack stack(int type,int count) { return (ItemStack){(uint16_t)type,(uint8_t)count,0,0,0}; }
static void aim(float x,float y,float z) {
    float dx=x-px,dz=z-pz;
    yaw=atan2f(dx,-dz);
    pitch=atan2f(y-py-1.55f,hypotf(dx,dz));
}
static int nearby_ground(int x,int z,int type) {
    for (int i=x-2;i<=x+5;++i) for (int j=z-2;j<=z+2;++j)
        if (!set_block(i,Y-1,j,type)) return 0;
    return 1;
}
static void step(int count) { while (count-->0) actors_step(.05f); }
static int shot_active(void) {
    const ActorFireball *f=actors_fireballs();
    for (int i=0;i<ACTOR_FIREBALLS;++i) if (f[i].seconds>0 && f[i].kind==THROWN_SNOWBALL) return 1;
    return 0;
}
int snow_selftest(void) {
    int bad=0;
    world_begin(1318); generate(); player_reset_rules(SURVIVAL); inventory_reset();
    CHECK(nearby_ground(8,8,STONE));
    px=8.5f; py=Y+.01f; pz=10.5f; selected_slot=0;
    inventory[0]=stack(PLANKS,1); inventory[1]=stack(STICK,2);
    CHECK(craft_take((int[]){0,-1,-1,1,-1,-1,1,-1,-1},3) && inventory[0].type==WOOD_SHOVEL);
    CHECK(set_block(8,Y,8,SNOW));
    aim(8.5f,Y+.53f,8.5f);
    CHECK(mine()!=NULL && block(8,Y,8)==AIR && drops[0].item.type==SNOWBALL &&
        drops[0].item.count==4 && inventory[0].damage==1);
    drops_reset();
    CHECK(set_block(8,Y,8,SNOW_LAYER));
    aim(8.5f,Y+.07f,8.5f);
    CHECK(mine()!=NULL && block(8,Y,8)==AIR && drops[0].item.type==SNOWBALL &&
        drops[0].item.count==1 && inventory[0].damage==2);
    drops_reset();
    inventory[0].enchants=enchant_code(ENCH_SILK_TOUCH,1);
    CHECK(set_block(8,Y,8,SNOW));
    aim(8.5f,Y+.53f,8.5f);
    CHECK(mine()!=NULL && block(8,Y,8)==AIR && drops[0].item.type==SNOW && drops[0].item.count==1);
    drops_reset();
    inventory[0]=stack(SNOWBALL,8);
    CHECK(craft_take((int[]){0,0,0,0},2) && craft_take((int[]){0,0,0,0},2));
    CHECK(inventory[1].type==SNOW && inventory[1].count==2);
    selected_slot=1;
    aim(8.5f,Y-.08f,8.5f);
    CHECK(game_tap().message && block(8,Y,8)==SNOW && inventory[1].count==1);
    aim(8.5f,Y+.97f,8.5f);
    CHECK(game_tap().message && block(8,Y+1,8)==SNOW && !inventory[1].count);

    selected_slot=0; inventory[0]=stack(PUMPKIN_ITEM,1); py=Y+2.01f;
    aim(8.5f,Y+1.95f,8.5f);
    CHECK(game_tap().placed && !inventory[0].count && block(8,Y+1,8)==SNOW &&
        block(8,Y+2,8)==PUMPKIN && !actors_in(OVERWORLD)[0].type);

    actors_reset();
    CHECK(world_select_dimension(NETHER) && nearby_ground(8,8,NETHERRACK));
    const ActorRecord *n=actors_in(NETHER);
    CHECK(actors_restore(NETHER,(ActorRecord){{12.5f,Y+.01f,8.5f},0,ACTOR_REEDLIZARD,20,0}));
    px=12.5f; py=Y+.01f; pz=11.5f;
    aim(12.5f,Y+.50f,8.5f); inventory[0]=stack(SNOWBALL,2);
    CHECK(game_tap().message && inventory[0].count==1 && shot_active());
    step(5);
    CHECK(n[0].health==REED_HEALTH && actors_reed_phase(NETHER,0)==REED_COOLED);
    actors_reset();
    CHECK(actors_launch_snowball((Vec3){12.5f,Y+1.4f,10.3f},(Vec3){0,0,1}));
    int health=player_rules.health;
    step(4);
    CHECK(player_rules.health==health);
    for (int i=1;i<ACTOR_FIREBALLS;++i)
        CHECK(actors_launch_snowball((Vec3){8.5f,Y+20.f,8.5f},(Vec3){0,1,0}));
    CHECK(game_tap().message && inventory[0].count==1);

    ItemIcon icon;
    CHECK(item_icon(SNOWBALL,&icon) && icon.path[0] && !item_stack_valid(stack(SNOWBALL,17)));
    for (int i=WOOD_SHOVEL;i<=DIAMOND_SHOVEL;++i)
        CHECK(item_icon(i,&icon) && item_def(i)->durability && !item_def(i)->pick_tier);
    printf("SNOW TEST harvest/craft/no old activation/player projectiles/icons failures=%d\n",bad);
    return bad;
}
