#ifndef VOXEL_TRIPWIRE_H
#define VOXEL_TRIPWIRE_H

#include "../world/redstone.h"

enum { TRIPWIRE_PARTS=5 };
int tripwire_wall(int raw);
int tripwire_floor(int raw);
int tripwire_place_hook(CircuitPos put,CircuitPos against);
int tripwire_place_string(CircuitPos at);
int tripwire_break(CircuitPos at,int with_shears);
void tripwire_wire_removed(CircuitPos at);
void tripwire_body_contact(CircuitPos at,BlockBox body);
unsigned tripwire_links(const int neighbours[4]);
/* Local coordinates; same wire height/orientation in rendering and picking. */
int tripwire_boxes(int raw,int grounded,unsigned links,int powered,BlockBox out[TRIPWIRE_PARTS]);
int tripwire_pick_boxes(CircuitPos at,int raw,BlockBox out[TRIPWIRE_PARTS]);

#endif
