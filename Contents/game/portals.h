#ifndef VOXEL_PORTALS_H
#define VOXEL_PORTALS_H
#include "game.h"

typedef struct { int valid; Vec3 position; } EndReturn;
void portals_reset(void);
EndReturn portals_end_return(void);
void portals_set_end_return(EndReturn point);
/* On loading an older save only: evacuate the player without erasing the archived region. */
int portals_return_from_retired_site(void);
/* NULL means the held item did not claim the use action. */
const char *portal_item_use(Hit hit);
enum { AQUIFER_SAMPLE_BRINE=1, AQUIFER_SAMPLE_VENT=2, AQUIFER_SAMPLES=3 };
const char *portals_use_aquifer(Hit hit);
unsigned portals_aquifer_samples(void);
int portals_aquifer_vent_danger(void);
int portals_restore_aquifer(unsigned samples);
unsigned portals_pressure_relief(void);
void portals_reload_pressure_relief(void);
/* Called only during active gameplay. May switch the dimension. */
const char *portals_step(float dt);

#endif
