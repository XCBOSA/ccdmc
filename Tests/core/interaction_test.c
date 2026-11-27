#include "../game/interaction.h"
#include "../game/actors.h"
#include "../game/armor.h"
#include "../game/button_arrows.h"
#include "../game/combat.h"
#include "../game/fishing.h"
#include "../game/horses.h"
#include "../world/redstone.h"
#include <stdio.h>

#define CHECK_TAP(e) do { if (!(e)) { printf("TAP TEST failed line %d: %s\n",__LINE__,#e); ++failures; } } while (0)
static void aim_lane(int x) { px=x+.5f; py=149.01f; pz=2.55f; yaw=pitch=0.f; }
static void equip(int type,int count) {
    selected_slot=0;
    inventory[0]=(ItemStack){(uint16_t)type,(uint8_t)count,0,0,0};
}
int interaction_selftest(void) {
    int failures=0;
    world_begin(410322);
    generate();
    player_reset_rules(SURVIVAL);
    inventory_reset();
    for (int x=-4;x<=8;++x) for (int z=-3;z<=4;++z) CHECK_TAP(set_block(x,148,z,STONE));
    CHECK_TAP(set_block(0,150,0,CHEST) && set_block(3,150,0,STONE) &&
        set_block(6,150,-1,STONE) && redstone_place_button((CircuitPos){6,150,0},(CircuitPos){6,150,-1},1));
    aim_lane(0);
    CHECK_TAP(game_tap().panel==INTERACTION_CHEST);
    equip(FISHING_ROD,1);
    CHECK_TAP(game_tap().panel==INTERACTION_NONE && fishing_view().phase==FISH_CAST);
    CHECK_TAP(game_tap().panel==INTERACTION_NONE && !fishing_active());
    equip(APPLE,2);
    player_rules.food=6;
    CHECK_TAP(game_tap().panel==INTERACTION_NONE && player_rules.food==10 && inventory[0].count==1);
    equip(IRON_HELMET,1);
    CHECK_TAP(game_tap().panel==INTERACTION_NONE && armor_worn(ARMOR_HEAD).type==IRON_HELMET);
    equip(IRON_INGOT,2);
    CHECK_TAP(game_tap().panel==INTERACTION_CHEST && inventory[0].count==2);
    aim_lane(3);
    CHECK_TAP(!game_tap().message && block(3,150,1)==AIR);
    equip(PLANKS,3);
    CHECK_TAP(game_tap().message && block(3,150,1)==PLANKS && inventory[0].count==2);
    equip(EGG,1);
    CHECK_TAP(game_tap().message && inventory[0].count==0 &&
        actors_fireballs()[0].kind==THROWN_EGG && actors_fireballs()[0].seconds>0);
    aim_lane(6);
    equip(AIR,0);
    CHECK_TAP(game_tap().message && redstone_button_power(6,150,0)==15);
    for (int n=0;n<30;++n) redstone_step(6,0);
    CHECK_TAP(!redstone_button_power(6,150,0));
    equip(BOW,1);
    inventory[1]=(ItemStack){ARROW,3,0,0,0};
    CHECK_TAP(game_tap().message && inventory[1].count==3 && !redstone_button_power(6,150,0));
    for (int n=0;n<20;++n) combat_step(.05f);
    CHECK_TAP(game_tap().message && inventory[1].count==2 && !redstone_button_power(6,150,0));
    for (int n=0;n<7;++n) combat_step(.025f);
    CHECK_TAP(redstone_button_power(6,150,0)==15 && button_arrow_present(6,150,0));
    combat_cancel_draw();
    actors_reset();

    player_reset_rules(CREATIVE);
    inventory_reset();
    CHECK_TAP(!inventory[4].count && game_select_hotbar(4) && selected_slot==4 && !inventory[4].count);
    CHECK_TAP(!game_select_hotbar(-1) && !game_select_hotbar(HOTBAR_SIZE) && selected_slot==4);
    CHECK_TAP(inventory_creative_pick(STONE) && inventory[4].type==STONE && inventory[4].count==STACK_MAX &&
        inventory[0].type==DIRT && inventory[0].count==10 && inventory[1].type==WATER);
    CHECK_TAP(game_select_hotbar(6) && !inventory[6].count && selected_slot==6);
    inventory_move(4,6);
    CHECK_TAP(!inventory[4].count && inventory[6].type==STONE && inventory[6].count==STACK_MAX);
    game_clear_jump_taps();
    vy=0; grounded=1;
    game_jump_tap(1000);
    CHECK_TAP(!player_rules.flying && vy>0.f);
    game_jump_tap(1240);
    CHECK_TAP(player_rules.flying && vy==0.f);
    game_jump_tap(1800);
    CHECK_TAP(player_rules.flying);
    game_jump_tap(2100);
    CHECK_TAP(!player_rules.flying);
    game_jump_tap(2500);
    game_jump_tap(2890);
    CHECK_TAP(!player_rules.flying);

    inventory_reset();
    aim_lane(0);
    pz=3.10f;
    CHECK_TAP(horse_restore(OVERWORLD,
        (HorseRecord){{.5f,149.01f,1.65f},0,0,0,0,EQUINE_HORSE,0,20,20,1,1,100,1,1,{0},0}));
    CHECK_TAP(game_tap().message && horse_rider()>=0);
    game_jump_tap(4000);
    game_jump_tap(4200);
    CHECK_TAP(!player_rules.flying && horse_rider()>=0 && !game_tap().message);
    CHECK_TAP(game_dismount() && horse_rider()<0);
    player_reset_rules(SURVIVAL);
    game_clear_jump_taps();
    game_jump_tap(5000);
    game_jump_tap(5190);
    CHECK_TAP(!player_rules.flying);
    world_reset();
    printf("TAP TEST active tools/placement/panels/projectiles/empty hotbar/double jump/mount failures=%d\n",failures);
    return failures;
}
