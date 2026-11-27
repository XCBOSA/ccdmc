#ifndef VOXEL_SAVE_H
#define VOXEL_SAVE_H

#include <stddef.h>
#include <stdint.h>
#include "../game/game.h"
#include "../world/flat.h"

enum { SAVE_NAME_MAX = 60, CURRENT_FORMAT=114, EMPTY_HORSE_GEAR_BYTES=DIMENSION_COUNT,
    EMPTY_ITEM_FRAMES_BYTES=2*DIMENSION_COUNT,
    EMPTY_LEAF_WORK_BYTES=4*DIMENSION_COUNT };
typedef struct {
    uint32_t id;
    uint64_t seed;
    char name[SAVE_NAME_MAX + 1];
    int readable;
    GameMode mode;
    int hardcore, hardcore_dead;
    unsigned format;
    int has_maps, has_explicit_seed;
    int structures_disabled, bonus_chest;
    int generation;
    WorldProfile profile;
    FlatLayout flat;
    Dimension dimension;
    int returned_from_retired_site; /* UI notice for this load only; not serialized. */
} SaveInfo;

/* Relative folders are scoped to the project in CCD. */
int save_open_directory(const char *directory);
const char *save_error(void);
int save_list(SaveInfo **out, size_t *count);
const SaveInfo *save_current(void);
int save_create(const char *name);
int save_create_in_mode(const char *name, GameMode mode);
int save_create_hardcore(const char *name);
int save_create_with_generation(const char *name, GameMode mode, int generation);
int save_create_seeded_generation(const char *name,GameMode mode,int generation,uint64_t seed);
int save_create_seeded(const char *name,GameMode mode,int hardcore,uint64_t seed);
/* A null seed retains automatic assignment. */
int save_create_profiled(const char *name,GameMode mode,int hardcore,WorldProfile profile,const uint64_t *seed);
int save_create_options(const char *name,GameMode mode,int hardcore,WorldProfile profile,
    const uint64_t *seed,int structures_disabled,int bonus_chest);
int save_create_layout(const char *name,GameMode mode,int hardcore,WorldProfile profile,
    const uint64_t *seed,int structures_disabled,int bonus_chest,const FlatLayout *flat);
int save_set_difficulty(GameDifficulty value);
int save_advanced_lighting(void);
int save_set_advanced_lighting(int enabled);
int save_load(uint32_t id);
int save_write(void);
typedef enum { SAVE_CHECKPOINT_FAILED=-1, SAVE_CHECKPOINT_IDLE, SAVE_CHECKPOINT_PENDING,
    SAVE_CHECKPOINT_WRITTEN } SaveCheckpoint;
/* Only call from the game thread. Stage live world data here; the worker
   owns the temporary stream from then until it finishes publishing it. */
SaveCheckpoint save_autosave_begin(void);
SaveCheckpoint save_autosave_poll(void);
void save_leave(void);

#endif
