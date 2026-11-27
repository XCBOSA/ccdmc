#ifndef VOXEL_SURVEY_H
#define VOXEL_SURVEY_H

#include "game.h"

enum { SURVEY_DEPTH=5, SURVEY_VERTICAL_DEPTH=8 };
typedef enum {
    SURVEY_NO_FACE, SURVEY_UNSUITABLE, SURVEY_INCOMPLETE, SURVEY_CLEAR,
    SURVEY_EXPOSED, SURVEY_SEAM
} SurveyState;
typedef enum { SURVEY_CENTER, SURVEY_LEFT, SURVEY_RIGHT, SURVEY_ABOVE, SURVEY_BELOW } SurveyLane;
typedef struct {
    SurveyState state;
    int rock, depth, vertical;
    SurveyLane lane;
} SurveyReading;

/* Probe an already targeted face. Uses resident cells only and never modifies
   the world. Behind an air gap, liquid, or unrelated structure there is no reading. */
SurveyReading survey_read(Hit hit);
const char *survey_tap(Hit hit);
const char *strata_sample_tap(Hit hit);
int strata_gauge_read(Hit hit,SurveyReading lanes[3]);
const char *strata_gauge_tap(Hit hit);

typedef enum { TIDE_DRY, TIDE_UNLOADED, TIDE_NO_EXIT, TIDE_EXIT } TideState;
typedef enum { TIDE_UP, TIDE_NORTH, TIDE_EAST, TIDE_SOUTH, TIDE_WEST, TIDE_DOWN } TideDirection;
typedef struct { TideState state; TideDirection first; unsigned steps; } TideReading;
/* Resident, connected water only. A wall, dry gap or unseen chunk is not a shortcut. */
TideReading tide_read(Vec3 point);
const char *tide_report(Vec3 point,int from_ray);

#endif
