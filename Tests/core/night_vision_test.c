#include "world_test.h"
#include "../game/game.h"
#include "../game/brewing.h"
#include "../game/actors.h"
#include "../game/dispenser_actions.h"
#include "../game/interaction.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { printf("NIGHT VISION TEST line %d: %s\n",__LINE__,#x); ++failures; } } while (0)

int night_vision_selftest(void) {
    int failures=0;
    enum {Y=171};
    world_begin(1712);
    generate();
    CHECK(game_set_difficulty(NORMAL));
    player_reset_rules(SURVIVAL);
    inventory_reset();
    inventory[0]=(ItemStack){0};
    actors_reset();
    CHECK(set_block(0,Y-1,0,STONE) && set_block(10,Y-1,0,STONE) && set_block(2,Y,-2,BREWING_STAND));
    px=pz=.5f; py=Y+.02f; vy=0; yaw=0; pitch=-1.44f;
    selected_slot=0;
    CHECK(brew_automation_insert(2,Y,-2,0,(ItemStack){WATER_BOTTLE,1,0,0,0}));
    CHECK(brew_automation_insert(2,Y,-2,1,(ItemStack){SHELL_FLAKE,1,0,0,0}));
    for (int i=0;i<6;++i) brewing_advance(1.f);
    CHECK(brew_withdraw(2,Y,-2,0) && inventory[0].type==POTION_NIGHT_VISION);
    CHECK(!brew_dose(BREW_QUICK,GOLDEN_CARROT).output &&
        brew_dose(BREW_RESERVE,SHELL_FLAKE).output==POTION_NIGHT_VISION_LONG &&
        brew_dose(BREW_MIST,SHELL_FLAKE).output==SPLASH_NIGHT_VISION &&
        brew_splash(SPLASH_NIGHT_VISION).duration==2700 &&
        brew_splash(SPLASH_NIGHT_VISION_LONG).duration==7200);

    const DispenseOutlet down={.x=0,.y=Y+2,.z=0,.dy=-1,.muzzle={.5f,Y+2.3f,.5f}};
    ItemStack dispenser={SPLASH_NIGHT_VISION,1,0,0,0};
    inventory[1]=(ItemStack){SPLASH_NIGHT_VISION_LONG,1,0,0,0};
    selected_slot=0;
    const unsigned long long revision=world_edit_version();
    CHECK(player_use_bottle() && inventory[0].type==GLASS_BOTTLE &&
        player_effects.night_vision==NIGHT_VISION_TICKS && player_night_vision_lift()==1.f &&
        world_edit_version()==revision);
    player_effects.night_vision=NIGHT_VISION_FADE_TICKS/2;
    CHECK(player_night_vision_lift()==.5f);
    for (int i=0;i<NIGHT_VISION_FADE_TICKS/2;++i) game_step(.05f,0,0,0,0);
    CHECK(!player_night_vision_lift());
    CHECK(actors_launch_splash((Vec3){10.5f,Y+.2f,.5f},(Vec3){0,-1,0},SPLASH_NIGHT_VISION));
    for (int i=0;i<10;++i) actors_step(.05f);
    CHECK(!player_effects.night_vision);
    CHECK(dispenser_action(&dispenser,1,0,down)==DISPENSE_USED && !dispenser.count);
    for (int i=0;i<10;++i) actors_step(.05f);
    CHECK(player_effects.night_vision==2700);
    selected_slot=1;
    Interaction used=game_tap();
    CHECK(used.message && strstr(used.message,"辨暗雾剂") && !inventory[1].count);
    for (int i=0;i<10;++i) actors_step(.05f);
    CHECK(player_effects.night_vision>2700 && player_effects.night_vision<7200);
    inventory[1]=(ItemStack){MILK_BUCKET,1,0,0,0};
    CHECK(actors_use_livestock(target()) && !player_effects.night_vision);
    CHECK(player_grant_night_vision(NIGHT_VISION_LONG_TICKS));
    player_take_damage(DAMAGE_VOID,MAX_HEALTH);
    CHECK(!player_effects.night_vision && !player_grant_night_vision(NIGHT_VISION_TICKS));
    player_reset_rules(CREATIVE);
    inventory[1]=(ItemStack){POTION_NIGHT_VISION_LONG,1,0,0,0};
    CHECK(player_use_bottle() && inventory[1].type==POTION_NIGHT_VISION_LONG &&
        player_effects.night_vision==NIGHT_VISION_LONG_TICKS);
    printf("NIGHT VISION TEST brew/drink/fade/splash/dispenser/milk/creative failures=%d\n",failures);
    return failures;
}
