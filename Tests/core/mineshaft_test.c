#define _POSIX_C_SOURCE 200809L
#include "../game/game.h"
#include "../game/drops.h"
#include "../game/actors.h"
#include "../game/dungeon.h"
#include "../game/item_icons.h"
#include "../game/minecarts.h"
#include "../world/mineshafts.h"
#include "../world/light.h"
#include "../world/space.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int failures;
#define CHECK(x) do { if (!(x)) { printf("MINESHAFT TEST line %d: %s\n",__LINE__,#x); ++failures; } } while (0)

static int chunk(int x) { return x/CHUNK_SIZE-(x%CHUNK_SIZE<0); }
static RailCell at(MineshaftPlan p,int u,int v,int y) {
    return (RailCell){p.x+(p.rotated ? v : u),p.floor+y,p.z+(p.rotated ? u : v)};
}
static int get(RailCell q) { return world_cell(q.x,q.y,q.z); }
static int find_plan(TerrainKey key,int rotated,MineshaftPlan *plan) {
    for (int x=-3;x<=3;++x) for (int z=-3;z<=3;++z)
        if (mineshaft_plan(key,x*MINESHAFT_REGION,z*MINESHAFT_REGION,plan) && plan->rotated==rotated) return 1;
    return 0;
}
static void check_seams(TerrainKey key,int rotation) {
    MineshaftPlan p={0};
    if (!find_plan(key,rotation,&p)) { CHECK(0 && "fixture missing"); return; }
    RailCell start=at(p,p.left+2,0,1),end=at(p,p.right-2,0,1);
    CHECK(chunk(start.x)!=chunk(end.x) || chunk(start.z)!=chunk(end.z));
    static TerrainResult saved,reversed,other;
    terrain_prepare(key,chunk(start.x),chunk(start.z),&saved);
    terrain_prepare(key,chunk(end.x),chunk(end.z),&other);
    terrain_prepare(key,chunk(start.x),chunk(start.z),&reversed);
    CHECK(!memcmp(saved.cells,reversed.cells,sizeof saved.cells) && saved.wide.count==reversed.wide.count &&
        !memcmp(saved.wide.cell,reversed.wide.cell,saved.wide.count*sizeof(TerrainWideCell)));
    terrain_prepare(key,chunk(end.x),chunk(end.z),&reversed);
    CHECK(!memcmp(other.cells,reversed.cells,sizeof other.cells) && other.wide.count==reversed.wide.count &&
        !memcmp(other.wide.cell,reversed.wide.cell,other.wide.count*sizeof(TerrainWideCell)));
    int rails=0,links=0,bridges=0,webs=0;
    for (int u=p.left;u<=p.right;++u) {
        RailCell q=at(p,u,0,1),ground=q;
        --ground.y;
        int raw=get(q),floor=get(ground);
        CHECK(solid(cell_type(floor)));
        CHECK(raw==AIR || rail_type(cell_type(raw)));
        bridges+=floor==PLANKS;
        if (rail_type(cell_type(raw))) {
            RailPiece piece;
            CHECK(rail_at(q,&piece) && piece.shape==(p.rotated ? STRAIGHT_NS : STRAIGHT_EW));
            RailCell forward=at(p,u+1,0,1),next;
            if (u<p.right && rail_type(cell_type(get(forward)))) {
                int entry=-1;
                CHECK(rail_link(q,1,&next,&entry) && next.x==forward.x && next.z==forward.z && next.y==forward.y);
                ++links;
            }
            ++rails;
        }
    }
    for (int v=p.north;v<=p.south;++v) {
        RailCell q=at(p,p.branch[1],v,1);
        CHECK(v==p.south-3 ? get(q)==MINE_CAVE_SPIDER_CELL : !solid(cell_type(get(q))));
        for (int i=-1;i<=1;++i) webs+=get(at(p,p.branch[1]+i,v,1))==COBWEB;
    }
    CHECK(rails>30 && links>16 && webs>=3);
    CHECK(get(at(p,4,3,1))==WOOD_FENCE && get(at(p,3,3,1))==TORCH);
    CHECK(get(at(p,0,p.north+3,1))==AIR);
    printf("MINESHAFT TEST rotation=%d center=%d,%d,%d rails=%d connected=%d bridge=%d webs=%d\n",
        rotation,p.x,p.floor+1,p.z,rails,links,bridges,webs);
    TerrainKey old=key;
    old.generation=GENERATOR_DUNGEONS;
    CHECK(!mineshaft_plan(old,p.x,p.z,&p));
    old=key; old.structures_disabled=1;
    CHECK(!mineshaft_plan(old,p.x,p.z,&p));
    old=key; old.dimension=NETHER;
    CHECK(!mineshaft_plan(old,p.x,p.z,&p));
}
static int drops_of(int type) {
    int n=0;
    for (int i=0;i<MAXDROP;++i) if (drops[i].item.type==type) n+=drops[i].item.count;
    return n;
}
static void check_worker(TerrainKey key) {
    MineshaftPlan p={0};
    if (!find_plan(key,0,&p) || !world_begin_generated(key.seed,key.generation) ||
        !world_stream_start()) { CHECK(0 && "terrain worker setup"); return; }
    const struct timespec wait={0,2000000};
    world_set_interactive(1);
    static TerrainResult expect;
    /* Compare wide overlays in worker-loaded chunks against synchronous generation. */
    RailCell q=at(p,p.branch[1],p.south,1);
    int cx=chunk(q.x),cz=chunk(q.z);
    size_t verified=0;
    for (int dz=0;dz>=-1;--dz) {
        terrain_prepare(key,cx,cz+dz,&expect);
        ChunkView v=chunk_view(cx,cz+dz);
        for (int i=0;i<2500 && !v.cells;++i) {
            world_stream_pump(cx*CHUNK_SIZE,(cz+dz)*CHUNK_SIZE);
            v=chunk_view(cx,cz+dz);
            if (!v.cells) nanosleep(&wait,NULL);
        }
        CHECK(v.cells!=NULL);
        if (!v.cells) continue;
        for (unsigned i=0;i<expect.wide.count;++i) {
            TerrainWideCell item=expect.wide.cell[i];
            if (item.cell!=COBWEB && item.cell!=MINE_CAVE_SPIDER_CELL) continue;
            int col=item.index/WY,y=item.index%WY,raw=-1;
            CHECK(world_resident_cell(cx*CHUNK_SIZE+col/CHUNK_SIZE,y,
                (cz+dz)*CHUNK_SIZE+col%CHUNK_SIZE,&raw) && raw==item.cell);
            ++verified;
        }
    }
    CHECK(verified>=3);
    world_stream_stop();
}
static void check_web(void) {
    CHECK(world_begin_profile(1712,GENERATOR_CURRENT,WORLD_SUPERFLAT));
    player_reset_rules(CREATIVE);
    px=8.5f; py=4.01f; pz=8.5f;
    yaw=pitch=0;
    (void)world_cell(8,4,8);
    ItemIcon icon;
    CHECK(item_icon(COBWEB_ITEM,&icon) && !solid(COBWEB) && light_opacity(COBWEB)==1);
    CHECK(!world_cobweb_contact(body_box(px,py,pz,.28f,1.72f)));
    (void)game_step(.05f,1,0,0,0);
    float clear=px-8.5f;
    px=8.5f; py=4.01f; pz=8.5f;
    CHECK(set_block(8,4,8,COBWEB));
    CHECK(world_cobweb_contact(body_box(px,py,pz,.28f,1.72f)));
    CHECK(!world_box_blocked(body_box(px,py,pz,.28f,1.72f),0));
    (void)game_step(.05f,1,0,0,0);
    CHECK(clear>.15f && fabsf((px-8.5f)/clear-.25f)<.01f);
    CHECK(set_block(8,4,8,AIR));
    player_reset_rules(SURVIVAL);
    CHECK(game_set_difficulty(NORMAL));
    px=8.5f; py=4.01f; pz=8.5f;
    for (int i=0;i<3;++i) {
        drops_reset();
        CHECK(set_block(8,5,6,COBWEB));
        selected_slot=0;
        inventory[0]=(ItemStack){(uint16_t)(i==0 ? AIR : i==1 ? IRON_SWORD : SHEARS),(uint8_t)(i!=0),0,0,0};
        Hit hit=target();
        CHECK(hit.hit && hit.x==8 && hit.y==5 && hit.z==6);
        CHECK(mine()!=NULL && block(8,5,6)==AIR && drops_of(STRING)==(i!=0));
        CHECK(inventory[0].damage==(i!=0));
    }
    CHECK(set_block(8,5,5,STONE));
    inventory[0]=(ItemStack){COBWEB_ITEM,2,0,0,0};
    CHECK(place()!=NULL && block(8,5,6)==COBWEB && inventory[0].count==1);
    drops_reset();
}
static void check_cave_spider(TerrainKey key) {
    MineshaftPlan p={0};
    if (!find_plan(key,0,&p) || !world_begin_generated(key.seed,key.generation)) { CHECK(0); return; }
    actors_reset();
    player_reset_rules(SURVIVAL);
    CHECK(game_set_difficulty(NORMAL));
    RailCell cage=at(p,p.branch[1],p.south-3,1);
    int cx=chunk(cage.x),cz=chunk(cage.z);
    for (int x=cx-2;x<=cx+2;++x) for (int z=cz-2;z<=cz+2;++z)
        (void)world_cell(x*CHUNK_SIZE,cage.y,z*CHUNK_SIZE);
    CHECK(get(cage)==MINE_CAVE_SPIDER_CELL);
    px=cage.x+.5f; py=cage.y+.01f; pz=cage.z-3.5f;
    Vec3 player={px,py,pz};
    for (int n=0;n<3000;++n) dungeon_step(1,player);
    const ActorRecord *actors=actors_in(OVERWORLD);
    int spiders=0;
    for (int i=0;i<ACTOR_CAPACITY;++i) if (actors[i].type==ACTOR_CAVE_SPIDER) ++spiders;
    CHECK(spiders>=1 && spiders<=DUNGEON_MOB_LIMIT);

    CHECK(world_begin_profile(key.seed,key.generation,WORLD_SUPERFLAT));
    for (int d=EASY;d<=HARD;++d) {
        actors_reset();
        player_reset_rules(SURVIVAL);
        CHECK(game_set_difficulty((GameDifficulty)d));
        px=5.5f; py=4.01f; pz=6.f;
        CHECK(set_block(4,4,7,STONE) && set_block(6,4,7,STONE) && set_block(5,4,7,COBWEB));
        Vec3 slot={5.5f,4.01f,7.5f};
        CHECK(!actors_spawn_dungeon(ACTOR_SPIDER,slot,0) &&
            actors_spawn_dungeon(ACTOR_CAVE_SPIDER,slot,0));
        actors_step(.025f);
        CHECK(player_rules.health<MAX_HEALTH);
        CHECK(player_effects.poison==(d==HARD ? CAVE_POISON_HARD : d==NORMAL ? CAVE_POISON_NORMAL : 0));
        CHECK(player_effects.poison_slow==(d!=EASY));
        actors_reset();
        int health=player_rules.health;
        if (d==NORMAL) {
            for (int i=0;i<10;++i) (void)game_step(.05f,0,0,0,0);
            CHECK(player_rules.health==health && player_effects.poison==CAVE_POISON_NORMAL-10);
            for (int i=0;i<16;++i) (void)game_step(.05f,0,0,0,0);
            CHECK(player_rules.health==health-1 && player_effects.poison==CAVE_POISON_NORMAL-26);
        }
    }
    actors_reset();
    player_reset_rules(SURVIVAL);
    CHECK(game_set_difficulty(NORMAL));
    printf("MINESHAFT TEST natural cave spiders=%d, one-cell body, bite and poison cadence\n",spiders);
}
static void check_cart(TerrainKey key) {
    MineshaftPlan p={0};
    if (!find_plan(key,0,&p) || !world_begin_generated(key.seed,key.generation)) { CHECK(0); return; }
    carts_reset();
    player_reset_rules(SURVIVAL);
    RailCell q=at(p,p.right-4,0,1);
    int raw=get(q);
    CHECK(mine_cart_track(raw));
    px=q.x-1.8f; py=q.y+.01f; pz=q.z+.5f;
    yaw=1.5707963f; pitch=-.49f; selected_slot=0;
    inventory[0]=(ItemStack){0};
    Vec3 player={px,py,pz};
    carts_activate_mineshaft((Vec3){px,py+20.f,pz});
    CHECK(!cart_on_track(q) && get(q)==raw);
    for (int i=0;i<CART_CAPACITY;++i)
        CHECK(cart_restore(OVERWORLD,(CartRecord){{(float)(40+i),45.f,.5f},{0,0,0},0,0,0,1,CART_RIDEABLE,0,1,{0,0,0},0}));
    carts_activate_mineshaft(player);
    CHECK(!cart_on_track(q) && get(q)==raw && !world_has_edit(q.x,q.y,q.z));
    carts_reset();
    carts_activate_mineshaft(player);
    int slot=-1;
    (void)cart_use_with_storage(&slot);
    CHECK(slot==0 && cart_on_track(q) && get(q)==TRACK_BASE+STRAIGHT_EW && world_has_edit(q.x,q.y,q.z));
    CartStorageView view={OVERWORLD,0,0};
    CHECK(cart_open_storage(0,&view));
    int stacks=0,items=0;
    for (int i=0;i<CART_STORAGE_SLOTS;++i) {
        ItemStack loot=cart_storage_item(&view,i);
        CHECK(item_stack_valid(loot));
        if (loot.count) {
            ++stacks; items+=loot.count;
            CHECK(cart_storage_move(&view,i,CART_STORAGE_SLOTS+i,0));
        }
    }
    CHECK(stacks>=3 && stacks<=5);
    drops_reset();
    CHECK(cart_take_down()!=NULL && !cart_storage_valid(&view));
    carts_activate_mineshaft(player);
    CHECK(!cart_on_track(q) && drops_of(MINECART)==1 && drops_of(CHEST)==1);
    CHECK(world_select_dimension(NETHER) && world_select_dimension(OVERWORLD));
    CHECK(get(q)==TRACK_BASE+STRAIGHT_EW);
    carts_activate_mineshaft(player);
    CHECK(!cart_on_track(q));
    printf("MINESHAFT TEST cart=%d,%d,%d stacks=%d items=%d no refill after eviction\n",q.x,q.y,q.z,stacks,items);
    carts_reset();
    drops_reset();
}
int mineshaft_selftest(void) {
    failures=0;
    TerrainKey key=TERRAIN_KEY(1712,GENERATOR_MINESHAFTS,OVERWORLD,WORLD_STANDARD,0);
    CHECK(world_begin_generated(key.seed,key.generation));
    check_seams(key,0);
    check_seams(key,1);
    check_worker(key);
    check_cave_spider(key);
    check_cart(key);
    key.profile=WORLD_SUPERFLAT;
    MineshaftPlan p={0};
    CHECK(!mineshaft_plan(key,0,0,&p));
    CHECK(!flat_parse("bedrock,59*stone,turf | meadow | tunnels",&key.flat));
    CHECK(find_plan(key,0,&p) && world_begin_layout(key.seed,key.generation,key.profile,0,&key.flat));
    CHECK(get(at(p,3,3,1))==TORCH && get(at(p,3,3,4))==STONE);
    CHECK(!flat_parse("bedrock,2*loam,turf | meadow | tunnels",&key.flat));
    CHECK(!mineshaft_plan(key,p.x,p.z,&p));
    check_web();
    world_reset();
    printf("MINESHAFT TEST failures=%d\n",failures);
    return failures;
}
