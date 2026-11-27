#ifndef VOXEL_SOUND_H
#define VOXEL_SOUND_H
#include "game.h"
enum { SOUND_RATE=48000, SOUND_CHANNELS=2 };
typedef enum {
    SOUND_STEP, SOUND_MINE, SOUND_PLACE, SOUND_SPLASH, SOUND_PLAYER_HURT,
    SOUND_ROCKET, SOUND_FIREWORK, SOUND_CAT_MEOW, SOUND_CAT_PURR, SOUND_CAT_HURT,
    SOUND_PIG, SOUND_COW, SOUND_SHEEP, SOUND_CHICKEN, SOUND_WOLF,
    SOUND_VILLAGER, SOUND_ZOMBIE, SOUND_SKELETON, SOUND_SHELLBACK, SOUND_SPIDER,
    SOUND_SILTBACK, SOUND_TIDERAY, SOUND_REEDLIZARD, SOUND_LANTERN_MOTH, SOUND_KILN_MOLE,
    SOUND_SEEP_BEETLE, SOUND_GOLEM, SOUND_SQUID, SOUND_HORSE,
    SOUND_AIR_PUMP, SOUND_FAULTBACK, SOUND_TIDE_CRAB, SOUND_TYPES
} SoundKind;
void sound_clear(void);
int sound_active(void);
void sound_play(SoundKind kind,float gain,unsigned variation,Vec3 point);
void sound_material(SoundKind kind,int raw,Vec3 point);
void sound_creature(int type,int hurt,int calm,Vec3 point,unsigned variation);
/* Main-thread synthesis: no locks, heap allocations, or audio-device callbacks. */
void sound_mix(float *stereo,size_t frames);
#endif
