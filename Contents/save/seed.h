#ifndef CCDMC_WORLD_SEED_H
#define CCDMC_WORLD_SEED_H

#include <stdint.h>

enum { WORLD_SEED_TEXT_MAX=256 };
typedef enum { SEED_INVALID=-1, SEED_AUTOMATIC, SEED_EXPLICIT } WorldSeedInput;

/* A blank field automatically assigns a seed; signed ASCII decimals are
   taken literally (including 0). Other valid text uses Java's UTF-16 hash. */
WorldSeedInput world_seed_parse(const char *text,uint64_t *seed);
int64_t world_seed_signed(uint64_t seed);

#endif
