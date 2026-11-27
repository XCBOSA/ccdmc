#include "droppers.h"
#include "container_automation.h"
#include "dispenser_actions.h"
#include "drops.h"
#include "stack_transfer.h"
#include "../world/redstone.h"
#include "../world/internal.h"
#include <stdlib.h>

enum { BUCKETS=1021 };
typedef struct Node { Dropper d; struct Node *next; } Node;
typedef struct { unsigned count; Node *buckets[BUCKETS]; } Bank;
static Bank banks[DIMENSION_COUNT];
static Bank *bank(void) { return &banks[world_dimension()]; }
int droppers_present(void) { return bank()->count!=0; }
static Node **at(int x,int y,int z) {
    unsigned index=((uint32_t)x*374761393u^(uint32_t)z*668265263u^(uint32_t)y*2246822519u)%BUCKETS;
    Node **link=&bank()->buckets[index];
    while (*link && ((*link)->d.x!=x || (*link)->d.y!=y || (*link)->d.z!=z)) link=&(*link)->next;
    return link;
}
static int position_valid(int x,int y,int z) {
    return (unsigned)y<WY && x>=-WORLD_SAVE_LIMIT && x<=WORLD_SAVE_LIMIT &&
        z>=-WORLD_SAVE_LIMIT && z<=WORLD_SAVE_LIMIT;
}
static Dropper *new_dropper(const Dropper *d) {
    Node **link=at(d->x,d->y,d->z);
    if (*link || bank()->count>=DROPPER_LIMIT) return NULL;
    Node *node=(Node *)calloc(1,sizeof *node);
    if (!node) return NULL;
    node->d=*d; *link=node; ++bank()->count;
    return &node->d;
}
const Dropper *dropper_at(int x,int y,int z) {
    Node *node=*at(x,y,z);
    return node ? &node->d : NULL;
}
int dropper_seed_temple(int x,int y,int z) {
    if (!position_valid(x,y,z)) return 0;
    Dropper d={0}; d.x=x; d.y=y; d.z=z;
    unsigned pick=hash_for_seed(world_seed()^0x6172726fu^(unsigned)y,x,z);
    d.slots[pick%DROPPER_SLOTS]=(ItemStack){ARROW,(uint8_t)(8+(pick>>8)%7u),0,0,0};
    return new_dropper(&d)!=NULL;
}
void dropper_forget(int x,int y,int z) {
    Node **link=at(x,y,z);
    if (!*link) return;
    Node *node=*link; *link=node->next; free(node);
    --bank()->count;
}
void droppers_reset(void) {
    for (int d=0;d<DIMENSION_COUNT;++d) {
        Bank *b=&banks[d];
        for (int i=0;i<BUCKETS;++i) while (b->buckets[i]) {
            Node *node=b->buckets[i]; b->buckets[i]=node->next; free(node);
        }
        b->count=0;
    }
}
int droppers_visit(Dimension d,DropperVisitor visit,void *ctx) {
    if ((unsigned)d>=DIMENSION_COUNT || !visit) return 0;
    for (int i=0;i<BUCKETS;++i)
        for (Node *n=banks[d].buckets[i];n;n=n->next) if (!visit(&n->d,ctx)) return 0;
    return 1;
}
int dropper_restore(const Dropper *d) {
    if (!d || !position_valid(d->x,d->y,d->z) ||
        !ejector_cell(world_recorded_cell(world_dimension(),d->x,d->y,d->z))) return 0;
    for (int i=0;i<DROPPER_SLOTS;++i) if (!item_stack_valid(d->slots[i])) return 0;
    return new_dropper(d)!=NULL;
}
int ejector_place(int x,int y,int z,int face,int type) {
    if ((unsigned)face>=EJECTOR_FACES || !ejector_type(type) || !position_valid(x,y,z) || !world_chunk_ready(x,z) ||
        (block(x,y,z)!=AIR && block(x,y,z)!=WATER)) return 0;
    Dropper d={0}; d.x=x; d.y=y; d.z=z;
    if (!new_dropper(&d)) return 0;
    const BlockEdit edit={x,y,z,(type==DISPENSER ? DISPENSER_BASE : DROPPER_BASE)+face};
    if (world_set_blocks(&edit,1)) return 1;
    dropper_forget(x,y,z);
    return 0;
}
int dropper_place(int x,int y,int z,int face) { return ejector_place(x,y,z,face,DROPPER); }
int dropper_move(int x,int y,int z,int from,int to,int single) {
    Node *n=*at(x,y,z);
    if (!n || !storage_stack_move(n->d.slots,DROPPER_SLOTS,from,to,single)) return 0;
    if (from<DROPPER_SLOTS || to<DROPPER_SLOTS) redstone_container_changed(x,y,z);
    return 1;
}
int dropper_insert(int x,int y,int z,ItemStack item) {
    Node *n=*at(x,y,z);
    if (!n) return 0;
    for (int i=0;i<DROPPER_SLOTS;++i) if (stack_receive(&n->d.slots[i],item,1)) {
        redstone_container_changed(x,y,z);
        return 1;
    }
    return 0;
}
int dropper_extract(int x,int y,int z,int slot) {
    Node *n=*at(x,y,z);
    if (!n || (unsigned)slot>=DROPPER_SLOTS || !n->d.slots[slot].count) return 0;
    stack_deduct(&n->d.slots[slot],1);
    redstone_container_changed(x,y,z);
    return 1;
}
int dropper_fire(int x,int y,int z,int raw) {
    Node *n=*at(x,y,z);
    int dir=ejector_face(raw);
    if (!n || dir<0) return 1;
    int dx=rail_dx(dir),dy=dir==EJECTOR_DOWN ? -1 : dir==EJECTOR_UP ? 1 : 0,dz=rail_dz(dir);
    int tx=x+dx,ty=y+dy,tz=z+dz;
    if (!position_valid(tx,ty,tz)) return 1;
    if (!world_chunk_ready(tx,tz)) return 0;
    Dropper *d=&n->d;
    int slots[DROPPER_SLOTS],count=0;
    for (int i=0;i<DROPPER_SLOTS;++i) if (d->slots[i].count) slots[count++]=i;
    if (!count) return 1;
    unsigned roll=++d->rolls;
    int slot=slots[hash_for_seed(world_seed()^roll*2654435761u^(uint32_t)y,x,z)%(unsigned)count];
    ItemStack item=d->slots[slot];
    item.count=1;
    Vec3 muzzle={x+.5f+dx*.64f,y+.5f+dy*.64f,z+.5f+dz*.64f};
    if (dispenser_cell(raw)) {
        DispenseResult result=dispenser_action(d->slots,DROPPER_SLOTS,slot,
            (DispenseOutlet){tx,ty,tz,dx,dy,dz,muzzle});
        if (result==DISPENSE_USED) redstone_container_changed(x,y,z);
        if (result!=DISPENSE_EJECT) return 1;
    }
    int inserted=dispenser_cell(raw) ? NO_INVENTORY : container_insert_one(tx,ty,tz,dy,item);
    if (inserted==NO_INVENTORY) {
        for (int i=0;i<MAXDROP;++i) if (!drops[i].item.count) {
            drops[i]=make_drop(muzzle.x,muzzle.y,muzzle.z,item);
            drops_dirty=1;
            inserted=INVENTORY_INSERTED;
            break;
        }
    }
    if (inserted==INVENTORY_INSERTED) dropper_extract(x,y,z,slot);
    return 1;
}
