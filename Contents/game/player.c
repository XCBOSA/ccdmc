#include "game.h"
#include "../world/build_blocks.h"
#include <float.h>
#include "furnace.h"
#include "brewing.h"
#include "anvil.h"
#include "gravity.h"
#include "armor.h"
#include "drops.h"
#include "experience.h"
#include "enchanting.h"
#include "portals.h"
#include "actors.h"
#include "combat.h"
#include "boats.h"
#include "minecarts.h"
#include "mining_press.h"
#include "horses.h"
#include "fishing.h"
#include "item_frames.h"
#include "fireworks.h"
#include "sound.h"
#include "chests.h"
#include "ender_chest.h"
#include "beds.h"
#include "farming.h"
#include "forestry.h"
#include "leaf_decay.h"
#include "tripwire.h"
#include "../world/rails.h"
#include "../world/lava.h"
#include "../world/redstone.h"
#include "hoppers.h"
#include "hydraulic_sorter.h"
#include "droppers.h"
#include "blast.h"
#include "fire.h"
#include "weather.h"
#include "field_supplies.h"
#include "pistons.h"
#include "navigation.h"
#include "../world/space.h"
#include "../world/light.h"
#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

float px = .5f, py, pz = .5f;
float vy, yaw, pitch = -.24f, mx, my;
int grounded, drops_dirty = 1;
static float water_clock;
static float day_fraction;
static float blast_x,blast_z;
static float lava_contact;
static GameDifficulty difficulty=NORMAL;
static PlayerBurn burning;
static uint64_t spawn_seed;
static WorldProfile spawn_profile;
static int spawn_generation, spawn_x, spawn_z, spawn_floor;
PlayerRules player_rules;
PlayerEffects player_effects;
float touch_altitude;

const char *game_mode_name(GameMode mode) { return mode == CREATIVE ? "创造" : "生存"; }

int game_difficulty_valid(int value) { return (unsigned)value<=HARD; }
GameDifficulty game_selected_difficulty(void) { return difficulty; }
GameDifficulty game_effective_difficulty(void) { return player_rules.hardcore ? HARD : difficulty; }
int game_set_difficulty(GameDifficulty value) {
    if (!game_difficulty_valid(value)) return 0;
    if (difficulty!=value && !player_rules.hardcore) player_rules.hurt_time=0.f;
    difficulty=value;
    return 1;
}

void player_reset_rules(GameMode mode) {
    player_rules = (PlayerRules){.mode = mode, .health = MAX_HEALTH, .food = MAX_FOOD,
        .air = MAX_AIR, .fall_origin = py};
    player_effects=(PlayerEffects){0};
    field_reset();
    day_fraction = 0;
    lava_contact = 0;
    player_extinguish();
    touch_altitude = 0;
    blast_x=blast_z=0;
}
PlayerBurn player_burn_state(void) { return burning; }
void player_extinguish(void) { burning=(PlayerBurn){0,0}; }
int player_restore_burn(PlayerBurn state) {
    if (state.remaining>PLAYER_MAX_BURN || state.due>PLAYER_BURN_PERIOD ||
        (!state.remaining && state.due) ||
        (state.remaining && (player_rules.mode!=SURVIVAL || !player_rules.health || player_effects.fire))) return 0;
    burning=state;
    return 1;
}
void player_catch_fire(unsigned ticks) {
    if (player_rules.mode!=SURVIVAL || !player_rules.health || player_effects.fire || player_in_water()) return;
    if (ticks>PLAYER_MAX_BURN) ticks=PLAYER_MAX_BURN;
    if (ticks>burning.remaining) burning.remaining=(uint16_t)ticks;
}
void player_blast_push(Vec3 direction,float strength) {
    if (!player_rules.health || player_rules.mode!=SURVIVAL) return;
    blast_x+=direction.x*strength*8.f;
    blast_z+=direction.z*strength*8.f;
    vy=fminf(8.f,fmaxf(-21.f,vy+direction.y*strength*8.f));
    grounded=0;
}

static int safe_spawn(int x, int z) {
    int h = terrain_height(x, z);
    if (h < world_sea_level() || !solid(block(x, h, z))) return 0;
    if (block(x, h + 1, z) != AIR || block(x, h + 2, z) != AIR) return 0;
    spawn_x = x; spawn_z = z; spawn_floor = h;
    return 1;
}

static void find_spawn(void) {
    int generation = world_generation();
    uint64_t seed = world_seed();
    WorldProfile profile=world_profile();
    if (spawn_generation == generation && spawn_seed == seed && spawn_profile==profile) return;
    spawn_seed = seed;
    spawn_generation = generation;
    spawn_profile=profile;
    spawn_x = spawn_z = 0;
    spawn_floor = terrain_height(0, 0);
    int sea = world_sea_level();
    if (spawn_floor < sea) spawn_floor = sea;
    if (generation == GENERATOR_LEGACY || safe_spawn(0, 0)) return;
    /* Ocean seeds need a dry default spawn; search rings on a fixed grid
       without generating any chunk until the column is above sea level. */
    for (int r = 16; r <= 1024; r += 16)
        for (int p = -r; p <= r; p += 16)
            if (safe_spawn(p, -r) || safe_spawn(p, r) ||
                (p > -r && p < r && (safe_spawn(-r, p) || safe_spawn(r, p)))) return;
}

static void move_to_spawn(void) {
    find_spawn();
    px = spawn_x + .5f; pz = spawn_z + .5f;
    py = spawn_floor + 1.01f;
    vy = mx = my = touch_altitude = 0;
    grounded = 0;
    yaw = 0;
    pitch = -.24f;
    player_rules.fall_origin = py;
}
Vec3 player_world_spawn(void) {
    return (Vec3){spawn_x+.5f,spawn_floor+1.f,spawn_z+.5f};
}

RespawnResult player_respawn(void) {
    if (player_rules.hardcore) return RESPAWN_HARDCORE;
    /* Do not strand the player on the death screen if retry also fails. */
    int retained=!player_rules.health && !death_drop_inventory((Vec3){px,py,pz});
    // The world, its edits and the day clock survive respawn.
    unsigned time = player_rules.day_tick;
    uint32_t day = player_rules.world_day;
    GameMode mode = player_rules.mode;
    lead_player_left(world_dimension());
    player_reset_rules(mode);
    player_extinguish();
    combat_cancel_draw();
    fishing_cancel();
    bed_cancel_sleep();
    boat_unseat();
    cart_unseat();
    horse_unseat();
    player_rules.day_tick = time;
    player_rules.world_day = day;
    if (world_dimension() != OVERWORLD) {
        world_select_dimension(OVERWORLD);
        drops_dirty = 1;
    }
    Vec3 beside;
    if (bed_respawn_position(&beside)) {
        px=beside.x;py=beside.y;pz=beside.z;
        vy=mx=my=touch_altitude=0; grounded=0; yaw=0; pitch=-.24f;
    } else move_to_spawn();
    /* World edits may occupy a previously safe column. */
    while (py < WY && collides(px, py, pz)) py += 1;
    player_rules.fall_origin = py;
    return retained ? RESPAWN_RETAINED : RESPAWN_OK;
}

void player_take_damage(DamageSource source,int amount) {
    if (player_rules.mode == CREATIVE || player_rules.health == 0 || amount <= 0) return;
    int fire=source==DAMAGE_DIRECT_FIRE || source==DAMAGE_BURNING;
    if (fire && player_effects.fire) return;
    unsigned protection=enchant_protection(source);
    int armor=0;
    if (source==DAMAGE_MELEE || source==DAMAGE_PROJECTILE || source==DAMAGE_EXPLOSION ||
        source==DAMAGE_DIRECT_FIRE || source==DAMAGE_ANVIL || source==DAMAGE_CONTACT) {
        armor=armor_points();
    }
    int helmet=source==DAMAGE_ANVIL && armor_worn(ARMOR_HEAD).count!=0;
    if (helmet) armor_wear_helmet((unsigned)amount*4u);
    if (armor) armor_wear(amount<4 ? 1u : (unsigned)amount/4);
    int health=player_rules.health*HEALTH_UNITS-player_rules.health_debt;
    uint64_t damage=(uint64_t)amount*(HEALTH_UNITS-armor);
    if (helmet) damage=(damage*3u+2u)/4u;
    if (protection) damage=(damage*(HEALTH_UNITS-protection)+HEALTH_UNITS/2)/HEALTH_UNITS;
    if (source==DAMAGE_STARVATION || source==DAMAGE_POISON) {
        GameDifficulty level=game_effective_difficulty();
        if (source==DAMAGE_STARVATION && level==PEACEFUL) return;
        int floor=(source==DAMAGE_POISON || level==NORMAL ? 1 : level==EASY ? 10 : 0)*HEALTH_UNITS;
        if (health<=floor) return;
        if (damage>(unsigned)(health-floor)) damage=(unsigned)(health-floor);
    }
    health=damage>=(unsigned)health ? 0 : health-(int)damage;
    if (damage) sound_play(SOUND_PLAYER_HURT,.77f,(unsigned)source,(Vec3){px,py+1.25f,pz});
    player_rules.health=(health+HEALTH_UNITS-1)/HEALTH_UNITS;
    player_rules.health_debt=(uint8_t)(player_rules.health*HEALTH_UNITS-health);
    if (bed_sleeping()) bed_cancel_sleep();
    if (!player_rules.health) {
        player_effects.regeneration=player_effects.regeneration_ii=0;
        player_effects.water_breathing=0;
        player_effects.night_vision=0;
            player_extinguish();
        mx=my=vy=touch_altitude=0;
        combat_cancel_draw();
        fishing_cancel();
        boat_unseat();
        cart_unseat();
        horse_unseat();
        experience_death(source==DAMAGE_VOID);
        if (source==DAMAGE_VOID) {
            if (!death_keep_inventory()) death_discard_inventory();
        } else {
            /* If staging fails, keep the original items in the dead state;
               respawning retries at the same position and in the same dimension. */
            death_drop_inventory((Vec3){px,py,pz});
        }
        lead_player_left(world_dimension());
    }
}
void player_afflict_cave_spider_poison(void) {
    GameDifficulty d=game_effective_difficulty();
    if (!player_rules.health || player_rules.mode!=SURVIVAL || d<NORMAL ||
        (player_effects.poison && !player_effects.poison_slow)) return;
    unsigned duration=d==HARD ? CAVE_POISON_HARD : CAVE_POISON_NORMAL;
    if (player_effects.poison<duration) {
        player_effects.poison=(uint16_t)duration;
        player_effects.poison_slow=1;
    }
}
void player_take_mob_damage(DamageSource source,int amount) {
    if (amount<=0) return;
    switch (game_effective_difficulty()) {
    case PEACEFUL: return;
    case EASY: if (amount>2) amount=amount/2+1; break;
    case HARD: amount=amount<=INT_MAX-amount/2 ? amount+amount/2 : INT_MAX; break;
    case NORMAL: break;
    }
    player_take_damage(source,amount);
}
int weakened_melee(int damage,unsigned weakness) { return weakness && damage>0 ? damage-1 : damage; }
static int in_lava(void) {
    return block((int)floorf(px),(int)floorf(py+.8f),(int)floorf(pz))==LAVA;
}

void player_feed(int amount) {
    if (player_rules.mode != SURVIVAL || !player_rules.health || amount <= 0) return;
    int space = MAX_FOOD - player_rules.food;
    player_rules.food += amount > space ? space : amount;
}

void player_jump(void) {
    if (bed_sleeping()) return;
    if (boat_rider()>=0) { boat_dismount(); return; }
    if (cart_rider()>=0) { cart_dismount(); return; }
    if (horse_rider()>=0) { horse_jump(.55f); return; }
    if (!player_rules.health || player_rules.flying) return;
    if (grounded) { vy = 6.6f; grounded = 0; player_rules.exhaustion += .15f; }
    else if (player_in_water() || in_lava()) vy = 4.0f;
}

void player_toggle_flight(void) {
    if (bed_sleeping()) return;
    if (player_rules.mode != CREATIVE) return;
    if (boat_rider()>=0 || cart_rider()>=0 || horse_rider()>=0) return;
    player_rules.flying = !player_rules.flying;
    player_rules.fall_origin = py;
    vy = touch_altitude = 0;
}

float world_daylight(void) {
    float phase = player_rules.day_tick * (6.28318530718f / DAY_TICKS);
    float sun = sinf(phase);
    float shade=weather_kind()==WEATHER_THUNDER ? .48f : .31f;
    return (.23f + .77f * fmaxf(0, fminf(1, .60f + 1.10f * sun))) * (1.f-shade*weather_intensity());
}

void generate(void) {
    sound_clear();
    fireworks_reset();
    item_frames_reset();
    navigation_reset();
    item_names_reset();
    world_reset();
    furnaces_reset();
    brewing_reset();
    chests_reset();
    ender_chest_reset();
    drops_reset();
    experience_reset();
    portals_reset();
    actors_reset();
    combat_reset();
    fishing_reset();
    bed_reset();
    farm_reset();
    boats_reset();
    carts_reset();
    horses_reset();
    inventory_reset();
    player_reset_rules(SURVIVAL);
    spawn_generation=-1;
    move_to_spawn();
    water_clock = 0;
    drops_dirty = 1;
}

typedef struct { int x0, x1, y0, y1, z0, z1; } Body;
static Body body_bounds(float x, float y, float z) {
    const float radius = .28f, height = 1.72f;
    return (Body){(int)floorf(x - radius), (int)floorf(x + radius),
                  (int)floorf(y + .001f), (int)floorf(y + height - .001f),
                  (int)floorf(z - radius), (int)floorf(z + radius)};
}

int collides(float x, float y, float z) {
    return world_box_blocked(body_box(x,y,z,.28f,1.72f),0);
}
static int move_collides(float x,float y,float z) {
    return world_move_blocked(body_box(px,py,pz,.28f,1.72f),body_box(x,y,z,.28f,1.72f),0);
}

Vec3 forward(void) {
    return (Vec3){cosf(pitch) * sinf(yaw), sinf(pitch), -cosf(pitch) * cosf(yaw)};
}

/* Point samples can step over a 1/64-high pad near a cell edge. Within its
   cell, intersect the whole interval instead of sampling the thin slab. */
static int ray_hits_lily(Vec3 eye,Vec3 ray,int x,int y,int z,float from,float to) {
    const float origin[]={eye.x,eye.y,eye.z},direction[]={ray.x,ray.y,ray.z};
    const float lo[]={(float)x,(float)y,(float)z},hi[]={x+1.f,y+LILY_PAD_HEIGHT,z+1.f};
    for (int i=0;i<3;++i) {
        if (fabsf(direction[i])<.000001f) {
            if (origin[i]<lo[i] || origin[i]>=hi[i]) return 0;
        } else {
            const float a=(lo[i]-origin[i])/direction[i],b=(hi[i]-origin[i])/direction[i];
            from=fmaxf(from,fminf(a,b)); to=fminf(to,fmaxf(a,b));
            if (from>to) return 0;
        }
    }
    return 1;
}

typedef struct { float t; int axis, sign; } RaySurface;
static int ray_box_face(Vec3 eye,Vec3 ray,BlockBox b,float limit,RaySurface *out) {
    const float origin[]={eye.x,eye.y,eye.z},d[]={ray.x,ray.y,ray.z};
    const float lo[]={b.x0,b.y0,b.z0},hi[]={b.x1,b.y1,b.z1};
    float near=-FLT_MAX,far=limit;
    int a=-1,s=0;
    for (int i=0;i<3;++i) {
        if (fabsf(d[i])<.000001f) {
            if (origin[i]<lo[i] || origin[i]>=hi[i]) return 0;
            continue;
        }
        float n=((d[i]>0 ? lo[i] : hi[i])-origin[i])/d[i];
        float f=((d[i]>0 ? hi[i] : lo[i])-origin[i])/d[i];
        if (n>near) { near=n; a=i; s=d[i]>0 ? -1 : 1; }
        if (f<far) far=f;
        if (near>far) return 0;
    }
    if (a<0 || near<0.f || far<=.0001f || near>limit) return 0;
    *out=(RaySurface){near,a,s};
    return 1;
}
static int building_surface(Vec3 eye,Vec3 ray,int x,int y,int z,int raw,float reach,RaySurface *surface) {
    BlockBox boxes[BUILD_BOXES];
    int count=build_world_boxes(x,y,z,raw,boxes);
    RaySurface best={reach,-1,0};
    for (int i=0;i<count;++i) {
        RaySurface q;
        if (ray_box_face(eye,ray,boxes[i],best.t,&q) && (best.axis<0 || q.t<best.t)) best=q;
    }
    if (best.axis<0) return 0;
    *surface=best;
    return 1;
}
static int building_hit(Vec3 eye,Vec3 ray,int x,int y,int z,int raw,float reach,Hit *hit) {
    RaySurface best;
    if (!building_surface(eye,ray,x,y,z,raw,reach,&best)) return 0;
    *hit=(Hit){1,x,y,z,x+(best.axis==0 ? best.sign : 0),y+(best.axis==1 ? best.sign : 0),
        z+(best.axis==2 ? best.sign : 0)};
    return 1;
}

/* Cast from the eye to the first solid block, retaining the preceding cell.
   Mode 2 sees flowing water so a pad cannot target a source behind a stream. */
static Hit trace_to(int liquids,int rails,int flames,float reach) {
    Vec3 eye = {px, py + 1.55f, pz}, f = forward();
    Hit hit = {0};
    int cx=(int)floorf(eye.x),cy=(int)floorf(eye.y),cz=(int)floorf(eye.z);
    int lx=cx,ly=cy,lz=cz,part_count=0;
    float partial=0.f;
    BlockBox parts[BARRIER_PARTS];
    int origin=world_cell(cx,cy,cz);
    if (build_partial(origin) && building_hit(eye,f,cx,cy,cz,origin,reach,&hit)) return hit;
    for (float t = .02f; t <= reach; t += .02f) {
        float line_x=eye.x+f.x*t,line_y=eye.y+f.y*t,line_z=eye.z+f.z*t;
        int x=(int)floorf(line_x);
        int y=(int)floorf(line_y);
        int z=(int)floorf(line_z);
        if (x!=cx || y!=cy || z!=cz) {
            lx=cx; ly=cy; lz=cz; cx=x; cy=y; cz=z;
            int raw=world_cell(x,y,z),b=cell_type(raw);
            if (build_partial(raw) && building_hit(eye,f,x,y,z,raw,reach,&hit)) return hit;
            partial=b==BED ? BED_HEIGHT : b==FARMLAND ? FARMLAND_HEIGHT : plant_height(raw);
            part_count=b==CACTUS || b==COCOA ? dye_plant_box(raw,x,y,z,parts) :
                b==ANVIL ? anvil_boxes(x,y,z,raw,parts) : b==HOPPER ? hopper_boxes(x,y,z,raw,parts) :
                b==TRIPWIRE || b==TRIPWIRE_HOOK ? tripwire_pick_boxes((CircuitPos){x,y,z},raw,parts) :
                barrier_type(b) ? barrier_world_boxes(x,y,z,raw,BARRIER_PICK,parts) :
                piston_cell(raw) ? piston_boxes(x,y,z,raw,parts) : 0;
            if (b==ENDER_CHEST) { parts[0]=ender_chest_box(x,y,z); part_count=1; }
            if (b==AIR_OUTLET) { parts[0]=air_outlet_box(x,y,z,raw); part_count=1; }
            if (b==LEVER || b==REDSTONE_TORCH || b==BUTTON) {
                part_count=b==LEVER ? redstone_lever_boxes(raw,parts) : b==REDSTONE_TORCH ?
                    redstone_torch_boxes(raw,parts) : redstone_button_box(raw,redstone_button_power(x,y,z),parts);
                for (int i=0;i<part_count;++i) {
                    parts[i].x0+=x; parts[i].x1+=x;
                    parts[i].y0+=y; parts[i].y1+=y;
                    parts[i].z0+=z; parts[i].z1+=z;
                }
            }
            if (b==PRESSURE_PLATE) {
                part_count=1;
                parts[0]=(BlockBox){x+.0625f,(float)y,z+.0625f,x+.9375f,y+redstone_plate_height(x,y,z),z+.9375f};
            }
            if ((solid(b) && partial==0.f && !part_count) || (rails && rail_type(b)) ||
                (flames && raw==FIRE_CELL) ||
                (liquids && (raw==WATER || raw==LAVA || (liquids==2 && water_strength(raw)))) ||
                (raw==LILY_PAD && ray_hits_lily(eye,f,x,y,z,t-.0201f,reach))) return (Hit){1,x,y,z,lx,ly,lz};
        }
        if (partial>0.f && line_y<y+partial) return (Hit){1,x,y,z,lx,ly,lz};
        for (int i=0;i<part_count;++i) if (box_contains(parts[i],line_x,line_y,line_z))
            return (Hit){1,x,y,z,lx,ly,lz};
    }
    return hit;
}
static Hit trace(int liquids,int rails,int flames) {
    return trace_to(liquids,rails,flames,player_rules.mode==CREATIVE ? 6.f : 5.f);
}
int player_clear_view(float distance) {
    return distance>0.f && distance<=6.f && !trace_to(0,0,0,distance).hit;
}
Hit target(void) { return trace(0,1,0); }

static int axe_material(int type) {
    return is_log(type) || is_chest(type) || type==PLANKS || type==BOOKSHELF ||
        type==CRAFTING_TABLE || type==WOOD_FENCE || type==FENCE_GATE || type==WOOD_DOOR ||
        type==PUMPKIN || type==WOOD_TRAPDOOR || type==LADDER || build_wooden(type);
}
MiningAim player_mining_aim(void) {
    ItemFrame frame;
    if (item_frame_aimed(&frame)) return (MiningAim){1,frame.x,frame.y,frame.z,-2-frame.face,450};
    Hit hit=trace(0,1,1);
    int raw=hit.hit ? world_cell(hit.x,hit.y,hit.z) : AIR;
    unsigned axe=player_rules.mode==SURVIVAL && (unsigned)selected_slot<HOTBAR_SIZE &&
        inventory[selected_slot].count ? axe_chop_milliseconds(inventory[selected_slot].type) : 0;
    int material=cell_type(raw);
    unsigned base=axe && hit.hit && axe_material(material) ? axe : 450;
    if (player_rules.mode==SURVIVAL && (geology_block(material) || coast_stone(material))) {
        ItemStack held=(unsigned)selected_slot<HOTBAR_SIZE ? inventory[selected_slot] : (ItemStack){0};
        const ItemDef *tool=item_def(held.type);
        unsigned tier=tool ? tool->pick_tier : 0;
        base=material==JOINT_BASALT ? 1420u : material==SLATE_ROCK ? 1220u :
            material==LIMESTONE_ROCK ? 760u : 930u;
        base-=tier*95u;
        if ((material==SLATE_ROCK || material==LIMESTONE_ROCK) && base>450u &&
            enchant_level(held.enchants,ENCH_LAYER_CLEAVER)) {
            base=base*3u/4u;
            if (base<450u) base=450u;
        }
    }
    return (MiningAim){hit.hit,hit.x,hit.y,hit.z,raw,base};
}
unsigned mine_hold_milliseconds(void) { return player_mining_aim().duration; }

static const char *mine_with_hold(unsigned quick_ms) {
    if (!player_rules.health) return quick_ms ? NULL : "请先重生";
    if (bed_sleeping()) return quick_ms ? NULL : "睡觉时不能破坏方块";
    if ((unsigned)selected_slot>=HOTBAR_SIZE) return quick_ms ? NULL : "手持物品未选中";
    if (quick_ms) {
        if (item_frame_aimed(NULL)) return NULL;
    } else {
        const char *frame=item_frame_attack();
        if (frame) return frame;
    }
    Hit h = trace(0,1,1);
    if (quick_ms) {
        if (player_rules.mode!=SURVIVAL || !h.hit) return NULL;
        unsigned axe=inventory[selected_slot].count ? axe_chop_milliseconds(inventory[selected_slot].type) : 0;
        unsigned required=axe && axe_material(block(h.x,h.y,h.z)) ? axe : 450;
        if (required>=450 || quick_ms<required) return NULL;
    }
    if (h.hit && block(h.x,h.y,h.z)==FIRE_CELL)
        return set_block(h.x,h.y,h.z,AIR) ? "已熄灭火焰" : "暂时无法灭火";
    Hit blocking=rail_type(h.hit ? block(h.x,h.y,h.z) : AIR) ? trace(0,0,0) : h;
    const char *attack = actors_attack(blocking);
    if (!attack) attack = boat_take_down();
    if (!attack) attack = cart_take_down();
    if (!attack) attack = horse_attack();
    if (attack) return attack;
    if (!h.hit || !inworld(h.x, h.y, h.z)) return "准星范围内没有可挖掘的方块";
    int raw=world_cell(h.x,h.y,h.z),type=cell_type(raw);
    if (aquifer_fixture(raw) || pressure_fixture(raw)) return "固定勘测设施不能拆除";
    if (player_rules.mode==SURVIVAL && (raw==SETTLING_LIME || raw==SETTLING_PACKED))
        return "模床有投料 · 脱模后才能拆除";
    const char *harvest=farm_break(h);
    if (harvest) return harvest;
    if (type==BED) return bed_break((BedPos){h.x,h.y,h.z});
    if (is_chest(type)) {
        const char *result=chest_break((ChestPos){h.x,h.y,h.z});
        ItemStack *held=&inventory[selected_slot];
        if (player_rules.mode==SURVIVAL && held->count && is_axe(held->type) && block(h.x,h.y,h.z)==AIR) {
            enchant_wear(held,1,0);
            if (!held->count) return "斧已损坏";
        }
        return result;
    }
    if (type==TRIPWIRE || type==TRIPWIRE_HOOK) {
        ItemStack *held=&inventory[selected_slot];
        int shears=type==TRIPWIRE && held->count && held->type==SHEARS;
        if (!tripwire_break((CircuitPos){h.x,h.y,h.z},shears)) return "暂时无法拆除绊线";
        if (shears && player_rules.mode==SURVIVAL) enchant_wear(held,1,0);
        return shears ? "已安全剪断绊线" : type==TRIPWIRE ? "已拆除绊线" : "已拆除绊线钩";
    }
    if (type==HOPPER) return hopper_break(h.x,h.y,h.z);
    if (piston_base_type(type) || type==PISTON_HEAD) return piston_break(h);
    int above=block(h.x,h.y+1,h.z),torch=above==TORCH && solid(type);
    int sapling_above=tree_soil(type) ? tree_sapling_drop(world_cell(h.x,h.y+1,h.z)) : AIR;
    int flower=tree_soil(type) && (above==DANDELION || above==POPPY);
    int mushroom=solid(type) && small_mushroom(above);
    int paired=torch || sapling_above!=AIR || flower || mushroom || (above==SNOW_LAYER && solid(type));
    BlockEdit remove[6]={{h.x,h.y,h.z,AIR},{h.x,h.y+1,h.z,AIR}};
    int remove_count=1+paired;
    ItemStack pods[4];
    int pod_count=type==JUNGLE_LOG ? farm_attached_cocoa(h.x,h.y,h.z,remove+remove_count,pods) : 0;
    remove_count+=pod_count;
    if (player_rules.mode == CREATIVE) {
        /* Like the other manual storage blocks, preserve contents on break. */
        if (ejector_type(type) && dropper_at(h.x,h.y,h.z)) {
            const Dropper *d=dropper_at(h.x,h.y,h.z);
            ItemStack items[DROPPER_SLOTS];
            int free_slots[DROPPER_SLOTS],count=0,found=0;
            for (int i=0;i<DROPPER_SLOTS;++i) if (d->slots[i].count) items[count++]=d->slots[i];
            for (int i=0;i<MAXDROP && found<count;++i) if (!drops[i].item.count) free_slots[found++]=i;
            if (found<count) return "掉落物已满，未破坏容器";
            if (!world_set_blocks(remove,remove_count)) return "无法记录修改";
            for (int i=0;i<count;++i)
                drops[free_slots[i]]=make_drop(h.x+.5f,h.y+.25f,h.z+.5f,items[i]);
            drops_dirty|=count!=0;
            return type==DISPENSER ? "已移除发射器" : "已移除投掷器";
        }
        if (!world_set_blocks(remove,remove_count)) return "无法记录修改";
        if (fence_type(type)) lead_fence_removed(h.x,h.y,h.z);
        if (type == FURNACE) furnace_forget(h.x, h.y, h.z, NULL);
        return "已移除方块";
    }
    if (type == BEDROCK) return "这个方块不能破坏";
    ItemStack *held = &inventory[selected_slot];
    const ItemDef *tool = item_def(held->type);
    if ((geology_block(type) || coast_stone(type)) && (!tool || tool->pick_tier<geology_pick_tier(type)))
        return type==JOINT_BASALT ? "玄武岩需要石质或更坚固的镐" :
            type==SLATE_ROCK ? "页岩需要石质或更坚固的镐" : "需要镐拆取这类岩材";
    int shovel=is_shovel(held->type),axe=is_axe(held->type);
    int web_sword=type==COBWEB && held->count && held->type>=WOOD_SWORD && held->type<=DIAMOND_SWORD;
    int web_shears=type==COBWEB && held->count && held->type==SHEARS;
    int item = mining_drop(type, tool ? tool->pick_tier : 0);
    /* Player edits are retained even if a block is placed back at the same
       coordinates, so a quarried block can never be doubled again. */
    int core=(type==SLATE_ROCK || type==LIMESTONE_ROCK) && item!=AIR &&
        enchant_level(held->enchants,ENCH_CORE_CLAMP) && !world_has_edit(h.x,h.y,h.z);
    if (web_sword || web_shears) item=STRING;
    if (type==SAPLING) item=tree_sapling_drop(raw);
    if (type==PRESSURE_PLATE) {
        int raw=world_cell(h.x,h.y,h.z);
        if (raw==PRESSURE_WOOD || (tool && tool->pick_tier)) item=redstone_plate_item(raw);
    }
    if (type==BUTTON) item=world_cell(h.x,h.y,h.z)>=BUTTON_WOOD ? WOOD_BUTTON_ITEM : STONE_BUTTON_ITEM;
    int xp_ore=type==COAL_ORE || type==DIAMOND_ORE || type==EMERALD_ORE || type==REDSTONE_ORE ||
        type==NETHER_QUARTZ_ORE || type==LAPIS_ORE;
    int giant=type==MUSHROOM_STEM || type==RED_MUSHROOM_BLOCK || type==BROWN_MUSHROOM_BLOCK;
    int silk=enchant_level(held->enchants,ENCH_SILK_TOUCH) && tool &&
        ((tool->pick_tier && item!=AIR && (xp_ore || type==IRON_ORE || type==GOLD_ORE || type==STONE ||
            type==BOOKSHELF || type==ENDER_CHEST)) || (axe && type==BOOKSHELF) ||
            ((tool->pick_tier || shovel || axe) && (type==GLASS || type==GLASS_PANE || type==CLAY || giant)) || web_shears);
    if (silk) item=stained_glass_cell(raw) ? stained_glass_item(raw) : type==GLASS_PANE ? pane_item(raw) :
        type==ENDER_CHEST ? ENDER_CHEST_ITEM :
        type==EMERALD_ORE ? EMERALD_ORE_ITEM : type==LAPIS_ORE ? LAPIS_ORE_ITEM :
        type==BROWN_MUSHROOM_BLOCK ? BROWN_MUSHROOM_BLOCK_ITEM : type==COBWEB ? COBWEB_ITEM : type;
    int mushroom_count=0;
    if (!silk && (type==RED_MUSHROOM_BLOCK || type==BROWN_MUSHROOM_BLOCK)) {
        mushroom_count=(int)((hash2(h.x+h.y*23,h.z)^(player_rules.day_tick*0x9e3779b1u))%10u)-7;
        if (mushroom_count>0) item=type==RED_MUSHROOM_BLOCK ? RED_MUSHROOM_ITEM : BROWN_MUSHROOM_ITEM;
    }
    if (shovel && (type==SNOW || type==SNOW_LAYER)) {
        silk=type==SNOW && enchant_level(held->enchants,ENCH_SILK_TOUCH);
        item=silk ? SNOW : SNOWBALL;
    }
    if (type==LEAF) item=tree_leaf_drop(raw,h.x,h.y,h.z,*held);
    if (type == GRAVEL && (hash2(h.x + 31*h.y,h.z) ^ player_rules.day_tick) % 10 == 0) item = FLINT;
    enum { RELEASE_SLOTS=DROPPER_SLOTS+8 };
    ItemStack released[RELEASE_SLOTS];
    int total = 0;
    if (item != AIR) {
        int count=silk ? 1 : type==REDSTONE_ORE ? 4+(hash2(h.x+17*h.y,h.z)&1u) :
            type==LAPIS_ORE ? 4+(int)(hash2(h.x+31*h.y,h.z)%5u) :
            type==BOOKSHELF ? 3 : type==CLAY || (item==SNOWBALL && type==SNOW) ? 4 :
            build_double(raw) || core ? 2 : mushroom_count>0 ? mushroom_count : 1;
        released[total++] = (ItemStack){(uint16_t)item,(uint8_t)count,0,0,0};
        if (type==ANVIL) released[total-1].damage=(uint16_t)anvil_stage(world_cell(h.x,h.y,h.z));
    }
    if (full_top_surface(raw) && rail_type(above)) released[total++]=(ItemStack){(uint16_t)mining_drop(above,0),1,0,0,0};
    if (torch) released[total++]=(ItemStack){TORCH,1,0,0,0};
    if (sapling_above!=AIR) released[total++]=(ItemStack){(uint16_t)sapling_above,1,0,0,0};
    if (flower) released[total++]=(ItemStack){(uint16_t)(above==DANDELION ? DANDELION_ITEM : POPPY_ITEM),1,0,0,0};
    if (mushroom) released[total++]=(ItemStack){(uint16_t)mining_drop(above,0),1,0,0,0};
    for (int i=0;i<pod_count;++i) released[total++]=pods[i];
    if (door_type(above) && door_support(world_cell(h.x,h.y,h.z))) {
        int raw=world_cell(h.x,h.y+1,h.z);
        if (!door_upper(raw)) released[total++]=(ItemStack){(uint16_t)door_item(raw),1,0,0,0};
    }
    const Furnace *f = type == FURNACE ? furnace_at(h.x, h.y, h.z) : NULL;
    if (f) for (int i = 0; i < FURNACE_SLOTS; i++)
        if (f->slots[i].count) released[total++] = f->slots[i];
    const BrewingStand *b=type==BREWING_STAND ? brew_at(h.x,h.y,h.z) : NULL;
    if (b) for (int i=0;i<BREW_SLOTS;++i) if (b->slots[i].count) released[total++]=b->slots[i];
    const Dropper *d=ejector_type(type) ? dropper_at(h.x,h.y,h.z) : NULL;
    if (d) for (int i=0;i<DROPPER_SLOTS;++i) if (d->slots[i].count) released[total++]=d->slots[i];
    int free_slots[RELEASE_SLOTS], found = 0;
    for (int i = 0; i < MAXDROP && found < total; i++)
        if (!drops[i].item.count) free_slots[found++] = i;
    if (found != total) return "掉落物已满，请先走近拾取";
    if (!world_set_blocks(remove,remove_count)) return "内存不足，无法记录修改";
    int fortune=enchant_level(held->enchants,ENCH_FORTUNE);
    if (!silk && xp_ore && item!=AIR && fortune>0) {
        if (type==REDSTONE_ORE) released[0].count+=(uint8_t)experience_roll(0,(unsigned)fortune);
        else {
            unsigned bonus=experience_roll(0,(unsigned)fortune+1);
            if (bonus>1) released[0].count=(uint8_t)(released[0].count*bonus);
        }
    }
    if (f) furnace_forget(h.x, h.y, h.z, NULL);
    for (int i = 0; i < total; i++) {
        float offset = .09f * i;
        drops[free_slots[i]] = make_drop(h.x+.5f+offset,h.y+.5f,h.z+.5f-offset,released[i]);
    }
    if (total) drops_dirty = 1;
    if (type==MONSTER_SPAWNER && tool && tool->pick_tier)
        experience_reward((Vec3){h.x+.5f,h.y+.42f,h.z+.5f},experience_roll(15,43));
    if (item!=AIR && !silk) experience_reward((Vec3){h.x+.5f,h.y+.42f,h.z+.5f},experience_ore(type));
    if (fence_type(type)) lead_fence_removed(h.x,h.y,h.z);
    int wears=tool && (tool->pick_tier || shovel || axe || web_sword || web_shears ||
        (type==LEAF && held->type==SHEARS));
    if (wears) enchant_wear(held,core ? 2u : 1u,0);
    int broke=wears && !held->count;
    player_rules.exhaustion += .1f;
    printf("MINE %d %d %d type=%d\n", h.x, h.y, h.z, type);
    fflush(stdout);
    if (broke) return axe ? "斧已损坏" : shovel ? "锹已损坏" : tool->pick_tier ? "镐已损坏" :
        web_sword ? "剑已损坏" : "剪刀已损坏";
    return core ? "已取芯 · 走近拾取双份岩材" :
        total ? "方块已破坏，走近拾取掉落物" : "方块已破坏，没有掉落物";
}

const char *mine(void) { return mine_with_hold(0); }
const char *mine_quick_chop(unsigned held_ms) { return held_ms ? mine_with_hold(held_ms) : NULL; }
static const char *tracked_mine(unsigned held_ms,int *removed) {
    Hit hit=trace(0,1,1);
    int before=hit.hit ? world_cell(hit.x,hit.y,hit.z) : AIR;
    const char *message=mine_with_hold(held_ms);
    int changed=hit.hit && world_cell(hit.x,hit.y,hit.z)!=before;
    if (changed && before!=AIR) sound_material(SOUND_MINE,before,(Vec3){hit.x+.5f,hit.y+.5f,hit.z+.5f});
    if (removed) *removed=changed;
    return message;
}
const char *mine_hold_action(int *repeat_block) { return tracked_mine(0,repeat_block); }
const char *mine_quick_chop_tracked(unsigned held_ms,int *removed) {
    if (removed) *removed=0;
    return held_ms ? tracked_mine(held_ms,removed) : NULL;
}

int player_eat(void) {
    ItemStack *held = &inventory[selected_slot];
    const ItemDef *def = item_def(held->type);
    if (!player_rules.health || player_rules.mode != SURVIVAL || !held->count ||
        !def || !def->food || player_rules.food >= MAX_FOOD) return 0;
    if (held->type==MUSHROOM_STEW) {
        if (!inventory_fill_container(selected_slot,(ItemStack){BOWL,1,0,0,0})) return 0;
        player_feed(def->food);
        return 1;
    }
    player_feed(def->food);
    if (held->type==PUFFERFISH) {
        player_effects.poison=PUFFER_POISON_TICKS;
        player_effects.poison_slow=0;
        player_effects.hunger=PUFFER_HUNGER_TICKS;
    } else if (held->type==RAW_CHICKEN &&
        hash2((int)floorf(px)+(int)player_rules.day_tick,(int)floorf(pz)+player_rules.food)%10u<3u &&
        player_effects.hunger<CHICKEN_HUNGER_TICKS) {
        player_effects.hunger=CHICKEN_HUNGER_TICKS;
    }
    if (!--held->count) *held = (ItemStack){0, 0, 0,0,0};
    return 1;
}

static const char *place_lily_pad(ItemStack *held) {
    Hit h=trace(2,0,0);
    if (!h.hit || h.ax!=h.x || h.ay!=h.y+1 || h.az!=h.z ||
        !inworld(h.x,h.y+1,h.z) || world_cell(h.x,h.y,h.z)!=WATER ||
        world_cell(h.x,h.y+1,h.z)!=AIR) return "睡莲只能放在空着的静水源表面";
    BlockBox pad={(float)h.x,h.y+1.f,(float)h.z,h.x+1.f,h.y+1.f+LILY_PAD_HEIGHT,h.z+1.f};
    if (box_overlap(pad,body_box(px,py,pz,.28f,1.72f))) return "先离开摆放位置";
    if (!set_block(h.x,h.y+1,h.z,LILY_PAD)) return "暂时无法放置睡莲";
    if (player_rules.mode==SURVIVAL && !--held->count) *held=(ItemStack){0};
    printf("PLACE %d %d %d type=%d\n",h.x,h.y+1,h.z,LILY_PAD); fflush(stdout);
    return "已放置睡莲";
}

static const char *place_building(ItemStack *held,int selected,Hit h) {
    const int mat=build_material(selected),slab=build_slab(selected);
    if (!h.hit || mat<0) return "请对准可放置的方块表面";
    int x=h.ax,y=h.ay,z=h.az;
    int dx=x-h.x,dy=y-h.y,dz=z-h.z;
    if (abs(dx)+abs(dy)+abs(dz)!=1) return "请对准一个方块面";
    int first=world_cell(h.x,h.y,h.z),next=world_cell(x,y,z),raw=AIR;
    if (slab && build_partial(first) && build_slab(first) && build_material(first)==mat &&
        dy==(build_is_full_top(first) ? -1 : 1)) {
        x=h.x; y=h.y; z=h.z;
        raw=build_slab_raw(mat,BUILD_SLAB_DOUBLE);
    } else if (slab && build_slab(next) && build_partial(next) && build_material(next)==mat) {
        raw=build_slab_raw(mat,BUILD_SLAB_DOUBLE);
    } else {
        if (next!=AIR && !water_strength(next)) return "没有可放置的相邻位置";
        Vec3 view=forward();
        float component=dx ? view.x : view.z;
        if (!dy && fabsf(component)<.000001f) return "请对准方块侧面";
        float t=dy ? 0.f : dx ? (h.x+(dx>0)-px)/component : (h.z+(dz>0)-pz)/component;
        RaySurface surface;
        if (!dy && build_partial(first) && building_surface((Vec3){px,py+1.55f,pz},view,h.x,h.y,h.z,first,6.f,&surface))
            t=surface.t;
        int high=dy<0 || (!dy && py+1.55f+t*view.y>h.y+.5f);
        int dir=((int)lroundf(remainderf(yaw,6.28318530718f)*.63661977236f)+4)%4;
        raw=slab ? build_slab_raw(mat,high ? BUILD_SLAB_TOP : BUILD_SLAB_BOTTOM) : build_stair_raw(mat,dir,high);
    }
    if (!raw || !inworld(x,y,z) || !world_chunk_ready(x,z)) return "等待区块载入";
    BlockBox boxes[BUILD_BOXES],player=body_box(px,py,pz,.28f,1.72f);
    int count=build_world_boxes(x,y,z,raw,boxes);
    for (int i=0;i<count;++i) if (box_overlap(player,boxes[i])) return "不能放置在玩家身体内";
    if (build_stair(raw)) for (int d=0;d<4;++d) {
        int nx=x+rail_dx(d),nz=z+rail_dz(d),other;
        if (!world_resident_cell(nx,y,nz,&other) || !build_stair(other)) continue;
        int near[4]; build_neighbors(nx,y,nz,near);
        unsigned before=build_mask(other,near);
        near[(d+2)&3]=raw;
        unsigned added=build_mask(other,near)&~before;
        int n=build_boxes(nx,y,nz,added,boxes);
        for (int i=0;i<n;++i) if (box_overlap(player,boxes[i])) return "楼梯转角会占用玩家位置";
    }
    if (actors_overlap_cell(x,y,z) || horse_overlaps_cell(x,y,z) || boat_overlaps_cell(x,y,z) ||
        cart_overlaps_cell(x,y,z)) return "这个位置有生物或载具";
    const BlockEdit edit={x,y,z,raw};
    if (!world_set_blocks(&edit,1)) return "暂时无法放置方块";
    if (player_rules.mode==SURVIVAL && !--held->count) *held=(ItemStack){0};
    printf("PLACE %d %d %d type=%d\n",x,y,z,raw); fflush(stdout);
    return build_slab(raw) ? build_double(raw) ? "已合并两块台阶" : "已放置台阶" : "已放置楼梯";
}

const char *place(void) {
    if (!player_rules.health) return "请先重生";
    if (bed_sleeping()) return "睡觉时不能放置方块";
    if ((unsigned)selected_slot>=HOTBAR_SIZE) return "手持物品未选中";
    ItemStack *held = &inventory[selected_slot];
    if (held->count && held->type==LILY_PAD_ITEM) return place_lily_pad(held);
    if (held->count && (slab_item(held->type) || stair_item(held->type))) {
        const ItemDef *def=item_def(held->type);
        if (def) return place_building(held,def->placed_block,target());
    }
    Hit h = target();
    if (!h.hit || !inworld(h.ax, h.ay, h.az) || (block(h.ax,h.ay,h.az) != AIR && block(h.ax,h.ay,h.az) != WATER))
        return "没有可放置的相邻位置";
    if (!held->count) return "手持方块不足，请采集或切换";
    const ItemDef *def = item_def(held->type);
    if (!def || !def->placed_block) return "手持物品不能放置";
    int selected = def->placed_block;
    if (selected==WATER && world_dimension()==NETHER && !inside_pressure_works()) return "水受热立即蒸发了";
    if (selected==AQUIFER_WINCH && (!uses_aquifer() || world_dimension()!=OVERWORLD ||
            world_cell(h.ax,h.ay,h.az)!=AIR || !solid(block(h.ax,h.ay-1,h.az))))
        return "绞盘需要架在地表世界的实心支撑上";
    const int attached=selected==WOOD_TRAPDOOR || selected==LADDER;
    int mounting_side=-1,high=0;
    if (attached) {
        int dx=h.x-h.ax,dz=h.z-h.az;
        if (h.y!=h.ay || abs(dx)+abs(dz)!=1 || world_cell(h.ax,h.ay,h.az)!=AIR)
            return "请对准完整方块的侧面";
        mounting_side=dx<0 ? WEST : dx>0 ? EAST : dz<0 ? NORTH : SOUTH;
        Vec3 f=forward();
        if (fabsf(dx ? f.x : f.z)<.00001f) return "请对准方块侧面";
        float t=dx ? (h.x+(dx<0)-px)/f.x : (h.z+(dz<0)-pz)/f.z;
        high=py+1.55f+t*f.y>=h.y+.5f;
        int raw=mounting_side+(selected==LADDER ? LADDER : WOOD_TRAPDOOR+(high ? TRAPDOOR_TOP : 0));
        BlockBox parts[BARRIER_PARTS];
        int count=barrier_world_boxes(h.ax,h.ay,h.az,raw,BARRIER_BODY,parts);
        for (int i=0;i<count;++i)
            if (box_overlap(parts[i],body_box(px,py,pz,.28f,1.72f))) return "不能放置在玩家身体内";
    }
    if (selected==SAPLING) return tree_use(h);
    if ((selected==POPPY || selected==DANDELION) &&
        (block(h.ax,h.ay,h.az)!=AIR || !tree_soil(block(h.ax,h.ay-1,h.az))))
        return "花需要种在土壤顶面";
    if (small_mushroom(selected) &&
        (block(h.ax,h.ay,h.az)!=AIR || !mushroom_can_stay(h.ax,h.ay,h.az)))
        return "蘑菇需要阴暗的实心顶面、菌丝或灰化土";
    if (selected==SUGAR_CANE) return farm_use(h);
    if (selected==BREWING_STAND && (block(h.ax,h.ay,h.az)!=AIR || !solid(block(h.ax,h.ay-1,h.az))))
        return "调剂台需要放在实心方块上";
    if (selected==TORCH && (h.ay<=h.y || block(h.ax,h.ay,h.az)!=AIR ||
        !solid(block(h.ax,h.ay-1,h.az)) || block(h.ax,h.ay-1,h.az)==FARMLAND ||
        stained_glass_cell(world_cell(h.ax,h.ay-1,h.az))))
        return "火把需要放在实心方块顶面";
    if (chest_inventory_type(cell_type(selected)) &&
        !chest_can_place_type((ChestPos){h.ax,h.ay,h.az},cell_type(selected)))
        return "不能把第三个箱子并到双箱上";
    if (selected == WATER && world_cell(h.ax, h.ay, h.az) == WATER)
        return "这里已有水源";
    Body body = body_bounds(px, py, pz);
    int obstacle=solid(selected)||barrier_type(selected)||selected==BREWING_STAND;
    if (pane_cell(selected)) {
        BlockBox pane[BARRIER_PARTS],player=body_box(px,py,pz,.28f,1.72f);
        int count=barrier_world_boxes(h.ax,h.ay,h.az,selected,BARRIER_BODY,pane);
        for (int i=0;i<count;++i) if (box_overlap(player,pane[i])) return "不能放置在玩家身体内";
    } else if (obstacle && !attached && h.ax >= body.x0 && h.ax <= body.x1 &&
        h.ay >= body.y0 && h.ay <= body.y1 && h.az >= body.z0 && h.az <= body.z1)
        return "不能放置在玩家身体内";
    if (obstacle && actors_overlap_cell(h.ax,h.ay,h.az)) return "不能放置在生物身体内";
    if (obstacle && boat_overlaps_cell(h.ax,h.ay,h.az)) return "这个位置有船";
    if (obstacle && cart_overlaps_cell(h.ax,h.ay,h.az)) return "这个位置有矿车";
    if (obstacle && horse_overlaps_cell(h.ax,h.ay,h.az)) return "这个位置有坐骑";
    /* A protruding top can also intersect feet standing in the next cell. */
    if (door_type(selected) && h.ay+1<WY &&
        ((h.ax>=body.x0 && h.ax<=body.x1 && h.az>=body.z0 && h.az<=body.z1 && h.ay+1>=body.y0 && h.ay+1<=body.y1) ||
         actors_overlap_cell(h.ax,h.ay+1,h.az) || horse_overlaps_cell(h.ax,h.ay+1,h.az) ||
         boat_overlaps_cell(h.ax,h.ay+1,h.az) || cart_overlaps_cell(h.ax,h.ay+1,h.az)))
        return "门的上半格有人或生物";
    if ((fence_type(selected) || selected==FENCE_GATE) && h.ay+1<WY &&
        ((h.ax>=body.x0 && h.ax<=body.x1 && h.az>=body.z0 && h.az<=body.z1 && py<h.ay+BARRIER_HEIGHT && py>=h.ay+1.f) ||
         actors_overlap_cell(h.ax,h.ay+1,h.az) || horse_overlaps_cell(h.ax,h.ay+1,h.az)))
        return "栅栏上方有人或生物";
    if (selected==PUMPKIN) {
        if (block(h.ax,h.ay,h.az)!=AIR || !solid(block(h.ax,h.ay-1,h.az))) return "南瓜需要放在完整方块上";
        int facing=((int)lroundf(remainderf(yaw,6.28318530718f)*.63661977236f)+6)%4;
        BlockEdit e={h.ax,h.ay,h.az,PUMPKIN_FACE_BASE+facing};
        if (!world_set_blocks(&e,1)) return "无法放置南瓜";
    } else if (selected==AIR_OUTLET) {
        int dx=h.ax-h.x,dy=h.ay-h.y,dz=h.az-h.z;
        if (!solid(block(h.x,h.y,h.z)) || dy<0 || abs(dx)+abs(dy)+abs(dz)!=1)
            return "水下气嘴需要安装在方块顶部或侧面";
        int heading=(int)lroundf(remainderf(yaw,6.28318530718f)*2.54647908947f);
        int dir=dx>0 ? EAST : dx<0 ? WEST : dz>0 ? SOUTH : NORTH;
        int raw=dy ? AIR_OUTLET_BASE+((heading+24)%16) : AIR_OUTLET_WALL+dir;
        if (box_overlap(body_box(px,py,pz,.30f,1.80f),air_outlet_box(h.ax,h.ay,h.az,raw)))
            return "不能放置在玩家身体内";
        BlockEdit e={h.ax,h.ay,h.az,raw};
        if (!world_set_blocks(&e,1)) return "无法安装水下气嘴";
    } else if (selected==ENDER_CHEST) {
        int heading=(int)lroundf(remainderf(yaw,6.28318530718f)*.63661977236f);
        BlockEdit e={h.ax,h.ay,h.az,ENDER_CHEST_BASE+((heading+6)%4)};
        if (!world_set_blocks(&e,1)) return "无法放置联储箱";
    } else if (selected==BED) {
        int heading=(int)lroundf(remainderf(yaw,6.28318530718f)*.63661977236f);
        int dir=(heading+4)%4;
        if (!bed_place((BedPos){h.ax,h.ay,h.az},dir)) return "床需要前后两格空地和坚实支撑";
    } else if (selected==FENCE_GATE) {
        int facing=(int)lroundf(remainderf(yaw,6.28318530718f)*.63661977236f);
        if (!gate_place(h.ax,h.ay,h.az,(facing+4)%4)) return "栅栏门需要放在实心方块上";
    } else if (attached) {
        if (!wall_attachment_place(h.ax,h.ay,h.az,mounting_side,selected==LADDER,high))
            return "这里没有合适的侧面支撑";
    } else if (door_type(selected)) {
        int dir=((int)lroundf(remainderf(yaw,6.28318530718f)*.63661977236f)+6)%4;
        int right=(dir+1)&3;
        int hinge=(px-h.ax-.5f)*rail_dx(right)+(pz-h.az-.5f)*rail_dz(right)>0;
        if (!door_place(h.ax,h.ay,h.az,dir,hinge,selected==IRON_DOOR)) return "门需要两格空位和完整方块支撑";
    } else if (selected==TRIPWIRE || selected==TRIPWIRE_HOOK) {
        CircuitPos put={h.ax,h.ay,h.az},against={h.x,h.y,h.z};
        if (selected==TRIPWIRE ? !tripwire_place_string(put) : !tripwire_place_hook(put,against))
            return selected==TRIPWIRE ? "绊线需要干燥的空位" : "绊线钩需要安装在完整方块侧面";
    } else if (selected==REDSTONE_WIRE) {
        if (!redstone_place_wire(h.ax,h.ay,h.az)) return "导脉砂需要铺在方块顶面";
    } else if (selected==LEVER) {
        if (!redstone_place_lever((CircuitPos){h.ax,h.ay,h.az},(CircuitPos){h.x,h.y,h.z},
            fabsf(sinf(yaw))>fabsf(cosf(yaw)))) return "拉杆需要附着在完整方块上";
    } else if (selected==REDSTONE_TORCH) {
        if (!redstone_place_torch((CircuitPos){h.ax,h.ay,h.az},(CircuitPos){h.x,h.y,h.z}))
            return "信号柱需要完整方块的顶面或侧面";
    } else if (selected==CISTERN_PROBE_BASE) {
        const int dx=h.ax-h.x,dz=h.az-h.z;
        int tank;
        if (h.ay!=h.y || abs(dx)+abs(dz)!=1 ||
            !world_resident_cell(h.x,h.y,h.z,&tank) || !cistern_cell(tank))
            return "水位探头需要横装在集雨罐侧面";
        const int front=dx>0 ? EAST : dx<0 ? WEST : dz>0 ? SOUTH : NORTH;
        const BlockEdit probe={h.ax,h.ay,h.az,(uint16_t)(CISTERN_PROBE_BASE+front)};
        if (!world_set_blocks(&probe,1)) return "暂时无法安装水位探头";
    } else if (selected==AIR_RELAY_BASE) {
        int dx=h.ax-h.x,dz=h.az-h.z;
        int heading=(int)lroundf(remainderf(yaw,6.28318530718f)*.63661977236f);
        int front=dx>0 ? EAST : dx<0 ? WEST : dz>0 ? SOUTH : dz<0 ? NORTH : (heading+6)%4;
        BlockEdit relay={h.ax,h.ay,h.az,AIR_RELAY_BASE+front};
        if (!world_set_blocks(&relay,1)) return "暂时无法放置储压信号箱";
    } else if (selected==REPEATER || selected==COMPARATOR) {
        int heading=(int)lroundf(remainderf(yaw,6.28318530718f)*.63661977236f);
        int facing=(heading+4)%4;
        if (!(selected==REPEATER ? redstone_place_repeater(h.ax,h.ay,h.az,facing) :
            redstone_place_comparator(h.ax,h.ay,h.az,facing))) return "线路元件需要放在完整方块的顶面";
    } else if (selected==HOPPER) {
        if (!hopper_place(h)) return "无法放置漏斗；请对准旁边或下方的方块";
    } else if (ejector_type(selected) || piston_base_type(selected)) {
        int heading=(int)lroundf(remainderf(yaw,6.28318530718f)*.63661977236f);
        int dir=(heading+6)%4;
        float eye=py+1.55f;
        if (fabsf(px-(h.ax+.5f))<2.f && fabsf(pz-(h.az+.5f))<2.f) {
            if (eye-h.ay>2.f) dir=EJECTOR_UP;
            else if (h.ay>eye) dir=EJECTOR_DOWN;
        }
        if (piston_base_type(selected)) {
            if (!piston_place(h.ax,h.ay,h.az,dir,selected==STICKY_PISTON)) return "无法放置活塞";
        } else if (!ejector_place(h.ax,h.ay,h.az,dir,selected))
            return selected==DISPENSER ? "无法放置发射器" : "无法放置投掷器";
    } else if (selected==BUTTON) {
        if (!redstone_place_button((CircuitPos){h.ax,h.ay,h.az},(CircuitPos){h.x,h.y,h.z},
            held->type==WOOD_BUTTON_ITEM)) return "按钮需要安装在完整方块的侧面";
    } else if (selected==PRESSURE_PLATE) {
        if (!redstone_place_plate(h.ax,h.ay,h.az,PRESSURE_BASE+held->type-STONE_PLATE_ITEM))
            return "压力板需要放在完整方块的顶面";
    } else if (selected==ANVIL) {
        if (!anvil_place(h.ax,h.ay,h.az,held->damage,fabsf(sinf(yaw))>fabsf(cosf(yaw))))
            return "无法放置铁砧";
    } else if (rail_type(selected)) {
        RailKind kind=selected==ACTIVATOR_RAIL ? RAIL_ACTIVATOR : selected==DETECTOR_RAIL ? RAIL_DETECTOR :
            selected==POWERED_RAIL ? RAIL_BOOSTER : RAIL_NORMAL;
        if (!rail_place((RailCell){h.ax,h.ay,h.az},kind,fabsf(sinf(yaw))>fabsf(cosf(yaw))))
            return "铁轨需要铺在坚实方块上，且上方不能有水";
    } else if (selected==LEAF) {
        int species=typed_leaf_item(held->type) ? held->type-BIRCH_LEAF_ITEM+1 : TREE_OAK;
        BlockEdit leaf={h.ax,h.ay,h.az,PLACED_LEAF_BASE+species};
        if (!world_set_blocks(&leaf,1)) return "无法放置树叶";
    } else if (!set_block(h.ax, h.ay, h.az, selected)) return "内存不足，无法记录修改";
    if (player_rules.mode == SURVIVAL && !--held->count) *held = (ItemStack){0, 0, 0,0,0};
    printf("PLACE %d %d %d type=%d\n", h.ax, h.ay, h.az, selected);
    fflush(stdout);
    return "方块已放置";
}

const char *player_use_gate(Hit h) {
    if (!h.hit || !player_rules.health) return NULL;
    GateState state;
    if (!gate_decode(world_cell(h.x,h.y,h.z),&state)) return NULL;
    if (state.open) {
        BlockBox shape[BARRIER_PARTS];
        int closed=GATE_BASE+state.facing;
        int n=barrier_world_boxes(h.x,h.y,h.z,closed,BARRIER_BODY,shape);
        if (n && box_overlap(shape[0],body_box(px,py,pz,.28f,1.72f))) return "先离开门口再关门";
        if (actors_overlap_cell(h.x,h.y,h.z) || horse_overlaps_cell(h.x,h.y,h.z) ||
            boat_overlaps_cell(h.x,h.y,h.z) || cart_overlaps_cell(h.x,h.y,h.z)) return "门口有生物或载具";
    }
    float dx=h.x+.5f-px,dz=h.z+.5f-pz;
    int approach=fabsf(dx)>fabsf(dz) ? (dx>0 ? EAST : WEST) : (dz>0 ? SOUTH : NORTH);
    if (!gate_toggle(h.x,h.y,h.z,approach)) return "无法改变门的状态";
    return state.open ? "已关栅栏门" : "已打开栅栏门";
}

int player_grant_water_breathing(unsigned duration) {
    if (!player_rules.health || duration<=player_effects.water_breathing ||
        duration>WATER_BREATHING_LONG_TICKS) return 0;
    player_effects.water_breathing=(uint16_t)duration;
    return 1;
}
int player_grant_night_vision(unsigned duration) {
    if (!player_rules.health || duration<=player_effects.night_vision ||
        duration>NIGHT_VISION_LONG_TICKS) return 0;
    player_effects.night_vision=(uint16_t)duration;
    return 1;
}
float player_night_vision_lift(void) {
    /* Preserve stable wide visibility, and ease back to ambient light at expiry. */
    return fminf(1.f,player_effects.night_vision/(float)NIGHT_VISION_FADE_TICKS);
}

const char *player_use_bottle(void) {
    if (!player_rules.health || (unsigned)selected_slot>=HOTBAR_SIZE) return NULL;
    ItemStack *held=&inventory[selected_slot];
    if (!held->count) return NULL;
    int item=held->type;
    if (field_flask_item(item)) {
        const Hit water=trace(1,0,0);
        return field_use_water(held,water.hit && world_cell(water.x,water.y,water.z)==WATER);
    }
    if (item==WATER_BOTTLE) return field_use_water(held,0);
    if (item==GLASS_BOTTLE) {
        Hit water=trace(1,0,0);
        if (!water.hit || world_cell(water.x,water.y,water.z)!=WATER) return "用空瓶对准水源装水";
        ItemStack bottle={WATER_BOTTLE,1,0,0,0};
        if (held->count==1) *held=bottle;
        else {
            if (!inventory_add_stack(bottle)) return "背包没有空间放水瓶";
            --held->count;
        }
        return "已装满水瓶";
    }
    if (!brew_is_potion(item) || brew_splash(item).effect!=BREW_SPLASH_NONE) return NULL;
    if (item==POTION_NIGHT_VISION || item==POTION_NIGHT_VISION_LONG) {
        player_grant_night_vision(item==POTION_NIGHT_VISION ? NIGHT_VISION_TICKS : NIGHT_VISION_LONG_TICKS);
        if (player_rules.mode==SURVIVAL) *held=(ItemStack){GLASS_BOTTLE,1,0,0,0};
        return "夜视生效";
    }
    if (item==POTION_WATER_BREATHING || item==POTION_WATER_BREATHING_LONG) {
        player_grant_water_breathing(item==POTION_WATER_BREATHING ? WATER_BREATHING_TICKS : WATER_BREATHING_LONG_TICKS);
        if (player_rules.mode==SURVIVAL) *held=(ItemStack){GLASS_BOTTLE,1,0,0,0};
        return "水肺生效";
    }
    if (player_rules.mode!=SURVIVAL) return "创造模式不需要喝滤剂";
    *held=(ItemStack){GLASS_BOTTLE,1,0,0,0};
    if (item==POTION_REGEN || item==POTION_REGEN_LONG || item==POTION_REGEN_II) {
        int strong=item==POTION_REGEN_II;
        unsigned duration=strong ? REGEN_II_TICKS : item==POTION_REGEN_LONG ? REGEN_LONG_TICKS : REGEN_TICKS;
        if (!player_effects.regeneration || strong>player_effects.regeneration_ii ||
            (strong==player_effects.regeneration_ii && duration>player_effects.regeneration)) {
            player_effects.regeneration=(uint16_t)duration;
            player_effects.regeneration_ii=(uint8_t)strong;
        }
        return strong ? "再生 II 生效" : "再生生效";
    }
    if (item==POTION_WEAKNESS || item==POTION_WEAKNESS_LONG) {
        unsigned duration=item==POTION_WEAKNESS ? WEAKNESS_TICKS : WEAKNESS_LONG_TICKS;
        if (player_effects.weakness<duration) player_effects.weakness=(uint16_t)duration;
        return "虚弱生效";
    }
    int ticks=item==POTION_FIRE ? FIRE_POTION_TICKS : item==POTION_FIRE_LONG ? FIRE_POTION_LONG_TICKS : 0;
    if (!ticks) return "已喝下，没有附加效果";
    if (player_effects.fire<ticks) player_effects.fire=(uint16_t)ticks;
    player_extinguish();
    return "抗火生效";
}

const char *player_use_bucket(void) {
    ItemStack *held = &inventory[selected_slot];
    if (!held->count || (held->type != BUCKET && held->type != LAVA_BUCKET && held->type != WATER_BUCKET)) return NULL;
    Hit h = trace(held->type == BUCKET,0,0);
    if (!h.hit) return "没有可使用铁桶的位置";
    if (held->type == BUCKET) {
        int cell = world_cell(h.x,h.y,h.z);
        if (cell != WATER && cell != LAVA) return "对准水源或熔岩源";
        ItemStack result = {(uint16_t)(cell == WATER ? WATER_BUCKET : LAVA_BUCKET),1,0,0,0};
        if (held->count == 1) {
            if (!set_block(h.x,h.y,h.z,AIR)) return "未能收取液体";
            *held = result;
        } else {
            ItemStack old[SLOT_COUNT];
            memcpy(old,inventory,sizeof old);
            --held->count;
            if (!inventory_add_stack(result) || !set_block(h.x,h.y,h.z,AIR)) {
                memcpy(inventory,old,sizeof old);
                return "未能收取液体，先腾出背包空间";
            }
        }
        return "已装入铁桶";
    }
    int liquid = held->type == LAVA_BUCKET ? LAVA : WATER;
    int vacant=world_cell(h.ax,h.ay,h.az);
    if (!inworld(h.ax,h.ay,h.az) || (vacant!=AIR && vacant!=FIRE_CELL &&
        !(liquid==LAVA ? lava_flow_cell(vacant) : water_strength(vacant) && vacant!=WATER)))
        return "没有可倒入液体的位置";
    int evaporated = liquid == WATER && world_dimension() == NETHER && !inside_pressure_works();
    if (!evaporated && !set_block(h.ax,h.ay,h.az,liquid)) return "无法放出液体";
    if (player_rules.mode == SURVIVAL) *held = (ItemStack){BUCKET,1,0,0,0};
    return evaporated ? "水受热立即蒸发了" : "已放出液体";
}

int player_in_water(void) {
    int x = (int)floorf(px), z = (int)floorf(pz);
    float chest = py + .8f;
    int y = (int)floorf(chest), cell = world_cell(x, y, z);
    return water_strength(cell) && chest - y < water_surface(cell, world_cell(x, y + 1, z));
}

static int climb_side_at(float at_x,float at_z) {
    int x=(int)floorf(at_x),z=(int)floorf(at_z),y=(int)floorf(py+.005f);
    /* Keep contact for the last few cm of the exit from a ladder. No
       special climbing permission for the open trapdoor itself. */
    for (int level=y;level>=y-1;--level) {
        int raw;
        if (py>level+1.17f || !world_resident_cell(x,level,z,&raw) || !ladder_cell(raw)) continue;
        const int d=raw-LADDER;
        float gap=d==WEST ? at_x-x : d==EAST ? x+1.f-at_x : d==NORTH ? at_z-z : z+1.f-at_z;
        if (gap<=.54f) return d;
    }
    return -1;
}
int player_on_ladder(void) { return !player_rules.flying && climb_side_at(px,pz)>=0; }

/* Resolve an approach against the ladder to its contact plane even if an
   entire movement step would put the player inside the rungs. */
static void move_player_axis(float dx,float dz) {
    if (!dx && !dz) return;
    if (!move_collides(px+dx,py,pz+dz)) { px+=dx; pz+=dz; return; }
    if (player_rules.flying) return;
    float lo=0,hi=1;
    if (climb_side_at(px+dx,pz+dz)<0 && climb_side_at(px+dx*.5f,pz+dz*.5f)<0) return;
    for (int i=0;i<6;++i) {
        float f=(lo+hi)*.5f;
        if (move_collides(px+dx*f,py,pz+dz*f)) hi=f;
        else lo=f;
    }
    px+=dx*lo; pz+=dz*lo;
}

static int climb_around_open_hatch(int side,float step) {
    int x=(int)floorf(px),z=(int)floorf(pz),y=(int)floorf(py+1.719f+step);
    int raw;
    if (!world_resident_cell(x,y,z,&raw) || !trapdoor_cell(raw) || wall_attachment_dir(raw)!=side ||
        !((raw-WOOD_TRAPDOOR)&TRAPDOOR_OPEN)) return 0;
    /* The open panel projects a little further from the wall than the rungs. */
    float ox=px-.08f*rail_dx(side),oz=pz-.08f*rail_dz(side);
    if (move_collides(ox,py,oz) || move_collides(ox,py+step,oz)) return 0;
    px=ox; pz=oz; py+=step;
    return 1;
}

static void update_player(float dt, int keys_side, int keys_front, int ascend, int descend) {
    float side = mx + keys_side;
    float front = -my + keys_front;
    float n = sqrtf(side * side + front * front);
    if (n > 1) { side /= n; front /= n; }
    int lava=in_lava(),swimming=player_in_water() || lava;
    int ladder=player_rules.flying ? -1 : climb_side_at(px,pz);
    float speed = player_rules.flying ? 9.0f : lava ? 1.7f : swimming ? 2.1f : 4.317f;
    speed*=field_stride_scale();
    if (n>.05f && grounded && ladder<0 && !player_rules.flying && !swimming) {
        int floor;
        if (world_resident_cell((int)floorf(px),(int)floorf(py-.06f),(int)floorf(pz),&floor) &&
            floor==BASALT_PAVING) speed*=1.18f;
    }
    float dx = (sinf(yaw) * front + cosf(yaw) * side) * speed * dt;
    float dz = (-cosf(yaw) * front + sinf(yaw) * side) * speed * dt;
    dx+=blast_x*dt; dz+=blast_z*dt;
    int web=world_cobweb_contact(body_box(px,py,pz,.28f,1.72f));
    if (web) { dx*=.25f; dz*=.25f; }
    float blast_decay=fmaxf(0.f,1.f-dt*5.f);
    blast_x*=blast_decay; blast_z*=blast_decay;

    /* Step up only for a single block, with headroom at both positions. */
    if (!player_rules.flying && ladder<0 && grounded && n > .05f && move_collides(px+dx,py,pz+dz)) {
        const float step_y=py+.507f;
        if (!move_collides(px,step_y,pz) && !move_collides(px+dx,step_y,pz+dz) &&
            move_collides(px+dx,step_y-.09f,pz+dz)) {
            /* Settle on the tread itself; adding a fixed half-unit on each
               collision would slowly lift the feet above long staircases. */
            float lo=py,hi=step_y;
            for (int i=0;i<7;++i) {
                float mid=(lo+hi)*.5f;
                if (move_collides(px+dx,mid,pz+dz)) lo=mid;
                else hi=mid;
            }
            py=hi;
            vy=0.f;
            player_rules.fall_origin=py;
        } else if (!collides(px,py+1.05f,pz) && !collides(px+dx,py+1.05f,pz+dz)) {
            vy=6.6f;
            grounded=0;
            printf("AUTOJUMP\n"); fflush(stdout);
        }
    }
    move_player_axis(dx,0);
    move_player_axis(0,dz);

    if (!player_rules.flying) ladder=climb_side_at(px,pz);
    if (player_rules.flying) {
        vy = 6.f * fmaxf(-1, fminf(1, touch_altitude + ascend - descend));
    } else if (ladder>=0) {
        int pushing=dx*rail_dx(ladder)+dz*rail_dz(ladder)>dt*.13f;
        vy=touch_altitude<-.16f ? -1.8f : descend ? 0.f :
            (touch_altitude>.16f || ascend || pushing) ? 2.65f : -.88f;
        player_rules.fall_origin=py;
    } else {
        if (vy >= 0 || swimming || grounded) player_rules.fall_origin = py;
        vy -= (swimming ? 5.0f : 18.0f) * dt;
        if (vy < (swimming ? -2.5f : -20.0f)) vy = swimming ? -2.5f : -20.0f;
    }
    float vertical=vy*dt;
    if (web) {
        vertical*=.05f;
        player_rules.fall_origin=py;
    }
    int steps = (int)(fabsf(vertical) / .08f) + 1;
    float step = vertical / steps;
    grounded = 0;
    for (int i = 0; i < steps; i++) {
        if (py + step <= WY + 11.f && !move_collides(px, py + step, pz)) py += step;
        else if (step>0.f && py+step<=WY+11.f && ladder>=0 && climb_around_open_hatch(ladder,step)) continue;
        else {
            if (step < 0) {
                grounded = 1;
                if (!player_rules.flying && !swimming && ladder<0) {
                    float distance = player_rules.fall_origin - py;
                    farm_trample((int)floorf(px),(int)floorf(py+step+.001f),(int)floorf(pz),distance);
                    float safe=3.2f;
                    if (distance > safe) player_take_damage(DAMAGE_FALL,(int)ceilf(distance-safe));
                }
                player_rules.fall_origin = py;
            }
            vy = 0;
            break;
        }
    }
    if (world_dimension() == END && py < 0) {
        py = 0; vy = 0;
        if (player_rules.mode == SURVIVAL) player_take_damage(DAMAGE_VOID,MAX_HEALTH);
        else if (!player_rules.flying) {
            px = 100.5f; pz = .5f; py = 49.01f;
            while (py < WY && collides(px,py,pz)) ++py;
        }
    }
    if (!player_rules.flying && n > .05f)
        player_rules.exhaustion += n * dt * (swimming ? .14f : .04f);
}

static void update_vitals(float dt) {
    if (player_rules.mode != SURVIVAL || !player_rules.health) return;
    int peaceful=game_effective_difficulty()==PEACEFUL;
    if (peaceful) player_rules.exhaustion=0.f;
    while (player_rules.exhaustion >= 4.f) {
        player_rules.exhaustion -= 4.f;
        if (player_rules.food > 0) --player_rules.food;
    }
    int x = (int)floorf(px), z = (int)floorf(pz);
    if (block(x,(int)floorf(py+.1f),z) == LAVA) {
        player_catch_fire(PLAYER_MAX_BURN);
        lava_contact += dt;
        if (lava_contact >= .5f) {
            lava_contact -= .5f;
            player_take_damage(DAMAGE_DIRECT_FIRE,4);
        }
        if (!player_rules.health) return;
    } else lava_contact = 0;
    float eye = py + 1.55f;
    int y = (int)floorf(eye), cell = world_cell(x, y, z);
    int submerged = water_strength(cell) && eye - y < water_surface(cell, world_cell(x, y + 1, z));
    if (submerged && player_effects.water_breathing) {
        /* Keep the player's current air, and start a fresh breath interval
           when protection ends. Surfacing still refills air normally. */
        player_rules.submerged_time=0.f;
    } else if (submerged) {
        player_rules.submerged_time += dt;
        if (player_rules.submerged_time >= .75f) {
            player_rules.submerged_time -= .75f;
            if (player_rules.air) {
                int respiration=enchant_level(armor_worn(ARMOR_HEAD).enchants,ENCH_RESPIRATION);
                if (!respiration || experience_roll(0,(unsigned)respiration)==0) --player_rules.air;
            }
            else player_take_damage(DAMAGE_DROWNING,2);
        }
    } else {
        player_rules.air = MAX_AIR;
        player_rules.submerged_time = 0;
    }
    if (!player_rules.health) return;
    int hungry = !peaceful && player_rules.food == 0;
    int healing = (peaceful || player_rules.food >= 18) && field_allows_recovery() &&
        (player_rules.health < MAX_HEALTH || player_rules.health_debt);
    float interval=peaceful ? 1.f : 4.f;
    if (hungry || healing) {
        player_rules.hurt_time += dt;
        if (player_rules.hurt_time >= interval) {
            player_rules.hurt_time -= interval;
            if (hungry) player_take_damage(DAMAGE_STARVATION,1);
            else if (healing) {
                if (player_rules.health<MAX_HEALTH) ++player_rules.health;
                else player_rules.health_debt=0;
                if (!peaceful) player_rules.exhaustion += 3.f;
            }
        }
    } else player_rules.hurt_time = 0;
}
static void update_burning(unsigned ticks) {
    if (player_rules.mode!=SURVIVAL || !player_rules.health || player_effects.fire || player_in_water() ||
        (weather_wets((int)floorf(px),(int)floorf(py+1.f),(int)floorf(pz)) && !in_lava())) {
        player_extinguish();
        return;
    }
    if (!ticks) return;
    int contact=fire_contact(body_box(px,py,pz,.28f,1.72f));
    if (contact) player_catch_fire(160);
    while (ticks-- && burning.remaining && player_rules.health) {
        if (burning.due) --burning.due;
        if (!burning.due) {
            if (contact || !in_lava()) player_take_damage(contact ? DAMAGE_DIRECT_FIRE : DAMAGE_BURNING,1);
            if (!player_rules.health) return;
            burning.due=PLAYER_BURN_PERIOD;
        }
        if (!--burning.remaining) player_extinguish();
    }
}

static void advance_effects(unsigned elapsed) {
    if (!player_rules.health) return;
    player_effects.water_breathing=player_effects.water_breathing>elapsed ?
        (uint16_t)(player_effects.water_breathing-elapsed) : 0;
    player_effects.night_vision=player_effects.night_vision>elapsed ?
        (uint16_t)(player_effects.night_vision-elapsed) : 0;
    if (player_rules.mode!=SURVIVAL) return;
    player_effects.fire=player_effects.fire>elapsed ? (uint16_t)(player_effects.fire-elapsed) : 0;
    player_effects.weakness=player_effects.weakness>elapsed ? (uint16_t)(player_effects.weakness-elapsed) : 0;
    for (unsigned i=0;i<elapsed && player_rules.health;++i) {
        if (player_effects.regeneration) {
            unsigned remaining=--player_effects.regeneration;
            unsigned period=player_effects.regeneration_ii ? REGEN_PERIOD/2 : REGEN_PERIOD;
            if (remaining%period==0) {
                if (player_rules.health<MAX_HEALTH) ++player_rules.health;
                else player_rules.health_debt=0;
            }
            if (!remaining) player_effects.regeneration_ii=0;
        }
        if (player_effects.poison) {
            int period=player_effects.poison_slow ? CAVE_POISON_PERIOD : 3;
            if (player_effects.poison%period==0) player_take_damage(DAMAGE_POISON,1);
            if (!--player_effects.poison) player_effects.poison_slow=0;
        }
        if (player_effects.hunger) {
            player_rules.exhaustion+=.015f;
            --player_effects.hunger;
        }
    }
}

float game_tick_fraction(void) { return day_fraction; }
void world_set_dawn(uint32_t day) {
    player_rules.world_day=day;
    player_rules.day_tick=0;
    day_fraction=0;
}
int game_step(float dt, int side, int front, int ascend, int descend) {
    if (!(dt > 0 && dt <= .1f) || !player_rules.health) return 0;
    day_fraction += dt * 20.f;
    unsigned elapsed = (unsigned)day_fraction;
    day_fraction -= elapsed;
    if (elapsed) carts_activate_mineshaft((Vec3){px,py,pz});
    unsigned ticks=player_rules.day_tick+elapsed;
    player_rules.world_day+=ticks/DAY_TICKS;
    player_rules.day_tick=ticks%DAY_TICKS;
    water_clock += dt;
    while (water_clock >= .12f) {
        water_step();
        water_clock -= .12f;
    }
    for (unsigned i=0;i<elapsed;++i) {
        int x=(int)floorf(px),z=(int)floorf(pz);
        lava_step();
        weather_step(x,z,1);
        fires_step(x,z,1);
        carts_tick_fuses();
        carts_tick_furnaces();
        blasts_step(1);
        if (!player_rules.health) return 0;
        leaves_step(x,z);
        redstone_step(x,z);
        hoppers_step(x,z);
        hydraulic_sorters_step(x,z,ticks-elapsed+i);
        carts_tick_hoppers(x,z);
    }
    float before_x=px,before_z=pz;
    int was_swimming=player_in_water();
    int on_boat=boats_step(dt,side,front);
    int on_cart=carts_step(dt,side,front);
    int on_horse=horses_step(dt,side,front);
    if (player_rules.health && !on_boat && !on_cart && !on_horse && !bed_sleeping())
        update_player(dt, side, front, ascend, descend);
    static float stride;
    if (!on_boat && !on_cart && !on_horse && !player_rules.flying) {
        int swimming=player_in_water();
        Vec3 origin={px,py+.30f,pz};
        if (swimming && !was_swimming) sound_play(SOUND_SPLASH,.68f,0,origin);
        else if (swimming || grounded) {
            float distance=hypotf(px-before_x,pz-before_z);
            if (distance<1.f) stride+=distance;
            if (stride>=.84f) {
                stride=0;
                if (swimming) sound_play(SOUND_SPLASH,.31f,1,origin);
                else sound_material(SOUND_STEP,world_cell((int)floorf(px),(int)floorf(py-.08f),(int)floorf(pz)),origin);
            }
        }
    } else stride=0;
    if (player_rules.mode==SURVIVAL && player_rules.hurt_time<=0.f &&
        world_cactus_contact(body_box(px,py,pz,.28f,1.72f))) player_take_damage(DAMAGE_CONTACT,1);
    gravity_step(elapsed);
    item_frames_step(elapsed);
    fireworks_step(dt);
    if (!player_rules.health) return 0;
    actors_step(dt);
    combat_step(dt);
    fishing_step(dt);
    farm_step(dt);
    bed_step(elapsed);
    advance_effects(elapsed);
    field_advance(elapsed,!on_boat && !on_cart && !on_horse && (px!=before_x || pz!=before_z),was_swimming);
    update_vitals(dt);
    update_burning(elapsed);
    if (!player_rules.health) return 0;
    experience_step(dt,elapsed);
    return drops_step(dt,elapsed);
}
