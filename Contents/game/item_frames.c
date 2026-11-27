#include "item_frames.h"
#include "drops.h"
#include "item_custom.h"
#include "../world/rails.h"
#include "../world/space.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static ItemFrame banks[DIMENSION_COUNT][ITEM_FRAME_LIMIT];
static unsigned count[DIMENSION_COUNT],revision;
static unsigned long long checked_edits[DIMENSION_COUNT];
static unsigned retry[DIMENSION_COUNT];

void item_frames_reset(void) {
    memset(banks,0,sizeof banks); memset(count,0,sizeof count);
    memset(checked_edits,0,sizeof checked_edits); memset(retry,0,sizeof retry);
    ++revision;
}
const ItemFrame *item_frames_in(Dimension d) { return (unsigned)d<DIMENSION_COUNT ? banks[d] : NULL; }
unsigned item_frames_revision(void) { return revision; }
void item_frame_basis(const ItemFrame *f,Vec3 *center,Vec3 *right,Vec3 *normal) {
    float dx=rail_dx(f->face),dz=rail_dz(f->face);
    *center=(Vec3){f->x+.5f+dx*.501f,f->y+.5f,f->z+.5f+dz*.501f};
    *right=(Vec3){dz,0,-dx};
    *normal=(Vec3){dx,0,dz};
}
static int find(Dimension d,int x,int y,int z,int face) {
    for (int i=0;i<ITEM_FRAME_LIMIT;++i) {
        ItemFrame *f=&banks[d][i];
        if (f->present && f->face==face && f->x==x && f->y==y && f->z==z) return i;
    }
    return -1;
}
int item_frame_restore(Dimension d,ItemFrame f) {
    if ((unsigned)d>=DIMENSION_COUNT || count[d]>=ITEM_FRAME_LIMIT || f.present!=1 ||
        f.face>3 || f.rotation>3 || f.detached>1 || (!f.item.count && f.rotation) ||
        f.x<=-WORLD_SAVE_LIMIT || f.x>=WORLD_SAVE_LIMIT ||
        f.z<=-WORLD_SAVE_LIMIT || f.z>=WORLD_SAVE_LIMIT ||
        !inworld(f.x,f.y,f.z) || !inworld(f.x+rail_dx(f.face),f.y,f.z+rail_dz(f.face)) ||
        !item_stack_valid(f.item) || f.item.count>1 || find(d,f.x,f.y,f.z,f.face)>=0) return 0;
    for (int i=0;i<ITEM_FRAME_LIMIT;++i) if (!banks[d][i].present) {
        banks[d][i]=f; ++count[d]; ++revision;
        checked_edits[d]=~0ull;
        return 1;
    }
    return 0;
}
static void remove_frame(Dimension d,ItemFrame *f) {
    *f=(ItemFrame){0}; --count[d]; ++revision;
}
static int release(const ItemFrame *f,int whole) {
    Vec3 c,u,n; item_frame_basis(f,&c,&u,&n);
    Drop loose[2];
    int qty=0,slots[2],found=0;
    c.x+=.16f*n.x; c.z+=.16f*n.z;
    if (f->item.count) loose[qty++]=make_drop(c.x,c.y,c.z,f->item);
    if (whole) loose[qty++]=make_drop(c.x+.07f*u.x,c.y,c.z+.07f*u.z,(ItemStack){ITEM_FRAME_ITEM,1,0,0,0});
    for (int i=0;i<qty;++i) if (!drop_valid(loose[i])) return 0;
    for (int i=0;i<MAXDROP && found<qty;++i) if (!drops[i].item.count) slots[found++]=i;
    if (found!=qty) return 0;
    for (int i=0;i<qty;++i) drops[slots[i]]=loose[i];
    if (qty) drops_dirty=1;
    return 1;
}
static int usable_support(int raw) {
    int type=cell_type(raw);
    return solid(type) && !build_partial(raw) && !piston_cell(raw) &&
        type!=ANVIL && type!=HOPPER && !is_chest(type) &&
        type!=ENCHANTING_TABLE && type!=BREWING_STAND;
}
static int occupied(int raw) { return raw!=AIR && !water_strength(raw); }
static int known_cell(Dimension d,int x,int y,int z,int *raw) {
    if (world_resident_cell(x,y,z,raw)) return 1;
    *raw=world_recorded_cell(d,x,y,z);
    return *raw>=0;
}
static int supported(Dimension d,const ItemFrame *f) {
    int raw;
    if (known_cell(d,f->x,f->y,f->z,&raw) && !usable_support(raw)) return 0;
    return !known_cell(d,f->x+rail_dx(f->face),f->y,f->z+rail_dz(f->face),&raw) || !occupied(raw);
}
void item_frames_step(unsigned ticks) {
    Dimension d=world_dimension();
    if (!ticks || !count[d]) return;
    unsigned long long edit=world_edit_version();
    int changed=checked_edits[d]!=edit;
    retry[d]=ticks<retry[d] ? retry[d]-ticks : 0;
    if (!changed && retry[d]) return;
    checked_edits[d]=edit;
    retry[d]=10;
    for (int i=0;i<ITEM_FRAME_LIMIT;++i) {
        ItemFrame *f=&banks[d][i];
        if (!f->present) continue;
        if (!f->detached && !supported(d,f)) { f->detached=1; ++revision; }
        if (f->detached && release(f,1)) remove_frame(d,f);
    }
}
static int aim(float reach) {
    Dimension d=world_dimension();
    if (!count[d]) return -1;
    Vec3 ray=forward(),eye={px,py+1.55f,pz};
    int best=-1;
    for (int i=0;i<ITEM_FRAME_LIMIT;++i) {
        const ItemFrame *f=&banks[d][i];
        if (!f->present || f->detached || fabsf(eye.x-(float)f->x)>reach+1.f ||
            fabsf(eye.y-(float)f->y)>reach+1.f || fabsf(eye.z-(float)f->z)>reach+1.f) continue;
        Vec3 c,u,n; item_frame_basis(f,&c,&u,&n);
        float viewing=ray.x*n.x+ray.z*n.z;
        if (viewing>=-.0001f) continue;
        float t=((c.x-eye.x)*n.x+(c.z-eye.z)*n.z+.055f)/viewing;
        if (t<=.015f || t>=reach) continue;
        float half=f->item.type==FILLED_MAP ? .499f : .375f;
        float along=(eye.x+ray.x*t-c.x)*u.x+(eye.z+ray.z*t-c.z)*u.z;
        if (fabsf(along)>half || fabsf(eye.y+ray.y*t-c.y)>half) continue;
        if (!supported(d,f)) continue;
        reach=t; best=i;
    }
    return best>=0 && player_clear_view(reach) ? best : -1;
}
static float interact_reach(void) { return player_rules.mode==CREATIVE ? 6.f : 5.f; }
int item_frame_aimed(ItemFrame *out) {
    int i=aim(interact_reach());
    if (i<0) return 0;
    if (out) *out=banks[world_dimension()][i];
    return 1;
}
const char *item_frame_aimed_name(Vec3 *anchor) {
    int i=aim(3.25f);
    if (i<0) return "";
    const ItemFrame *f=&banks[world_dimension()][i];
    const char *name=item_override(f->item);
    if (*name && anchor) {
        Vec3 u,n; item_frame_basis(f,anchor,&u,&n);
        anchor->x+=n.x*.075f; anchor->y+=.42f; anchor->z+=n.z*.075f;
    }
    return name;
}
const char *item_frame_use(void) {
    Dimension d=world_dimension();
    int i=aim(interact_reach());
    ItemStack *held=(unsigned)selected_slot<HOTBAR_SIZE ? &inventory[selected_slot] : NULL;
    if (i>=0) {
        ItemFrame *f=&banks[d][i];
        if (f->item.count) {
            f->rotation=(f->rotation+1)&3;
            ++revision;
            return "已旋转展示物";
        }
        if (!held || !held->count) return "手持物品，轻点展示框放入";
        f->item=*held; f->item.count=1;
        if (player_rules.mode==SURVIVAL && !--held->count) *held=(ItemStack){0};
        ++revision;
        return "已放入展示框";
    }
    if (!held || !held->count || held->type!=ITEM_FRAME_ITEM) return NULL;
    Hit h=target();
    if (!h.hit || h.y!=h.ay || abs(h.ax-h.x)+abs(h.az-h.z)!=1 ||
        !usable_support(world_cell(h.x,h.y,h.z))) return "对准完整方块的侧面挂展示框";
    int dir=h.ax<h.x ? WEST : h.ax>h.x ? EAST : h.az<h.z ? NORTH : SOUTH;
    if (!world_chunk_ready(h.ax,h.az)) return "等待区块载入";
    if (occupied(world_cell(h.ax,h.ay,h.az))) return "展示框前方需要留空";
    if (!item_frame_restore(d,(ItemFrame){.x=h.x,.y=h.y,.z=h.z,.face=(uint8_t)dir,.present=1}))
        return "此处已有展示框，或达到世界上限";
    if (player_rules.mode==SURVIVAL && !--held->count) *held=(ItemStack){0};
    return "已挂上物品展示框";
}
const char *item_frame_attack(void) {
    Dimension d=world_dimension();
    int i=aim(interact_reach());
    if (i<0) return NULL;
    ItemFrame *f=&banks[d][i];
    int breaking=!f->item.count || player_rules.mode==CREATIVE;
    if (player_rules.mode==SURVIVAL && !release(f,breaking)) return "掉落物已满，请先拾取";
    if (breaking) { remove_frame(d,f); return "已摘下展示框"; }
    f->item=(ItemStack){0}; f->rotation=0; ++revision;
    return "已取出展示物，走近拾取";
}
