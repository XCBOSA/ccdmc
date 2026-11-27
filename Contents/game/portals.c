#include "portals.h"
#include "combat.h"
#include "fire.h"
#include "enchanting.h"
#include "boats.h"
#include "minecarts.h"
#include "horses.h"
#include "fishing.h"
#include "beds.h"
#include "../world/aquifer_layout.h"
#include "../world/pressure_layout.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static EndReturn end_return;
enum { AQUIFER_IDLE, AQUIFER_DESCEND, AQUIFER_RETURN, PRESSURE_DESCEND, PRESSURE_RETURN };
static int aquifer_trip;
static unsigned aquifer_samples;
static unsigned pressure_relief;
static float vent_exposure;
static int vent_danger;

void portals_reset(void) {
    end_return = (EndReturn){0, {0,0,0}};
    aquifer_trip=AQUIFER_IDLE;
    aquifer_samples=0;
    pressure_relief=0;
    vent_exposure=0;
    vent_danger=0;
}
EndReturn portals_end_return(void) { return end_return; }
void portals_set_end_return(EndReturn point) { end_return = point; }
unsigned portals_aquifer_samples(void) { return aquifer_samples; }
int portals_aquifer_vent_danger(void) { return vent_danger; }
int portals_restore_aquifer(unsigned samples) {
    if (samples>AQUIFER_SAMPLES || (samples && !uses_aquifer())) return 0;
    aquifer_samples=samples;
    return 1;
}

static int vent_sealed(void) {
    int raw;
    return world_resident_cell(AQUIFER_SEAL_X,AQUIFER_SEAL_Y,0,&raw) && raw==LAMINATED_MASONRY;
}

unsigned portals_pressure_relief(void) {
    return inside_pressure_works() ? pressure_relief : 0;
}
void portals_reload_pressure_relief(void) {
    unsigned branches=0;
    for (int side=0;side<2;++side) {
        const int x=side ? PRESSURE_VALVE_X : -PRESSURE_VALVE_X;
        if (world_recorded_cell(NETHER,x,PRESSURE_FLOOR+1,PRESSURE_VALVE_Z)==PRESSURE_OPEN)
            branches|=1u<<side;
    }
    pressure_relief=branches;
}

static const char *use_pressure_station(Hit h,int raw) {
    if (raw==PRESSURE_LIFT) {
        aquifer_trip=PRESSURE_RETURN;
        return "正在升回含水带";
    }
    if (raw==PRESSURE_CLOSED || raw==PRESSURE_OPEN) {
        if (!pressure_valve_site(h.x,h.y,h.z)) return "此处不是排压分路";
        if (raw==PRESSURE_OPEN) return "这一路已排压";
        if (!set_block(h.x,h.y,h.z,PRESSURE_OPEN)) return "阀门暂时无法转动";
        pressure_relief|=h.x>0 ? 2u : 1u;
        vent_exposure=0;
        vent_danger=0;
        return portals_pressure_relief()==3 ? "两路排压完成 · 前往岩廊尽头的烧结炉" : "一路已排压 · 还需打开对侧阀门";
    }
    if (raw!=PRESSURE_KILN) return NULL;
    if (portals_pressure_relief()!=3) return "压力未平衡 · 先开启东西两路排压阀";
    if ((unsigned)selected_slot>=HOTBAR_SIZE || !inventory[selected_slot].count ||
        inventory[selected_slot].type!=COBBLESTONE) return "手持圆石轻点烧结炉，可烧成岩砖";
    if (!inventory_fill_container(selected_slot,(ItemStack){SINTERED_TILE_ITEM,2,0,0,0}))
        return "背包没有空间放烧结岩砖";
    return "地热烧结完成 · 获得两块烧结岩砖";
}

const char *portals_use_aquifer(Hit h) {
    if (!uses_aquifer() || !h.hit) return NULL;
    int raw;
    if (!world_resident_cell(h.x,h.y,h.z,&raw)) return NULL;
    if (inside_pressure_works()) return use_pressure_station(h,raw);
    if (inside_aquifer() && raw==PRESSURE_HATCH) {
        if (aquifer_samples!=AQUIFER_SAMPLES) return "先完成潮沟和热裂隙两路取样";
        if (player_in_water() || player_rules.flying) return "先站稳，再启动巡检升降机";
        aquifer_trip=PRESSURE_DESCEND;
        return "正在下降到压力岩廊";
    }
    if (raw==AQUIFER_WINCH) {
        if (world_dimension()!=OVERWORLD) return "勘测绞盘需要架在地表世界";
        if (player_in_water() || player_rules.flying ||
            !solid(block(h.x,h.y-1,h.z))) return "绞盘要有稳固支撑，先站稳再下井";
        aquifer_trip=AQUIFER_DESCEND;
        return "正在沿绞盘下降";
    }
    if (!inside_aquifer()) return NULL;
    if (raw==AQUIFER_DOCK) {
        aquifer_trip=AQUIFER_RETURN;
        return "正在沿缆索返回";
    }
    if (raw!=AQUIFER_BRINE && raw!=AQUIFER_VENT) return NULL;
    unsigned bit=raw==AQUIFER_BRINE ? AQUIFER_SAMPLE_BRINE : AQUIFER_SAMPLE_VENT;
    if (aquifer_samples&bit) return "这个取样点已经完成，去查另一路或回营";
    ItemStack *held=(unsigned)selected_slot<HOTBAR_SIZE ? &inventory[selected_slot] : NULL;
    if (bit==AQUIFER_SAMPLE_VENT && !vent_sealed()) {
        if (!held || !held->count || held->type!=LAMINATE_ITEM)
            return "手持层压砌板轻点取样头，先封住气孔";
        int at_seal;
        if (!world_resident_cell(AQUIFER_SEAL_X,AQUIFER_SEAL_Y,0,&at_seal) || at_seal!=AIR)
            return "气孔有杂物，先清空再封闭";
        if (fabsf(px-AQUIFER_SEAL_X-.5f)<.8f && fabsf(pz-.5f)<.8f && py<AQUIFER_SEAL_Y+1)
            return "退开一点再封闭气孔";
        if (!set_block(AQUIFER_SEAL_X,AQUIFER_SEAL_Y,0,LAMINATED_MASONRY)) return "暂时无法封闭气孔";
        if (player_rules.mode!=CREATIVE && !--held->count) *held=(ItemStack){0};
        vent_exposure=0;
        vent_danger=0;
        return "气孔已封闭 · 换岩层探锤取样";
    }
    if (!held || !held->count || held->type!=SURVEY_TAPPER) return "手持岩层探锤，轻点取样头";
    if (bit==AQUIFER_SAMPLE_BRINE) {
        int at_eye;
        if (!world_resident_cell((int)floorf(px),(int)floorf(py+1.55f),(int)floorf(pz),&at_eye) ||
            !water_strength(at_eye)) return "要潜入潮沟，在水下敲击取样头";
    }
    if (!inventory_add(bit==AQUIFER_SAMPLE_BRINE ? BRINE_CORE : THERMAL_CORE))
        return "背包已满，留出位置再取样";
    aquifer_samples|=bit;
    return bit==AQUIFER_SAMPLE_BRINE ? "已取样：盐析岩芯" : "已取样：换热岩芯";
}


const char *portal_item_use(Hit h) {
    ItemStack *held = &inventory[selected_slot];
    if (!held->count) return NULL;
    if (held->type == FLINT_AND_STEEL) {
        if (!h.hit) return "没有可点火的位置";
        int tnt=block(h.x,h.y,h.z)==TNT_BLOCK;
        int action=tnt ? fire_use_igniter(h.x,h.y,h.z) : fire_use_igniter(h.ax,h.ay,h.az);
        if (!action) return "这里无法点火";
        if (player_rules.mode==SURVIVAL) enchant_wear(held,1,0);
        return action==IGNITE_TNT ? "TNT 已点燃，立即离开" : "已点火";
    }
    return NULL;
}


static int safe_stand(float x, int y, float z) {
    int a = (int)floorf(x), c = (int)floorf(z);
    int floor = block(a,y-1,c);
    return y > 0 && y+2 < WY && solid(floor) && !collides(x,(float)y+.01f,z) &&
        block(a,y,c) == AIR && block(a,y+1,c) == AIR;
}

static int find_end_return(int x,int y,int z,Vec3 *out) {
    for (int r = 1; r <= 7; ++r) for (int a = -r; a <= r; ++a) for (int b = -r; b <= r; ++b) {
        if (abs(a) != r && abs(b) != r) continue;
        if (safe_stand(x+a+.5f,y,z+b+.5f)) {
            *out = (Vec3){x+a+.5f,y+.01f,z+b+.5f};
            return 1;
        }
    }
    return 0;
}

static const char *arrive(Dimension dim,Vec3 p,float direction) {
    px = p.x; py = p.y; pz = p.z; yaw = direction; pitch = -.12f;
    combat_cancel_draw();
    fishing_cancel();
    bed_cancel_sleep();
    boat_unseat();
    cart_unseat();
    horse_unseat();
    mx = my = vy = touch_altitude = 0;
    grounded = 0; drops_dirty = 1;
    player_rules.fall_origin = py;
    player_rules.air = MAX_AIR;
    player_rules.submerged_time = 0;
    printf("DIMENSION %d ARRIVE %.2f %.2f %.2f\n",(int)dim,px,py,pz);
    fflush(stdout);
    return dim==NETHER ? "压力岩廊 · 东西分路排压后可烧结岩砖" :
        dim==END ? "已到含水带 · 西侧潮沟，东侧热裂隙" : "已返回主世界";
}

static int camp_landing(Vec3 requested,Vec3 *target) {
    int x=(int)floorf(requested.x),y=(int)floorf(requested.y),z=(int)floorf(requested.z);
    if (safe_stand(requested.x,y,requested.z)) {
        *target=(Vec3){requested.x,y+.01f,requested.z};
        return 1;
    }
    return find_end_return(x,y,z,target);
}

int portals_return_from_retired_site(void) {
    Dimension from=world_dimension();
    if (!((from==END && !uses_aquifer()) || (from==NETHER && !inside_pressure_works())) ||
        !world_select_dimension(OVERWORLD)) return 0;
    Vec3 target;
    if (!(end_return.valid && camp_landing(end_return.position,&target)) &&
        !camp_landing(player_world_spawn(),&target)) {
        world_select_dimension(from);
        return 0;
    }
    /* Leave all world banks untouched. Only the player's transient travel state moves. */
    player_rules.flying=0;
    arrive(OVERWORLD,target,0);
    return 1;
}

static const char *aquifer_travel(int trip) {
    Dimension start=world_dimension();
    if (trip==AQUIFER_DESCEND && start!=OVERWORLD) return NULL;
    if (trip==AQUIFER_RETURN && !inside_aquifer()) return NULL;
    int interactive=world_set_interactive(0);
    if (trip==AQUIFER_DESCEND) {
        Vec3 back={px,py,pz};
        if (!world_select_dimension(END) || !safe_stand(.5f,AQUIFER_FLOOR+1,-.5f)) {
            world_select_dimension(start);
            world_set_interactive(interactive);
            return "井底通道被堵住，绞盘没有运行";
        }
        end_return=(EndReturn){1,back};
        world_set_interactive(interactive);
        lead_player_left(start);
        player_rules.flying=0;
        const char *message=arrive(END,(Vec3){.5f,AQUIFER_FLOOR+1.01f,-.5f},3.14159265f);
        pitch=-.34f;
        return message;
    }
    if (!world_select_dimension(OVERWORLD)) {
        world_set_interactive(interactive);
        return "地面营地暂时无法载入";
    }
    Vec3 target_pos=end_return.valid ? end_return.position : player_world_spawn();
    int x=(int)floorf(target_pos.x),y=(int)floorf(target_pos.y),z=(int)floorf(target_pos.z);
    if (!safe_stand(target_pos.x,y,target_pos.z) && !find_end_return(x,y,z,&target_pos))
        target_pos=player_world_spawn();
    if (!safe_stand(target_pos.x,(int)floorf(target_pos.y),target_pos.z)) {
        world_select_dimension(start);
        world_set_interactive(interactive);
        return "营地和出生点没有可站立的空间";
    }
    world_set_interactive(interactive);
    lead_player_left(start);
    return arrive(OVERWORLD,target_pos,0);
}

static const char *pressure_travel(int trip) {
    Dimension from=world_dimension();
    if ((trip==PRESSURE_DESCEND && (!inside_aquifer() || aquifer_samples!=AQUIFER_SAMPLES)) ||
        (trip==PRESSURE_RETURN && !inside_pressure_works())) return NULL;
    Dimension destination=trip==PRESSURE_DESCEND ? NETHER : END;
    const int inbound=trip==PRESSURE_DESCEND;
    const Vec3 requested=inbound ? (Vec3){.5f,PRESSURE_FLOOR+1.01f,1.5f} :
        (Vec3){.5f,AQUIFER_FLOOR+1.01f,PRESSURE_AQUIFER_HATCH_Z+1.5f};
    const int interactive=world_set_interactive(0);
    Vec3 landing;
    if (!world_select_dimension(destination) || !camp_landing(requested,&landing)) {
        world_select_dimension(from);
        world_set_interactive(interactive);
        return "升降机出口被堵住，先保持在原地";
    }
    world_set_interactive(interactive);
    lead_player_left(from);
    player_rules.flying=0;
    vent_exposure=0;
    vent_danger=0;
    return arrive(destination,landing,inbound ? 0.f : 3.14159265f);
}

const char *portals_step(float dt) {
    if (!player_rules.health) { aquifer_trip=AQUIFER_IDLE; vent_exposure=0; vent_danger=0; return NULL; }
    if (aquifer_trip!=AQUIFER_IDLE) {
        int trip=aquifer_trip;
        aquifer_trip=AQUIFER_IDLE;
        return trip>=PRESSURE_DESCEND ? pressure_travel(trip) : aquifer_travel(trip);
    }
    if (inside_pressure_works()) {
        const unsigned branch=px<.5f ? 1u : 2u;
        const unsigned relief=portals_pressure_relief();
        vent_danger=player_rules.mode==SURVIVAL && fabsf(fabsf(px-.5f)-11.f)<4.5f &&
            fabsf(pz-PRESSURE_VALVE_Z-.5f)<3.5f && py>=PRESSURE_FLOOR && py<PRESSURE_FLOOR+5 &&
            !(relief&branch);
        if (vent_danger) {
            vent_exposure+=dt;
            if (vent_exposure>=1.45f) {
                vent_exposure=0;
                player_take_damage(DAMAGE_DIRECT_FIRE,1);
            }
        } else vent_exposure=0;
        return NULL;
    }
    if (inside_aquifer()) {
        vent_danger=player_rules.mode==SURVIVAL && fabsf(px-AQUIFER_SEAL_X-.5f)<1.6f &&
            fabsf(pz-.5f)<1.5f && py>=AQUIFER_FLOOR-1 && py<=AQUIFER_FLOOR+3 && !vent_sealed();
        if (vent_danger) {
            vent_exposure+=dt;
            if (vent_exposure>=1.9f) {
                vent_exposure=0;
                player_take_damage(DAMAGE_DIRECT_FIRE,2);
            }
        } else vent_exposure=0;
        return NULL;
    }
    vent_danger=0;
    vent_exposure=0;
    return NULL;
}
