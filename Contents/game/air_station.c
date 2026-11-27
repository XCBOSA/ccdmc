#include "air_station.h"
#include "sound.h"
#include "../world/redstone.h"
#include <math.h>
#include <stdio.h>

typedef struct { int x,y,z; } Cell;
static const Cell faces[]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
static int equals(Cell a,Cell b) { return a.x==b.x && a.y==b.y && a.z==b.z; }
static int resident(Cell p,int *raw) {
    return world_resident_cell(p.x,p.y,p.z,raw);
}
static int set_pressure(Cell p,int charge) {
    BlockEdit edit={p.x,p.y,p.z,AIR_STATION_BASE+charge};
    return world_set_blocks(&edit,1);
}
static int same_dry_area(Cell p) {
    int x=(int)floorf(px),y=(int)floorf(py+1.55f),z=(int)floorf(pz),eye=0,inlet=0;
    return resident((Cell){x,y,z},&eye) && !water_strength(eye) &&
        resident((Cell){p.x,p.y+1,p.z},&inlet) && inlet==AIR;
}
static int submerged(void) {
    float eye=py+1.55f;
    Cell p={(int)floorf(px),(int)floorf(eye),(int)floorf(pz)};
    int water,above;
    return resident(p,&water) && water_strength(water) &&
        resident((Cell){p.x,p.y+1,p.z},&above) && eye-p.y<water_surface(water,above);
}

typedef enum { AIR_ROUTE_MISSING,AIR_ROUTE_READY,AIR_ROUTE_UNLOADED,AIR_ROUTE_AMBIGUOUS,AIR_ROUTE_TOO_LARGE } Route;
static Route find_station(Cell outlet,Cell *station,int *pressure) {
    Cell queue[AIR_PIPE_LIMIT+1]={outlet};
    unsigned tail=1;
    int found=0,missing=0;
    for (unsigned head=0;head<tail;++head) {
        Cell p=queue[head];
        for (unsigned f=0;f<sizeof faces/sizeof faces[0];++f) {
            Cell q={p.x+faces[f].x,p.y+faces[f].y,p.z+faces[f].z};
            int raw;
            if (!resident(q,&raw)) {
                /* An unloaded branch might lead to a second pump. Don't
                   commit against an incomplete network. */
                missing=1; continue;
            }
            if (air_station_cell(raw) && head) {
                if (found && !equals(*station,q)) return AIR_ROUTE_AMBIGUOUS;
                *station=q; *pressure=raw-AIR_STATION_BASE; found=1;
                continue;
            }
            if (raw!=AIR_PIPE) continue;
            unsigned i=0;
            while (i<tail && !equals(queue[i],q)) ++i;
            if (i<tail) continue;
            if (tail>=sizeof queue/sizeof queue[0]) return AIR_ROUTE_TOO_LARGE;
            queue[tail++]=q;
        }
    }
    return missing ? AIR_ROUTE_UNLOADED : found ? AIR_ROUTE_READY : AIR_ROUTE_MISSING;
}

const char *air_station_tap(Hit hit) {
    if (!hit.hit || !player_rules.health ||
        hit.x<=-WORLD_SAVE_LIMIT || hit.x>=WORLD_SAVE_LIMIT ||
        hit.z<=-WORLD_SAVE_LIMIT || hit.z>=WORLD_SAVE_LIMIT || !inworld(hit.x,hit.y,hit.z)) return NULL;
    Cell p={hit.x,hit.y,hit.z};
    int raw;
    if (!resident(p,&raw)) return NULL;
    const Vec3 at={p.x+.5f,p.y+.5f,p.z+.5f};
    static char message[100];
    if (air_relay_cell(raw)) {
        if ((unsigned)selected_slot>=HOTBAR_SIZE || inventory[selected_slot].count) return NULL;
        int charge=0;
        switch (redstone_press_air_relay(p.x,p.y,p.z,&charge)) {
        case AIR_RELAY_STARTED:
            sound_play(SOUND_AIR_PUMP,.50f,(unsigned)charge+17,at);
            snprintf(message,sizeof message,"已发出储压脉冲 · 储压 %d/%d",charge,AIR_STATION_MAX);
            return message;
        case AIR_RELAY_BUSY: return "储压信号箱 · 脉冲尚未结束";
        case AIR_RELAY_UNLOADED: return "储压信号箱 · 等待进气侧区块加载";
        case AIR_RELAY_DISCONNECTED: return "储压信号箱 · 背面需紧贴供气站";
        case AIR_RELAY_UNCHARGED: return "储压信号箱 · 请为供气站加压";
        default: return "未能发出脉冲 · 未消耗储压";
        }
    }
    if (air_station_cell(raw)) {
        int charge=raw-AIR_STATION_BASE;
        if (!same_dry_area(p)) return "供气站进气口需留空，请在干处加压";
        if (charge>=AIR_STATION_MAX) return "供气站 · 储压已满 6/6";
        charge=charge+AIR_REFILL_COST>AIR_STATION_MAX ? AIR_STATION_MAX : charge+AIR_REFILL_COST;
        if (!set_pressure(p,charge)) return "供气站无法储压，请重试";
        sound_play(SOUND_AIR_PUMP,.64f,(unsigned)charge,at);
        snprintf(message,sizeof message,"供气站 · 储压 %d/%d",charge,AIR_STATION_MAX);
        return message;
    }
    if (!air_outlet_cell(raw)) return NULL;
    if (!submerged()) return "水下气嘴 · 潜入水中后使用";
    if (player_rules.air>=MAX_AIR) return "换气已满 · 不消耗储压";
    Cell station={0};
    int charge=0;
    switch (find_station(p,&station,&charge)) {
    case AIR_ROUTE_UNLOADED: return "供气管路有未加载区段";
    case AIR_ROUTE_AMBIGUOUS: return "同一管路只能接一台供气站";
    case AIR_ROUTE_TOO_LARGE: return "供气管路最多 24 段，请分成独立支路";
    case AIR_ROUTE_MISSING: return "气嘴未接通供气站 · 检查导气管";
    case AIR_ROUTE_READY: break;
    }
    if (charge<AIR_REFILL_COST) return "储压不足 · 回到干处为供气站加压";
    if (!set_pressure(station,charge-AIR_REFILL_COST)) return "换气失败，未消耗储压";
    player_rules.air=MAX_AIR;
    player_rules.submerged_time=0.f;
    sound_play(SOUND_TIDERAY,.50f,(unsigned)charge,at);
    snprintf(message,sizeof message,"已补满换气 · 储压 %d/%d",charge-AIR_REFILL_COST,AIR_STATION_MAX);
    return message;
}
