#include "beds.h"
#include "weather.h"
#include "drops.h"
#include "actors.h"
#include "boats.h"
#include "chests.h"
#include "horses.h"
#include "minecarts.h"
#include "fishing.h"
#include <math.h>

enum { SLEEP_TICKS=100 };
static const int step[4][2]={{0,-1},{1,0},{0,1},{-1,0}};
static BedAnchor home;
static int remaining;
static uint32_t wake_day;
static BedPos sleeping_head;
static const char *message;

static BedPos move(BedPos p,int dir,int k) {
    return (BedPos){p.x+step[dir][0]*k,p.y,p.z+step[dir][1]*k};
}
static int same(BedPos a,BedPos b) { return a.x==b.x && a.y==b.y && a.z==b.z; }
static int key(BedPos p) { return world_cell(p.x,p.y,p.z); }
static int pair(BedPos p,BedPos *f,BedPos *h) {
    int raw=key(p);
    if (!is_bed_cell(raw)) return 0;
    int code=raw-BED_BASE,dir=code&3,head=(code&4)!=0;
    BedPos q=move(p,dir,head ? -1 : 1);
    if (key(q)!=(BED_BASE+(code^4))) return 0;
    *f=head ? q : p; *h=head ? p : q;
    return 1;
}
static int place_clear(BedPos p) {
    if (!inworld(p.x,p.y,p.z) || block(p.x,p.y,p.z)!=AIR || p.y==0) return 0;
    int support=block(p.x,p.y-1,p.z);
    if (!solid(support) || chest_shape_type(support)) return 0;
    if (px+.28f>p.x && px-.28f<p.x+1 && pz+.28f>p.z && pz-.28f<p.z+1 &&
        py<p.y+BED_HEIGHT && py+1.72f>p.y) return 0;
    return !actors_overlap_cell(p.x,p.y,p.z) && !horse_overlaps_cell(p.x,p.y,p.z) &&
        !boat_overlaps_cell(p.x,p.y,p.z) && !cart_overlaps_cell(p.x,p.y,p.z);
}
int bed_place(BedPos foot,int direction) {
    if ((unsigned)direction>=4 || !place_clear(foot)) return 0;
    BedPos head=move(foot,direction,1);
    if (!place_clear(head)) return 0;
    BlockEdit e[]={{foot.x,foot.y,foot.z,BED_BASE+direction},
                   {head.x,head.y,head.z,BED_BASE+4+direction}};
    return world_set_blocks(e,2);
}
BedAnchor bed_anchor(void) { return home; }
int bed_restore_anchor(BedAnchor anchor) {
    if ((anchor.valid!=0 && anchor.valid!=1) ||
        (anchor.valid && !inworld(anchor.head.x,anchor.head.y,anchor.head.z))) return 0;
    home=anchor;
    return 1;
}
int bed_sleeping(void) { return remaining!=0; }
const char *bed_take_message(void) { const char *m=message; message=NULL; return m; }
void bed_cancel_sleep(void) { remaining=0; message=NULL; }
void bed_reset(void) { home=(BedAnchor){0,{0,0,0}}; bed_cancel_sleep(); }

const char *bed_break(BedPos p) {
    if (!is_bed_cell(key(p))) return NULL;
    BedPos f={0,0,0},h={0,0,0};
    int paired=pair(p,&f,&h);
    int drop=-1;
    if (player_rules.mode==SURVIVAL) {
        for (int i=0;i<MAXDROP;++i) if (!drops[i].item.count) { drop=i; break; }
        if (drop<0) return "掉落区已满，床未拆除";
    }
    BlockEdit e[2]={{p.x,p.y,p.z,AIR},{0,0,0,AIR}};
    if (paired) {
        BedPos other=same(f,p) ? h : f;
        e[1]=(BlockEdit){other.x,other.y,other.z,AIR};
    }
    if (!world_set_blocks(e,paired ? 2 : 1)) return "无法记录拆除，床未改变";
    if (drop>=0) {
        drops[drop]=make_drop(p.x+.5f,p.y+.42f,p.z+.5f,(ItemStack){BED,1,0,0,0});
        drops_dirty=1;
    }
    return "床已拆下";
}

static int blocked(BedPos p) {
    return solid(block(p.x,p.y+1,p.z)) || block(p.x,p.y+1,p.z)==WATER ||
        block(p.x,p.y+1,p.z)==LAVA;
}
static int within(BedPos p) {
    return fabsf(px-(p.x+.5f))<=3.2f && fabsf(pz-(p.z+.5f))<=3.2f && fabsf(py-p.y)<=2.1f;
}
static const char *bed_blast(BedPos f,BedPos h) {
    BlockEdit edits[345];
    int n=0;
    edits[n++]=(BlockEdit){f.x,f.y,f.z,AIR};
    edits[n++]=(BlockEdit){h.x,h.y,h.z,AIR};
    for (int x=-3;x<=3;++x) for (int y=-3;y<=3;++y) for (int z=-3;z<=3;++z) {
        if (x*x+y*y+z*z>9) continue;
        BedPos p={h.x+x,h.y+y,h.z+z};
        if (same(p,f) || same(p,h) || !inworld(p.x,p.y,p.z)) continue;
        int t=block(p.x,p.y,p.z);
        if (!solid(t) || t==BEDROCK || t==OBSIDIAN ||
            t==FURNACE || chest_shape_type(t) || rail_type(block(p.x,p.y+1,p.z))) continue;
        edits[n++]=(BlockEdit){p.x,p.y,p.z,AIR};
    }
    if (!world_set_blocks(edits,n)) return "无法记录维度反应，床未改变";
    float a=px-h.x-.5f,b=py-h.y,c=pz-h.z-.5f;
    float d=sqrtf(a*a+b*b+c*c);
    if (d<7) player_take_damage(DAMAGE_EXPLOSION,(int)ceilf((7.f-d)*4.f));
    return "这个维度不能睡觉，床爆炸了";
}
const char *bed_use(Hit hit) {
    if (!hit.hit || !player_rules.health) return NULL;
    BedPos target={hit.x,hit.y,hit.z};
    if (!is_bed_cell(key(target))) return NULL;
    BedPos f,h;
    if (!pair(target,&f,&h)) return "床不完整，无法使用";
    if (!within(h) && !within(f)) return "离床太远";
    if (world_dimension()!=OVERWORLD) return bed_blast(f,h);
    if (blocked(h) || blocked(f)) return "床上方被挡住了";
    if (player_rules.flying || boat_rider()>=0 || cart_rider()>=0 || horse_rider()>=0 ||
        player_in_water()) return "请站在床旁再睡觉";
    if ((player_rules.day_tick<12542 || player_rules.day_tick>23460) && weather_kind()!=WEATHER_THUNDER)
        return "只能在夜晚或雷暴时睡觉";
    if (actors_hostile_near((Vec3){h.x+.5f,(float)h.y,h.z+.5f},8.f,5.f))
        return "附近有怪物，无法入睡";
    home=(BedAnchor){1,h};
    remaining=SLEEP_TICKS; sleeping_head=h; message=NULL;
    wake_day=player_rules.world_day+1u;
    fishing_cancel(); mx=my=vy=touch_altitude=0;
    return "正在睡觉，重生点已设定";
}
void bed_step(unsigned ticks) {
    if (!remaining) return;
    BedPos f,h;
    if (world_dimension()!=OVERWORLD || !player_rules.health ||
        !pair(sleeping_head,&f,&h) || !same(h,sleeping_head) || blocked(h) || blocked(f) ||
        actors_hostile_near((Vec3){sleeping_head.x+.5f,(float)sleeping_head.y,sleeping_head.z+.5f},8.f,5.f)) {
        bed_cancel_sleep(); message="睡眠中断";
        return;
    }
    if (ticks<(unsigned)remaining) { remaining-=(int)ticks; return; }
    bed_cancel_sleep();
    world_set_dawn(wake_day);
    weather_clear_at_dawn();
    message="天亮了；重生点仍在这张床旁";
}
int bed_respawn_position(Vec3 *out) {
    if (!out || !home.valid || world_dimension()!=OVERWORLD) return 0;
    BedPos f,h;
    if (pair(home.head,&f,&h) && same(h,home.head) && !blocked(h) && !blocked(f)) {
        const BedPos p[]={h,f};
        for (int i=0;i<2;++i) for (int off=0;off<4;++off) {
            BedPos q=move(p[i],off,1);
            int under=block(q.x,q.y-1,q.z),at=block(q.x,q.y,q.z);
            Vec3 where={q.x+.5f,q.y+.01f,q.z+.5f};
            if (!inworld(q.x,q.y,q.z) || !solid(under) || at!=AIR ||
                block(q.x,q.y+1,q.z)!=AIR || collides(where.x,where.y,where.z)) continue;
            *out=where;
            return 1;
        }
    }
    home.valid=0;
    return 0;
}
