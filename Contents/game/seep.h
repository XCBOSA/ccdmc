#ifndef CCDMC_SEEP_H
#define CCDMC_SEEP_H
#include "game.h"

/* Count wet faces without requesting chunks. -1: no exposed top; -2: missing
   neighbors. Never select a softer material from partially loaded data. */
int seep_wet_faces(int x,int y,int z);
const char *seep_use(Hit hit);
const char *settling_mold_use(Hit hit);

#endif
