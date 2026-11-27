#include "../game/actors.h"
#include "../game/blast.h"
#include "../game/drops.h"
#include "../game/interaction.h"
#include "../game/item_icons.h"
#include "../game/pistons.h"
#include "../world/rails.h"
#include "../world/space.h"
#include <stdio.h>
#include <string.h>

enum { Y=150 };
#define CHECK(c) do { if (!(c)) { printf("PISTON TEST failed line %d: %s\n",__LINE__,#c); ++failures; } } while (0)
static void run(int x,int z) { for (int i=0;i<8;++i) redstone_step(x,z); }
static int raw(CircuitPos p) { return world_cell(p.x,p.y,p.z); }
static int block_at(CircuitPos p,int b) { return set_block(p.x,p.y,p.z,b); }
static int dropped(int item) {
    int n=0;
    for (int i=0;i<MAXDROP;++i) if (drops[i].item.type==item) n+=drops[i].item.count;
    return n;
}
int piston_selftest(void) {
    int failures=0;
    world_begin(18301); generate(); player_reset_rules(SURVIVAL); inventory_reset();
    inventory[0]=(ItemStack){PLANKS,3,0,0,0}; inventory[1]=(ItemStack){COBBLESTONE,4,0,0,0};
    inventory[2]=(ItemStack){IRON_INGOT,1,0,0,0}; inventory[3]=(ItemStack){REDSTONE_DUST,1,0,0,0};
    const int recipe[]={0,0,0,1,2,1,1,3,1};
    CHECK(craft_preview(recipe,3).type==PISTON_ITEM && craft_take(recipe,3));
    CHECK(inventory[0].type==PISTON_ITEM && inventory[0].count==1);
    inventory[1]=(ItemStack){SILT_GEL,1,0,0,0};
    const int sticky[]={1,-1,0,-1};
    CHECK(craft_preview(sticky,2).type==STICKY_PISTON_ITEM && craft_take(sticky,2));
    ItemIcon normal,green;
    CHECK(item_icon(PISTON_ITEM,&normal) && item_icon(STICKY_PISTON_ITEM,&green) &&
        normal.rgb[2]!=green.rgb[2] && strcmp(normal.path[0],green.path[0])==0);

    px=2.5f; py=Y-1.14f; pz=2.75f; yaw=0; pitch=0; selected_slot=0;
    CHECK(set_block(2,Y,-1,STONE) && game_tap().message && world_cell(2,Y,0)==PISTON_BASE+PISTON_STICKY+SOUTH);
    CHECK(inventory[0].count==0 && set_block(2,Y,1,REDSTONE_BLOCK));
    run(2,0);
    CHECK(!piston_extended(world_cell(2,Y,0))); /* Front power never triggers extension. */
    CHECK(set_block(2,Y,1,AIR) && set_block(3,Y+1,0,REDSTONE_BLOCK)); /* Quasi-connectivity. */
    run(2,0);
    CHECK(piston_extended(world_cell(2,Y,0)) && piston_head_cell(world_cell(2,Y,1)));
    CHECK(set_block(3,Y+1,0,AIR));
    run(2,0);
    CHECK(!piston_extended(world_cell(2,Y,0)) && world_cell(2,Y,1)==AIR);

    CircuitPos base={-17,Y,-4},supply=piston_offset(base,WEST,1);
    CHECK(piston_place(base.x,base.y,base.z,EAST,1));
    for (int i=1;i<=13;++i) CHECK(block_at(piston_offset(base,EAST,i),i==1 ? GLASS : COBBLESTONE));
    CHECK(block_at(supply,REDSTONE_BLOCK));
    run(base.x,base.z);
    CHECK(!piston_extended(raw(base)) && raw(piston_offset(base,EAST,1))==GLASS);
    CHECK(block_at(piston_offset(base,EAST,13),AIR));
    run(base.x,base.z);
    CircuitPos front=piston_offset(base,EAST,1),moved=piston_offset(base,EAST,2);
    CHECK(piston_extended(raw(base)) && raw(front)==PISTON_HEAD_BASE+PISTON_STICKY+EAST &&
        raw(moved)==GLASS && raw(piston_offset(base,EAST,13))==COBBLESTONE);
    CHECK(!world_point_blocked(front.x+.25f,Y+.12f,front.z+.12f,1) &&
        world_point_blocked(front.x+.90f,Y+.12f,front.z+.12f,1) &&
        world_point_blocked(front.x+.10f,Y+.48f,front.z+.48f,1));
    CHECK(block_at(supply,AIR));
    run(base.x,base.z);
    CHECK(!piston_extended(raw(base)) && raw(front)==GLASS &&
        raw(moved)==AIR && raw(piston_offset(base,EAST,13))==COBBLESTONE);
    CHECK(block_at(front,OBSIDIAN) && block_at(supply,REDSTONE_BLOCK));
    run(base.x,base.z);
    CHECK(!piston_extended(raw(base)) && raw(front)==OBSIDIAN);
    CHECK(block_at(front,AIR));
    run(base.x,base.z);
    CHECK(piston_extended(raw(base)));
    CHECK(piston_break((Hit){1,front.x,front.y,front.z,0,0,0}) &&
        raw(base)==AIR && raw(front)==AIR && dropped(STICKY_PISTON_ITEM)==1);

    CHECK(piston_place(0,Y,18,EAST,0) && set_block(1,Y,18,STONE) &&
        set_block(2,Y-1,18,STONE) && set_block(2,Y,18,TORCH) && set_block(-1,Y,18,REDSTONE_BLOCK));
    run(0,18);
    CHECK(piston_extended(world_cell(0,Y,18)) && world_cell(2,Y,18)==STONE && dropped(TORCH)==1);
    CHECK(set_block(-1,Y,18,AIR));
    run(0,18);
    CHECK(world_cell(1,Y,18)==AIR && world_cell(2,Y,18)==STONE);

    CHECK(piston_place(-5,Y,24,EJECTOR_UP,0) && set_block(-5,Y-1,24,REDSTONE_BLOCK) && set_block(-5,Y+1,24,STONE));
    run(-5,24);
    CHECK(world_cell(-5,Y+1,24)==PISTON_HEAD_BASE+EJECTOR_UP && world_cell(-5,Y+2,24)==STONE);
    CHECK(piston_place(-7,Y,24,EJECTOR_DOWN,0) && set_block(-7,Y+1,24,REDSTONE_BLOCK) && set_block(-7,Y-1,24,GLASS));
    run(-7,24);
    CHECK(world_cell(-7,Y-1,24)==PISTON_HEAD_BASE+EJECTOR_DOWN && world_cell(-7,Y-2,24)==GLASS);

    px=10.53f; py=Y+.01f; pz=24.5f; vy=0;
    CHECK(piston_place(9,Y,24,EAST,0) && set_block(8,Y,24,REDSTONE_BLOCK));
    CHECK(actors_restore(OVERWORLD,(ActorRecord){{10.5f,Y+.01f,24.74f},0,ACTOR_PIG,10,0}));
    run(9,24);
    CHECK(px>11.27f && actors_in(OVERWORLD)[0].feet.x>11.38f && !collides(px,py,pz));
    CHECK(piston_place(15,Y,25,EAST,1) && set_block(14,Y,25,REDSTONE_BLOCK));
    run(15,25);
    CHECK(piston_extended(world_cell(15,Y,25)) && piston_head_cell(world_cell(16,Y,25)));
    CHECK(blast_queue_cart((Vec3){16.5f,Y,25.5f},0.f,0));
    blasts_step(1);
    CHECK(world_cell(15,Y,25)==AIR && world_cell(16,Y,25)==AIR);
    world_reset(); drops_reset();
    printf("PISTON TEST craft/icons/tap/front/QC/12-block/seam/sticky/obstacle/shape/fragile/vertical/entities/blast failures=%d\n",failures);
    return failures;
}
