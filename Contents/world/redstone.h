#ifndef VOXEL_REDSTONE_H
#define VOXEL_REDSTONE_H
#include "barriers.h"
#include <stddef.h>

enum { LEVER_FLOOR_NS, LEVER_FLOOR_EW, LEVER_CEILING_NS, LEVER_CEILING_EW, LEVER_WALL };
typedef struct { int x,y,z; } CircuitPos;
typedef struct { int mask,raised; } WireShape;
typedef struct {
    CircuitPos position;
    uint8_t remaining,target,cooldown,count;
    uint8_t ages[8];
    uint8_t output; /* Analogue comparator output, including while another edge waits. */
} RedstoneTiming;
enum { REDSTONE_TIMING_FILE_LIMIT=65536 };
typedef int (*RedstoneTimingVisitor)(const RedstoneTiming *record,void *context);
void redstone_reset(void);
void redstone_after_edit(int x,int y,int z,int before,int after);
void redstone_trap_changed(int x,int y,int z);
int redstone_restore(int x,int y,int z,int raw);
void redstone_activate_terrain_device(int x,int y,int z,int raw);
void redstone_step(int x,int z);
int redstone_receives(int x,int y,int z);
int redstone_wire_power(int x,int y,int z);
WireShape redstone_wire_shape(int x,int y,int z);
int redstone_place_wire(int x,int y,int z);
int redstone_place_lever(CircuitPos put,CircuitPos against,int east_west);
int redstone_toggle_lever(int x,int y,int z);
int redstone_lever_boxes(int raw,BlockBox out[4]);
int redstone_place_torch(CircuitPos put,CircuitPos against);
int redstone_place_repeater(int x,int y,int z,int facing);
int redstone_next_delay(int x,int y,int z);
int redstone_repeater_locked(int x,int y,int z);
int redstone_place_comparator(int x,int y,int z,int facing);
int redstone_toggle_comparator(int x,int y,int z);
int redstone_comparator_power(int x,int y,int z);
int redstone_cistern_probe_level(int x,int y,int z,int raw);
int redstone_toggle_cistern_probe(int x,int y,int z,int *next);
void redstone_cistern_changed(int x,int y,int z,int before,int after);
void redstone_container_changed(int x,int y,int z);
int redstone_place_plate(int x,int y,int z,int material);
int redstone_plate_power(int x,int y,int z);
int redstone_plate_item(int raw);
float redstone_plate_height(int x,int y,int z);
int redstone_place_button(CircuitPos put,CircuitPos against,int wooden);
int redstone_press_button(int x,int y,int z);
enum { AIR_RELAY_TICKS=30, AIR_RELAY_POWER=12, AIR_RELAY_COST=2 };
typedef enum {
    AIR_RELAY_ERROR, AIR_RELAY_STARTED, AIR_RELAY_BUSY, AIR_RELAY_UNLOADED, AIR_RELAY_DISCONNECTED, AIR_RELAY_UNCHARGED
} AirRelayResult;
AirRelayResult redstone_press_air_relay(int x,int y,int z,int *remaining);
int redstone_button_power(int x,int y,int z);
int redstone_tripwire_power(int x,int y,int z);
void redstone_tripwire_pulse(CircuitPos one,CircuitPos other);
enum { DETECTOR_RAIL_TICKS=20 }; /* Game ticks before the occupied rail is sampled again. */
void redstone_detector_contact(CircuitPos p);
int redstone_detector_power(int x,int y,int z);
int redstone_button_box(int raw,int pressed,BlockBox *out);
void redstone_plate_contact(int x,int y,int z,int living);
static inline int pressure_interval(int raw) { return raw==PRESSURE_GOLD || raw==PRESSURE_IRON ? 10 : 20; }
int redstone_torch_boxes(int raw,BlockBox out[4]);
size_t redstone_timing_count(Dimension d);
int redstone_visit_timing(Dimension d,RedstoneTimingVisitor visit,void *context);
int redstone_restore_timing(Dimension d,RedstoneTiming record);
int redstone_replace(int x,int y,int z,int raw);
#endif
