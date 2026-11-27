#include "survey.h"
#include "sound.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int survey_rock(int cell) { return cell==SLATE_ROCK || cell==LIMESTONE_ROCK || cell==JOINT_BASALT; }
static int substrate(int cell) {
    switch (cell_type(cell)) {
    case STONE: case GRASS: case DIRT: case GRAVEL: case SAND: case SANDSTONE: case CLAY:
    case PODZOL: case RED_SAND: case MYCELIUM: case COAL_ORE: case IRON_ORE:
    case GOLD_ORE: case DIAMOND_ORE: case REDSTONE_ORE: case LAPIS_ORE:
    case SLATE_ROCK: case LIMESTONE_ROCK: case JOINT_BASALT:
        return 1;
    default:
        return 0;
    }
}

SurveyReading survey_read(Hit h) {
    SurveyReading out={SURVEY_NO_FACE,0,0,0,SURVEY_CENTER};
    if (!h.hit || h.y<0 || h.y>=WY || world_dimension()!=OVERWORLD) return out;
    if (h.x<INT_MIN+SURVEY_DEPTH+1 || h.x>INT_MAX-SURVEY_DEPTH-1 ||
        h.z<INT_MIN+SURVEY_DEPTH+1 || h.z>INT_MAX-SURVEY_DEPTH-1) return out;
    const int64_t dx=(int64_t)h.x-h.ax,dy=(int64_t)h.y-h.ay,dz=(int64_t)h.z-h.az;
    if (dx<-1 || dx>1 || dy<-1 || dy>1 || dz<-1 || dz>1 || dx*dx+dy*dy+dz*dz!=1) return out;
    int cell;
    if (!world_resident_cell(h.x,h.y,h.z,&cell)) {
        out.state=SURVEY_INCOMPLETE;
        return out;
    }
    if (survey_rock(cell)) {
        out.state=SURVEY_EXPOSED; out.rock=cell;
        return out;
    }
    if (!substrate(cell)) { out.state=SURVEY_UNSUITABLE; return out; }
    int nx=(int)dx,ny=(int)dy,nz=(int)dz;
    out.vertical=ny;
    int sx[]={0,nz,-nz,0,0}, sy[]={0,0,0,1,-1}, sz[]={0,-nx,nx,0,0};
    unsigned active=ny ? 1u : 31u;
    const int limit=ny ? SURVEY_VERTICAL_DEPTH : SURVEY_DEPTH;
    int missing=0;
    /* Each wall lane starts at the exposed face and stops at the first
       discontinuity. Work is capped at 30 resident reads, on tap only. */
    for (int depth=0; depth<=limit && active; ++depth) {
        for (int lane=SURVEY_CENTER; lane<=SURVEY_BELOW; ++lane) {
            unsigned bit=1u<<lane;
            if (!(active&bit)) continue;
            if (!depth && lane==SURVEY_CENTER) continue;
            int x=h.x+sx[lane]+nx*depth,y=h.y+sy[lane]+ny*depth,z=h.z+sz[lane]+nz*depth;
            if (!world_resident_cell(x,y,z,&cell)) { missing=1; active&=~bit; continue; }
            if (!substrate(cell)) { active&=~bit; continue; }
            if (survey_rock(cell)) {
                out.state=depth ? SURVEY_SEAM : SURVEY_EXPOSED;
                out.rock=cell; out.depth=depth; out.lane=(SurveyLane)lane;
                return out;
            }
        }
    }
    out.state=missing ? SURVEY_INCOMPLETE : SURVEY_CLEAR;
    return out;
}

const char *survey_tap(Hit hit) {
    if (world_dimension()!=OVERWORLD) return "岩层探锤只适用于地表世界的岩层";
    SurveyReading reading=survey_read(hit);
    if (reading.state==SURVEY_NO_FACE) return "对准岩面或土层，轻点敲击";
    if (reading.state==SURVEY_UNSUITABLE) return "这里不是可勘探的岩面或土层";
    if (reading.state==SURVEY_INCOMPLETE) return "附近地形未加载，靠近后再敲一次";
    sound_material(SOUND_MINE,reading.rock ? reading.rock : STONE,
        (Vec3){hit.x+.5f,hit.y+.5f,hit.z+.5f});
    if (reading.state==SURVEY_CLEAR) return "未探到页岩、石灰岩或玄武岩，换一处再敲";

    static char text[100];
    const char *directions[]={"正面","左侧一格","右侧一格","上一格","下一格"};
    const char *rock=item_name(reading.rock==SLATE_ROCK ? SLATE_ITEM :
        reading.rock==JOINT_BASALT ? JOINT_BASALT_ITEM : LIMESTONE_ITEM);
    if (reading.state==SURVEY_EXPOSED) {
        if (reading.lane==SURVEY_CENTER)
            snprintf(text,sizeof text,"露头：%s · %s",rock,geology_pick_tier(reading.rock)>1 ? "用石镐或更好的镐" : "可用木镐采集");
        else snprintf(text,sizeof text,"露头：%s · 敲击点%s",rock,directions[reading.lane]);
    } else {
        const char *from=reading.vertical ? (reading.vertical>0 ? "向上" : "向下") : directions[reading.lane];
        snprintf(text,sizeof text,"%s回声 · %s%s%d 格",rock,from,reading.vertical ? "" : "往里 ",reading.depth);
    }
    return text;
}

const char *strata_sample_tap(Hit hit) {
    if (!hit.hit || world_dimension()!=OVERWORLD || world_generation()<GENERATOR_STRATA_GALLERIES ||
        (unsigned)selected_slot>=HOTBAR_SIZE || inventory[selected_slot].type!=SURVEY_TAPPER ||
        !inventory[selected_slot].count) return NULL;
    int raw;
    if (!world_resident_cell(hit.x,hit.y,hit.z,&raw) || !strata_sample_cell(raw)) return NULL;
    const uint16_t type=raw==STRATA_SAMPLE_SHALE ? SHALE_SECTION : LIME_SECTION;
    ItemStack backup[SLOT_COUNT];
    memcpy(backup,inventory,sizeof backup);
    if (!inventory_add_stack((ItemStack){type,1,0,0,0})) return "背包已满 · 样片仍在交界处";
    const BlockEdit sample={hit.x,hit.y,hit.z,strata_sample_host(raw)};
    if (!world_set_blocks(&sample,1)) {
        memcpy(inventory,backup,sizeof backup);
        return "暂时无法取样 · 交界岩未改变";
    }
    sound_material(SOUND_MINE,sample.cell,(Vec3){hit.x+.5f,hit.y+.5f,hit.z+.5f});
    return type==SHALE_SECTION ? "取到页岩交界样片 · 可去高台寻找石灰岩样片" :
        "取到石灰岩交界样片 · 两种样片可用来合成测线尺";
}

int strata_gauge_read(Hit hit,SurveyReading lanes[3]) {
    if (!lanes || !hit.hit || hit.y<0 || hit.y>=WY || hit.y!=hit.ay ||
        world_dimension()!=OVERWORLD ||
        hit.x< -WORLD_SAVE_LIMIT || hit.x>WORLD_SAVE_LIMIT ||
        hit.z< -WORLD_SAVE_LIMIT || hit.z>WORLD_SAVE_LIMIT) return 0;
    const int64_t dx=(int64_t)hit.x-hit.ax,dz=(int64_t)hit.z-hit.az;
    if (dx<-1 || dx>1 || dz<-1 || dz>1 || dx*dx+dz*dz!=1) return 0;
    const SurveyLane names[]={SURVEY_LEFT,SURVEY_CENTER,SURVEY_RIGHT};
    for (int i=0;i<3;++i) {
        const int offset=1-i;
        const int x0=hit.x+(int)dz*offset,z0=hit.z-(int)dx*offset;
        /* Three independent rays. No jumping across a cavity, and no
           synchronous terrain work to fill a missing lane. */
        SurveyReading r={SURVEY_CLEAR,0,0,0,names[i]};
        for (int depth=0;depth<=SURVEY_DEPTH;++depth) {
            int raw;
            if (!world_resident_cell(x0+(int)dx*depth,hit.y,z0+(int)dz*depth,&raw)) {
                r.state=SURVEY_INCOMPLETE; break;
            }
            if (strata_sample_cell(raw)) raw=strata_sample_host(raw);
            if (survey_rock(raw)) {
                r.rock=raw; r.depth=depth; r.state=depth ? SURVEY_SEAM : SURVEY_EXPOSED; break;
            }
            if (!substrate(raw)) { r.state=SURVEY_UNSUITABLE; break; }
        }
        lanes[i]=r;
    }
    return 1;
}

const char *strata_gauge_tap(Hit hit) {
    SurveyReading lanes[3];
    if (!strata_gauge_read(hit,lanes)) return "用测线尺对准地表世界的竖直岩壁";
    char line[3][44];
    for (int i=0;i<3;++i) {
        const SurveyReading r=lanes[i];
        if (r.rock) {
            const char *rock=r.rock==SLATE_ROCK ? "页岩" : r.rock==LIMESTONE_ROCK ? "石灰岩" : "玄武岩";
            if (r.depth) snprintf(line[i],sizeof line[i],"%s %d 格",rock,r.depth);
            else snprintf(line[i],sizeof line[i],"%s露头",rock);
        } else snprintf(line[i],sizeof line[i],"%s",r.state==SURVEY_INCOMPLETE ? "未载入" :
            r.state==SURVEY_UNSUITABLE ? "岩面中断" : "未探到");
    }
    static char msg[162];
    snprintf(msg,sizeof msg,"左：%s · 中：%s · 右：%s",line[0],line[1],line[2]);
    sound_material(SOUND_MINE,STONE,(Vec3){hit.x+.5f,hit.y+.5f,hit.z+.5f});
    return msg;
}

enum { TIDE_SIDE=5,TIDE_BELOW=2,TIDE_ABOVE=8,TIDE_W=2*TIDE_SIDE+1,
       TIDE_H=TIDE_BELOW+TIDE_ABOVE+1,TIDE_CELLS=TIDE_W*TIDE_W*TIDE_H };
typedef struct { int8_t x,y,z; uint8_t first; uint16_t length; } TideNode;

TideReading tide_read(Vec3 point) {
    TideReading out={TIDE_DRY,TIDE_UP,0};
    if (!(fabsf(point.x)<WORLD_SAVE_LIMIT && fabsf(point.z)<WORLD_SAVE_LIMIT &&
        point.y>=0.f && point.y<WY)) return out;
    int x=(int)floorf(point.x),y=(int)floorf(point.y),z=(int)floorf(point.z),raw,above;
    if (!world_resident_cell(x,y,z,&raw) || !world_resident_cell(x,y+1,z,&above)) {
        out.state=TIDE_UNLOADED; return out;
    }
    if (!water_strength(raw) || point.y>=y+water_surface(raw,above)) return out;
    unsigned char seen[TIDE_CELLS]={0};
    TideNode queue[TIDE_CELLS];
    unsigned front=0,back=1;
    queue[0]=(TideNode){0,0,0,0,0};
    seen[(TIDE_BELOW*TIDE_W+TIDE_SIDE)*TIDE_W+TIDE_SIDE]=1;
    static const int8_t offset[6][3]={{0,1,0},{0,0,-1},{1,0,0},{0,0,1},{-1,0,0},{0,-1,0}};
    out.state=TIDE_NO_EXIT;
    while (front<back) {
        TideNode n=queue[front++];
        for (unsigned dir=0;dir<6;++dir) {
            int dx=n.x+offset[dir][0],dy=n.y+offset[dir][1],dz=n.z+offset[dir][2];
            if (dx< -TIDE_SIDE || dx>TIDE_SIDE || dy< -TIDE_BELOW || dy>TIDE_ABOVE ||
                dz< -TIDE_SIDE || dz>TIDE_SIDE) continue;
            /* The air test must precede the visited test: a cell first reached
               sideways may still be a valid surface approached from below. */
            if (dir==TIDE_UP && world_resident_cell(x+dx,y+dy,z+dz,&raw) && raw==AIR)
                return (TideReading){TIDE_EXIT,(TideDirection)(n.length ? n.first : dir),n.length+1u};
            unsigned at=(unsigned)(((dy+TIDE_BELOW)*TIDE_W+dx+TIDE_SIDE)*TIDE_W+dz+TIDE_SIDE);
            if (seen[at]) continue;
            seen[at]=1;
            if (!world_resident_cell(x+dx,y+dy,z+dz,&raw)) { out.state=TIDE_UNLOADED; continue; }
            if (!water_strength(raw)) continue;
            /* Count each water cell at most once. Avoid token water layers
               that cannot contain a swimmer's head. */
            if (!world_resident_cell(x+dx,y+dy+1,z+dz,&above)) { out.state=TIDE_UNLOADED; continue; }
            if (water_surface(raw,above)<.5f || back>=TIDE_CELLS) continue;
            queue[back++]=(TideNode){(int8_t)dx,(int8_t)dy,(int8_t)dz,
                (uint8_t)(n.length ? n.first : dir),(uint16_t)(n.length+1)};
        }
    }
    return out;
}

const char *tide_report(Vec3 point,int from_ray) {
    TideReading r=tide_read(point);
    if (r.state==TIDE_DRY) return from_ray ? "潮帆鳐不在水下，暂时读不到潮路" : "头部潜入水中再读取潮路";
    if (r.state==TIDE_UNLOADED) return "潮路边缘还没加载，靠近后再试";
    if (r.state==TIDE_NO_EXIT) return "附近没有读到连通的换气水面";
    static const char *const names[]={"上","北","东","南","西","下"};
    static const char *const relative[]={"前","右前","右","右后","后","左后","左","左前"};
    static char text[104];
    const char *title=from_ray ? "从鳐所在处" : "潮路";
    if (r.first>=TIDE_NORTH && r.first<=TIDE_WEST) {
        float heading=(r.first-TIDE_NORTH)*1.5707963268f,angle=heading-yaw;
        if (!isfinite(angle)) angle=0.f;
        int octant=(int)floorf(remainderf(angle,6.2831853f)/.7853981634f+.5f);
        snprintf(text,sizeof text,"%s · 向%s（%s） · %u 格换气",title,names[r.first],relative[(octant+8)%8],r.steps);
    } else snprintf(text,sizeof text,"%s · 先向%s · %u 格换气",title,names[r.first],r.steps);
    sound_play(SOUND_TIDERAY,.56f,r.steps,point);
    return text;
}
