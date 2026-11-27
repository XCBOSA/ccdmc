#ifndef VOXEL_ACTORS_H
#define VOXEL_ACTORS_H

#include "game.h"
#include "../world/barriers.h"
#include "leads.h"
#include "villagers.h"

enum { ACTOR_CAPACITY = 64, ACTOR_FIREBALLS = 24 };
typedef enum { ACTOR_EMPTY, ACTOR_PIG, ACTOR_ZOMBIE, ACTOR_TIDERAY, ACTOR_REEDLIZARD,
    ACTOR_CHICKEN, ACTOR_SPIDER, ACTOR_SHEEP, ACTOR_SKELETON, ACTOR_COW, ACTOR_SILTBACK, ACTOR_ARCHIVE_MAGMA, ACTOR_SHELLBACK,
    ACTOR_VILLAGER, ACTOR_IRON_GOLEM, ACTOR_SNOW_GOLEM, ACTOR_WOLF, ACTOR_OCELOT, ACTOR_SQUID,
    ACTOR_STRANDED_GUIDE, ACTOR_TIDE_CRAB, ACTOR_FAULTBACK, ACTOR_CAVE_SPIDER, ACTOR_LANTERN_MOTH,
    ACTOR_KILN_MOLE, ACTOR_SEEP_BEETLE, ACTOR_TYPES } ActorType;
enum { SEEP_BEETLE_HEALTH=13 };
enum { FAULTBACK_HEALTH=26 };
enum { FAULTBACK_IDLE, FAULTBACK_WINDUP, FAULTBACK_CHARGE, FAULTBACK_RECOVER, FAULTBACK_CALM };
enum { REED_IDLE, REED_WARN, REED_VENT, REED_COOLED };
enum { REED_HEALTH=20, REED_HARVESTED=1, REED_COOL_TICKS=240 };
#define REED_RADIUS .56f
#define REED_HEIGHT .95f
#define REED_REACH 5.7f
#define REED_SPREAD .36f
#define FAULTBACK_RADIUS .76f
#define FAULTBACK_HEIGHT 1.17f
enum { KILN_MOLE_HEALTH=12 };
enum { LANTERN_MOTH_HEALTH=10 };
#define LANTERN_MOTH_RADIUS .28f
#define LANTERN_MOTH_HEIGHT .56f
/* Retain packed actor IDs and old health range in saves, not the old behavior. */
enum { GOLEM_PLAYER_BUILT=1, GOLEM_HEALTH=100, CRAWLER_FACTORY_HEALTH=72, SNOW_GOLEM_HEALTH=24 };
enum { PET_TAMED=1, PET_SITTING=2, WOLF_TAMED=PET_TAMED, WOLF_SITTING=PET_SITTING,
    WOLF_WILD_HEALTH=8, WOLF_TAMED_HEALTH=20 };
enum { CAT_TUXEDO=4, CAT_TABBY=8, CAT_SIAMESE=12, CAT_COAT_MASK=12, CAT_HEALTH=10 };
enum { SQUID_HEALTH=10, SQUID_LOW_Y=46, SQUID_HIGH_Y=62 };
#define SNOW_GOLEM_RADIUS .44f
#define SNOW_GOLEM_HEIGHT .78f
#define GOLEM_RADIUS .46f
#define GOLEM_HEIGHT 1.14f
enum { SHEEP_SHORN=1, SILTBACK_HEALTH=14, SILTBACK_WORK_SECONDS=9 };
#define SHELLBACK_RADIUS .56f
#define SHELLBACK_HEIGHT .79f
enum { SHELLBACK_RIPE=1, SHELLBACK_HEALTH=14, SHELLBACK_GROW_SECONDS=12 };
enum { TIDERAY_HEALTH=16 };
static inline int actors_is_zombie(int type) { return type==ACTOR_ZOMBIE; }
static inline int actors_is_bovine(int type) { return type==ACTOR_COW; }
enum { TIDE_CRAB_WORK_TICKS=360 };
static inline int actors_can_wear_armor(int type) { return actors_is_zombie(type) || type==ACTOR_SKELETON; }
enum { ROAMING_VILLAGER=128, STRANDED_HEALTH=8, GUIDE_REST_TICKS=140 };
typedef struct { VillagerOrigin origin; uint16_t rest_ticks; } StrandedGuideState;
float actors_siltback_raking(Dimension d,int slot);
enum { ANIMAL_GROW_TICKS=24000, ANIMAL_COOLDOWN_TICKS=6000, ANIMAL_LOVE_TICKS=600,
       CHICKEN_EGG_MIN_TICKS=6000, CHICKEN_EGG_MAX_TICKS=12000 };
/* Companion to the actor slot. Timers tick only while this animal is simulated. */
typedef struct { uint16_t grow, cooldown, love, egg; uint8_t managed; } ActorCare;

/* Persistent subset, without timers or references to render resources. */
typedef struct {
    Vec3 feet;
    float heading;
    uint8_t type, health, traits;
} ActorRecord;
typedef struct { uint16_t helmet_wear; uint8_t health_debt, door_breaker; } ActorArmorState;
typedef struct { int x,y,z; unsigned stage; float swing; } ActorDoorAttack;
enum { HOSTILE_FIREBALL, HOSTILE_ARROW, THROWN_EGG, THROWN_SNOWBALL, THROWN_WEAKNESS, THROWN_WEAKNESS_LONG,
    THROWN_WATER_BREATHING, THROWN_WATER_BREATHING_LONG,
    THROWN_NIGHT_VISION, THROWN_NIGHT_VISION_LONG };
int actors_launch_egg(Vec3 from,Vec3 direction);
int actors_launch_snowball(Vec3 from,Vec3 direction);
const char *actors_throw_snowball(void);
const char *actors_throw_splash(void);
int actors_launch_splash(Vec3 from,Vec3 direction,int item);
int actors_splash_item(int projectile_kind);
const char *actors_resupply_guide(Hit blocking);
const char *actors_take_affliction_note(void);
StrandedGuideState actors_stranded_state(Dimension d,int slot);
int actors_restore_stranded(Dimension d,ActorRecord a,StrandedGuideState state);
int actors_restore_roaming(Dimension d,ActorRecord a,VillagerSave market);
int actors_validate_stranded_origins(void);
typedef struct { Vec3 point, velocity; float seconds; int kind,source; } ActorFireball;

unsigned actors_revision(void);
void actors_anvil_impact(BlockBox strike,int damage);
void actors_explosion_hits(const int damage[ACTOR_CAPACITY]);
void actors_lightning(Vec3 point);
void actors_piston_push(BlockBox sweep,int direction);
ActorDoorAttack actors_door_attack(Dimension d,int index);
float actors_squid_pulse(Dimension d,int index);
float actors_golem_swing(Dimension d,int index);
const char *actors_camp_machine_use(Hit blocking);
int actors_on_fire(Dimension d,int index);
const ActorRecord *actors_in(Dimension dim);
const ActorCare *actors_care_in(Dimension dim);
int actors_is_livestock(int type);
int actors_has_care(int type);
int actors_wolf_angry(Dimension d,int index);
int actors_cat_sitting(Dimension d,int index);
int actors_cat_blocks_chest(int x,int y,int z);
int actors_mole_warm(Dimension d,int index);
int actors_visit_heat_bench(Hit bench);
const char *actors_use_kiln_mole(Hit hit);
const ActorFireball *actors_fireballs(void);
void actors_reset(void);
void actors_apply_difficulty(void);
int actors_restore(Dimension dim, ActorRecord record);
/* Runtime-only entity; persistent home and health live in the village ledger. */
int actors_spawn_villager(Vec3 point,int role,int health);
int actors_spawn_dungeon(int type,Vec3 point,float heading);
int actors_spawn_egg(int type,Vec3 point,float heading);
/* Called after the host block has been successfully removed. */
int actors_aimed_villager(Hit blocking);
uint16_t actors_name_id(Dimension d,int slot);
int actors_set_name(Dimension d,int slot,uint16_t name);
const char *actors_aimed_name(Vec3 *head);
const char *actors_use_name_tag(Hit blocking);
int actors_restore_with_care(Dimension dim, ActorRecord record, ActorCare care);
int actors_restore_with_lead(Dimension dim,ActorRecord record,ActorCare care,LeadLink lead);
ActorArmorState actors_armor_state(Dimension dim,int slot);
int actors_restore_snapshot(Dimension dim,ActorRecord record,ActorCare care,LeadLink lead,ActorArmorState armor);
LeadLink *actors_lead_at(Dimension dim,int slot);
const char *actors_use_lead(Hit blocking_hit);
void actors_step(float dt);

/* Returns NULL when a block or empty space was targeted instead. */
const char *actors_attack(Hit blocking_hit);
const char *actors_moth_station(Hit hit);
float actors_moth_wing(Dimension d,int index);
int actors_moth_pollinating(Dimension d,int index);
const char *actors_shear(Hit blocking_hit);
const char *actors_use_livestock(Hit blocking_hit);
const char *actors_use_tide_crab(Hit blocking_hit);
float actors_tide_crab_stride(Dimension dim,int slot);
const char *actors_collect_shell(Hit blocking_hit);
int actors_reed_phase(Dimension d,int slot);
unsigned actors_reed_cooldown(Dimension d,int slot);
int actors_restore_reed_cooldown(Dimension d,int slot,unsigned ticks);
const char *actors_collect_reed(Hit blocking_hit);
const char *actors_sound_ray(Hit blocking_hit);
const char *actors_probe_faultback(Hit blocking_hit);
int actors_faultback_phase(Dimension d,int index);
const char *actors_use_pet(Hit blocking_hit);
int actors_hostile_near(Vec3 position,float horizontal,float vertical);
int actors_hit_arrow(Vec3 point, int damage);
int actors_hit_arrow_from(Vec3 point,int damage,int player);
int actors_overlap_cell(int x, int y, int z);
int actors_body_at(Dimension d,int slot,BlockBox *out);

#endif
