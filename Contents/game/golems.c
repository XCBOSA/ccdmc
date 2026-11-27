#include "golems.h"
#include "actors.h"
#include "boats.h"
#include "horses.h"
#include "minecarts.h"
#include "../world/space.h"
#include <math.h>

int camp_machine_cooled(int x,int y,int z) {
    int support,above;
    if (!world_resident_cell(x,y-1,z,&support) || support!=LAMINATED_MASONRY ||
        !world_resident_cell(x,y,z,&above) || above!=AIR) return 0;
    static const int offsets[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
    for (int i=0;i<4;++i) {
        int raw;
        if (world_resident_cell(x+offsets[i][0],y-1,z+offsets[i][1],&raw) && water_strength(raw)) return 1;
    }
    return 0;
}
static int room(BlockBox body) {
    if (box_overlap(body,body_box(px,py,pz,.28f,1.72f))) return 0;
    for (int i=0;i<ACTOR_CAPACITY;++i) {
        BlockBox actor;
        if (actors_body_at(world_dimension(),i,&actor) && box_overlap(body,actor)) return 0;
    }
    for (int x=(int)floorf(body.x0);x<(int)ceilf(body.x1);++x)
        for (int y=(int)floorf(body.y0);y<(int)ceilf(body.y1);++y)
            for (int z=(int)floorf(body.z0);z<(int)ceilf(body.z1);++z) {
                int raw;
                if (!world_resident_cell(x,y,z,&raw) || raw!=AIR || boat_overlaps_cell(x,y,z) ||
                    cart_overlaps_cell(x,y,z) || horse_overlaps_cell(x,y,z)) return 0;
            }
    return 1;
}
const char *camp_machine_deploy(Hit h) {
    if ((unsigned)selected_slot>=HOTBAR_SIZE) return NULL;
    ItemStack *held=&inventory[selected_slot];
    int pulse=held->type==PULSE_KIT;
    if (!held->count || (!pulse && held->type!=CRAWLER_KIT)) return NULL;
    int x=h.ax,y=h.ay,z=h.az,below;
    if (!h.hit || h.ay!=h.y+1 || y<1 || !inworld(x,y,z) ||
        !world_resident_cell(x,y-1,z,&below) || !full_top_surface(below))
        return "设备需部署在完整、平坦的顶面";
    for (int i=0;i<4;++i)
        if (!world_point_blocked(x+(i&1 ? .93f : .07f),y-.02f,z+(i&2 ? .93f : .07f),1))
            return "设备需部署在完整、平坦的顶面";
    if (pulse && !camp_machine_cooled(x,y,z)) return "脉冲机需要侧面接水的层压砌板";
    Vec3 feet={x+.5f,y+.01f,z+.5f};
    if (!room(body_box(feet.x,feet.y,feet.z,pulse ? SNOW_GOLEM_RADIUS : GOLEM_RADIUS,
        pulse ? SNOW_GOLEM_HEIGHT : GOLEM_HEIGHT))) return "设备展开位置被挡住，未扣除";
    float heading=remainderf(yaw+3.14159265f,6.2831853f);
    ActorRecord record=pulse ? (ActorRecord){feet,heading,ACTOR_SNOW_GOLEM,SNOW_GOLEM_HEALTH,0} :
        (ActorRecord){feet,heading,ACTOR_IRON_GOLEM,CRAWLER_FACTORY_HEALTH,GOLEM_PLAYER_BUILT};
    if (!actors_restore(world_dimension(),record)) return "设备容量已满，未扣除";
    if (player_rules.mode==SURVIVAL && !--held->count) *held=(ItemStack){0};
    return pulse ? "已部署水冷脉冲机" : "已部署岩甲巡护机";
}
