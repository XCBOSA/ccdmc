#include "../game/drops.h"
#include "../game/interaction.h"
#include "../game/item_custom.h"
#include "../game/item_frames.h"
#include "../game/item_icons.h"
#include "../game/mining_press.h"
#include "../game/navigation.h"
#include "../world/rails.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FRAME TEST line %d: %s\n",__LINE__,#c); ++failures; } } while (0)

static void face_south_wall(void) { px=.5f; py=4.f; pz=3.5f; yaw=0.f; pitch=0.f; }
static int loose_count(int item) {
    int n=0;
    for (int i=0;i<MAXDROP;++i) if (drops[i].item.type==item) n+=drops[i].item.count;
    return n;
}
int item_frame_selftest(void) {
    failures=0;
    if (!world_begin_options(1712,GENERATOR_CURRENT,WORLD_SUPERFLAT,1)) return 1;
    generate(); player_reset_rules(SURVIVAL); face_south_wall();
    inventory[0]=(ItemStack){STICK,8,0,0,0};
    inventory[1]=(ItemStack){LEATHER,1,0,0,0};
    const int recipe[]={0,0,0,0,1,0,0,0,0};
    CHECK(craft_preview(recipe,3).type==ITEM_FRAME_ITEM && craft_take(recipe,3));
    CHECK(!loose_count(ITEM_FRAME_ITEM));
    ItemIcon icon;
    CHECK(item_icon(ITEM_FRAME_ITEM,&icon) && icon.path[0] && *icon.path[0]);
    CHECK(set_block(0,5,0,STONE));
    int frame_slot=-1;
    for (int i=0;i<SLOT_COUNT;++i) if (inventory[i].type==ITEM_FRAME_ITEM) {frame_slot=i;break;}
    CHECK(frame_slot>=0 && frame_slot<HOTBAR_SIZE);
    if (frame_slot<0 || frame_slot>=HOTBAR_SIZE) return failures+1;
    selected_slot=frame_slot;
    CHECK(game_tap().message && !inventory[selected_slot].count);
    ItemFrame snapshot;
    const ItemFrame *frames=item_frames_in(OVERWORLD);
    CHECK(item_frame_aimed(&snapshot) && snapshot.present && snapshot.face==SOUTH && !snapshot.item.count);
    CHECK(world_cell(0,5,1)==AIR); /* Frame isn't a collidable cube. */
    ItemStack named={IRON_AXE,1,11,0,0};
    CHECK(item_set_custom(&named,2,"远征斧"));
    inventory[selected_slot]=named;
    CHECK(game_tap().message && !inventory[selected_slot].count && item_same_kind(frames[0].item,named));
    CHECK(!strcmp(item_frame_aimed_name(NULL),"远征斧"));
    for (unsigned rot=1;rot<=4;++rot) {
        CHECK(game_tap().message && frames[0].rotation==(rot&3));
    }
    CHECK(set_block(0,5,2,STONE) && !item_frame_aimed(NULL));
    CHECK(set_block(0,5,2,AIR));
    inventory[selected_slot]=(ItemStack){DIAMOND_AXE,1,0,0,0};
    CHECK(!mine_quick_chop(450) && player_mining_aim().duration==450);
    int repeat=1;
    CHECK(mine_hold_action(&repeat) && !repeat && frames[0].present && !frames[0].item.count);
    CHECK(loose_count(IRON_AXE)==1 && world_cell(0,5,0)==STONE);
    CHECK(mine() && !frames[0].present && loose_count(ITEM_FRAME_ITEM)==1);
    CHECK(item_frame_restore(OVERWORLD,(ItemFrame){.x=0,.y=5,.z=0,.face=SOUTH,.present=1,.item=named}));
    CHECK(item_frame_restore(NETHER,(ItemFrame){.x=0,.y=5,.z=0,.face=SOUTH,.present=1}));
    CHECK(!item_frame_restore(OVERWORLD,(ItemFrame){.x=0,.y=5,.z=0,.face=SOUTH,.present=1}));
    CHECK(!item_frame_restore(OVERWORLD,(ItemFrame){.x=INT_MAX,.y=5,.z=0,.face=SOUTH,.present=1}));
    for (int i=0;i<MAXDROP;++i) drops[i]=make_drop(0,4,0,(ItemStack){STONE,1,0,0,0});
    CHECK(set_block(0,5,0,AIR));
    item_frames_step(1);
    CHECK(frames[0].present && frames[0].detached && frames[0].item.count==1 && !item_frame_aimed(NULL));
    drops[0]=(Drop){0};
    item_frames_step(10);
    CHECK(frames[0].present && !loose_count(IRON_AXE));
    drops[1]=(Drop){0};
    item_frames_step(10);
    CHECK(!frames[0].present && loose_count(IRON_AXE)==1 && loose_count(ITEM_FRAME_ITEM)==1);
    CHECK(item_frames_in(NETHER)[0].present && !item_frames_in(NETHER)[0].detached);
    item_frames_reset(); drops_reset(); world_reset();
    printf("FRAME TEST recipe, touch, four rotations, occlusion, support loss and atomic drops failures=%d\n",failures);
    return failures;
}
