#include "../game/actors.h"
#include "../game/interaction.h"
#include "../game/item_icons.h"
#include "../game/villagers.h"
#include "../world/flat.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("VILLAGER TEST failed line %d: %s\n",__LINE__,#c); ++failures; } } while (0)

static int find_role(int role) {
    const ActorRecord *a=actors_in(OVERWORLD);
    for (int i=0;i<ACTOR_CAPACITY;++i) if (a[i].type==ACTOR_VILLAGER && a[i].traits==role) return i;
    return -1;
}
static int count_item(int type) {
    int total=0;
    for (int i=0;i<SLOT_COUNT;++i) if (inventory[i].type==type) total+=inventory[i].count;
    return total;
}
static void stock_item(int slot,int type,int count) { inventory[slot]=(ItemStack){(uint16_t)type,(uint8_t)count,0,0,0}; }
static void setup_village(VillagePlan *p) {
    for (int home=0;home<VILLAGE_HOMES;++home) {
        int x,z;
        if (village_home_entry(p,home,&x,&z)) (void)world_cell(x,p->floor,z);
    }
    int x,z;
    village_global(p,-11,3,&x,&z);
    px=x+.5f; py=p->floor+1.01f; pz=z+.5f; vy=0.f;
}
static VillagerSession next_to(int slot) {
    if (slot<0) return 0;
    Vec3 feet=actors_in(OVERWORLD)[slot].feet;
    px=feet.x; py=feet.y; pz=feet.z+.8f; vy=0;
    return villagers_open(slot);
}
static int capture(const VillagerSave *record,void *context) {
    VillagerSave *wanted=(VillagerSave *)context;
    if (record->home==wanted->home) *wanted=*record;
    return 1;
}
static int specialty_selftest(void) {
    int failures=0;
    const char *const profiles[]={"meadow","coast","dryland"};
    const char *const labels[]={"内陆","潮岸与湿地","干热地带"};
    const int results[]={CRAWLER_KIT,HYDRAULIC_SORTER_ITEM,AIR_STATION_ITEM};
    for (int i=0;i<3;++i) {
        FlatLayout layout;
        char layers[64];
        snprintf(layers,sizeof layers,"bedrock,4*loam,turf | %s",profiles[i]);
        CHECK(!flat_parse(layers,&layout) && world_begin_layout(971,GENERATOR_CURRENT,WORLD_SUPERFLAT,1,&layout));
        generate();
        player_reset_rules(SURVIVAL);
        VillagerSave market={-27,5,42,VILLAGER_SMITH,VILLAGER_HEALTH,3,{0},0};
        CHECK(actors_restore_roaming(OVERWORLD,
            (ActorRecord){{.5f,6.01f,.5f},0,ACTOR_VILLAGER,VILLAGER_HEALTH,ROAMING_VILLAGER|VILLAGER_SMITH},market));
        VillagerSession s=next_to(0);
        VillagerOffer o={0};
        CHECK(s && !strcmp(villagers_workshop_region(s),labels[i]) && !villagers_handoffs_needed(s) &&
            villagers_offer(s,2,&o) && o.result.type==results[i] && o.cost[0].count && o.cost[1].count);
        villagers_close(s);
    }
    printf("WORKSHOP TEST inland/wet/dry specialties failures=%d\n",failures);
    return failures;
}
int villager_selftest(void) {
    int failures=0,ok=0,raw;
    CHECK(world_begin_generated(992382978u,GENERATOR_VILLAGES));
    generate();
    player_reset_rules(SURVIVAL);
    VillagePlan p;
    if (!village_plan_for(144,-97,&p)) { puts("VILLAGER TEST missing seeded settlement"); return 1; }
    int ex,ez;
    CHECK(village_home_entry(&p,VILLAGER_FARMER,&ex,&ez));
    px=p.x+.5f; py=p.floor+1.01f; pz=p.z+.5f;
    CHECK(!world_resident_cell(ex,p.floor,ez,&raw));
    villagers_step(1);
    CHECK(find_role(VILLAGER_FARMER)<0 && !world_resident_cell(ex,p.floor,ez,&raw));
    setup_village(&p);
    villagers_step(20);
    for (int role=0;role<VILLAGE_HOMES;++role) CHECK(find_role(role)>=0);
    int farmer=find_role(VILLAGER_FARMER);
    if (farmer<0) return failures+1;
    Vec3 a=actors_in(OVERWORLD)[farmer].feet;
    yaw=atan2f(a.x-px,-(a.z-pz)); pitch=0.f;
    memset(inventory,0,sizeof inventory); selected_slot=0;
    Interaction trade=game_tap();
    CHECK(trade.panel==INTERACTION_VILLAGER && trade.actor==farmer);
    VillagerSession s=villagers_open(farmer);
    VillagerOffer o={0};
    CHECK(villagers_count(s)==1 && villagers_role(s)==VILLAGER_FARMER && villagers_offer(s,0,&o));
    CHECK(o.cost[0].type==WHEAT && o.cost[1].type==SUGAR && o.result.type==FRUIT_RATION && o.result.count==3);
    CHECK(villagers_handoffs_needed(s)==2 && !villagers_offer(s,-1,&o) && !villagers_offer(s,1,&o));
    ItemIcon icon;
    CHECK(item_icon(FRUIT_RATION,&icon));
    int max=o.remaining,price=o.cost[0].count,second=o.cost[1].count;
    for (int i=0;i<SLOT_COUNT;++i) stock_item(i,STONE,64);
    stock_item(0,WHEAT,price+1);
    stock_item(1,SUGAR,second+1);
    ItemStack before[SLOT_COUNT];
    memcpy(before,inventory,sizeof before);
    (void)villagers_exchange(s,0,&ok);
    CHECK(!ok && !memcmp(before,inventory,sizeof before) && villagers_offer(s,0,&o) && o.remaining==max);
    memset(inventory,0,sizeof inventory);
    stock_item(4,WHEAT,price);
    stock_item(1,SUGAR,second-1);
    (void)villagers_exchange(s,0,&ok);
    CHECK(!ok && count_item(WHEAT)==price && count_item(SUGAR)==second-1 && count_item(FRUIT_RATION)==0);
    stock_item(2,SUGAR,2); /* Costs may span stacks. */
    (void)villagers_exchange(s,0,&ok);
    CHECK(ok && !count_item(WHEAT) && count_item(SUGAR)==1 && count_item(FRUIT_RATION)==3 &&
        villagers_count(s)==1 && villagers_handoffs_needed(s)==1);
    villagers_close(s);
    villagers_step(100);
    s=next_to(farmer);
    CHECK(villagers_count(s)==1 && villagers_handoffs_needed(s)==1);
    for (int i=1;i<max;++i) {
        stock_item(7,WHEAT,price);
        stock_item(8,SUGAR,second);
        (void)villagers_exchange(s,0,&ok);
        CHECK(ok && villagers_count(s)==2);
    }
    stock_item(7,WHEAT,price);
    (void)villagers_exchange(s,0,&ok);
    CHECK(!ok && inventory[7].count==price && count_item(FRUIT_RATION)==3*max &&
        villagers_offer(s,0,&o) && o.remaining==0);
    villagers_step(100);
    CHECK(villagers_count(s)==2 && villagers_offer(s,0,&o) && !o.remaining);
    villagers_close(s);
    villagers_step(VILLAGER_RESTOCK_TICKS-1);
    s=villagers_open(farmer);
    CHECK(villagers_count(s)==2 && villagers_offer(s,0,&o) && !o.remaining);
    villagers_close(s);
    villagers_step(1);
    s=villagers_open(farmer);
    CHECK(villagers_count(s)==2 && villagers_offer(s,0,&o) && o.remaining==max &&
        villagers_offer(s,1,&o) && o.result.type==FILTER_MOSS);
    stock_item(7,o.cost[0].type,o.cost[0].count);
    stock_item(8,o.cost[1].type,o.cost[1].count);
    (void)villagers_exchange(s,1,&ok);
    CHECK(ok && count_item(FILTER_MOSS)==2 && villagers_handoffs_needed(s)==1);
    villagers_close(s);

    int librarian=find_role(VILLAGER_LIBRARIAN);
    CHECK(librarian>=0);
    memset(inventory,0,sizeof inventory);
    for (int i=0;i<2;++i) {
        VillagerSession open=next_to(librarian);
        CHECK(villagers_offer(open,i,&o));
        for (int delivery=0;delivery<WORKSHOP_HANDOFFS;++delivery) {
            stock_item(7,o.cost[0].type,o.cost[0].count);
            stock_item(8,o.cost[1].type,o.cost[1].count);
            (void)villagers_exchange(open,i,&ok);
            CHECK(ok && villagers_count(open)==i+1+(delivery==WORKSHOP_HANDOFFS-1));
        }
        villagers_close(open);
    }
    VillagerSession book=next_to(librarian);
    CHECK(villagers_count(book)==3 && villagers_offer(book,2,&o) &&
        o.cost[0].count && o.cost[1].count && !o.result.enchants && !villagers_handoffs_needed(book));
    if (o.cost[1].type==STRIATED_GLASS) memset(inventory,0,sizeof inventory);
    stock_item(7,o.cost[0].type,o.cost[0].count);
    int stored=count_item(o.cost[0].type);
    (void)villagers_exchange(book,2,&ok);
    CHECK(!ok && count_item(o.cost[0].type)==stored && !count_item(o.result.type));
    stock_item(11,o.cost[1].type,o.cost[1].count);
    (void)villagers_exchange(book,2,&ok);
    CHECK(ok && count_item(o.result.type)==o.result.count && !count_item(o.cost[1].type));
    villagers_close(book);

    s=next_to(farmer);
    px+=160.f;
    actors_step(.05f);
    CHECK(find_role(VILLAGER_FARMER)==-1 && !villagers_count(s));
    setup_village(&p);
    villagers_step(20);
    farmer=find_role(VILLAGER_FARMER);
    if (farmer<0) { CHECK(farmer>=0); return failures; }
    s=next_to(farmer);
    CHECK(villagers_count(s)>=2);
    int damage[ACTOR_CAPACITY]={0}; damage[farmer]=50;
    actors_explosion_hits(damage);
    CHECK(!villagers_count(s) && find_role(VILLAGER_FARMER)<0);
    villagers_step(20);
    CHECK(find_role(VILLAGER_FARMER)<0);
    VillagerSave record={0};
    CHECK(villagers_visit(OVERWORLD,capture,&record) && record.x==p.x && !record.health && record.unlocked>=2);
    CHECK(!villagers_restore(record));
    record.z++;
    CHECK(!villagers_restore(record));
    CHECK(world_select_dimension(NETHER) && !villagers_count(book));
    printf("WORKSHOP TEST atomic handoffs, live unlock, independent restock, resident lifecycle failures=%d\n",failures);
    return failures+specialty_selftest();
}
