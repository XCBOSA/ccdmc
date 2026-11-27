#include "world_test.h"
#include "../game/game.h"
#include "../game/actors.h"
#include "../game/interaction.h"
#include "../game/item_icons.h"
#include "../game/pistons.h"
#include "../world/internal.h"
#include "../world/redstone.h"
#include "../world/space.h"
#include "../world/villages.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("DOOR TEST failed line %d: %s\n",__LINE__,#c); ++bad; } } while (0)
static void aim(float x,float y,float z) {
    yaw=atan2f(x-px,-(z-pz)); pitch=atan2f(y-py-1.55f,hypotf(x-px,z-pz));
}
static void signal(int x,int z) { for (int i=0;i<8;++i) redstone_step(x,z); }
static int identical_pair(int x,int y,int z) {
    int raw=world_cell(x,y,z);
    return door_cell(raw) && !door_upper(raw) && world_cell(x,y+1,z)==door_peer(raw);
}
static int count_dropped(int id) {
    int n=0;
    for (int i=0;i<MAXDROP;++i) if (drops[i].item.type==id) n+=drops[i].item.count;
    return n;
}
int door_selftest(void) {
    int bad=0;
    world_begin(33);
    generate(); player_reset_rules(SURVIVAL);
    memset(inventory,0,sizeof inventory);
    const int slots[]={0,1,-1,2,3,-1,4,5,-1};
    const int types[]={PLANKS,IRON_INGOT},items[]={WOOD_DOOR_ITEM,IRON_DOOR_ITEM};
    for (int t=0;t<2;++t) {
        for (int i=0;i<6;++i) inventory[i]=(ItemStack){(uint16_t)types[t],1,0,0,0};
        ItemStack result=craft_preview(slots,3);
        ItemIcon icon;
        CHECK(result.type==items[t] && result.count==1 && item_def(items[t])->max_stack==1 &&
            item_icon(items[t],&icon) && craft_take(slots,3));
    }
    const int x=-16,y=150,z=4;
    CHECK(!door_place(x,y,z,NORTH,0,0) && !door_place(x,WY-1,z,NORTH,0,0));
    CHECK(set_block(x,y-1,z,STONE) && set_block(x,y+1,z,STONE));
    CHECK(!door_place(x,y,z,NORTH,0,0) && world_cell(x,y,z)==AIR);
    CHECK(set_block(x,y+1,z,AIR));
    BlockEdit missing={x,y,z,DOOR_BASE};
    CHECK(!world_set_blocks(&missing,1) && world_cell(x,y,z)==AIR);
    px=x+.5f; py=y; pz=z+2.5f; selected_slot=0;
    inventory[0]=(ItemStack){WOOD_DOOR_ITEM,1,0,0,0};
    aim(x+.5f,y-.16f,z+.5f);
    CHECK(game_tap().message && !inventory[0].count && identical_pair(x,y,z));
    int wood=world_cell(x,y,z),face=door_bits(wood)&3;
    CHECK(face==SOUTH && world_point_blocked(x+.5f,y+.5f,z+.91f,1) &&
        world_point_blocked(x+.5f,y+1.5f,z+.91f,1));
    aim(x+.5f,y+1.51f,z+.5f);
    CHECK(target().y==y+1);
    game_tap();
    CHECK(identical_pair(x,y,z) && (door_bits(world_cell(x,y,z))&DOOR_OPEN) &&
        !collides(x+.5f,y,z+.5f) && !world_point_blocked(x+.5f,y+1.52f,z+.91f,1));
    CHECK(door_toggle(x,y,z));
    CHECK(mine() && world_cell(x,y,z)==AIR && world_cell(x,y+1,z)==AIR && count_dropped(WOOD_DOOR_ITEM)==1);
    CHECK(door_place(x,y,z,NORTH,0,1) && !door_toggle(x,y+1,z));
    CHECK(set_block(x-1,y+1,z,REDSTONE_BLOCK)); signal(x,z);
    CHECK((door_bits(world_cell(x,y,z))&(DOOR_OPEN|DOOR_POWERED))==(DOOR_OPEN|DOOR_POWERED) && identical_pair(x,y,z));
    CHECK(set_block(x-1,y+1,z,AIR)); signal(x,z);
    CHECK((door_bits(world_cell(x,y,z))&(DOOR_OPEN|DOOR_POWERED))==0 && identical_pair(x,y,z));
    CHECK(piston_place(x+1,y+1,z,WEST,0) && set_block(x+2,y+1,z,REDSTONE_BLOCK)); signal(x,z);
    CHECK(world_cell(x,y,z)==AIR && piston_head_cell(world_cell(x,y+1,z)) && count_dropped(IRON_DOOR_ITEM)==1);
    /* Double doors give opposite hinges, while each half remains independent. */
    CHECK(set_block(x+4,y-1,z,STONE) && door_place(x+4,y,z,NORTH,0,0));
    CHECK(set_block(x+3,y-1,z,STONE) && door_place(x+3,y,z,NORTH,0,0));
    CHECK(!(door_bits(world_cell(x+3,y,z))&DOOR_HINGE) && (door_bits(world_cell(x+4,y,z))&DOOR_HINGE));
    CHECK(set_block(x+4,y-1,z,AIR) && world_cell(x+4,y,z)==AIR && world_cell(x+4,y+1,z)==AIR && identical_pair(x+3,y,z));

    const uint32_t seed=992382978u;
    VillagePlan v;
    int dx,dz,dir;
    CHECK(world_begin_generated(seed,GENERATOR_VILLAGES));
    if (!village_plan_for(144,-97,&v) || !village_home_door(&v,0,&dx,&dz,&dir)) return bad+1;
    CHECK(world_cell(dx,v.floor+1,dz)==AIR && world_cell(dx,v.floor+2,dz)==AIR);
    CHECK(world_begin_generated(seed,GENERATOR_DOORS));
    generate();
    for (int i=0;i<VILLAGE_HOMES;++i) {
        CHECK(village_home_door(&v,i,&dx,&dz,&dir));
        CHECK(world_cell(dx,v.floor+1,dz)==DOOR_BASE+dir && identical_pair(dx,v.floor+1,dz) &&
            !world_has_edit(dx,v.floor+1,dz));
    }
    CHECK(village_home_door(&v,0,&dx,&dz,&dir));
    int low=v.floor+1;
    /* A natural door is not in any authored circuit index yet. Power still reaches it. */
    int ex=dx+rail_dx(dir),ez=dz+rail_dz(dir);
    CHECK(world_cell(ex,low+1,ez)==AIR && set_block(ex,low+1,ez,REDSTONE_BLOCK)); signal(dx,dz);
    CHECK(world_cell(dx,low,dz)==DOOR_BASE+dir+DOOR_OPEN+DOOR_POWERED && identical_pair(dx,low,dz));
    CHECK(set_block(ex,low+1,ez,AIR)); signal(dx,dz);
    /* From the actual seed checkpoint, the door and its resident can compete
       for the same scene tap. Walking normally must still let us open it. */
    px=dx+.5f+2*rail_dx(dir); pz=dz+.5f+2*rail_dz(dir); py=low+.01f; vy=0.f;
    yaw=atan2f(-rail_dx(dir),rail_dz(dir)); pitch=0.f; selected_slot=0;
    memset(inventory,0,sizeof inventory);
    for (int i=0;i<18;++i) game_step(.05f,0,1,0,0);
    float approach=(px-dx-.5f)*rail_dx(dir)+(pz-dz-.5f)*rail_dz(dir);
    int overlapping_resident=0;
    for (int i=0;i<ACTOR_CAPACITY;++i) {
        BlockBox b;
        if (actors_in(OVERWORLD)[i].type==ACTOR_VILLAGER && actors_body_at(OVERWORLD,i,&b))
            overlapping_resident|=box_contains(b,px,py+1.55f,pz);
    }
    CHECK(approach>=.5f && overlapping_resident);
    CHECK(game_tap().panel==INTERACTION_NONE);
    CHECK(world_cell(dx,low,dz)==DOOR_BASE+dir+DOOR_OPEN);
    for (int i=0;i<15;++i) game_step(.05f,0,1,0,0);
    CHECK((px-dx-.5f)*rail_dx(dir)+(pz-dz-.5f)*rail_dz(dir)<-.5f);
    CHECK(world_select_dimension(NETHER) && !door_cell(world_cell(dx,low,dz)) && world_select_dimension(OVERWORLD));
    CHECK(world_cell(dx,low,dz)==DOOR_BASE+dir+DOOR_OPEN && identical_pair(dx,low,dz));
    CHECK(set_block(dx,low+1,dz,AIR) && world_select_dimension(END) && world_select_dimension(OVERWORLD) &&
        world_cell(dx,low,dz)==AIR && world_cell(dx,low+1,dz)==AIR);
    printf("DOOR TEST crafting/icons/placement/touch/physics/paired-break/upper-redstone/piston/generation/reload failures=%d\n",bad);
    return bad;
}
