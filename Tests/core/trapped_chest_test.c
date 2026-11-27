#include "world_test.h"
#include "../game/chests.h"
#include "../game/drops.h"
#include "../game/hoppers.h"
#include "../game/interaction.h"
#include "../game/item_icons.h"
#include "../world/redstone.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("TRAPPED CHEST TEST failed line %d: %s\n",__LINE__,#c); ++bad; } } while (0)
enum { Y=159, W=-17, E=-16 };

static ItemStack stack(int type,int count) { return (ItemStack){(uint16_t)type,(uint8_t)count,0,0,0}; }
static void circuit(void) { for (int i=0;i<6;++i) redstone_step(W,0); }
static unsigned drop_count(int item) {
    unsigned count=0;
    for (int i=0;i<MAXDROP;++i) if (drops[i].item.type==item) count+=drops[i].item.count;
    return count;
}

int trapped_chest_selftest(void) {
    int bad=0;
    world_begin(771722); generate(); player_reset_rules(SURVIVAL);
    memset(inventory,0,sizeof inventory);
    inventory[0]=stack(IRON_INGOT,1); inventory[1]=stack(STICK,1);
    inventory[2]=stack(PLANKS,1); inventory[3]=stack(CHEST,2);
    int hook_recipe[]={0,-1,-1,1,-1,-1,2,-1,-1};
    ItemStack probe=craft_preview(hook_recipe,3);
    CHECK(probe.type==TRIPWIRE_HOOK_ITEM && probe.count==2 && craft_take(hook_recipe,3));
    probe=craft_preview((int[]){3,-1,-1,0},2);
    CHECK(probe.type==TRAPPED_CHEST_ITEM && probe.count==1 && craft_take((int[]){3,-1,-1,0},2));
    ItemIcon icon;
    CHECK(item_icon(TRIPWIRE_HOOK_ITEM,&icon) && icon.path[0][0] &&
        item_icon(TRAPPED_CHEST_ITEM,&icon) && icon.path[0][0] &&
        item_def(TRAPPED_CHEST_ITEM)->placed_block==TRAPPED_CHEST && valid_cell(TRAPPED_CHEST_CELL));

    /* Two trapped halves can join across a negative chunk boundary,
       and immediately beside a regular chest without sharing contents. */
    ChestPos left={W,Y,0},right={E,Y,0},ordinary={E+1,Y,0};
    ChestView view={OVERWORLD,{{0,0,0},{0,0,0}},0,0};
    ChestView normal=view,automation=view;
    CHECK(set_block(E+1,Y,0,CHEST) && set_block(E,Y,0,TRAPPED_CHEST));
    CHECK(chest_joint(right)==CHEST_ALONE && chest_joint(ordinary)==CHEST_ALONE &&
        chest_can_place_type(left,TRAPPED_CHEST));
    CHECK(set_block(W,Y,0,TRAPPED_CHEST) && world_cell(W,Y,0)==TRAPPED_CHEST_CELL &&
        chest_joint(left)==CHEST_EAST && chest_joint(right)==CHEST_WEST &&
        !chest_can_place_type((ChestPos){W-1,Y,0},TRAPPED_CHEST));
    CHECK(chest_open(ordinary,&normal)==CHEST_OK && normal.count==1 && normal.kind==CHEST);
    CHECK(chest_open(right,&view)==CHEST_OK && view.count==2 && view.kind==TRAPPED_CHEST &&
        view.halves[0].x==W && view.halves[1].x==E);
    inventory[5]=stack(DIAMOND,4);
    CHECK(chest_move(&view,DOUBLE_CHEST_SLOTS+5,0,0)==CHEST_OK &&
        chest_slot(&view,0).count==4 && !chest_slot(&normal,0).count && chest_signal(right)==1);
    CHECK(chest_automation_open(left,&automation) && chest_slot(&automation,0).type==DIAMOND &&
        !chest_trap_power(left) && !chest_trap_power(right));

    CHECK(set_block(W,Y-2,0,STONE) && hopper_place((Hit){1,W,Y-2,0,W,Y-1,0}));
    const Hopper *hopper=hopper_at(W,Y-1,0);
    CHECK(hopper);
    CHECK(set_block(E,Y-1,0,STONE) && set_block(E+1,Y-1,0,REDSTONE_LAMP));
    CHECK(set_block(E,Y,1,REDSTONE_LAMP) && set_block(E,Y-1,-1,STONE) &&
        redstone_place_comparator(E,Y,-1,NORTH));
    circuit();
    CHECK(world_cell(E,Y,1)==LAMP_OFF && world_cell(E+1,Y-1,0)==LAMP_OFF &&
        redstone_comparator_power(E,Y,-1)==1 && !redstone_receives(W,Y-1,0));
    /* A blocked lid cannot begin a viewing session using an old handle. */
    CHECK(set_block(E,Y+1,0,STONE) && chest_open(left,&automation)==CHEST_BLOCKED);
    chest_access_begin(&view);
    CHECK(!chest_trap_power(left) && !chest_trap_power(right));
    CHECK(set_block(E,Y+1,0,AIR));
    chest_access_begin(&view);
    circuit();
    CHECK(chest_trap_power(left)==1 && chest_trap_power(right)==1 && redstone_receives(W,Y-1,0) &&
        world_cell(E,Y,1)==LAMP_ON && world_cell(E+1,Y-1,0)==LAMP_ON &&
        redstone_comparator_power(E,Y,-1)==1);
    hoppers_step(W,0);
    CHECK(chest_slot(&view,0).count==4 && hopper && !hopper->slots[0].count);
    chest_access_end(); circuit(); hoppers_step(W,0);
    CHECK(!chest_trap_power(left) && !chest_trap_power(right) && world_cell(E,Y,1)==LAMP_OFF &&
        world_cell(E+1,Y-1,0)==LAMP_OFF && redstone_comparator_power(E,Y,-1)==1 &&
        chest_slot(&view,0).count==3 && hopper && hopper->slots[0].type==DIAMOND);

    chest_access_begin(&view);
    CHECK(world_select_dimension(NETHER) && !chest_trap_power(left) &&
        world_select_dimension(OVERWORLD) && !chest_trap_power(right));
    chest_access_begin(&view);
    CHECK(chest_break(left) && world_cell(W,Y,0)==AIR && !chest_trap_power(right) &&
        !chest_view_valid(&view) && drop_count(TRAPPED_CHEST_ITEM)==1 && drop_count(DIAMOND)==3);

    /* Real player aim and tap route place and open, never equip a fake item. */
    CHECK(set_block(1,Y,3,STONE) && set_block(1,Y,4,AIR));
    inventory[0]=stack(TRAPPED_CHEST_ITEM,2);
    selected_slot=0; px=1.5f; py=Y-1.05f; pz=6.5f; yaw=pitch=0;
    CHECK(target().hit && target().z==3 && game_tap().message && inventory[0].count==1 &&
        world_cell(1,Y,4)==TRAPPED_CHEST_CELL);
    Interaction tapped=game_tap();
    CHECK(tapped.panel==INTERACTION_CHEST && tapped.hit.z==4);
    printf("TRAPPED CHEST TEST craft/place/mixed-pair/seam/power/comparator/hopper/dimension/drop failures=%d\n",bad);
    return bad;
}
