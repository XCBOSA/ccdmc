#ifndef VOXEL_INTERACTION_H
#define VOXEL_INTERACTION_H
#include "game.h"

typedef enum { INTERACTION_NONE, INTERACTION_CHEST, INTERACTION_BREW,
    INTERACTION_ENCHANT, INTERACTION_ANVIL, INTERACTION_WORKBENCH, INTERACTION_FURNACE,
    INTERACTION_HOPPER, INTERACTION_EJECTOR, INTERACTION_VILLAGER, INTERACTION_CART_STORAGE,
    INTERACTION_ENDER_CHEST } InteractionPanel;
typedef struct { InteractionPanel panel; Hit hit; const char *message; int actor,placed; } Interaction;

/* One scene tap. A held active tool takes its action rather than being
   presented to block placement. UI presents any requested panel. */
Interaction game_tap(void);
int game_select_hotbar(int index);
const char *game_dismount(void);
void game_jump_tap(uint64_t now_ms);
void game_clear_jump_taps(void);
#endif
