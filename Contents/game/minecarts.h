#ifndef VOXEL_MINECARTS_H
#define VOXEL_MINECARTS_H

#include "game.h"
#include "../world/rails.h"

enum { CART_CAPACITY = 24, CART_STORAGE_SLOTS=27, HOPPER_CART_SLOTS=5, HOPPER_CART_TICKS=4 };
typedef enum { CART_RIDEABLE, CART_TNT, CART_CHEST, CART_HOPPER, CART_FURNACE } CartKind;
enum { CART_TNT_FUSE=80, CART_TNT_IMPACT_PENDING=CART_TNT_FUSE+1,
       CART_TNT_PROTECTED_PENDING=CART_TNT_FUSE+2 };
enum { CART_FUEL_PER_COAL=3600, CART_FUEL_MAX=32767 };
#define CART_BODY_RADIUS .52f
#define CART_BODY_HEIGHT .54f
#define CART_FREE_HORIZONTAL_LIMIT 8.f
#define CART_FREE_VERTICAL_LIMIT 16.f
typedef struct {
    Vec3 position;
    RailCell track;
    float along,speed,heading;
    int occupied;
    uint8_t kind,fuse;
    uint8_t off_track;
    Vec3 free_velocity;
    float fall_distance;
} CartRecord;
typedef struct { Dimension dimension; int slot; uint64_t serial; } CartStorageView;
typedef struct { uint16_t fuel; float x,z; } CartFurnaceDrive;

void carts_reset(void);
unsigned carts_revision(void);
const CartRecord *carts_in(Dimension d);
int cart_restore(Dimension d,CartRecord record);
int cart_rider(void);
int cart_restore_rider(int index);
void cart_unseat(void);
const char *cart_dismount(void);
const char *cart_use(void);
const char *cart_use_with_storage(int *open_slot);
int cart_launch_on_rail(RailCell spot);
int cart_launch_tnt_on_rail(RailCell spot);
int cart_launch_chest_on_rail(RailCell spot);
int cart_launch_hopper_on_rail(RailCell spot);
int cart_launch_furnace_on_rail(RailCell spot);
const char *cart_take_down(void);
int carts_step(float dt,int keyboard_side,int keyboard_front);
int cart_overlaps_cell(int x,int y,int z);
int cart_on_track(RailCell spot);
void carts_tick_fuses(void);
void carts_tick_furnaces(void);
void carts_tick_hoppers(int player_x,int player_z);
void carts_activate_mineshaft(Vec3 player);
void cart_blast_hit(int index);

const ItemStack *cart_cargo_in(Dimension d,int slot);
int cart_cargo_count(Dimension d,int slot);
int cart_restore_cargo(Dimension d,int slot,const ItemStack *items,int count);
int cart_hopper_cooldown(Dimension d,int slot);
int cart_restore_hopper_cooldown(Dimension d,int slot,int ticks);
int cart_open_storage(int slot,CartStorageView *view);
int cart_storage_valid(const CartStorageView *view);
ItemStack cart_storage_item(const CartStorageView *view,int index);
int cart_storage_move(const CartStorageView *view,int from,int to,int single);
int cart_container_at(int x,int y,int z);
int cart_container_insert(int slot,ItemStack item);
int cart_container_extract(int slot,int index);
int cart_detector_signal(RailCell spot);
int cart_furnace_drive(Dimension d,int slot,CartFurnaceDrive *out);
int cart_restore_furnace_drive(Dimension d,int slot,CartFurnaceDrive drive);

#endif
