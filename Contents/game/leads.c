#include "leads.h"
#include "drops.h"
#include "actors.h"
#include "horses.h"
#include <math.h>
#include <stddef.h>

static unsigned revision;
unsigned lead_revision(void) { return revision; }
int lead_valid(LeadLink a) {
    if (a.kind==LEAD_FENCE)
        return a.x>=-WORLD_SAVE_LIMIT && a.x<=WORLD_SAVE_LIMIT && a.z>=-WORLD_SAVE_LIMIT &&
            a.z<=WORLD_SAVE_LIMIT && a.y>=0 && a.y<WY;
    return (a.kind==LEAD_FREE || a.kind==LEAD_PLAYER || a.kind==LEAD_PENDING) && !a.x && !a.y && !a.z;
}
void lead_set(LeadLink *a,LeadLink b) {
    if (a && (a->kind!=b.kind || a->x!=b.x || a->y!=b.y || a->z!=b.z)) { *a=b; ++revision; }
}
int lead_anchor(LeadLink link,Vec3 *out) {
    if (!out) return 0;
    if (link.kind==LEAD_PLAYER && player_rules.health) { *out=(Vec3){px,py+.95f,pz}; return 1; }
    if (link.kind==LEAD_FENCE && fence_type(block(link.x,link.y,link.z))) {
        *out=(Vec3){link.x+.5f,link.y+.88f,link.z+.5f}; return 1;
    }
    return 0;
}
static float distance2(Vec3 a,Vec3 b) {
    float x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;
    return x*x+y*y+z*z;
}
static int leave_items(Dimension d,Vec3 p,int count) {
    if (player_rules.mode==CREATIVE || !count) return 1;
    int n=(count+STACK_MAX-1)/STACK_MAX,free_count=0,slots[4];
    if (n<1 || n>(int)(sizeof slots/sizeof *slots)) return 0;
    Drop *list=dimension_drops[d];
    for (int i=0;i<MAXDROP && free_count<n;++i) if (!list[i].item.count) slots[free_count++]=i;
    if (free_count!=n) return 0;
    for (int i=0;i<n;++i) {
        int stack=count>STACK_MAX ? STACK_MAX : count;
        list[slots[i]]=make_drop(p.x,p.y+.26f,p.z,(ItemStack){LEAD,(uint8_t)stack,0,0,0});
        count-=stack;
    }
    drops_dirty=1;
    return 1;
}
static void snap(Dimension d,LeadLink *a,Vec3 at) {
    lead_set(a,leave_items(d,at,1) ? (LeadLink){LEAD_FREE,0,0,0} : (LeadLink){LEAD_PENDING,0,0,0});
}
int lead_release(Dimension d,LeadLink *link,Vec3 at) {
    if ((unsigned)d>=DIMENSION_COUNT || !link) return 0;
    if (!link->kind) return 1;
    if (!leave_items(d,at,1)) return 0;
    lead_set(link,(LeadLink){LEAD_FREE,0,0,0});
    return 1;
}
int lead_tick(Dimension dim,LeadLink *link,Vec3 feet,Vec3 *anchor) {
    if (!link->kind) return 0;
    Vec3 neck={feet.x,feet.y+.5f,feet.z};
    if (!lead_anchor(*link,anchor) || distance2(neck,*anchor)>100.f) { snap(dim,link,feet); return 0; }
    return 1;
}
int lead_allows(Vec3 from,Vec3 next,Vec3 anchor) {
    from.y+=.50f; next.y+=.50f;
    float to=distance2(next,anchor);
    return to<=5.25f*5.25f || to<distance2(from,anchor);
}
const char *lead_use_animal(LeadLink *a,Vec3 feet) {
    if (!a || !player_rules.health || selected_slot<0 || selected_slot>=HOTBAR_SIZE) return NULL;
    ItemStack *held=&inventory[selected_slot];
    if (held->count && held->type!=LEAD) return NULL;
    if (a->kind==LEAD_PLAYER) {
        if (!leave_items(world_dimension(),feet,1)) return "掉落区已满，绳子未解开";
        lead_set(a,(LeadLink){LEAD_FREE,0,0,0});
        return "已解开拴绳";
    }
    if (!held->count) return NULL;
    if (a->kind==LEAD_FENCE) return "这只动物已经拴在栅栏上";
    if (a->kind==LEAD_PENDING) return "请先清理地上的掉落物";
    lead_set(a,(LeadLink){LEAD_PLAYER,0,0,0});
    if (player_rules.mode==SURVIVAL && !--held->count) *held=(ItemStack){0,0,0,0,0};
    return "已牵住动物，对栅栏使用可以系紧";
}

typedef struct { LeadLink *link; Vec3 feet; } Entry;
enum { MAX_LINKS=ACTOR_CAPACITY+HORSE_CAPACITY };
static int gather(Dimension d,Entry found[MAX_LINKS]) {
    int n=0;
    const ActorRecord *a=actors_in(d);
    const HorseRecord *h=horses_in(d);
    if (!a || !h) return 0;
    for (int i=0;i<ACTOR_CAPACITY;++i) if (a[i].type) {
        LeadLink *l=actors_lead_at(d,i);
        if (l->kind) found[n++]=(Entry){l,a[i].feet};
    }
    for (int i=0;i<HORSE_CAPACITY;++i) if (h[i].type) {
        LeadLink *l=horses_lead_at(d,i);
        if (l->kind) found[n++]=(Entry){l,h[i].feet};
    }
    return n;
}
static int attached(LeadLink a,int x,int y,int z) { return a.kind==LEAD_FENCE && a.x==x && a.y==y && a.z==z; }
const char *lead_use_fence(Hit h) {
    if (!h.hit || !player_rules.health || !fence_type(block(h.x,h.y,h.z))) return NULL;
    Dimension d=world_dimension();
    Entry list[MAX_LINKS];
    int n=gather(d,list),tied=0,old=0;
    Vec3 center={h.x+.5f,h.y+.7f,h.z+.5f};
    for (int i=0;i<n;++i) {
        LeadLink *a=list[i].link;
        if (a->kind==LEAD_PLAYER && distance2(list[i].feet,center)<49.f) {
            lead_set(a,(LeadLink){LEAD_FENCE,h.x,h.y,h.z}); ++tied;
        } else if (attached(*a,h.x,h.y,h.z)) ++old;
    }
    if (tied) return "已把动物拴到栅栏";
    if (!old) return NULL;
    if (selected_slot<0 || selected_slot>=HOTBAR_SIZE ||
        (inventory[selected_slot].count && inventory[selected_slot].type!=LEAD)) return NULL;
    if (!leave_items(d,center,old)) return "掉落区已满，未解开栅栏上的绳子";
    for (int i=0;i<n;++i) if (attached(*list[i].link,h.x,h.y,h.z))
        lead_set(list[i].link,(LeadLink){LEAD_FREE,0,0,0});
    return "已解开栅栏上的绳子";
}
void lead_fence_removed(int x,int y,int z) {
    Dimension d=world_dimension();
    Entry list[MAX_LINKS];
    int n=gather(d,list);
    for (int i=0;i<n;++i) if (attached(*list[i].link,x,y,z)) snap(d,list[i].link,list[i].feet);
}
void lead_player_left(Dimension d) {
    Entry list[MAX_LINKS];
    int n=gather(d,list);
    for (int i=0;i<n;++i) if (list[i].link->kind==LEAD_PLAYER) snap(d,list[i].link,list[i].feet);
}
