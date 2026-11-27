#include "world_test.h"
#include "../game/game.h"
#include "../game/brewing.h"
#include "../game/actors.h"
#include "../game/dispenser_actions.h"
#include "../game/interaction.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { printf("WATER BREATHING TEST line %d: %s\n",__LINE__,#x); ++failures; } } while (0)



int water_breathing_selftest(void) {
    int failures=0;
    enum {Y=170};
    world_begin(1712);
    generate();
    CHECK(game_set_difficulty(NORMAL));
    player_reset_rules(SURVIVAL);
    CHECK(set_block(3,Y,3,BREWING_STAND));
    inventory_reset();
    for (int i=0;i<3;++i) {
        inventory[i]=(ItemStack){WATER_BOTTLE,1,0,0,0};
        CHECK(brew_deposit(3,Y,3,i,i));
    }
    inventory[3]=(ItemStack){PUFFERFISH,1,0,0,0};
    px=3.5f; py=Y+.02f; pz=5.65f; yaw=0.f; pitch=-.47f; selected_slot=3;
    player_rules.food=13;
    Interaction opening=game_tap();
    CHECK(opening.panel==INTERACTION_BREW && inventory[3].type==PUFFERFISH &&
        inventory[3].count==1 && !player_effects.poison);
    CHECK(!brew_dose(BREW_QUICK,PUFFERFISH).output && !brew_deposit(3,Y,3,BREW_INGREDIENT,3));
    inventory[3]=(ItemStack){SEEP_TILE_ITEM,6,0,0,0};
    CHECK(brew_deposit(3,Y,3,BREW_INGREDIENT,3));
    for (int i=0;i<5;++i) brewing_advance(1.f);
    const BrewingStand *stand=brew_at(3,Y,3);
    if (!stand) return failures+1;
    CHECK(stand->progress==100 && stand->slots[0].type==WATER_BOTTLE && stand->slots[BREW_INGREDIENT].count==6);
    brewing_advance(1.f);
    CHECK(stand->slots[0].type==POTION_WATER_BREATHING && stand->slots[1].type==WATER_BOTTLE &&
        stand->slots[2].type==WATER_BOTTLE && stand->slots[BREW_INGREDIENT].count==5);
    CHECK(brew_withdraw(3,Y,3,0) && inventory[0].type==POTION_WATER_BREATHING);
    for (int i=0;i<15;++i) brewing_advance(1.f);
    CHECK(brew_withdraw(3,Y,3,1) && inventory[1].type==POTION_WATER_BREATHING_LONG);
    for (int i=0;i<10;++i) brewing_advance(1.f);
    CHECK(brew_withdraw(3,Y,3,2) && inventory[2].type==SPLASH_WATER_BREATHING);
    CHECK(!brew_at(3,Y,3));

    for (int dx=-1;dx<=1;++dx) for (int dz=-1;dz<=1;++dz) {
        CHECK(set_block(dx,Y-1,dz,STONE));
        for (int dy=0;dy<3;++dy) CHECK(set_block(dx,Y+dy,dz,(dx || dz) ? STONE : WATER));
    }
    px=pz=.5f; py=Y+.02f; vy=0; player_rules.food=13;
    /* Use a real unmodified air tick before drinking. */
    for (int i=0;i<20;++i) game_step(.05f,0,0,0,0);
    CHECK(player_rules.air==MAX_AIR-1);
    selected_slot=1;
    CHECK(player_use_bottle() && inventory[1].type==GLASS_BOTTLE &&
        player_effects.water_breathing==WATER_BREATHING_LONG_TICKS);
    selected_slot=0;
    CHECK(player_use_bottle() && inventory[0].type==GLASS_BOTTLE &&
        player_effects.water_breathing==WATER_BREATHING_LONG_TICKS);
    for (int i=0;i<340;++i) game_step(.05f,0,0,0,0);
    CHECK(player_rules.air==MAX_AIR-1 && player_rules.health==MAX_HEALTH &&
        player_effects.water_breathing==WATER_BREATHING_LONG_TICKS-340);
    player_effects.water_breathing=1;
    for (int i=0;i<20;++i) game_step(.05f,0,0,0,0);
    CHECK(!player_effects.water_breathing && player_rules.air==MAX_AIR-2);
    player_rules.air=0;
    CHECK(player_grant_water_breathing(WATER_BREATHING_TICKS));
    for (int i=0;i<20;++i) game_step(.05f,0,0,0,0);
    CHECK(!player_rules.air && player_rules.health==MAX_HEALTH);
    inventory[0]=(ItemStack){MILK_BUCKET,1,0,0,0};
    CHECK(actors_use_livestock(target()) && !player_effects.water_breathing);
    for (int i=0;i<20;++i) game_step(.05f,0,0,0,0);
    CHECK(player_rules.health==MAX_HEALTH-2);

    actors_reset();
    CHECK(set_block(8,Y-1,0,STONE) && set_block(18,Y-1,0,STONE));
    px=8.5f; py=Y+.02f; pz=.5f; vy=0; yaw=0; pitch=-1.44f;
    selected_slot=2;
    CHECK(!player_use_bottle() && inventory[2].count==1);
    Interaction used=game_tap();
    CHECK(used.message && strstr(used.message,"助潜雾剂") && !inventory[2].count);
    for (int i=0;i<10;++i) actors_step(.05f);
    CHECK(player_effects.water_breathing>0 && player_effects.water_breathing<7200);
    player_effects.water_breathing=0;
    CHECK(actors_launch_splash((Vec3){18.5f,Y+.2f,.5f},(Vec3){0,-1,0},SPLASH_WATER_BREATHING));
    for (int i=0;i<10;++i) actors_step(.05f);
    CHECK(!player_effects.water_breathing);
    ItemStack captured={SPLASH_WATER_BREATHING,1,0,0,0};
    DispenseOutlet dispenser={.x=8,.y=Y+2,.z=0,.dy=-1,.muzzle={8.5f,Y+2.3f,.5f}};
    CHECK(dispenser_action(&captured,1,0,dispenser)==DISPENSE_USED && !captured.count);
    for (int i=0;i<10;++i) actors_step(.05f);
    CHECK(player_effects.water_breathing==2700 && !player_effects.weakness);
    player_take_damage(DAMAGE_VOID,MAX_HEALTH);
    CHECK(!player_rules.health && !player_effects.water_breathing && !player_grant_water_breathing(100));

    player_reset_rules(CREATIVE);
    inventory[2]=(ItemStack){POTION_WATER_BREATHING,1,0,0,0};
    CHECK(player_use_bottle() && inventory[2].type==POTION_WATER_BREATHING);
    game_step(.05f,0,0,0,0);
    CHECK(player_effects.water_breathing==WATER_BREATHING_TICKS-1);
    printf("WATER BREATHING TEST brew/drink/swim/expiry/splash/dispenser/milk/creative failures=%d\n",failures);
    return failures;
}
