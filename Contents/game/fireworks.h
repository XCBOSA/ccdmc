#ifndef VOXEL_FIREWORKS_H
#define VOXEL_FIREWORKS_H
#include "game.h"

enum { FW_SMALL, FW_LARGE, FW_STAR, FW_SPIRAL, FW_BURST, FW_SHAPES };
enum { FW_TRAIL=1, FW_TWINKLE=2, FW_MAX_STARS=7, FW_VARIANTS=4096, FW_FLIGHTS=12, FW_SPARKS=256 };
typedef struct { uint16_t colors,fade; uint8_t shape,flags; } FireworkStar;
/* Flight zero is a stand-alone star. A rocket can have zero or more stars. */
typedef struct { uint8_t flight,count; FireworkStar star[FW_MAX_STARS]; } FireworkSpec;
typedef struct {
    Vec3 point;
    FireworkSpec spec;
    float age,fuse,burst,angle;
    uint32_t seed;
} FireworkFlight;
typedef struct { Vec3 point; float size; unsigned color; } FireworkSpark;

static inline int firework_item(int type) { return type==FIREWORK_STAR || type==FIREWORK_ROCKET; }
void fireworks_reset(void);
const FireworkSpec *firework_spec(ItemStack item);
/* Intern only on successful inventory operations or while decoding a save. */
int firework_intern(ItemStack *item,FireworkSpec spec,int *created);
void firework_rollback(int created);
unsigned firework_variant_count(void);
ItemStack firework_recipe(const ItemStack *grid,int slots,FireworkSpec *spec);
void firework_describe(const FireworkSpec *spec,char *text,size_t capacity);

int firework_launch(ItemStack item,Vec3 point,float angle);
const char *firework_use(Hit hit);
void fireworks_step(float dt);
const FireworkFlight *fireworks_in(Dimension d);
unsigned fireworks_revision(void);
unsigned firework_sparks(const FireworkFlight *flight,FireworkSpark out[FW_SPARKS]);

#endif
