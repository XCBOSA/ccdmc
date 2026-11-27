#ifndef CCDMC_VILLAGERS_H
#define CCDMC_VILLAGERS_H

#include "game.h"
#include "../world/villages.h"

enum { VILLAGER_HEALTH=20, VILLAGER_OFFERS=3, VILLAGER_RESTOCK_TICKS=40, WORKSHOP_HANDOFFS=2 };
enum { VILLAGER_WANDER, VILLAGER_WAIT, VILLAGER_ADVANCE };
typedef enum { VILLAGER_FARMER, VILLAGER_LIBRARIAN, VILLAGER_PRIEST, VILLAGER_SMITH, VILLAGER_BUTCHER } VillagerRole;
typedef uint32_t VillagerSession;
typedef struct { ItemStack cost[2], result; int remaining; const char *name; } VillagerOffer;
typedef struct {
    int x,y,z;
    uint8_t home,health,unlocked,uses[VILLAGER_OFFERS],restock;
} VillagerSave;
typedef struct {
    VillagerSave market;
    uint8_t linked;
} VillagerOrigin;
typedef int (*VillagerVisitor)(const VillagerSave *save,void *context);

void villagers_reset(void);
void villagers_clear_actors(void);
void villagers_step(unsigned elapsed);
void villagers_actor_removed(int slot);
void villagers_hurt(int slot,unsigned health);
int villagers_market_valid(VillagerSave market);
int villagers_roaming_market(int slot,VillagerSave *out);
int villagers_bind_roaming(int slot,VillagerSave market);
int villagers_mark_stranded(int slot,VillagerOrigin *out);
int villagers_origin_valid(VillagerOrigin source);
int villagers_rejoin(int slot,VillagerOrigin source);
int villagers_can_walk(int slot,Vec3 to);
int villagers_navigate(int slot,Vec3 feet,float dt,Vec3 *waypoint);
void villagers_path_blocked(int slot);
int villagers_open_path_door(int slot,Vec3 next,float radius,float height);
void villagers_door_changed(int x,int y,int z);
int villagers_visit(Dimension dimension,VillagerVisitor visitor,void *context);
int villagers_restore(VillagerSave save);
int villagers_restore_named(VillagerSave save,uint16_t name);
uint16_t villagers_resident_name(VillagerSave save);
void villagers_set_actor_name(int slot,uint16_t name);
const char *villagers_role_name(unsigned role);
VillagerSession villagers_open(int actor);
void villagers_close(VillagerSession session);
int villagers_count(VillagerSession session);
int villagers_role(VillagerSession session);
const char *villagers_workshop_region(VillagerSession session);
int villagers_handoffs_needed(VillagerSession session);
int villagers_offer(VillagerSession session,int index,VillagerOffer *out);
const char *villagers_exchange(VillagerSession session,int index,int *succeeded);

#endif
