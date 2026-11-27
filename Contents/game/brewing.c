#include "brewing.h"
#include "stack_transfer.h"
#include "../world/redstone.h"
#include <stdlib.h>
#include <string.h>

typedef struct { int ingredient,output[BREW_BOTTLES]; } Recipe;
/* Fresh water only. Extraction lane chooses the yield, dose, and cycle time. */
static const Recipe recipes[]={
    {LIMESTONE_ITEM,{POTION_FIRE,POTION_FIRE_LONG,AIR}},
    {PUMPKIN_ITEM,{POTION_REGEN,POTION_REGEN_LONG,AIR}},
    {SEEP_TILE_ITEM,{POTION_WATER_BREATHING,POTION_WATER_BREATHING_LONG,SPLASH_WATER_BREATHING}},
    {SHELL_FLAKE,{POTION_NIGHT_VISION,POTION_NIGHT_VISION_LONG,SPLASH_NIGHT_VISION}},
    {BROWN_MUSHROOM_ITEM,{POTION_WEAKNESS,POTION_WEAKNESS_LONG,SPLASH_WEAKNESS}}
};
typedef struct { BrewingStand *stands; size_t count,capacity; float fraction; } Bank;
static Bank banks[DIMENSION_COUNT];
static Bank *current(void) { return &banks[world_dimension()]; }

const char *brew_slot_name(int slot) {
    static const char *names[]={"快调","浓缩","雾化","原料"};
    return (unsigned)slot<BREW_SLOTS ? names[slot] : "";
}
BrewDose brew_dose(int lane,int material) {
    if (lane==BREW_QUICK && material==REED_GRIT) return (BrewDose){POTION_FIRE_LONG,2,180};
    static const unsigned cost[]={1,3,2}, time[]={120,300,200};
    if ((unsigned)lane<BREW_BOTTLES)
        for (size_t i=0;i<sizeof recipes/sizeof *recipes;++i)
            if (recipes[i].ingredient==material && recipes[i].output[lane]!=AIR)
                return (BrewDose){recipes[i].output[lane],cost[lane],time[lane]};
    return (BrewDose){0};
}
int brew_is_potion(int item) {
    return item==WATER_BOTTLE || item==AWKWARD_POTION || item==MUNDANE_POTION ||
        item==POTION_FIRE || item==POTION_FIRE_LONG || item==POTION_REGEN ||
        item==POTION_REGEN_LONG || item==POTION_REGEN_II ||
        item==POTION_WATER_BREATHING || item==POTION_WATER_BREATHING_LONG ||
        item==POTION_NIGHT_VISION || item==POTION_NIGHT_VISION_LONG ||
        brew_splash(item).effect!=BREW_SPLASH_NONE ||
        item==POTION_WEAKNESS || item==POTION_WEAKNESS_LONG;
}
BrewSplash brew_splash(int item) {
    switch (item) {
    case SPLASH_WEAKNESS: return (BrewSplash){BREW_SPLASH_WEAKNESS,WEAKNESS_TICKS*3/4};
    case SPLASH_WEAKNESS_LONG: return (BrewSplash){BREW_SPLASH_WEAKNESS,WEAKNESS_LONG_TICKS*3/4};
    case SPLASH_WATER_BREATHING: return (BrewSplash){BREW_SPLASH_WATER_BREATHING,WATER_BREATHING_TICKS*3/4};
    case SPLASH_WATER_BREATHING_LONG: return (BrewSplash){BREW_SPLASH_WATER_BREATHING,WATER_BREATHING_LONG_TICKS*3/4};
    case SPLASH_NIGHT_VISION: return (BrewSplash){BREW_SPLASH_NIGHT_VISION,NIGHT_VISION_TICKS*3/4};
    case SPLASH_NIGHT_VISION_LONG: return (BrewSplash){BREW_SPLASH_NIGHT_VISION,NIGHT_VISION_LONG_TICKS*3/4};
    default: return (BrewSplash){BREW_SPLASH_NONE,0};
    }
}
static int reagent(int id) { return brew_dose(BREW_QUICK,id).output!=AIR; }
static int recoverable_reagent(int id) {
    if (reagent(id)) return 1;
    /* Old cartridge contents remain removable; they don't run any recipe. */
    return id==FILTER_MOSS || id==MAGMA_CREAM || id==REDSTONE_DUST || id==LANTERN_SCALES ||
        id==GLOWSTONE_DUST || id==PUFFERFISH || id==GUNPOWDER || id==GOLDEN_CARROT || id==FERMENTED_SPIDER_EYE;
}
static BrewDose ready_dose(const BrewingStand *s,int lane) {
    ItemStack ingredients=s->slots[BREW_INGREDIENT],bottle=s->slots[lane];
    BrewDose d=brew_dose(lane,ingredients.type);
    return bottle.type==WATER_BOTTLE && bottle.count && d.output && ingredients.count>=d.cost ? d : (BrewDose){0};
}
int brew_pending(const BrewingStand *s,BrewDose *dose) {
    if (dose) *dose=(BrewDose){0};
    if (s && s->slots[BREW_INGREDIENT].count) {
        if (s->active && s->active<=BREW_BOTTLES) {
            int lane=(int)s->active-1;
            BrewDose d=ready_dose(s,lane);
            if (d.output) {
                if (dose) *dose=d;
                return lane;
            }
        }
        for (int lane=0;lane<BREW_BOTTLES;++lane) {
            BrewDose d=ready_dose(s,lane);
            if (!d.output) continue;
            if (dose) *dose=d;
            return lane;
        }
    }
    return -1;
}
static void recheck_lane(BrewingStand *s,int previous) {
    int lane=brew_pending(s,NULL);
    if (lane<0 || lane!=previous) { s->progress=0; s->active=0; }
}
static BrewingStand *mutable_at(int x,int y,int z) {
    Bank *b=current();
    for (size_t i=0;i<b->count;++i) {
        BrewingStand *s=&b->stands[i];
        if (s->x==x && s->y==y && s->z==z) return s;
    }
    return NULL;
}
const BrewingStand *brew_at(int x,int y,int z) { return mutable_at(x,y,z); }
static BrewingStand *append(int x,int y,int z) {
    Bank *b=current();
    if (b->count==b->capacity) {
        size_t next=b->capacity ? b->capacity*2 : 4;
        if (next<b->capacity || next>SIZE_MAX/sizeof *b->stands) return NULL;
        BrewingStand *mem=(BrewingStand *)realloc(b->stands,next*sizeof *mem);
        if (!mem) return NULL;
        b->stands=mem; b->capacity=next;
    }
    BrewingStand *s=&b->stands[b->count++];
    memset(s,0,sizeof *s); s->x=x; s->y=y; s->z=z;
    return s;
}
void brew_forget(int x,int y,int z) {
    BrewingStand *s=mutable_at(x,y,z);
    if (!s) return;
    Bank *b=current();
    *s=b->stands[--b->count];
}
size_t brewing_count_in(Dimension d) { return (unsigned)d<DIMENSION_COUNT ? banks[d].count : 0; }
const BrewingStand *brewing_at_index(Dimension d,size_t i) {
    return i<brewing_count_in(d) ? &banks[d].stands[i] : NULL;
}
void brewing_reset(void) {
    for (int d=0;d<DIMENSION_COUNT;++d) free(banks[d].stands);
    memset(banks,0,sizeof banks);
}

int brew_deposit(int x,int y,int z,int slot,int source) {
    if ((unsigned)slot>=BREW_SLOTS || (unsigned)source>=SLOT_COUNT || block(x,y,z)!=BREWING_STAND) return 0;
    ItemStack *from=&inventory[source];
    if (!from->count || !item_stack_valid(*from)) return 0;
    int ingredient=slot==BREW_INGREDIENT;
    if (ingredient ? !reagent(from->type) : from->type!=WATER_BOTTLE) return 0;
    BrewingStand *s=mutable_at(x,y,z);
    if (!s && !(s=append(x,y,z))) return 0;
    ItemStack *to=&s->slots[slot];
    if (to->count && (!ingredient || !item_same_kind(*to,*from))) return 0;
    unsigned count=from->count,free_space=item_def(from->type)->max_stack-to->count;
    if (count>free_space) count=free_space;
    if (!count) return 0;
    int previous=brew_pending(s,NULL);
    *to=(ItemStack){from->type,(uint8_t)(to->count+count),0,0,from->custom};
    from->count=(uint8_t)(from->count-count);
    if (!from->count) *from=(ItemStack){0,0,0,0,0};
    recheck_lane(s,previous);
    redstone_container_changed(x,y,z);
    return 1;
}
int brew_withdraw(int x,int y,int z,int slot) {
    BrewingStand *s=mutable_at(x,y,z);
    if ((unsigned)slot>=BREW_SLOTS || !s || !s->slots[slot].count || block(x,y,z)!=BREWING_STAND ||
        !inventory_add_stack(s->slots[slot])) return 0;
    int previous=brew_pending(s,NULL);
    s->slots[slot]=(ItemStack){0,0,0,0,0};
    recheck_lane(s,previous);
    redstone_container_changed(x,y,z);
    for (int i=0;i<BREW_SLOTS;++i) if (s->slots[i].count) return 1;
    brew_forget(x,y,z);
    return 1;
}
int brew_automation_insert(int x,int y,int z,int top,ItemStack item) {
    if (block(x,y,z)!=BREWING_STAND || !item.count || !item_stack_valid(item) ||
        (top ? !reagent(item.type) : item.type!=WATER_BOTTLE)) return 0;
    BrewingStand *s=mutable_at(x,y,z);
    if (!s && !(s=append(x,y,z))) return 0;
    int previous=brew_pending(s,NULL);
    int lo=top ? BREW_INGREDIENT : 0, hi=top ? BREW_SLOTS : BREW_BOTTLES;
    for (int i=lo;i<hi;++i) if (stack_receive(&s->slots[i],item,1)) {
        recheck_lane(s,previous);
        redstone_container_changed(x,y,z);
        return 1;
    }
    return 0;
}
void brew_automation_take_bottle(int x,int y,int z,int slot) {
    if ((unsigned)slot>=BREW_BOTTLES || block(x,y,z)!=BREWING_STAND) return;
    BrewingStand *s=mutable_at(x,y,z);
    if (!s || !s->slots[slot].count || s->slots[slot].type==WATER_BOTTLE) return;
    stack_deduct(&s->slots[slot],1);
    redstone_container_changed(x,y,z);
    for (int i=0;i<BREW_SLOTS;++i) if (s->slots[i].count) return;
    brew_forget(x,y,z);
}
static void advance_stand(BrewingStand *s,unsigned ticks) {
    int changed=0;
    while (ticks) {
        BrewDose dose;
        int lane=brew_pending(s,&dose);
        if (lane<0) { s->progress=0; s->active=0; break; }
        unsigned remaining=dose.ticks-s->progress;
        if (ticks<remaining) {
            s->progress=(uint16_t)(s->progress+ticks);
            s->active=(uint8_t)(lane+1);
            break;
        }
        ticks-=remaining;
        s->slots[lane].type=(uint16_t)dose.output;
        stack_deduct(&s->slots[BREW_INGREDIENT],dose.cost);
        changed=1;
        s->progress=0;
        s->active=0;
    }
    if (changed) redstone_container_changed(s->x,s->y,s->z);
}
void brewing_advance(float dt) {
    if (!(dt>0.f && dt<=1.f)) return;
    Bank *b=current();
    if (!b->count) return;
    b->fraction+=dt*20.f;
    unsigned ticks=(unsigned)b->fraction;
    b->fraction-=ticks;
    if (!ticks) return;
    /* Committed edits invalidate removed stands. No per-tick chunk reads:
       a distant station must not churn the renderer's chunk cache. */
    for (size_t i=0;i<b->count;++i) advance_stand(&b->stands[i],ticks);
}
int brewing_restore(const BrewingStand *s) {
    if (!s || !inworld(s->x,s->y,s->z) || block(s->x,s->y,s->z)!=BREWING_STAND ||
        mutable_at(s->x,s->y,s->z) || s->active>BREW_BOTTLES || (!s->active!=!s->progress)) return 0;
    int count=0;
    for (int i=0;i<BREW_SLOTS;++i) {
        ItemStack item=s->slots[i];
        if (!item_stack_valid(item)) return 0;
        if (item.count) {
            if (i==BREW_INGREDIENT ? !recoverable_reagent(item.type) : !brew_is_potion(item.type)) return 0;
            ++count;
        }
    }
    if (!count) return 0;
    BrewDose dose;
    if (s->progress && (brew_pending(s,&dose)!=(int)s->active-1 || s->progress>=dose.ticks)) return 0;
    BrewingStand *slot=append(s->x,s->y,s->z);
    if (!slot) return 0;
    *slot=*s;
    return 1;
}
