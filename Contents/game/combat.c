#include "combat.h"
#include "enchanting.h"
#include "../world/space.h"
#include "actors.h"
#include "horses.h"
#include "button_arrows.h"
#include <math.h>
#include <string.h>

static unsigned revision;
static FlyingArrow arrows[DIMENSION_COUNT][ARROW_CAPACITY];
static int drawing_slot = -1;
static Dimension drawing_dimension;
static float draw_time;
static const char *message;

static Vec3 add(Vec3 a, Vec3 b) { return (Vec3){a.x+b.x,a.y+b.y,a.z+b.z}; }
static Vec3 scale(Vec3 a,float v) { return (Vec3){a.x*v,a.y*v,a.z*v}; }
static float length2(Vec3 p) { return p.x*p.x+p.y*p.y+p.z*p.z; }
static Vec3 eyes(void) { return (Vec3){px,py+1.55f,pz}; }

void combat_cancel_draw(void) { drawing_slot = -1; draw_time = 0; }
void combat_reset(void) {
    memset(arrows,0,sizeof arrows);
    combat_cancel_draw();
    message = NULL;
    ++revision;
}
unsigned combat_revision(void) { return revision+button_arrows_revision(); }
const FlyingArrow *combat_arrows(void) { return arrows[world_dimension()]; }
const char *combat_take_message(void) {
    const char *active = message;
    message = NULL;
    return active;
}
static int ammo_slot(void) {
    for (int i = 0; i < SLOT_COUNT; ++i) if (inventory[i].type == ARROW && inventory[i].count) return i;
    return -1;
}
static int launch_arrow(Vec3 position,Vec3 velocity,int damage,int recoverable,int machine) {
    FlyingArrow *pool=arrows[world_dimension()];
    for (int i=0;i<ARROW_CAPACITY;++i) if (pool[i].life<=0) {
        pool[i]=(FlyingArrow){position,velocity,5.f,damage,(uint8_t)recoverable,(uint8_t)machine};
        ++revision;
        return 1;
    }
    return 0;
}
int combat_dispenser_arrow(Vec3 position,Vec3 direction) {
    return launch_arrow(position,scale(direction,22.f),4,1,1);
}
const char *bow_use(void) {
    if ((unsigned)selected_slot>=HOTBAR_SIZE) { combat_cancel_draw(); return NULL; }
    ItemStack *held = &inventory[selected_slot];
    if (!player_rules.health || !held->count || held->type != BOW) {
        combat_cancel_draw();
        return NULL;
    }
    int ammo = ammo_slot();
    if (ammo < 0 && player_rules.mode == SURVIVAL) {
        combat_cancel_draw();
        return "背包没有箭";
    }
    Dimension dim = world_dimension();
    if (drawing_slot != selected_slot || drawing_dimension != dim) {
        drawing_slot = selected_slot;
        drawing_dimension = dim;
        draw_time = 0;
        return "正在拉弓，再轻点画面松弦";
    }
    float strength = fminf(1.f,draw_time);
    if (strength < .20f) return "弓还没拉开，再等一下";
    Vec3 dir=forward();
    int power=enchant_level(held->enchants,ENCH_POWER),infinity=enchant_level(held->enchants,ENCH_INFINITY);
    if (launch_arrow(add(eyes(),scale(dir,.32f)),scale(dir,13.f+35.f*strength),
        (int)(2.f+7.f*strength)+(power ? (power+2)/2 : 0),player_rules.mode==SURVIVAL && !infinity,0)) {
        if (player_rules.mode == SURVIVAL) {
            if (!enchant_level(held->enchants,ENCH_INFINITY) && !--inventory[ammo].count)
                inventory[ammo]=(ItemStack){0,0,0,0,0};
            enchant_wear(held,1,0);
        }
        combat_cancel_draw();
        return "已射箭";
    }
    return "飞行中的箭太多，请等一下";
}

static void move_arrows(float dt) {
    FlyingArrow *list = arrows[world_dimension()];
    for (int i = 0; i < ARROW_CAPACITY; ++i) {
        FlyingArrow *a = &list[i];
        if (a->life <= 0) continue;
        ++revision;
        a->life -= dt;
        a->velocity.y -= 12.f*dt;
        Vec3 movement = scale(a->velocity,dt);
        int steps = (int)ceilf(sqrtf(length2(movement))/.15f);
        if (steps < 1) steps = 1;
        Vec3 step = scale(movement,1.f/steps);
        for (int j = 0; j < steps && a->life > 0; ++j) {
            Vec3 previous=a->position;
            Vec3 p = a->position = add(a->position,step);
            if (p.y < 0 || p.y > WY+40 || fabsf(p.x) >= WORLD_SAVE_LIMIT || fabsf(p.z) >= WORLD_SAVE_LIMIT ||
                button_arrow_impact(previous,p,a->recoverable) ||
                world_point_blocked(p.x,p.y,p.z,0)) {
                a->life = 0;
                break;
            }
            if (a->machine && player_rules.mode==SURVIVAL && player_rules.health &&
                fabsf(p.x-px)<.38f && fabsf(p.z-pz)<.38f && p.y>=py && p.y<=py+1.75f) {
                player_take_damage(DAMAGE_PROJECTILE,a->damage);
                a->life=0;
            }
            else if (actors_hit_arrow_from(p,a->damage,!a->machine) || horse_hit_arrow(p,a->damage)) a->life = 0;
        }
    }
}

void combat_step(float dt) {
    if (!(dt > 0 && dt <= .1f)) return;
    if ((unsigned)selected_slot>=HOTBAR_SIZE || drawing_slot != selected_slot || drawing_dimension != world_dimension() ||
        inventory[selected_slot].type != BOW || !player_rules.health) combat_cancel_draw();
    else draw_time = fminf(1.f,draw_time+dt);
    move_arrows(dt);
}
