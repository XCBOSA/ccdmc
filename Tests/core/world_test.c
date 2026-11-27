#include "world_test.h"
#include "../world/internal.h"
#include "../world/space.h"
#include "../world/redstone.h"
#include "../world/light.h"
#include "../world/pressure_layout.h"
#include "../game/game.h"
#include "../game/furnace.h"
#include "../game/brewing.h"
#include "../game/armor.h"
#include "../game/drops.h"
#include "../game/experience.h"
#include "../game/enchanting.h"
#include "../game/anvil.h"
#include "../game/gravity.h"
#include "../game/portals.h"
#include "../game/actors.h"
#include "../game/blast.h"
#include "../game/combat.h"
#include "../game/interaction.h"
#include "../game/navigation.h"
#include "../game/pistons.h"
#include "../game/boats.h"
#include "../game/minecarts.h"
#include "../game/horses.h"
#include "../game/fishing.h"
#include "../game/weather.h"
#include "../game/chests.h"
#include "../game/ender_chest.h"
#include "../game/container_automation.h"
#include "../game/container_signal.h"
#include "../game/item_custom.h"
#include "../game/hoppers.h"
#include "../game/beds.h"
#include "../game/farming.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(test) do { if (!(test)) { printf("CRAFT TEST failed line %d: %s\n", __LINE__, #test); ++failures; } } while (0)

static unsigned orb_points(Dimension d) {
    size_t n=0;
    const XPOrb *orbs=experience_orbs(d,&n);
    unsigned amount=0;
    for (size_t i=0;i<n;++i) amount+=orbs[i].value;
    return amount;
}
static int crafting_selftest(void) {
    int failures = 0;
    int grid[9] = {-1, -1, -1, 2};
    inventory_reset();
    inventory[2] = (ItemStack){WOOD, 3, 0,0,0};
    REQUIRE(craft_preview(grid, 2).type == PLANKS);
    REQUIRE(craft_take(grid, 2) && inventory[2].count == 2 &&
        inventory[3].type == PLANKS && inventory[3].count == 4);
    for (int i = 0; i < 4; i++) grid[i] = 3;
    REQUIRE(craft_take(grid, 2) && inventory[3].type == CRAFTING_TABLE && inventory[3].count == 1);
    grid[0] = grid[1] = grid[2] = -1; grid[3] = 2;
    REQUIRE(craft_take(grid, 2) && craft_take(grid, 2) && !inventory[2].count &&
        inventory[4].type == PLANKS && inventory[4].count == 8);
    grid[0] = grid[2] = 4; grid[1] = grid[3] = -1;
    REQUIRE(craft_take(grid, 2) && inventory[2].type == STICK && inventory[4].count == 6);
    int pick[] = {4, 4, 4, -1, 2, -1, -1, 2, -1};
    REQUIRE(!craft_preview(pick, 2).count && !craft_take(pick, 2));
    REQUIRE(craft_preview(pick, 3).type == WOOD_PICKAXE && craft_take(pick, 3));
    REQUIRE(inventory[5].type == WOOD_PICKAXE && inventory[5].count == 1 && !inventory[5].damage);
    REQUIRE(inventory_add(WOOD_PICKAXE) && inventory[6].type == WOOD_PICKAXE && inventory[5].count == 1);
    inventory[5].damage = 5;
    inventory_move(5, 6);
    REQUIRE(inventory[6].damage == 5 && inventory[5].damage == 0);
    inventory[4].count = 2;
    REQUIRE(!craft_take(pick, 3) && inventory[4].count == 2);
    for (int i = 0; i < SLOT_COUNT; i++) inventory[i] = (ItemStack){DIRT, STACK_MAX, 0,0,0};
    inventory[0] = (ItemStack){WOOD, 2, 0,0,0};
    grid[0] = 0; grid[1] = grid[2] = grid[3] = -1;
    REQUIRE(craft_preview(grid, 2).type == PLANKS && !craft_take(grid, 2) && inventory[0].count == 2);
    grid[0] = SLOT_COUNT;
    REQUIRE(!craft_take(grid, 2));
    REQUIRE(mining_drop(STONE, 0) == AIR && mining_drop(STONE, 1) == COBBLESTONE &&
        mining_drop(IRON_ORE, 1) == AIR && mining_drop(IRON_ORE, 2) == IRON_ORE &&
        mining_drop(COAL_ORE, 1) == COAL && mining_drop(DIAMOND_ORE, 3) == DIAMOND);
    REQUIRE(!set_block(0, 4, 0, WOOD_PICKAXE));
    player_reset_rules(SURVIVAL);
    inventory[0] = (ItemStack){APPLE, 2, 0,0,0}; selected_slot = 0;
    REQUIRE(!player_eat() && inventory[0].count == 2);
    player_rules.food = 13;
    REQUIRE(player_eat() && player_rules.food == 17 && inventory[0].count == 1);
    printf("CRAFT TEST shapes/transactions/tiers/durability/food failures=%d\n", failures);
    return failures;
}

static int furnace_selftest(void) {
    int failures = 0;
    world_begin(19);
    generate();
    const int x = -65, y = WY - 10, z = 81;
    REQUIRE(set_block(x, y, z, FURNACE));
    REQUIRE(set_block(x + 1, y, z, FURNACE));
    inventory[2] = (ItemStack){IRON_ORE, 3, 0,0,0};
    inventory[3] = (ItemStack){PLANKS, 2, 0,0,0};
    inventory[4] = (ItemStack){WOOD, 1, 0,0,0};
    inventory[5] = (ItemStack){COAL, 1, 0,0,0};
    REQUIRE(!furnace_deposit(x, y, z, FURNACE_INPUT, 3));
    REQUIRE(!furnace_deposit(x, y, z, FURNACE_FUEL, 2));
    REQUIRE(!furnace_deposit(x + 2, y, z, FURNACE_INPUT, 2));
    REQUIRE(furnace_deposit(x, y, z, FURNACE_INPUT, 2));
    REQUIRE(furnace_deposit(x, y, z, FURNACE_FUEL, 3));
    REQUIRE(furnace_deposit(x + 1, y, z, FURNACE_INPUT, 4));
    REQUIRE(furnace_deposit(x + 1, y, z, FURNACE_FUEL, 5));
    for (int i = 0; i < 9; i++) furnaces_advance(1);
    const Furnace *f = furnace_at(x, y, z);
    REQUIRE(f && f->cook == 180 && f->burn == 120 && !f->slots[FURNACE_OUTPUT].count);
    furnaces_advance(1);
    REQUIRE(f && f->slots[FURNACE_OUTPUT].type == IRON_INGOT && f->slots[FURNACE_OUTPUT].count == 1);
    const Furnace *g = furnace_at(x + 1, y, z);
    REQUIRE(g && g->slots[FURNACE_OUTPUT].type == CHARCOAL && g->slots[FURNACE_OUTPUT].count == 1);
    for (int i = 0; i < SLOT_COUNT; i++) inventory[i] = (ItemStack){DIRT, STACK_MAX, 0,0,0};
    REQUIRE(!furnace_withdraw(x, y, z, FURNACE_OUTPUT) && f && f->slots[FURNACE_OUTPUT].count == 1);
    inventory[2] = (ItemStack){0, 0, 0,0,0};
    REQUIRE(furnace_withdraw(x, y, z, FURNACE_OUTPUT) && inventory[2].type == IRON_INGOT);
    for (int i = 0; i < 10; i++) furnaces_advance(1);
    REQUIRE(f && f->slots[FURNACE_OUTPUT].count == 1 && f->slots[FURNACE_INPUT].count == 1 &&
        f->slots[FURNACE_FUEL].count == 0 && f->burn == 200);
    REQUIRE(set_block(x, y + 1, z, AIR) && set_block(x, y + 2, z, AIR));
    px = x + .5f; py = y + 1.01f; pz = z + .5f; pitch = -1.45f; yaw = 0;
    inventory[0] = (ItemStack){WOOD_PICKAXE, 1, 0,0,0}; selected_slot = 0;
    for (int i = 2; i < MAXDROP; i++) drops[i].item = (ItemStack){DIRT, 1, 0,0,0};
    (void)mine();
    REQUIRE(block(x, y, z) == FURNACE && inventory[0].damage == 0);
    drops[2].item = (ItemStack){0, 0, 0,0,0};
    (void)mine();
    REQUIRE(block(x, y, z) == AIR && !furnace_at(x, y, z) && furnace_at(x + 1, y, z));
    REQUIRE(drops[0].item.type == FURNACE && drops[1].item.type == IRON_ORE &&
        drops[2].item.type == IRON_INGOT && inventory[0].damage == 1);
    furnaces_reset();
    printf("FURNACE TEST rules/burn/output/backpressure/isolation/break failures=%d\n", failures);
    return failures;
}

static int game_selftest(void) {
    int failures = 0;
    world_begin(7);
    generate();
    const int x = 125, z = 125, floor = WY - 19;
    for (int y = floor + 1; y < WY; ++y)
        for (int dx = -2; dx <= 2; ++dx)
            for (int dz = -2; dz <= 2; ++dz) set_block(x + dx, y, z + dz, AIR);
    set_block(x, floor, z, STONE);
    px = x + .5f; pz = z + .5f; py = floor + 12.f;
    player_reset_rules(SURVIVAL);
    for (int i = 0; i < 100; ++i) game_step(.05f, 0, 0, 0, 0);
    if (!grounded || player_rules.health >= MAX_HEALTH || !player_rules.health) ++failures;
    player_rules.exhaustion = 4.f;
    game_step(.05f, 0, 0, 0, 0);
    if (player_rules.food != MAX_FOOD - 1 || inventory_creative_pick(STONE)) ++failures;
    player_feed(3);
    if (player_rules.food != MAX_FOOD) ++failures;
    unsigned time = player_rules.day_tick;
    player_take_damage(DAMAGE_VOID,MAX_HEALTH);
    if (player_rules.health != 0) ++failures;
    player_respawn();
    if (player_rules.health != MAX_HEALTH || player_rules.day_tick != time ||
        block(x, floor, z) != STONE) ++failures;

    for (int dy = 1; dy <= 3; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                set_block(x + dx, floor + dy, z + dz, dx || dz ? STONE : WATER);
    px = x + .5f; pz = z + .5f; py = floor + 1.02f;
    player_reset_rules(SURVIVAL);
    for (int i = 0; i < 340; ++i) game_step(.05f, 0, 0, 0, 0);
    if (player_rules.air != 0 || player_rules.health >= MAX_HEALTH) ++failures;
    py = floor + 4.f; vy = 0;
    game_step(.05f, 0, 0, 0, 0);
    if (player_rules.air != MAX_AIR) ++failures;

    player_reset_rules(CREATIVE);
    player_take_damage(DAMAGE_MELEE,1);
    player_toggle_flight();
    float before = py;
    for (int i = 0; i < 10; ++i) game_step(.05f, 0, 0, 1, 0);
    if (!player_rules.flying || py < before + 2.9f || player_rules.health != MAX_HEALTH) ++failures;
    for (int i = 0; i < 10; ++i) game_step(.05f, 0, 0, 0, 1);
    if (fabsf(py - before) > .05f) ++failures;
    set_block(x, floor + 3, z, AIR);
    set_block(x, floor + 2, z, STONE);
    px = x + .5f; pz = z + .5f; py = floor + 4.0f;
    pitch = -1.45f; yaw = 0;
    selected_slot = 0;
    if (!inventory_creative_pick(GRAVEL)) ++failures;
    (void)place();
    if (block(x, floor + 3, z) != GRAVEL || inventory[0].count != STACK_MAX)
        ++failures;
    (void)mine();
    if (block(x, floor + 3, z) != AIR || drops[0].item.count) ++failures;
    player_rules.day_tick = 6000;
    float day = world_daylight();
    player_rules.day_tick = 18000;
    if (world_daylight() >= day) ++failures;
    world_set_dawn(7);
    player_rules.day_tick=DAY_TICKS-1;
    game_step(.05f,0,0,0,0);
    if (player_rules.world_day!=8 || world_moon_phase()!=0 || player_rules.day_tick!=0) ++failures;
    printf("RULES TEST creative/flight/fall/drowning/respawn/day failures=%d\n", failures);
    return failures;
}

static void settle_water(void) {
    for (int i = 0; i < 80; i++) water_step();
}

static int water_selftest(void) {
    int failures = 0;
    const int x = -1, z = 15, y = WY - 4;
    world_reset();
    /* Falling water must not spread at the top of an unsupported column. */
    for (int dz = -9; dz <= 9; dz++) for (int dx = -9; dx <= 9; dx++)
        set_block(x + dx, y - 4, z + dz, STONE);
    settle_water();
    if (!set_block(x, y, z, WATER)) return 1;
    if (block(x, y - 1, z) != AIR) failures++;
    water_step();
    if (block(x, y - 1, z) != WATER || block(x, y - 2, z) != AIR) failures++;
    settle_water();
    if (world_cell(x, y, z) != WATER || block(x, y - 1, z) != WATER ||
        block(x + 1, y, z) != AIR || block(x, y, z + 1) != AIR) failures++;
    const int floor = y - 3;
    if (water_strength(world_cell(x + 1, floor, z)) != WATER_LEVELS - 1 ||
        water_strength(world_cell(x, floor, z + 2)) != WATER_LEVELS - 2 ||
        block(x + WATER_LEVELS, floor, z) != AIR) failures++;

    /* Flow levels and mesh revision keys survive chunk eviction. */
    unsigned long long revision = chunk_revision(0, 0);
    for (int i = 0; i < CHUNK_CACHE + 2; i++) block(6000 + i * CHUNK_SIZE, 0, -5000);
    if (water_strength(world_cell(x + 1, floor, z)) != WATER_LEVELS - 1 ||
        !revision || chunk_revision(0, 0) != revision) failures++;

    set_block(x, y, z, AIR);
    settle_water();
    if (block(x, y - 1, z) != AIR || block(x + 1, floor, z) != AIR) failures++;
    /* Adding and removing support must update the horizontal outlets too. */
    set_block(x, y - 1, z, STONE);
    set_block(x, y, z, WATER);
    settle_water();
    if (block(x + 1, y, z) != WATER) failures++;
    set_block(x, y - 1, z, AIR);
    settle_water();
    if (block(x + 1, y, z) != AIR || block(x, y - 1, z) != WATER) failures++;
    set_block(x, floor - 1, z, AIR);
    settle_water();
    if (block(x, floor - 1, z) != WATER) failures++;
    printf("WATER TEST gravity/spread/range/drain/chunk-seams/eviction failures=%d\n", failures);
    return failures;
}

static uint32_t chunk_fingerprint(ChunkView view, int height) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < CHUNK_SIZE * CHUNK_SIZE; i++)
        for (int y = 0; y < height; y++) h = (h ^ view.cells[i * WY + y]) * 16777619u;
    return h;
}

static int climate_selftest(void) {
    const uint32_t seeds[] = {0, 2575243985u, 992382978u};
    const int coords[][2] = {{0,0},{-1,-1},{-4,2},{5,-3},{131,-71}};
    /* Measured from the v4 code before raising the chunk height. */
    const uint32_t expected[][5] = {
        {0xebdbad00u,0x4d88a5acu,0x24a8f976u,0x03fd31cfu,0xd8444c97u},
        {0xead1611bu,0x4f567838u,0xb8d341a9u,0xfd7b0ebdu,0x95978566u},
        {0x4cc4f56du,0x95784f2cu,0xa2d6c3d6u,0xea0ebbbdu,0xed3a66bcu}
    };
    int failure = 0, seen[BIOME_COUNT] = {0}, families = 0;
    for (int s = 0; s < 3; s++) {
        world_begin(seeds[s]);
        for (int i = 0; i < 5; i++)
            if (chunk_fingerprint(chunk_view(coords[i][0], coords[i][1]), LEGACY_WY) != expected[s][i])
                failure++;
    }
    if (!world_begin_generated(98213, GENERATOR_CLIMATE) || world_sea_level() != 63) failure++;
    int lowest = WY, highest = 0;
    for (int x = -3600; x <= 3600; x += 45) for (int z = -3600; z <= 3600; z += 51) {
        Biome b = terrain_biome(x, z);
        int h = terrain_height(x, z);
        if (h < lowest) lowest = h;
        if (h > highest) highest = h;
        if (b <= BIOME_LEGACY || b >= BIOME_COUNT) failure++;
        else seen[b]++;
    }
    for (int b = 1; b < BIOME_NETHER; b++) {
        families += seen[b] != 0;
        if (!seen[b]) printf("CLIMATE missing biome %d (%s)\n", b, biome_name((Biome)b));
    }
    if (families != BIOME_NETHER - 1 || lowest >= 47 || highest < 96) failure++;
    const int sx = -17, sz = -1, cx = -2, cz = -1, index = 15 * CHUNK_SIZE + 15;
    ChunkView west = chunk_view(cx, cz), east = chunk_view(cx + 1, cz);
    int real_top = west.tops[index];
    if (west.biomes[index] != terrain_biome(sx, sz) || east.biomes[15] != terrain_biome(sx + 1, sz) ||
        west.cells[index * WY + real_top] == AIR || real_top >= WY - 1) failure++;
    uint32_t first = chunk_fingerprint(west, WY);
    if (!set_block(sx, WY - 1, sz, PACKED_ICE) || chunk_view(cx, cz).tops[index] != WY - 1) failure++;
    for (int i = 0; i < CHUNK_CACHE + 8; i++) (void)chunk_view(2000 + i, -1700);
    if (chunk_view(cx, cz).tops[index] != WY - 1 || !set_block(sx, WY - 1, sz, AIR) ||
        chunk_view(cx, cz).tops[index] != real_top || chunk_fingerprint(chunk_view(cx, cz), WY) != first) failure++;
    world_begin_generated(98213, GENERATOR_CLIMATE);
    if (chunk_fingerprint(chunk_view(cx, cz), WY) != first) failure++;
    world_begin_generated(3721138039u, GENERATOR_CLIMATE);
    generate();
    int spawn_x = (int)floorf(px), spawn_z = (int)floorf(pz);
    int feet = (int)floorf(py);
    if (terrain_biome(0, 0) != BIOME_DEEP_OCEAN || (spawn_x == 0 && spawn_z == 0) ||
        feet <= world_sea_level() || !solid(block(spawn_x, feet - 1, spawn_z)) ||
        collides(px, py, pz)) failure++;
    px += 20; pz -= 20;
    player_respawn();
    if ((int)floorf(px) != spawn_x || (int)floorf(pz) != spawn_z) failure++;
    printf("CLIMATE TEST legacy/biomes=%d/%d/y=%d..%d/eviction failures=%d\n",
        families, BIOME_NETHER - 1, lowest, highest, failure);
    return failure;
}

static int portal_selftest(void) {
    int failures = 0;
    const int x = -40, y = 100, z = -16;
    world_begin_generated(98213,GENERATOR_ADVENTURE);
    generate();
    player_reset_rules(SURVIVAL);
    for (int a = -1; a <= 2; ++a) for (int c = -1; c <= 1; ++c) {
        REQUIRE(set_block(x+a,y-1,z+c,OBSIDIAN));
        for (int b = 0; b <= 3; ++b) REQUIRE(set_block(x+a,y+b,z+c,AIR));
    }
    for (int a = 0; a <= 1; ++a) REQUIRE(set_block(x+a,y+3,z,OBSIDIAN));
    for (int b = 0; b < 3; ++b) {
        REQUIRE(set_block(x-1,y+b,z,OBSIDIAN));
        if (b != 1) REQUIRE(set_block(x+2,y+b,z,OBSIDIAN));
    }
    inventory[0] = (ItemStack){FLINT_AND_STEEL,1,0,0,0};
    Hit click = {1,x,y-1,z,x,y,z};
    REQUIRE(portal_item_use(click) && block(x,y,z)==FIRE_CELL && inventory[0].damage==1);
    REQUIRE(set_block(x+2,y+1,z,OBSIDIAN));
    REQUIRE(set_block(x,y,z,AIR));
    REQUIRE(portal_item_use(click) && block(x,y,z)==FIRE_CELL && inventory[0].damage==2);
    px = x+.5f; py = y+.01f; pz = z+.5f;
    for (int i = 0; i < 24; ++i) REQUIRE(!portals_step(.25f) && world_dimension()==OVERWORLD);
    REQUIRE(!set_block(x,y,z,NETHER_PORTAL_X) && !set_block(x,y,z,NETHER_PORTAL_Z));

    /* Old IDs are decoded only; a runtime world cannot create or activate them. */
    REQUIRE(!set_block(x,144,z,ARCHIVE_FRAME) && !set_block(x,144,z,ARCHIVE_GATE) &&
        !item_def(ARCHIVE_BEARING_ITEM));
    inventory[0]=(ItemStack){QUARTZ,1,0,0,0};
    REQUIRE(!portal_item_use((Hit){1,x,144,z,0,0,0}) && inventory[0].count==1);
    px=x+.5f; py=144.01f; pz=z+.5f;
    for (int i=0;i<24;++i) REQUIRE(!portals_step(.25f) && world_dimension()==OVERWORLD);
    REQUIRE(set_block(x,144,z,LAVA) && set_block(x+1,144,z,WATER));
    water_step();
    REQUIRE(block(x,144,z)==OBSIDIAN);
    printf("TRANSIT TEST ordinary fire/retired gates/obsidian failures=%d\n",failures);
    return failures;
}

static int actors_selftest(void) {
    int failures = 0;
    world_begin(31);
    generate();
    player_rules.day_tick = 14000;
    px = .5f; py = 225.01f; pz = .5f; yaw = pitch = 0;
    for (int i = -2; i <= 2; ++i)
        for (int j = -4; j <= 2; ++j) REQUIRE(set_block(i,224,j,STONE));
    REQUIRE(!actors_restore(OVERWORLD,(ActorRecord){{0,225,0},0,ACTOR_PIG,11,0}));
    REQUIRE(!actors_restore(NETHER,(ActorRecord){{0,40,0},0,ACTOR_ZOMBIE,20,0}));
    REQUIRE(actors_restore(OVERWORLD,(ActorRecord){{.5f,225.01f,-1.5f},0,ACTOR_ZOMBIE,2,0}));
    REQUIRE(actors_overlap_cell(0,225,-2));
    selected_slot = 0;
    inventory[0] = (ItemStack){WOOD_PICKAXE,1,0,0,0};
    REQUIRE(set_block(0,226,-1,STONE));
    REQUIRE(!actors_attack(target()) && actors_in(OVERWORLD)[0].health == 2);
    REQUIRE(set_block(0,226,-1,AIR));
    REQUIRE(actors_attack(target()) && !actors_in(OVERWORLD)[0].type &&
        drops[0].item.type == ROTTEN_FLESH && inventory[0].damage == 2);
    REQUIRE(orb_points(OVERWORLD)==5 && !experience_points());
    REQUIRE(actors_restore(OVERWORLD,(ActorRecord){{.5f,225.01f,-.8f},0,ACTOR_ZOMBIE,20,0}));
    REQUIRE(world_select_dimension(END) && !actors_overlap_cell(0,225,-1));
    for (int i = 0; i < 10; ++i) actors_step(.05f);
    REQUIRE(player_rules.health == MAX_HEALTH && world_select_dimension(OVERWORLD));
    actors_step(.05f);
    REQUIRE(player_rules.health == MAX_HEALTH - 3);
    actors_step(.05f);
    REQUIRE(player_rules.health == MAX_HEALTH - 3);
    actors_reset();
    REQUIRE(actors_restore(OVERWORLD,(ActorRecord){{.5f,225.01f,-1.5f},0,ACTOR_PIG,2,0}));
    pitch = -.6f;
    REQUIRE(actors_attack(target()) && drops[1].item.type == RAW_PORK);
    REQUIRE(set_block(0,225,1,FURNACE));
    inventory[2] = (ItemStack){RAW_PORK,1,0,0,0};
    inventory[3] = (ItemStack){COAL,1,0,0,0};
    REQUIRE(furnace_deposit(0,225,1,FURNACE_INPUT,2) && furnace_deposit(0,225,1,FURNACE_FUEL,3));
    for (int i = 0; i < 10; ++i) furnaces_advance(1);
    const Furnace *f = furnace_at(0,225,1);
    REQUIRE(f && f->slots[FURNACE_OUTPUT].type == COOKED_PORK);
    printf("ACTOR TEST hit/occlusion/retaliation/isolation/drop/food failures=%d\n",failures);
    return failures;
}

static int nether_survival_selftest(void) {
    int failures = 0;
    int rooms = 0, x_room = 0, z_room = 0, y_room = 0;
    world_begin_generated(58100,GENERATOR_FORTRESS);
    generate();
    REQUIRE(world_select_dimension(NETHER));
    for (int x = -82; x <= -44; ++x) for (int z = -82; z <= -44; ++z) {
        int y = nether_fortress_floor(x,z);
        if (y < 0) continue;
        REQUIRE(block(x,y,z)==NETHER_BRICKS && block(x,y+1,z)==AIR &&
            block(x,y+4,z)==AIR && block(x,y+5,z)==NETHER_BRICKS);
        if (abs(x+64) <= 4 && abs(z+64) <= 4) { x_room=x; z_room=z; y_room=y; }
        ++rooms;
    }
    REQUIRE(rooms > 90 && y_room > 55);
    px=x_room+.5f; py=y_room+1.01f; pz=z_room+.5f;
    player_rules.mode=CREATIVE;
    int cx=(int)floorf(px/CHUNK_SIZE),cz=(int)floorf(pz/CHUNK_SIZE);
    for (int x=cx-3;x<=cx+3;++x) for (int z=cz-3;z<=cz+3;++z)
        REQUIRE(chunk_view(x,z).cells);
    int reedlizard=0;
    for (int i=0;i<600 && !reedlizard;++i) {
        actors_step(.1f);
        for (int j=0;j<ACTOR_CAPACITY;++j) {
            ActorRecord a=actors_in(NETHER)[j];
            if (a.type==ACTOR_REEDLIZARD) reedlizard=1;
            REQUIRE(a.type!=ACTOR_FAULTBACK);
        }
    }
    REQUIRE(reedlizard);
    world_begin_generated(58100,GENERATOR_ADVENTURE);
    generate();
    REQUIRE(world_select_dimension(NETHER) && nether_fortress_floor(x_room,z_room) == -1 &&
        block(x_room,y_room,z_room) != NETHER_BRICKS);

    /* A single directional release, not a homing projectile. Test the
       locked bearing, the wall added after warning, and heat protection. */
    px=.5f; py=145.01f; pz=.5f; yaw=pitch=0;
    for (int z=-8;z<=1;++z)
        for (int x=-3;x<=3;++x) REQUIRE(set_block(x,144,z,NETHER_BRICKS));
    REQUIRE(game_set_difficulty(NORMAL));
    player_reset_rules(SURVIVAL);
    REQUIRE(actors_restore(NETHER,(ActorRecord){{.5f,145.01f,-4.f},0,ACTOR_REEDLIZARD,REED_HEALTH,0}));
    actors_step(.05f);
    REQUIRE(actors_reed_phase(NETHER,0)==REED_WARN && actors_fireballs()[0].seconds<=0 &&
        player_rules.health==MAX_HEALTH);
    for (int y=145; y<149; ++y) REQUIRE(set_block(0,y,-2,NETHER_BRICKS));
    for (int i=0;i<32;++i) actors_step(.05f);
    REQUIRE(actors_reed_phase(NETHER,0)==REED_VENT && player_rules.health==MAX_HEALTH);
    for (int y=145; y<149; ++y) REQUIRE(set_block(0,y,-2,AIR));
    for (int i=0;i<25;++i) actors_step(.05f);
    REQUIRE(player_rules.health==MAX_HEALTH); /* The release has already happened. */
    actors_reset(); player_reset_rules(SURVIVAL);
    REQUIRE(actors_restore(NETHER,(ActorRecord){{.5f,145.01f,-4.f},0,ACTOR_REEDLIZARD,REED_HEALTH,0}));
    actors_step(.05f); px=2.9f;
    for (int i=0;i<35;++i) actors_step(.05f);
    REQUIRE(player_rules.health==MAX_HEALTH && actors_reed_phase(NETHER,0)==REED_VENT);
    actors_reset(); px=.5f;
    REQUIRE(actors_restore(NETHER,(ActorRecord){{.5f,145.01f,-4.f},0,ACTOR_REEDLIZARD,REED_HEALTH,0}));
    for (int i=0;i<39;++i) actors_step(.05f);
    REQUIRE(player_rules.health==MAX_HEALTH-5);
    actors_reset(); player_reset_rules(SURVIVAL);
    player_effects.fire=FIRE_POTION_TICKS;
    REQUIRE(actors_restore(NETHER,(ActorRecord){{.5f,145.01f,-4.f},0,ACTOR_REEDLIZARD,REED_HEALTH,0}));
    for (int i=0;i<39;++i) actors_step(.05f);
    REQUIRE(player_rules.health==MAX_HEALTH);
    REQUIRE(world_select_dimension(OVERWORLD) && actors_fireballs()[0].seconds<=0);
    REQUIRE(world_select_dimension(NETHER));
    actors_reset();
    memset(inventory,0,sizeof inventory);
    inventory[0]=(ItemStack){IRON_INGOT,2,0,0,0};
    inventory[1]=(ItemStack){STICK,1,0,0,0};
    const int sword_grid[9]={-1,0,-1,-1,0,-1,-1,1,-1};
    selected_slot=0;
    REQUIRE(craft_take(sword_grid,3) && inventory[0].type == IRON_SWORD && !inventory[0].damage);
    REQUIRE(actors_restore(NETHER,(ActorRecord){{.5f,145.01f,-1.4f},0,ACTOR_REEDLIZARD,20,0}));
    pitch=-.45f;
    REQUIRE(actors_attack(target()) && actors_in(NETHER)[0].health==14 && inventory[0].damage==1);
    actors_reset();
    REQUIRE(actors_restore(NETHER,(ActorRecord){{.5f,145.01f,-2.5f},0,ACTOR_REEDLIZARD,REED_HEALTH,0}));
    pitch=-.352f;
    inventory[0]=(ItemStack){SNOWBALL,1,0,0,0};
    REQUIRE(game_tap().message && !inventory[0].count);
    for (int i=0;i<9;++i) actors_step(.05f);
    REQUIRE(actors_reed_phase(NETHER,0)==REED_COOLED && actors_in(NETHER)[0].health==REED_HEALTH);
    inventory[1]=(ItemStack){SURVEY_TAPPER,1,0,0,0};
    selected_slot=1;
    REQUIRE(game_tap().message && actors_in(NETHER)[0].traits==REED_HARVESTED &&
        inventory[0].type==REED_FIN && inventory[0].count==1);
    REQUIRE(game_tap().message && inventory[0].count==1);
    ItemStack fin=inventory[0];
    REQUIRE(world_select_dimension(OVERWORLD));

    memset(inventory,0,sizeof inventory);
    inventory[0]=fin;
    const int powder_grid[4]={0,-1,-1,-1};
    REQUIRE(craft_take(powder_grid,2) && inventory[0].type==REED_GRIT && inventory[0].count==2);
    BrewDose coolant=brew_dose(BREW_QUICK,REED_GRIT);
    REQUIRE(coolant.output==POTION_FIRE_LONG && coolant.cost==2 && coolant.ticks==180);
    REQUIRE(set_block(3,150,0,BREWING_STAND));
    inventory[2]=(ItemStack){WATER_BOTTLE,1,0,0,0};
    REQUIRE(brew_deposit(3,150,0,BREW_QUICK,2) && brew_deposit(3,150,0,BREW_INGREDIENT,0));
    for (int i=0;i<8;++i) brewing_advance(1.f);
    const BrewingStand *tank=brew_at(3,150,0);
    REQUIRE(tank && tank->slots[BREW_QUICK].type==WATER_BOTTLE && tank->progress==160 && !inventory[0].count);
    brewing_advance(1.f);
    REQUIRE(tank && tank->slots[BREW_QUICK].type==POTION_FIRE_LONG && !tank->slots[BREW_INGREDIENT].count);
    inventory[0]=(ItemStack){REED_GRIT,2,0,0,0}; /* Reject the old side recipe. */
    inventory[1]=(ItemStack){STRIATED_GLASS,1,0,0,0};
    const int retired_grid[4]={0,-1,-1,1};
    REQUIRE(!craft_take(retired_grid,2) && inventory[0].count==2 && inventory[1].type==STRIATED_GLASS);
    REQUIRE(world_begin_generated(451931,GENERATOR_CURRENT) && world_select_dimension(NETHER));
    actors_reset();
    REQUIRE(chunk_view(0,-2).cells && chunk_view(0,-1).cells);
    px=.5f; py=PRESSURE_FLOOR+1.02f; pz=-10.5f;
    player_reset_rules(SURVIVAL);
    for (int i=0;i<80;++i) actors_step(.05f);
    int kiln_reeds=0;
    for (int i=0;i<ACTOR_CAPACITY;++i) {
        ActorRecord a=actors_in(NETHER)[i];
        if (a.type!=ACTOR_REEDLIZARD) continue;
        REQUIRE(fabsf(a.feet.x-5.5f)<.02f && fabsf(a.feet.z+16.5f)<.02f);
        ++kiln_reeds;
    }
    REQUIRE(kiln_reeds==1);
    printf("HOT GALLERY TEST legacy terrain, warning, cover, dodge, cooling, safe harvest, brew and kiln habitat failures=%d\n",failures);
    return failures;
}

static void aim_at(Vec3 p) {
    float x=p.x-px, y=p.y-py-1.55f, z=p.z-pz;
    yaw=atan2f(x,-z);
    pitch=atan2f(y,hypotf(x,z));
}

static int archery_selftest(void) {
    int failures = 0;
    world_begin_generated(58100,GENERATOR_FORTRESS);
    generate();
    px=.5f; py=225.f; pz=.5f; yaw=0; pitch=-.62f;
    inventory[0]=(ItemStack){IRON_SWORD,1,0,0,0};
    REQUIRE(actors_restore(OVERWORLD,(ActorRecord){{.5f,225.f,-1.56f},0,ACTOR_CHICKEN,4,0}));
    REQUIRE(actors_attack(target()) && drops[0].item.type==FEATHER && drops[1].item.type==RAW_CHICKEN);
    REQUIRE(actors_restore(OVERWORLD,(ActorRecord){{.5f,225.f,-1.56f},0,ACTOR_SPIDER,6,0}));
    REQUIRE(actors_attack(target()) && drops[2].item.type==STRING);
    memset(inventory,0,sizeof inventory);
    inventory[0]=(ItemStack){STRING,3,0,0,0}; inventory[1]=(ItemStack){STICK,4,0,0,0};
    inventory[2]=(ItemStack){FLINT,1,0,0,0}; inventory[3]=drops[0].item;
    const int bow_grid[9]={-1,1,0,1,-1,0,-1,1,0};
    REQUIRE(craft_take(bow_grid,3) && inventory[0].type==BOW && inventory[1].count==1);
    const int arrow_grid[9]={2,-1,-1,1,-1,-1,3,-1,-1};
    REQUIRE(craft_take(arrow_grid,3) && inventory[1].type==ARROW && inventory[1].count==4);
    actors_reset();
    px=.5f; py=225.01f; pz=.5f; yaw=0; pitch=-.08f;
    REQUIRE(actors_restore(OVERWORLD,(ActorRecord){{.5f,225.01f,-5.5f},0,ACTOR_COW,10,0}));
    const ActorRecord *herd=actors_in(OVERWORLD);
    REQUIRE(herd && herd[0].type==ACTOR_COW);
    REQUIRE(set_block(0,226,-3,STONE));
    REQUIRE(bow_use() && bow_use() && inventory[1].count==4 && !inventory[0].damage);
    for (int i=0;i<20;++i) combat_step(.05f);
    REQUIRE(bow_use() && inventory[1].count==3 && inventory[0].damage==1);
    for (int i=0;i<12;++i) combat_step(.05f);
    REQUIRE(herd[0].health==10 && combat_arrows()[0].life<=0);
    REQUIRE(set_block(0,226,-3,AIR));
    REQUIRE(bow_use());
    for (int i=0;i<20;++i) combat_step(.05f);
    REQUIRE(bow_use() && inventory[1].count==2 && inventory[0].damage==2);
    REQUIRE(world_select_dimension(NETHER) && combat_arrows()[0].life<=0);
    REQUIRE(world_select_dimension(OVERWORLD) && combat_arrows()[0].life>0);
    for (int i=0;i<12;++i) combat_step(.05f);
    REQUIRE(herd[0].health<10 && combat_arrows()[0].life<=0);
    printf("ARCHERY TEST crafting/draw/ammo/occlusion/travel/hit failures=%d\n",failures);
    return failures;
}

static int boat_selftest(void) {
    int failures=0;
    world_begin(55341);
    generate();
    memset(inventory,0,sizeof inventory);
    inventory[0]=(ItemStack){PLANKS,5,0,0,0};
    const int boat_grid[9]={0,-1,0,0,0,0,-1,-1,-1};
    REQUIRE(craft_take(boat_grid,3) && inventory[0].type==BOAT && inventory[0].count==1);
    for (int x=-3;x<=3;++x) for (int z=-12;z<=3;++z) {
        REQUIRE(set_block(x,144,z,STONE));
        REQUIRE(set_block(x,145,z,z>=2 || z==-9 ? STONE : WATER));
        if (z==-9) REQUIRE(set_block(x,146,z,STONE));
    }
    px=.5f; py=146.01f; pz=2.5f; yaw=0; pitch=-.57f;
    REQUIRE(boat_use() && boats_in(OVERWORLD)[0].occupied && !inventory[0].count);
    Vec3 position=boats_in(OVERWORLD)[0].position;
    REQUIRE(boat_overlaps_cell((int)floorf(position.x),(int)floorf(position.y),(int)floorf(position.z)));
    aim_at((Vec3){position.x,position.y+.22f,position.z});
    REQUIRE(boat_use() && boat_rider()==0);
    yaw=0; pitch=0;
    for (int i=0;i<70;++i) REQUIRE(boats_step(.05f,0,1));
    REQUIRE(pz<position.z-3.f && pz>-8.5f && pz==boats_in(OVERWORLD)[0].position.z);
    float stop=pz;
    for (int i=0;i<20;++i) boats_step(.05f,0,1);
    REQUIRE(fabsf(pz-stop)<.001f);
    int dock_z=(int)floorf(pz);
    REQUIRE(set_block(1,145,dock_z,STONE));
    REQUIRE(boat_dismount() && boat_rider()==-1 && px>1.f && py>146.f);
    REQUIRE(world_select_dimension(END) && !boats_in(END)[0].occupied && boat_rider()==-1);
    REQUIRE(world_select_dimension(OVERWORLD) && boats_in(OVERWORLD)[0].occupied);
    position=boats_in(OVERWORLD)[0].position;
    aim_at((Vec3){position.x,position.y+.22f,position.z});
    REQUIRE(boat_take_down() && !boats_in(OVERWORLD)[0].occupied && drops[0].item.type==BOAT);
    printf("BOAT TEST craft/place/board/steer/dock/isolation/recover failures=%d\n",failures);
    return failures;
}

static int railway_selftest(void) {
    int failures=0;
    REQUIRE(world_begin_generated(19,GENERATOR_RAILWAYS));
    int ore=0;
    for (int x=-16;x<16;++x) for (int z=-16;z<16;++z)
        for (int y=2;y<=16;++y) ore+=block(x,y,z)==REDSTONE_ORE;
    REQUIRE(ore>10 && mining_drop(REDSTONE_ORE,2)==AIR && mining_drop(REDSTONE_ORE,3)==REDSTONE_DUST);
    REQUIRE(world_begin_generated(19,GENERATOR_FORTRESS));
    for (int x=-16;x<16;++x) for (int z=-16;z<16;++z)
        for (int y=2;y<=16;++y) REQUIRE(block(x,y,z)!=REDSTONE_ORE);
    generate();
    memset(inventory,0,sizeof inventory);
    inventory[0]=(ItemStack){IRON_INGOT,5,0,0,0};
    const int cart_grid[9]={0,-1,0,0,0,0,-1,-1,-1};
    REQUIRE(craft_take(cart_grid,3) && inventory[0].type==MINECART);
    inventory[1]=(ItemStack){IRON_INGOT,6,0,0,0}; inventory[2]=(ItemStack){STICK,2,0,0,0};
    const int rail_grid[9]={1,-1,1,1,2,1,1,-1,1};
    REQUIRE(craft_take(rail_grid,3) && inventory[1].type==RAIL && inventory[1].count==16);
    inventory[3]=(ItemStack){GOLD_INGOT,6,0,0,0}; inventory[4]=(ItemStack){REDSTONE_DUST,1,0,0,0};
    const int powered_grid[9]={3,-1,3,3,2,3,3,4,3};
    REQUIRE(craft_take(powered_grid,3) && inventory[2].type==POWERED_RAIL && inventory[2].count==6);

    for (int x=12;x<=18;++x) for (int z=-2;z<=5;++z) {
        REQUIRE(set_block(x,144,z,STONE));
        if (z>=3) REQUIRE(set_block(x,145,z,z==4 && x==17 ? REDSTONE_BLOCK : STONE));
    }
    for (int x=14;x<=17;++x) REQUIRE(rail_place((RailCell){x,145,0},RAIL_NORMAL,1));
    for (int z=1;z<=2;++z) REQUIRE(rail_place((RailCell){17,145,z},RAIL_NORMAL,0));
    for (int z=3;z<=5;++z) REQUIRE(rail_place((RailCell){17,146,z},RAIL_BOOSTER,0));
    REQUIRE(set_block(17,146,6,STONE)); /* this test uses a station bumper */
    RailPiece piece;
    REQUIRE(!solid(block(16,145,0)) && rail_at((RailCell){17,145,0},&piece) && piece.shape==BEND_SW);
    REQUIRE(rail_at((RailCell){17,145,2},&piece) && piece.shape==RISE_S);
    REQUIRE(rail_is_energized((RailCell){17,146,3}) && !rail_is_energized((RailCell){17,145,2}));
    RailCell next;
    int entry=0;
    REQUIRE(rail_link((RailCell){17,145,2},1,&next,&entry) && next.y==146 && next.z==3 && entry==0);
    REQUIRE(rail_link((RailCell){17,146,3},0,&next,&entry) && next.y==145 && next.z==2 && entry==1);
    REQUIRE(set_block(17,145,4,STONE) && !rail_is_energized((RailCell){17,146,3}));
    REQUIRE(set_block(17,145,4,REDSTONE_BLOCK));

    px=13.5f; py=145.01f; pz=.5f;
    aim_at((Vec3){15.5f,145.05f,.5f});
    REQUIRE(cart_use() && carts_in(OVERWORLD)[0].occupied && !inventory[0].count);
    Vec3 cart=carts_in(OVERWORLD)[0].position;
    aim_at((Vec3){cart.x,cart.y+.25f,cart.z});
    REQUIRE(cart_use() && cart_rider()==0);
    int passed_chunk=0, passed_bend=0, passed_rise=0;
    for (int i=0;i<160;++i) {
        const CartRecord *c=&carts_in(OVERWORLD)[0];
        yaw=c->heading;
        REQUIRE(carts_step(.05f,0,1));
        passed_chunk|=c->track.x>=16;
        passed_bend|=c->track.x==17 && c->track.z>=1;
        passed_rise|=c->track.y==146;
    }
    REQUIRE(passed_chunk && passed_bend && passed_rise && pz>4.f && pz<6.f);
    const CartRecord *c=&carts_in(OVERWORLD)[0];
    REQUIRE(c->speed==0 && fabsf(px-c->position.x)<.001f && fabsf(py-c->position.y-.25f)<.001f);
    REQUIRE(cart_dismount() && cart_rider()==-1 && !collides(px,py,pz));
    REQUIRE(set_block(17,145,4,AIR) && !rail_type(block(17,146,4)));
    REQUIRE(world_select_dimension(NETHER) && !carts_in(NETHER)[0].occupied && cart_rider()==-1);
    REQUIRE(world_select_dimension(OVERWORLD));
    cart=carts_in(OVERWORLD)[0].position;
    aim_at((Vec3){cart.x,cart.y+.25f,cart.z});
    REQUIRE(cart_take_down() && !carts_in(OVERWORLD)[0].occupied && drops[0].item.type==MINECART);
    for (int x=-24;x<=-15;++x) {
        REQUIRE(set_block(x,134,-4,x==-24 ? REDSTONE_BLOCK : STONE));
        REQUIRE(rail_place((RailCell){x,135,-4},RAIL_BOOSTER,1));
    }
    REQUIRE(rail_is_energized((RailCell){-16,135,-4}) && !rail_is_energized((RailCell){-15,135,-4}));
    px=-24.5f; py=135.01f; pz=-3.5f;
    const CartRecord initial={{-23.5f,135.045f,-3.5f},{-24,135,-4},.5f,.6f,1.57f,1,CART_RIDEABLE,0,0,{0,0,0},0};
    REQUIRE(cart_restore(OVERWORLD,initial));
    for (int i=0;i<4;++i) carts_step(.05f,0,0);
    float powered_speed=carts_in(OVERWORLD)[0].speed;
    carts_reset();
    REQUIRE(set_block(-24,134,-4,STONE) && cart_restore(OVERWORLD,initial));
    for (int i=0;i<4;++i) carts_step(.05f,0,0);
    float idle_speed=carts_in(OVERWORLD)[0].speed;
    REQUIRE(powered_speed>1.5f && idle_speed>0.f && idle_speed<.4f);
    printf("RAIL POWER TEST enabled=%.2f disabled=%.2f blocks/s\n",powered_speed,idle_speed);
    printf("RAILWAY TEST ore/craft/shape/seams/powered/slope/ride/break/isolation failures=%d\n",failures);
    return failures;
}

static int detector_rail_selftest(void) {
    int failures=0;
    enum { X=15,Y=145 };
    const RailCell sensor={X,Y,0},booster={X,Y,-1};
    const int recipe[]={1,-1,1,1,2,1,1,3,1};
    world_begin(310);
    generate();
    memset(inventory,0,sizeof inventory);
    inventory[1]=(ItemStack){IRON_INGOT,6,0,0,0};
    inventory[2]=(ItemStack){STONE_PLATE_ITEM,1,0,0,0};
    inventory[3]=(ItemStack){REDSTONE_DUST,1,0,0,0};
    REQUIRE(craft_preview(recipe,3).type==DETECTOR_RAIL_ITEM && craft_take(recipe,3));
    REQUIRE(inventory[0].type==DETECTOR_RAIL_ITEM && inventory[0].count==6 && !inventory[1].count);
    for (int x=X-2;x<=X+2;++x) for (int z=-4;z<=3;++z) REQUIRE(set_block(x,Y-1,z,STONE));
    for (int z=-4;z<=3;++z) {
        RailKind kind=z==0 ? RAIL_DETECTOR : z==-1 ? RAIL_BOOSTER : RAIL_NORMAL;
        REQUIRE(rail_place((RailCell){X,Y,z},kind,0));
    }
    RailPiece shape;
    REQUIRE(rail_at(sensor,&shape) && shape.kind==RAIL_DETECTOR && shape.shape==STRAIGHT_NS);
    REQUIRE(set_block(X+1,Y,0,REDSTONE_LAMP) && redstone_place_comparator(X-1,Y,0,WEST));
    REQUIRE(!rail_is_energized(booster) && !redstone_detector_power(X,Y,0));
    REQUIRE(!redstone_restore_timing(OVERWORLD,(RedstoneTiming){.position={X,Y,0},
        .remaining=DETECTOR_RAIL_TICKS+1,.output=15}));
    REQUIRE(cart_launch_on_rail(sensor));
    REQUIRE(redstone_detector_power(X,Y,0)==15 && rail_is_energized(booster));
    for (int i=0;i<DETECTOR_RAIL_TICKS+1;++i) redstone_step(X,0);
    REQUIRE(world_cell(X+1,Y,0)==LAMP_ON && redstone_detector_power(X,Y,0)==15 &&
        redstone_comparator_power(X-1,Y,0)==0);
    REQUIRE(world_select_dimension(NETHER) && !redstone_detector_power(X,Y,0) &&
        !cart_on_track(sensor));
    REQUIRE(world_select_dimension(OVERWORLD) && redstone_detector_power(X,Y,0)==15);
    px=X+.5f; py=Y+.295f; pz=.5f; yaw=0;
    REQUIRE(cart_restore_rider(0));
    int passed=0;
    for (int i=0;i<55;++i) {
        redstone_step(X,(int)pz);
        carts_step(.05f,0,1);
        passed|=carts_in(OVERWORLD)[0].track.z<-1;
    }
    REQUIRE(passed && !redstone_detector_power(X,Y,0) && world_cell(X+1,Y,0)==LAMP_OFF &&
        !rail_is_energized(booster));
    REQUIRE(cart_launch_on_rail(sensor));
    redstone_step(X,0);
    REQUIRE(world_cell(X+1,Y,0)==LAMP_ON && mining_drop(DETECTOR_RAIL,0)==DETECTOR_RAIL_ITEM);
    REQUIRE(set_block(X,Y-1,0,AIR));
    for (int i=0;i<8;++i) redstone_step(X,0);
    REQUIRE(world_cell(X,Y,0)==AIR && !redstone_detector_power(X,Y,0) && world_cell(X+1,Y,0)==LAMP_OFF);
    REQUIRE(set_block(X,Y-1,0,CHEST) && rail_place(sensor,RAIL_DETECTOR,0));
    REQUIRE(chest_break((ChestPos){X,Y-1,0})!=NULL);
    int recovered=0;
    for (int i=0;i<MAXDROP;++i) recovered|=drops[i].item.type==DETECTOR_RAIL_ITEM && drops[i].item.count==1;
    REQUIRE(recovered && world_cell(X,Y,0)==AIR);
    printf("DETECTOR RAIL TEST recipe/cross-seam lamp/booster/comparator/hold/release/support/isolation failures=%d\n",failures);
    return failures;
}

static int activator_rail_selftest(void) {
    int failures=0;
    enum { Y=145 };
    world_begin(318);
    generate();
    memset(inventory,0,sizeof inventory);
    inventory[1]=(ItemStack){IRON_INGOT,6,0,0,0};
    inventory[2]=(ItemStack){STICK,2,0,0,0};
    inventory[3]=(ItemStack){REDSTONE_TORCH_ITEM,1,0,0,0};
    const int r[]={1,2,1,1,3,1,1,2,1};
    REQUIRE(craft_take(r,3) && inventory[0].type==ACTIVATOR_RAIL_ITEM && inventory[0].count==6);
    inventory[1]=(ItemStack){TNT_ITEM,1,0,0,0};
    inventory[2]=(ItemStack){MINECART,1,0,0,0};
    const int diagonal[]={2,-1,-1,1};
    REQUIRE(craft_take(diagonal,2) && inventory[1].type==TNT_MINECART);
    for (int z=-2;z<=9;++z) {
        REQUIRE(set_block(0,Y-1,z,STONE));
        REQUIRE(rail_place((RailCell){0,Y,z},z<0 ? RAIL_BOOSTER : RAIL_ACTIVATOR,0));
    }
    REQUIRE(set_block(1,Y,0,GLASS));
    const RailCell track={0,Y,0},booster={0,Y,-1};
    REQUIRE(!rail_is_energized(track) && cart_launch_tnt_on_rail(track) &&
        carts_in(OVERWORLD)[0].kind==CART_TNT && !carts_in(OVERWORLD)[0].fuse);
    px=.5f; py=Y+.295f; pz=.5f;
    REQUIRE(!cart_restore_rider(0));
    REQUIRE(set_block(0,Y-1,9,REDSTONE_BLOCK));
    REQUIRE(!rail_is_energized(track) && rail_is_energized((RailCell){0,Y,1}));
    REQUIRE(set_block(0,Y-1,8,REDSTONE_BLOCK));
    REQUIRE(rail_is_energized(track) && !rail_is_energized(booster));
    carts_step(.05f,0,0);
    REQUIRE(carts_in(OVERWORLD)[0].fuse==CART_TNT_FUSE && !carts_in(OVERWORLD)[0].speed);
    REQUIRE(set_block(0,Y-1,8,STONE) && set_block(0,Y-1,9,STONE) && !rail_is_energized(track));
    player_reset_rules(CREATIVE);
    for (int i=0;i<CART_TNT_FUSE-1;++i) { carts_tick_fuses(); blasts_step(1); }
    REQUIRE(carts_in(OVERWORLD)[0].fuse==1 && world_cell(1,Y,0)==GLASS);
    carts_tick_fuses();
    size_t n=0;
    const PrimedBlast *blast=blasts_in(OVERWORLD,&n);
    REQUIRE(!carts_in(OVERWORLD)[0].occupied && n==1 && blast[0].kind==BLAST_CART_TRACK);
    blasts_step(1);
    REQUIRE(world_cell(1,Y,0)==AIR && world_cell(0,Y-1,0)==STONE &&
        activator_track(world_cell(0,Y,0)) && rail_type(block(0,Y,-1)));

    REQUIRE(set_block(20,Y-1,0,STONE) && rail_place((RailCell){20,Y,0},RAIL_NORMAL,1));
    const CartRecord impact={{20.90f,Y+.045f,.5f},{20,Y,0},.90f,4.f,1.57f,1,CART_TNT,0,0,{0,0,0},0};
    px=20.9f; py=Y+.01f; pz=2.5f;
    REQUIRE(cart_restore(OVERWORLD,impact));
    carts_step(.05f,0,0);
    REQUIRE(carts_in(OVERWORLD)[0].occupied && !carts_in(OVERWORLD)[0].speed);
    carts_reset();
    REQUIRE(set_block(21,Y,0,GLASS) && cart_restore(OVERWORLD,impact));
    carts_step(.05f,0,0);
    blast=blasts_in(OVERWORLD,&n);
    REQUIRE(!carts_in(OVERWORLD)[0].occupied && n==1 && blast[0].kind==BLAST_CART_IMPACT);
    blasts_step(1);
    REQUIRE(world_cell(21,Y,0)==AIR && world_cell(20,Y,0)==AIR);

    REQUIRE(set_block(40,Y-1,0,REDSTONE_BLOCK) && rail_place((RailCell){40,Y,0},RAIL_ACTIVATOR,0) &&
        cart_launch_on_rail((RailCell){40,Y,0}));
    px=40.5f; py=Y+.295f; pz=.5f;
    REQUIRE(cart_restore_rider(0));
    carts_step(.05f,0,0);
    carts_tick_fuses();
    REQUIRE(cart_rider()==0 && !carts_in(OVERWORLD)[0].fuse);
    printf("ACTIVATOR TEST recipe/range/network isolation/80 ticks/protection/impact/rider failures=%d\n",failures);
    return failures;
}

static int chest_cart_selftest(void) {
    int failures=0;
    enum { X=15,Y=145 };
    world_begin(511);
    generate();
    memset(inventory,0,sizeof inventory);
    inventory[0]=(ItemStack){CHEST,1,0,0,0};
    inventory[1]=(ItemStack){MINECART,1,0,0,0};
    const int recipe[]={0,-1,-1,1};
    REQUIRE(craft_take(recipe,2) && inventory[0].type==CHEST_MINECART && !inventory[1].count);
    RailCell detector={X,Y,2};
    REQUIRE(set_block(X,Y-1,2,STONE) && set_block(X+1,Y-1,2,STONE));
    REQUIRE(rail_place(detector,RAIL_DETECTOR,0) && redstone_place_comparator(X+1,Y,2,EAST));
    REQUIRE(cart_launch_chest_on_rail(detector));
    CartStorageView view={OVERWORLD,0,0};
    REQUIRE(cart_open_storage(0,&view) && cart_storage_valid(&view));
    REQUIRE(!cart_storage_move(&view,-1,0,0) && !cart_storage_item(&view,CART_STORAGE_SLOTS).count);
    inventory[1]=(ItemStack){COBBLESTONE,8,0,0,0};
    REQUIRE(cart_storage_move(&view,CART_STORAGE_SLOTS+1,0,1) && inventory[1].count==7 &&
        cart_storage_item(&view,0).count==1);
    REQUIRE(cart_storage_move(&view,CART_STORAGE_SLOTS+1,0,0) && !inventory[1].count &&
        cart_storage_item(&view,0).count==8);
    for (int i=0;i<8;++i) redstone_step(X,2);
    REQUIRE(redstone_detector_power(X,Y,2)==15 && redstone_comparator_power(X+1,Y,2)==1);
    ItemStack cargo[CART_STORAGE_SLOTS];
    for (int i=0;i<CART_STORAGE_SLOTS;++i) cargo[i]=(ItemStack){COBBLESTONE,64,0,0,0};
    REQUIRE(cart_restore_cargo(OVERWORLD,0,cargo,CART_STORAGE_SLOTS) && cart_detector_signal(detector)==15 &&
        !cart_container_insert(0,(ItemStack){APPLE,1,0,0,0}));
    REQUIRE(world_select_dimension(NETHER) && !cart_storage_valid(&view) &&
        cart_container_at(X,Y,2)==-1 && !cart_detector_signal(detector));
    REQUIRE(world_select_dimension(OVERWORLD) && cart_storage_valid(&view));
    carts_reset();
    REQUIRE(!cart_storage_valid(&view));

    REQUIRE(hopper_place((Hit){1,X,Y-2,0,X,Y-1,0}) &&
        rail_place((RailCell){X,Y,0},RAIL_NORMAL,0) &&
        hopper_place((Hit){1,X,Y,0,X,Y+1,0}) &&
        cart_launch_chest_on_rail((RailCell){X,Y,0}) && !cart_storage_valid(&view));
    REQUIRE(cart_open_storage(0,&view));
    for (int i=0;i<5;++i) REQUIRE(hopper_insert(X,Y+1,0,(ItemStack){APPLE,1,0,0,0}));
    REQUIRE(set_block(X+1,Y-1,0,REDSTONE_BLOCK));
    for (int i=0;i<48;++i) hoppers_step(X,0);
    const Hopper *in=hopper_at(X,Y+1,0),*out=hopper_at(X,Y-1,0);
    REQUIRE(in && out && !in->slots[0].count && !out->slots[0].count && cart_storage_item(&view,0).count==5);
    REQUIRE(set_block(X+1,Y-1,0,AIR));
    for (int i=0;i<48;++i) hoppers_step(X,0);
    REQUIRE(out && out->slots[0].type==APPLE && out->slots[0].count==5 && !cart_storage_item(&view,0).count);

    REQUIRE(cart_container_insert(0,(ItemStack){DIAMOND,1,0,0,0}));
    for (int i=0;i<MAXDROP;++i) drops[i]=make_drop(X+.5f,Y+.24f,.5f,(ItemStack){STONE,1,0,0,0});
    cart_blast_hit(0);
    REQUIRE(cart_storage_valid(&view) && cart_storage_item(&view,0).type==DIAMOND);
    drops_reset();
    cart_blast_hit(0);
    REQUIRE(!cart_storage_valid(&view) && drops[0].item.type==MINECART && drops[1].item.type==CHEST &&
        drops[2].item.type==DIAMOND);
    printf("CHEST CART TEST recipe/slot identity/comparator/hopper lock/push/pull/spill/isolation failures=%d\n",failures);
    return failures;
}

static int hopper_cart_selftest(void) {
    int failures=0;
    enum { X=-17,Y=146,Z=-1 };
    const RailCell rail={X,Y,Z};
    world_begin(511);
    generate();
    memset(inventory,0,sizeof inventory);
    inventory[0]=(ItemStack){HOPPER_ITEM,1,0,0,0};
    inventory[1]=(ItemStack){MINECART,1,0,0,0};
    const int recipe[]={0,-1,-1,1};
    REQUIRE(craft_take(recipe,2) && inventory[0].type==HOPPER_MINECART && !inventory[1].count);
    REQUIRE(set_block(X,Y-1,Z,REDSTONE_BLOCK) && rail_place(rail,RAIL_ACTIVATOR,0) &&
        rail_is_energized(rail) && cart_launch_hopper_on_rail(rail));
    CartStorageView view={OVERWORLD,0,0};
    REQUIRE(cart_cargo_count(OVERWORLD,0)==HOPPER_CART_SLOTS && cart_open_storage(0,&view));
    REQUIRE(!cart_container_extract(0,HOPPER_CART_SLOTS) && !cart_storage_item(&view,HOPPER_CART_SLOTS).count);
    REQUIRE(set_block(X,Y+1,Z,STONE)); /* The item lies on a solid block above the cart. */
    Vec3 pos=carts_in(OVERWORLD)[0].position;
    drops[0]=make_drop(pos.x,Y+2.13f,pos.z,(ItemStack){IRON_PICKAXE,1,17,0,0});
    REQUIRE(item_set_custom(&drops[0].item,3,"移动回收"));
    carts_tick_hoppers(X,Z);
    REQUIRE(drops[0].item.count==1 && !cart_storage_item(&view,0).count);
    REQUIRE(set_block(X,Y-1,Z,STONE) && !rail_is_energized(rail));
    carts_tick_hoppers(X,Z);
    REQUIRE(!drops[0].item.count && cart_storage_item(&view,0).damage==17 &&
        !strcmp(item_display_name(cart_storage_item(&view,0)),"移动回收") &&
        cart_hopper_cooldown(OVERWORLD,0)==HOPPER_CART_TICKS);
    REQUIRE(set_block(X,Y+1,Z,CHEST));
    ChestView chest;
    REQUIRE(chest_automation_open((ChestPos){X,Y+1,Z},&chest));
    for (int i=0;i<2;++i) REQUIRE(chest_automation_insert(&chest,(ItemStack){APPLE,1,0,0,0}));
    for (int i=0;i<HOPPER_CART_TICKS-1;++i) carts_tick_hoppers(X,Z);
    REQUIRE(chest_slot(&chest,0).count==2);
    carts_tick_hoppers(X,Z);
    REQUIRE(chest_slot(&chest,0).count==1 && cart_storage_item(&view,1).count==1);
    REQUIRE(!cart_restore_hopper_cooldown(OVERWORLD,0,HOPPER_CART_TICKS+1));
    REQUIRE(set_block(X,Y-1,Z,AIR) && hopper_place((Hit){1,X,Y-2,Z,X,Y-1,Z}));
    /* Only a stationary block hopper unloads the cart. */
    for (int i=0;i<25;++i) { carts_tick_hoppers(X,Z); hoppers_step(X,Z); }
    const Hopper *receiver=hopper_at(X,Y-1,Z);
    REQUIRE(receiver && receiver->slots[0].damage==17 && receiver->slots[1].count==2 &&
        !cart_storage_item(&view,0).count && !cart_storage_item(&view,1).count && !chest_slot(&chest,0).count);
    REQUIRE(world_select_dimension(NETHER) && !cart_storage_valid(&view) && !cart_cargo_count(NETHER,0));
    REQUIRE(world_select_dimension(OVERWORLD) && cart_storage_valid(&view));
    printf("HOPPER CART TEST recipe/capture through block/activator/cooldown/unload/isolation failures=%d\n",failures);
    return failures;
}

static int furnace_cart_selftest(void) {
    int failures=0;
    const int recipe[]={0,-1,-1,1};
    for (int order=0;order<2;++order) {
        world_begin(511); generate();
        player_reset_rules(SURVIVAL);
        memset(inventory,0,sizeof inventory);
        inventory[0]=(ItemStack){FURNACE,1,0,0,0};
        inventory[1]=(ItemStack){MINECART,1,0,0,0};
        REQUIRE(craft_take(recipe,2) && inventory[0].type==FURNACE_MINECART && !inventory[1].count);
        for (int z=-8;z<=1;++z) REQUIRE(set_block(0,143,z,STONE));
        for (int z=-7;z<=0;++z) REQUIRE(rail_place((RailCell){0,144,z},RAIL_NORMAL,0));
        REQUIRE(set_block(0,144,-8,STONE));
        int engine=order,front=1-order;
        if (order) REQUIRE(cart_launch_chest_on_rail((RailCell){0,144,-2}));
        REQUIRE(cart_launch_furnace_on_rail((RailCell){0,144,0}));
        if (!order) REQUIRE(cart_launch_chest_on_rail((RailCell){0,144,-2}));
        px=2.84f; py=144.01f; pz=1.9f; yaw=-1.032f; pitch=-.53f;
        selected_slot=1;
        inventory[1]=(ItemStack){(uint16_t)(order ? CHARCOAL : COAL),2,0,0,0};
        const char *action=cart_use();
        REQUIRE(action && !strcmp(action,"熔炉矿车已点火") && inventory[1].count==1 && cart_rider()<0);
        CartFurnaceDrive motor;
        REQUIRE(cart_furnace_drive(OVERWORLD,engine,&motor) && motor.fuel==3600 && motor.z<-.99f);
        for (int t=0;t<120;++t) {
            carts_tick_furnaces(); carts_step(.05f,0,0);
            const CartRecord *bank=carts_in(OVERWORLD);
            REQUIRE(bank[front].position.z<bank[engine].position.z-.86f);
        }
        const CartRecord *bank=carts_in(OVERWORLD);
        REQUIRE(bank[front].track.z<=-6 && bank[engine].track.z<=-5);
        REQUIRE(cart_furnace_drive(OVERWORLD,engine,&motor) && motor.fuel==3480);
        CartFurnaceDrive malformed={1,0,0};
        uint32_t invalid_bits=0x7fc00000u;
        memcpy(&malformed.z,&invalid_bits,sizeof invalid_bits);
        REQUIRE(!cart_restore_furnace_drive(OVERWORLD,engine,malformed));
        REQUIRE(!cart_restore_furnace_drive(OVERWORLD,engine,(CartFurnaceDrive){0,0,1}));
        REQUIRE(cart_restore_furnace_drive(OVERWORLD,engine,(CartFurnaceDrive){1,0,-1}));
        REQUIRE(world_select_dimension(NETHER));
        carts_tick_furnaces();
        REQUIRE(!cart_furnace_drive(NETHER,engine,&motor));
        REQUIRE(world_select_dimension(OVERWORLD));
        REQUIRE(cart_furnace_drive(OVERWORLD,engine,&motor) && motor.fuel==1);
        carts_tick_furnaces();
        REQUIRE(cart_furnace_drive(OVERWORLD,engine,&motor) && !motor.fuel && motor.z==0.f);
    }
    printf("FURNACE CART TEST recipe/coal/charcoal/traction/both-slot-orders/no-overlap/tick/expiry failures=%d\n",failures);
    return failures;
}

static int rail_junction_selftest(void) {
    int failures=0;
    enum { X=-16, Y=144, Z=0 };
    const RailCell mid={X,Y,Z};
    const int off[]={BEND_ES,BEND_SW,BEND_NE,BEND_ES};
    const int on[]={BEND_SW,BEND_WN,BEND_WN,BEND_NE};
    RailPiece rail={{{0,0},{0,0}},RAIL_NORMAL,0};
    for (int missing=0;missing<4;++missing) {
        world_begin(511); generate();
        for (int x=X-1;x<=X+1;++x)
            for (int z=Z-1;z<=Z+1;++z) REQUIRE(set_block(x,Y-1,z,STONE));
        REQUIRE(rail_place(mid,RAIL_NORMAL,0));
        for (int d=0;d<4;++d) if (d!=missing)
            REQUIRE(rail_place((RailCell){X+rail_dx(d),Y,Z+rail_dz(d)},RAIL_NORMAL,d&1));
        REQUIRE(rail_at(mid,&rail) && rail.shape==off[missing]);
        int x=X+rail_dx(missing),z=Z+rail_dz(missing);
        REQUIRE(redstone_place_lever((CircuitPos){x,Y,z},(CircuitPos){x,Y-1,z},0));
        REQUIRE(redstone_toggle_lever(x,Y,z)==2);
        for (int t=0;t<8;++t) redstone_step(X,Z);
        REQUIRE(rail_at(mid,&rail) && rail.shape==on[missing]);
        REQUIRE(cart_launch_on_rail(mid));
        REQUIRE(set_block(x,Y,z,AIR)); /* No powered blocks left in the neighbourhood. */
        for (int t=0;t<8;++t) redstone_step(X,Z);
        REQUIRE(rail_at(mid,&rail) && rail.shape==on[missing]);
        carts_reset();
        for (int t=0;t<8;++t) redstone_step(X,Z);
        REQUIRE(rail_at(mid,&rail) && rail.shape==off[missing]);
    }

    world_begin(511); generate();
    for (int x=X-4;x<=X+3;++x)
        for (int z=Z-3;z<=Z+3;++z) REQUIRE(set_block(x,Y-1,z,STONE));
    for (int z=Z-3;z<=Z+3;++z)
        REQUIRE(rail_place((RailCell){X,Y,z},RAIL_NORMAL,0));
    REQUIRE(set_block(X,Y,Z-4,STONE) && set_block(X,Y,Z+4,STONE));
    for (int x=X+1;x<=X+3;++x)
        REQUIRE(rail_place((RailCell){x,Y,Z},RAIL_NORMAL,1));
    REQUIRE(redstone_place_lever((CircuitPos){X-4,Y,Z},(CircuitPos){X-4,Y-1,Z},0));
    for (int x=X-3;x<X;++x) REQUIRE(redstone_place_wire(x,Y,Z));
    for (int powered=0;powered<2;++powered) {
        if (powered) REQUIRE(redstone_toggle_lever(X-4,Y,Z)==2);
        for (int t=0;t<16;++t) redstone_step(X,Z);
        REQUIRE(rail_at(mid,&rail) && rail.shape==(powered ? BEND_NE : BEND_ES));
        REQUIRE(redstone_wire_power(X-1,Y,Z)==(powered ? 13 : 0));
        REQUIRE(!rail_is_energized(mid));
        REQUIRE(cart_launch_furnace_on_rail((RailCell){X+3,Y,Z}) &&
            cart_restore_furnace_drive(OVERWORLD,0,(CartFurnaceDrive){300,-1,0}));
        px=X+2.5f; py=Y+.01f; pz=Z+1.5f;
        for (int t=0;t<120;++t) { carts_tick_furnaces(); carts_step(.05f,0,0); }
        const CartRecord *c=carts_in(OVERWORLD);
        REQUIRE(c[0].track.x==X && c[0].track.z==Z+(powered ? -3 : 3));
        carts_reset();
    }
    REQUIRE(set_block(X,Y,Z-1,AIR));
    REQUIRE(rail_at(mid,&rail) && rail.shape==BEND_ES); /* Only one fit after the edit. */
    printf("RAIL JUNCTION TEST four rotations/chunk seam/occupied retry/falling signal/wire/buttons topology failures=%d\n",failures);
    return failures;
}

static int free_cart_selftest(void) {
    int failures=0;
    enum { TOP=144, FLOOR=141, Z=5 };
    for (int dir=-1;dir<=1;dir+=2) {
        world_begin(511); generate();
        for (int x=-5;x<=5;++x) REQUIRE(set_block(x,FLOOR-1,Z,STONE));
        for (int x=-1;x<=1;++x)
            REQUIRE(set_block(x,TOP-1,Z,STONE) && rail_place((RailCell){x,TOP,Z},RAIL_NORMAL,1));
        for (int y=FLOOR;y<TOP;++y) REQUIRE(set_block(4*dir,y,Z,STONE));
        RailCell start={dir,TOP,Z};
        const CartRecord *a=&carts_in(OVERWORLD)[0];
        REQUIRE(cart_restore(OVERWORLD,(CartRecord){{dir+.5f,TOP+.045f,Z+.5f},start,.5f,
            dir*5.f,dir*1.570796f,1,CART_RIDEABLE,0,0,{0,0,0},0}));
        px=a->position.x; py=a->position.y+.25f; pz=a->position.z;
        REQUIRE(cart_restore_rider(0));
        int air=0,free=0;
        float origin=px;
        for (int j=0;j<70;++j) {
            REQUIRE(carts_step(.05f,0,0));
            free|=a->off_track && !cart_on_track(start);
            air|=a->off_track && a->position.y>FLOOR+.5f && a->position.y<TOP-.2f && a->free_velocity.y<0;
            REQUIRE(cart_rider()==0 && fabsf(py-a->position.y-.25f)<.001f &&
                px==a->position.x && pz==a->position.z);
        }
        REQUIRE(free && air && dir*(px-origin)>1.f && fabsf(a->position.y-FLOOR)<.018f);
        /* Directional block coordinates are [-4,-3) and [4,5). */
        REQUIRE((dir>0 ? px<=3.561f : px>=-2.561f) && !a->free_velocity.x && !a->free_velocity.y);
        unsigned rev=carts_revision();
        for (int j=0;j<40;++j) carts_step(.05f,0,0);
        REQUIRE(carts_revision()==rev); /* resting cart does not invalidate the render mesh */
        cart_unseat();
        carts_reset();
    }
    /* A lower line catches a falling car, but only within the same cell
       and close enough to the rail; repainting the old cell is not needed. */
    world_begin(511); generate();
    const RailCell top={3,TOP,Z},bottom={3,FLOOR,Z};
    REQUIRE(set_block(3,TOP-1,Z,STONE) && rail_place(top,RAIL_DETECTOR,1));
    REQUIRE(set_block(3,FLOOR-1,Z,STONE) && rail_place(bottom,RAIL_NORMAL,1));
    REQUIRE(cart_launch_chest_on_rail(top));
    REQUIRE(cart_container_insert(0,(ItemStack){STONE,1,0,0,0})==1);
    px=2.5f; py=TOP+.01f; pz=Z+2.5f;
    REQUIRE(cart_detector_signal(top)==1 && set_block(3,TOP-1,Z,AIR) && !rail_type(block(3,TOP,Z)));
    carts_step(.05f,0,0);
    REQUIRE(carts_in(OVERWORLD)[0].off_track && !cart_on_track(top) && !cart_detector_signal(top));
    for (int j=0;j<40;++j) carts_step(.05f,0,0);
    REQUIRE(cart_on_track(bottom) && !carts_in(OVERWORLD)[0].off_track &&
        cart_cargo_in(OVERWORLD,0)[0].count==1);
    CartRecord invalid={{0,150.f,.5f},{0,0,0},0,0,0,1,CART_RIDEABLE,0,1,{0,0,0},0};
    uint32_t nan_bits=0x7fc00000u;
    memcpy(&invalid.free_velocity.x,&nan_bits,sizeof nan_bits);
    REQUIRE(!cart_restore(OVERWORLD,invalid));
    printf("FREE CART TEST both directions/rider/wall/rest/removal/lower rail/cargo/validation failures=%d\n",failures);
    return failures;
}

static int ender_chest_selftest(void) {
    int failures=0;
    enum { Y=144 };
    world_begin(511); generate(); player_reset_rules(SURVIVAL);
    memset(inventory,0,sizeof inventory);
    inventory[0]=(ItemStack){PLANKS,2,0,0,0};
    inventory[1]=(ItemStack){IRON_INGOT,1,0,0,0};
    inventory[2]=(ItemStack){QUARTZ,1,0,0,0};
    const int grid[]={0,1,2,0};
    REQUIRE(craft_take(grid,2) && inventory[0].type==ENDER_CHEST_ITEM && !inventory[1].count);
    REQUIRE(set_block(0,Y-1,0,STONE) && set_block(0,Y-1,2,STONE));
    px=.5f; py=Y+.01f; pz=2.5f; selected_slot=0;
    aim_at((Vec3){.5f,Y-.18f,.5f});
    Interaction a=game_tap();
    REQUIRE(a.message && !inventory[0].count && world_cell(0,Y,0)==ENDER_CHEST_BASE+SOUTH);
    REQUIRE(light_emission(ENDER_CHEST_BASE+SOUTH)==7 && light_opacity(ENDER_CHEST_BASE+SOUTH)<16);
    REQUIRE(!world_point_blocked(.01f,Y+.4f,.5f,1) && !world_point_blocked(.5f,Y+.9f,.5f,1) &&
        world_point_blocked(.5f,Y+.45f,.5f,1));
    aim_at((Vec3){.5f,Y+.43f,.5f});
    REQUIRE(game_tap().panel==INTERACTION_ENDER_CHEST);
    EnderChestView pocket;
    const ChestPos loc={0,Y,0};
    REQUIRE(set_block(0,Y+1,0,STONE) && ender_chest_open(loc,&pocket)==CHEST_BLOCKED);
    REQUIRE(set_block(0,Y+1,0,GLASS) && ender_chest_open(loc,&pocket)==CHEST_OK);
    inventory[1]=(ItemStack){IRON_PICKAXE,1,17,enchant_code(ENCH_SILK_TOUCH,1),0};
    REQUIRE(item_set_custom(&inventory[1],4,"个人的镐"));
    inventory[2]=(ItemStack){APPLE,3,0,0,0};
    REQUIRE(ender_chest_move(&pocket,DOUBLE_CHEST_SLOTS+1,26,0)==CHEST_OK &&
        ender_chest_move(&pocket,DOUBLE_CHEST_SLOTS+2,0,1)==CHEST_OK &&
        inventory[2].count==2 && ender_chest_item(&pocket,0).count==1);
    REQUIRE(ender_chest_move(&pocket,0,27,0)==CHEST_NO_MOVE &&
        ender_chest_move(&pocket,0,26,1)==CHEST_NO_MOVE);
    ChestView ordinary;
    REQUIRE(set_block(1,Y,0,ENDER_CHEST) && chest_joint(loc)==CHEST_ALONE &&
        !chest_automation_open(loc,&ordinary) && !chests_count_in(OVERWORLD) &&
        container_signal(0,Y,0,ENDER_CHEST)==-1);
    REQUIRE(container_insert_one(0,Y,0,-1,(ItemStack){APPLE,1,0,0,0})==NO_INVENTORY);
    REQUIRE(ender_chest_item(&pocket,0).count==1);
    REQUIRE(world_select_dimension(NETHER) && set_block(0,Y+1,0,AIR) && set_block(0,Y,0,ENDER_CHEST));
    REQUIRE(!ender_chest_valid(&pocket) && ender_chest_open(loc,&pocket)==CHEST_OK);
    ItemStack old=ender_chest_item(&pocket,26);
    REQUIRE(old.damage==17 && old.enchants && item_prior_work(old)==4 && !strcmp(item_display_name(old),"个人的镐"));
    REQUIRE(world_select_dimension(OVERWORLD) && ender_chest_open(loc,&pocket)==CHEST_OK);

    drops_reset();
    inventory[0]=(ItemStack){WOOD_PICKAXE,1,0,0,0};
    REQUIRE(mine() && block(0,Y,0)==AIR && !ender_chest_valid(&pocket));
    REQUIRE(drops[0].item.type==ENDER_CHEST_ITEM && drops[0].item.count==1);
    REQUIRE(set_block(0,Y,0,ENDER_CHEST) && ender_chest_open(loc,&pocket)==CHEST_OK &&
        ender_chest_item(&pocket,26).damage==17);
    drops_reset();
    inventory[0]=(ItemStack){DIAMOND_PICKAXE,1,0,enchant_code(ENCH_SILK_TOUCH,1),0};
    REQUIRE(mine() && drops[0].item.type==ENDER_CHEST_ITEM && drops[0].item.count==1);
    REQUIRE(ender_chest_items()[26].damage==17 && chests_count_in(OVERWORLD)==0);
    player_take_damage(DAMAGE_VOID,MAX_HEALTH);
    REQUIRE(!player_rules.health && player_respawn()==RESPAWN_OK);
    REQUIRE(ender_chest_items()[26].damage==17);
    world_begin(512); generate();
    REQUIRE(!ender_chest_valid(&pocket) && !ender_chest_items()[0].count && !ender_chest_items()[26].count);
    printf("ENDER CHEST craft/place/shape/light/automation/Nether/break/Silk Touch/death/isolated world failures=%d\n",failures);
    return failures;
}

static int tnt_cart_fall_selftest(void) {
    int failures=0;
    enum { Y=140, Z=5 };
    for (int mode=0;mode<5;++mode) {
        world_begin(511); generate();
        int height=mode==0 ? 2 : mode==4 ? 58 : 3;
        RailCell upper={0,Y+height,Z};
        RailCell lower={0,Y,Z};
        REQUIRE(set_block(0,Y-1,Z,mode==3 ? REDSTONE_BLOCK : STONE));
        REQUIRE(set_block(0,upper.y-1,Z,mode==2 ? REDSTONE_BLOCK : STONE));
        REQUIRE(rail_place(upper,mode==2 ? RAIL_ACTIVATOR : RAIL_NORMAL,1));
        REQUIRE(cart_launch_tnt_on_rail(upper));
        if (mode==3) REQUIRE(rail_place(lower,RAIL_ACTIVATOR,1));
        REQUIRE(set_block(1,Y,Z,GLASS));
        px=1.5f; py=Y+.01f; pz=Z+2.5f;
        REQUIRE(set_block(0,upper.y-1,Z,AIR) && !rail_type(block(0,upper.y,Z)));
        carts_step(.05f,0,0);
        const CartRecord *cart=carts_in(OVERWORLD);
        REQUIRE(cart[0].occupied && cart[0].off_track && cart[0].fall_distance>0.f);
        size_t n=0;
        blasts_in(OVERWORLD,&n);
        REQUIRE(n==0);
        for (int step=0;step<130;++step) carts_step(.05f,0,0);
        const PrimedBlast *b=blasts_in(OVERWORLD,&n);
        if (!mode) REQUIRE(!n && cart[0].occupied && cart[0].off_track && !cart[0].fall_distance);
        else {
            REQUIRE(n==1 && !cart[0].occupied && fabsf(b[0].point.y-Y)<.21f);
            REQUIRE(b[0].kind==(mode==2 ? BLAST_CART_TRACK : BLAST_CART_IMPACT));
            if (mode==4) REQUIRE(b[0].speed.x==BLAST_CART_SPEED_LIMIT);
            else REQUIRE(b[0].speed.x>=6.f && b[0].speed.x<6.3f);
            if (mode==1) {
                blasts_step(1);
                REQUIRE(block(1,Y,Z)==AIR);
            }
        }
    }

    world_begin(511); generate();
    REQUIRE(set_block(0,Y-1,Z,STONE) && set_block(0,Y,Z,WATER));
    REQUIRE(cart_restore(OVERWORLD,(CartRecord){{.5f,Y+6.f,Z+.5f},{0,0,0},0,0,0,1,CART_TNT,0,1,{0,0,0},0}));
    px=2.f; py=Y; pz=Z+2.f;
    for (int step=0;step<100;++step) carts_step(.05f,0,0);
    size_t n=0;
    blasts_in(OVERWORLD,&n);
    REQUIRE(!n && carts_in(OVERWORLD)[0].occupied && !carts_in(OVERWORLD)[0].fall_distance);
    printf("TNT CART FALL sub-threshold/3 blocks/primed/activator order/cap/water failures=%d\n",failures);
    return failures;
}

static HorseRecord equine(int type,float x,float y,float z,int tamed) {
    HorseRecord a;
    memset(&a,0,sizeof a);
    a.feet=(Vec3){x,y,z}; a.type=(uint8_t)type;
    a.coat=2; a.gait=70; a.spring=80; a.max_health=23; a.health=19;
    a.temper=tamed ? 85 : 0; a.tamed=(uint8_t)tamed;
    return a;
}
static int horse_selftest(void) {
    int failures=0;
    REQUIRE(world_begin_generated(171,GENERATOR_RAILWAYS));
    int sx=0,sz=0;
    nearest_stronghold(0,0,&sx,&sz);
    REQUIRE(block(sx+5,STRONGHOLD_FLOOR+1,sz-4)==AIR);
    REQUIRE(world_begin_generated(171,GENERATOR_EQUINE));
    generate();
    int cy=STRONGHOLD_FLOOR+1;
    REQUIRE(block(sx+5,cy,sz-4)==SADDLE_CHEST);
    ChestPos chest={sx+5,cy,sz-4};
    ChestView opened;
    for (int i=0;i<SLOT_COUNT;++i) inventory[i]=(ItemStack){DIRT,64,0,0,0};
    REQUIRE(chest_open(chest,&opened)==CHEST_OK && block(sx+5,cy,sz-4)==CHEST &&
        inventory[0].count==64 && chest_slot(&opened,13).type==SADDLE);
    inventory[0]=(ItemStack){0,0,0,0,0};
    REQUIRE(chest_move(&opened,13,DOUBLE_CHEST_SLOTS,0)==CHEST_OK && inventory[0].type==SADDLE);
    REQUIRE(chest_open(chest,&opened)==CHEST_OK && !chest_slot(&opened,13).count &&
        inventory[0].count==1 && inventory[1].type==DIRT);
    inventory[1]=(ItemStack){APPLE,1,0,0,0}; inventory[2]=(ItemStack){WHEAT,1,0,0,0};
    inventory[3]=(ItemStack){0,0,0,0,0};
    inventory[4]=(ItemStack){SUGAR,1,0,0,0};
    const int ration[]={1,2,4,-1};
    REQUIRE(craft_preview(ration,2).type==FRUIT_RATION && craft_take(ration,2) &&
        inventory[1].type==FRUIT_RATION && inventory[1].count==2 && !inventory[2].count && !inventory[4].count);
    inventory[3]=inventory[1]; inventory[1]=(ItemStack){0,0,0,0,0};

    for (int x=-27;x<=0;++x) for (int z=-5;z<=5;++z) {
        REQUIRE(set_block(x,159,z,STONE));
        for (int y=160;y<=164;++y) REQUIRE(set_block(x,y,z,AIR));
    }
    REQUIRE(horse_restore(OVERWORLD,equine(EQUINE_HORSE,-18.5f,160.01f,.5f,0)));
    const HorseRecord *list=horses_in(OVERWORLD);
    px=-21.5f; py=160.01f; pz=.5f;
    aim_at((Vec3){list[0].feet.x,160.95f,.5f});
    selected_slot=0;
    REQUIRE(horse_use() && horse_rider()==-1 && inventory[0].type==SADDLE);
    /* A wall must prevent mounting, feeding and attacking through it. */
    REQUIRE(set_block(-20,161,0,STONE) && set_block(-20,160,0,STONE));
    REQUIRE(!horse_use() && !horse_attack());
    REQUIRE(set_block(-20,161,0,AIR) && set_block(-20,160,0,AIR));
    selected_slot=1;
    REQUIRE(horse_use() && horse_rider()==0);
    for (int i=0;i<33;++i) horses_step(.05f,0,1);
    REQUIRE(horse_rider()==-1 && list[0].temper==5 && !list[0].tamed);
    for (int round=0;round<24 && !list[0].tamed;++round) {
        px=-21.5f; py=160.01f; pz=.5f;
        aim_at((Vec3){list[0].feet.x,160.95f,.5f});
        if (horse_rider()<0) REQUIRE(horse_use());
        for (int i=0;i<31;++i) horses_step(.05f,0,0);
    }
    REQUIRE(list[0].tamed && horse_rider()==0 && !list[0].saddle);
    float still=px;
    for (int i=0;i<15;++i) horses_step(.05f,0,1);
    REQUIRE(px==still && horse_use() && horse_rider()==-1);
    px=-21.5f; py=160.01f; pz=.5f;
    aim_at((Vec3){list[0].feet.x,160.95f,.5f});
    selected_slot=0;
    REQUIRE(horse_use() && list[0].saddle && !inventory[0].count);
    REQUIRE(horse_use() && horse_rider()==0);
    yaw=1.57079633f;
    for (int i=0;i<10;++i) horses_step(.05f,0,1);
    REQUIRE(px>-16.f && fabsf(px-list[0].feet.x)<.001f);
    REQUIRE(set_block(-13,160,0,STONE) && set_block(-13,161,0,STONE));
    float base=list[0].feet.y;
    horse_jump(1.f);
    float peak=base;
    for (int i=0;i<25;++i) {
        horses_step(.04f,0,1);
        peak=fmaxf(peak,list[0].feet.y);
    }
    REQUIRE(peak>base+1.9f && px>-12.f);
    for (int i=0;i<22;++i) horses_step(.04f,0,0);
    REQUIRE(horse_use() && horse_rider()==-1 && !collides(px,py,pz));

    horses_reset();
    REQUIRE(horse_restore(OVERWORLD,equine(EQUINE_HORSE,-13.5f,160.01f,-2.5f,1)));
    REQUIRE(horse_restore(OVERWORLD,equine(EQUINE_DONKEY,-10.5f,160.01f,-2.5f,1)));
    selected_slot=3;
    px=-15.5f; py=160.01f; pz=-2.5f; aim_at((Vec3){-13.5f,161.f,-2.5f});
    REQUIRE(horse_use() && list[0].love==HORSE_LOVE && inventory[3].count==1);
    px=-8.5f; aim_at((Vec3){-10.5f,161.f,-2.5f});
    REQUIRE(horse_use() && list[1].love==HORSE_LOVE && !inventory[3].count);
    horses_step(.06f,0,0);
    REQUIRE(list[2].type==EQUINE_MULE && list[2].age>0 && !list[2].saddle &&
        list[0].cooldown==HORSE_COOLDOWN && list[1].cooldown>=HORSE_COOLDOWN-2);
    REQUIRE(world_select_dimension(NETHER) && !horses_in(NETHER)[0].type && horse_rider()==-1);
    REQUIRE(world_select_dimension(OVERWORLD) && horses_in(OVERWORLD)[2].type==EQUINE_MULE);
    printf("HORSE TEST loot/feeding/taming/occlusion/crossing/jump/breeding/isolation failures=%d\n",failures);
    return failures;
}

static FishPhase fish_until(FishPhase wanted,int limit) {
    for (int i=0;i<limit;++i) {
        FishPhase current=fishing_view().phase;
        if (current==wanted || current==FISH_IDLE ||
            (current==FISH_GROUNDED && wanted!=FISH_GROUNDED)) return current;
        fishing_step(.1f);
    }
    return fishing_view().phase;
}
static int fishing_junk(ItemStack item) {
    return item.type==LEATHER_BOOTS || item.type==LEATHER || item.type==BONE || item.type==WATER_BOTTLE ||
        item.type==STRING || (item.type==FISHING_ROD && !item.enchants) || item.type==BOWL ||
        item.type==STICK || item.type==INK_SAC || item.type==TRIPWIRE_HOOK_ITEM || item.type==ROTTEN_FLESH;
}
static int fishing_distribution_test(void) {
    int failures=0;
    uint64_t luck=enchant_code(ENCH_LUCK_OF_THE_SEA,3),lure=enchant_code(ENCH_LURE,3);
    const uint64_t effects[]={0,luck,lure,luck|lure};
    const int junk_expected[]={100,25,70,0}, treasure_expected[]={50,80,20,50};
    for (int levels=0;levels<4;++levels) {
        uint32_t random=0x12340001u;
        int junk=0,treasure=0,counts[ITEM_LIMIT]={0};
        const int samples=40000;
        for (int i=0;i<samples;++i) {
            ItemStack item=fishing_roll_catch(&random,effects[levels]);
            if (!item_stack_valid(item) || !item.count) { ++failures; break; }
            ++counts[item.type];
            junk+=fishing_junk(item);
            treasure+=item.type==SADDLE || item.type==NAME_TAG || item.type==LILY_PAD_ITEM || item.enchants!=0;
        }
        REQUIRE(junk_expected[levels] ? abs(junk-junk_expected[levels]*samples/1000)<230 : !junk);
        REQUIRE(abs(treasure-treasure_expected[levels]*samples/1000)<180);
        REQUIRE(counts[RAW_FISH] && counts[RAW_SALMON] && counts[CLOWNFISH] && counts[PUFFERFISH] &&
            counts[SADDLE] && counts[NAME_TAG] && counts[LILY_PAD_ITEM] && counts[BOW] &&
            counts[ENCHANTED_BOOK] && counts[FISHING_ROD]);
        if (junk_expected[levels])
            REQUIRE(counts[INK_SAC] && counts[WATER_BOTTLE] && counts[LEATHER_BOOTS] && counts[TRIPWIRE_HOOK_ITEM]);
        printf("FISH DISTRIBUTION levelset=%d junk=%d treasure=%d samples=%d\n",levels,junk,treasure,samples);
    }
    REQUIRE(enchant_valid((ItemStack){FISHING_ROD,1,1,luck|lure,0}));
    REQUIRE(enchant_valid((ItemStack){ENCHANTED_BOOK,1,0,luck,0}));
    REQUIRE(!enchant_valid((ItemStack){BOW,1,0,lure,0}));
    REQUIRE(!enchant_valid((ItemStack){FISHING_ROD,1,0,enchant_code(ENCH_FORTUNE,1),0}));
    return failures;
}
static int fishing_wait_ticks(uint64_t enchants) {
    fishing_reset();
    inventory[0]=(ItemStack){FISHING_ROD,1,0,enchants,0};
    if (!fishing_use() || fish_until(FISH_WAIT,35)!=FISH_WAIT) return -1;
    int ticks=0;
    while (ticks<700 && fishing_view().phase==FISH_WAIT) { fishing_step(.1f); ++ticks; }
    return fishing_view().phase==FISH_APPROACH ? ticks : -1;
}
static int fishing_pond(int water_y) {
    for (int x=-22;x<=-6;++x) for (int z=-2;z<=2;++z) {
        if (!set_block(x,water_y,z,x<=-21 ? STONE : WATER)) return 0;
        for (int y=water_y+1;y<=water_y+5;++y) if (!set_block(x,y,z,AIR)) return 0;
    }
    px=-21.5f; py=water_y+1.01f; pz=.5f; yaw=1.57079633f; pitch=-.38f;
    return 1;
}
static int fishing_weather_test(void) {
    int failures=0;
    /* Weather must be measured in a rain biome, not on a frozen mountain. */
    REQUIRE(world_begin_profile(970,GENERATOR_CURRENT,WORLD_SUPERFLAT));
    generate();
    REQUIRE(fishing_pond(7));
    selected_slot=0;
    int clear=fishing_wait_ticks(0);
    WeatherSave rain=weather_record();
    rain.kind=WEATHER_RAIN; rain.remaining=12000; rain.strike=0; rain.cover=8;
    REQUIRE(weather_restore(rain));
    int wet=fishing_wait_ticks(0);
    REQUIRE(clear>0 && wet>0 && wet<clear && abs(wet*5-clear*4)<=5);
    Vec3 float_at=fishing_view().point;
    const int x=(int)floorf(float_at.x),z=(int)floorf(float_at.z);
    REQUIRE(set_block(x,11,z,STONE));
    int sheltered=fishing_wait_ticks(0);
    REQUIRE(sheltered>0 && abs(sheltered-2*clear)<=2);
    fishing_cancel();
    printf("FISH WEATHER dry=%d rain=%d covered=%d failures=%d\n",clear,wet,sheltered,failures);
    return failures;
}
static int fishing_selftest(void) {
    int failures=fishing_distribution_test()+fishing_weather_test();
    /* This generator existed before any structures had saddle chests. */
    REQUIRE(world_begin_generated(970,GENERATOR_RAILWAYS));
    generate();
    REQUIRE(fishing_pond(159));
    memset(inventory,0,sizeof inventory);
    selected_slot=0;
    inventory[0]=(ItemStack){STICK,3,0,0,0}; inventory[1]=(ItemStack){STRING,2,0,0,0};
    const int grid[]={-1,-1,0,-1,0,1,0,-1,1};
    REQUIRE(craft_take(grid,3) && inventory[0].type==FISHING_ROD && !inventory[1].count);
    int plain=fishing_wait_ticks(0),faster=fishing_wait_ticks(enchant_code(ENCH_LURE,2));
    REQUIRE(plain>=50 && plain<=301 && faster>0 && plain-faster>=48);
    fishing_reset();
    inventory[0]=(ItemStack){FISHING_ROD,1,0,0,0};
    REQUIRE(fishing_use() && fishing_view().phase==FISH_CAST &&
        fish_until(FISH_WAIT,35)==FISH_WAIT);
    REQUIRE(fishing_use() && !fishing_active() && !inventory[0].damage && !inventory[1].count);

    REQUIRE(set_block(-20,161,0,STONE) && set_block(-20,160,0,STONE));
    REQUIRE(fishing_use() && fish_until(FISH_GROUNDED,30)==FISH_GROUNDED);
    REQUIRE(fishing_use() && inventory[0].damage==2 && !inventory[1].count);
    REQUIRE(set_block(-20,161,0,AIR) && set_block(-20,160,0,AIR));

    REQUIRE(fishing_use() && fish_until(FISH_APPROACH,350)==FISH_APPROACH);
    float hovering=fishing_view().point.y;
    REQUIRE(fish_until(FISH_BITE,30)==FISH_BITE && fishing_view().point.y<hovering-.12f);
    REQUIRE(fish_until(FISH_WAIT,30)==FISH_WAIT && !inventory[1].count);
    fishing_cancel();

    /* If a wall appears after a cast, no catch can go through it. */
    REQUIRE(fishing_use() && fish_until(FISH_BITE,350)==FISH_BITE);
    REQUIRE(set_block(-20,161,0,STONE) && set_block(-20,160,0,STONE));
    REQUIRE(fishing_use() && !fishing_active() && !inventory[1].count && inventory[0].damage==2);
    REQUIRE(set_block(-20,161,0,AIR) && set_block(-20,160,0,AIR));

    REQUIRE(fishing_use() && fish_until(FISH_WAIT,35)==FISH_WAIT);
    Vec3 floating=fishing_view().point;
    int bx=(int)floorf(floating.x), by=(int)floorf(floating.y), bz=(int)floorf(floating.z);
    REQUIRE(set_block(bx,by,bz,AIR));
    for (int i=0;i<4;++i) fishing_step(.1f);
    REQUIRE(!fishing_active() && !inventory[1].count && set_block(bx,by,bz,WATER));

    int counts[ITEM_LIMIT];
    memset(counts,0,sizeof counts);
    int tries=0;
    for (;tries<512 && !(counts[SADDLE] && counts[RAW_FISH] && counts[RAW_SALMON] &&
                         counts[CLOWNFISH] && counts[PUFFERFISH]);++tries) {
        inventory[0]=(ItemStack){FISHING_ROD,1,0,0,0};
        inventory[1]=(ItemStack){0,0,0,0,0};
        if (!fishing_use() || fish_until(FISH_BITE,350)!=FISH_BITE || !fishing_use() ||
            !item_stack_valid(inventory[1]) || inventory[1].type>=ITEM_LIMIT ||
            !inventory[1].count || inventory[0].damage!=1 || fishing_active()) {
            ++failures;
            break;
        }
        ++counts[inventory[1].type];
    }
    REQUIRE(counts[SADDLE] && counts[RAW_FISH] && counts[RAW_SALMON] && counts[CLOWNFISH] && counts[PUFFERFISH]);
    REQUIRE(orb_points(OVERWORLD)>=(unsigned)tries && orb_points(OVERWORLD)<=6u*(unsigned)tries);
    unsigned before_xp=orb_points(OVERWORLD);
    for (int i=1;i<SLOT_COUNT;++i) inventory[i]=(ItemStack){STONE,STACK_MAX,0,0,0};
    for (int i=0;i<MAXDROP;++i) drops[i]=make_drop(px,py,pz,(ItemStack){STONE,1,0,0,0});
    inventory[0]=(ItemStack){FISHING_ROD,1,0,0,0};
    REQUIRE(fishing_use() && fish_until(FISH_BITE,350)==FISH_BITE && fishing_use());
    REQUIRE(fishing_active() && !inventory[0].damage && orb_points(OVERWORLD)==before_xp);
    drops[8].item=(ItemStack){0,0,0,0,0};
    REQUIRE(fishing_use() && !fishing_active() && inventory[0].damage==1 &&
        drops[8].item.count && drops[8].item.type!=STONE);
    REQUIRE(orb_points(OVERWORLD)>before_xp && orb_points(OVERWORLD)<=before_xp+6);
    REQUIRE(fishing_use() && world_select_dimension(NETHER));
    fishing_step(.05f);
    REQUIRE(!fishing_active() && world_select_dimension(OVERWORLD) && !fishing_active());
    memset(drops,0,sizeof(Drop)*MAXDROP);
    inventory[0]=(ItemStack){PUFFERFISH,1,0,0,0};
    player_rules.food=13; player_rules.health=2;
    REQUIRE(player_eat() && player_effects.poison==PUFFER_POISON_TICKS &&
        player_effects.hunger==PUFFER_HUNGER_TICKS);
    for (int i=0;i<10;++i) game_step(.05f,0,0,0,0);
    REQUIRE(player_rules.health==1 && player_effects.poison==PUFFER_POISON_TICKS-10 &&
        player_effects.hunger==PUFFER_HUNGER_TICKS-10);
    player_respawn();
    REQUIRE(!player_effects.poison && !player_effects.hunger && !fishing_active());
    printf("FISHING TEST old-terrain/recipe/bite/wall/miss/loot/full-inventory/reset/poison tries=%d saddle=%d wait=%d lure=%d failures=%d\n",
        tries,counts[SADDLE],plain,faster,failures);
    return failures;
}

static int chest_selftest(void) {
    int failures=0;
    REQUIRE(world_begin_generated(994,GENERATOR_EQUINE));
    generate();
    selected_slot=0;
    inventory[0]=(ItemStack){PLANKS,16,0,0,0}; inventory[1]=(ItemStack){0,0,0,0,0};
    const int recipe[]={0,0,0,0,-1,0,0,0,0};
    REQUIRE(craft_take(recipe,3) && craft_take(recipe,3) && inventory[1].type==CHEST &&
        inventory[1].count==2);
    ChestPos l={-17,150,4},r={-16,150,4},third={-15,150,4};
    REQUIRE(set_block(l.x,149,l.z,STONE) && set_block(r.x,149,r.z,STONE));
    REQUIRE(set_block(l.x,150,l.z,CHEST) && chest_can_place(r));
    REQUIRE(set_block(r.x,150,r.z,CHEST) && !chest_can_place(third));
    REQUIRE(chest_joint(l)==CHEST_EAST && chest_joint(r)==CHEST_WEST);
    REQUIRE(!chest_can_place((ChestPos){-17,150,3}));
    px=-12.5f; py=150.01f; pz=4.5f; selected_slot=1;
    aim_at((Vec3){-15.5f,150.57f,4.5f});
    REQUIRE(place() && inventory[1].count==2 && block(third.x,150,third.z)==AIR);
    selected_slot=0;
    ChestView left,right;
    REQUIRE(chest_open(l,&left)==CHEST_OK && chest_open(r,&right)==CHEST_OK && left.count==2 &&
        right.count==2 && right.halves[0].x==l.x && left.halves[1].x==r.x);
    REQUIRE(!chests_count_in(OVERWORLD)); /* Opening an empty chest does not allocate a record. */
    inventory[0]=(ItemStack){PLANKS,56,0,0,0};
    REQUIRE(chest_move(&left,54,26,0)==CHEST_OK && !inventory[0].count);
    inventory[0]=(ItemStack){PLANKS,20,0,0,0};
    REQUIRE(chest_move(&right,54,26,0)==CHEST_OK && inventory[0].count==12 &&
        chest_slot(&left,26).count==64);
    REQUIRE(chest_move(&right,54,26,0)==CHEST_NO_MOVE && inventory[0].count==12);
    REQUIRE(chest_move(&left,54,53,1)==CHEST_OK && inventory[0].count==11 && chest_slot(&right,53).count==1);
    inventory[2]=(ItemStack){DIAMOND_PICKAXE,1,424,0,0};
    REQUIRE(chest_move(&right,56,53,1)==CHEST_NO_MOVE);
    REQUIRE(chest_move(&right,56,53,0)==CHEST_OK && inventory[2].type==PLANKS &&
        chest_slot(&left,53).type==DIAMOND_PICKAXE && chest_slot(&left,53).damage==424);
    REQUIRE(chest_move(&left,90,53,0)==CHEST_NO_MOVE && chest_move(&left,-1,0,0)==CHEST_NO_MOVE);
    REQUIRE(chests_count_in(OVERWORLD)==2 && chests_count_in(NETHER)==0);
    REQUIRE(set_block(l.x,151,l.z,STONE) && chest_open(r,&right)==CHEST_BLOCKED &&
        chest_move(&left,26,54,0)==CHEST_MISSING && inventory[0].count==11);
    REQUIRE(set_block(l.x,151,l.z,AIR) && world_select_dimension(NETHER) &&
        set_block(l.x,150,l.z,CHEST));
    REQUIRE(chest_move(&left,26,54,0)==CHEST_MISSING);
    REQUIRE(chest_open(l,&right)==CHEST_OK && !chest_slot(&right,26).count && world_select_dimension(OVERWORLD));

    /* Removing half a double chest is atomic even during drop backpressure. */
    for (int i=0;i<MAXDROP;++i) drops[i]=make_drop(.5f,150,.5f,(ItemStack){STONE,1,0,0,0});
    REQUIRE(chest_break(l) && block(l.x,150,l.z)==CHEST && chest_slot(&left,26).count==64);
    memset(drops,0,sizeof(Drop)*MAXDROP);
    REQUIRE(chest_break(l) && block(l.x,150,l.z)==AIR && drops[0].item.type==CHEST &&
        drops[1].item.type==PLANKS && drops[1].item.count==64);
    REQUIRE(chest_joint(r)==CHEST_ALONE);
    REQUIRE(chest_move(&left,53,54,0)==CHEST_MISSING);
    REQUIRE(chest_open(r,&right)==CHEST_OK && right.count==1 &&
        chest_slot(&right,26).type==DIAMOND_PICKAXE && chest_slot(&right,26).damage==424 &&
        chest_move(&right,26,54,0)==CHEST_OK && inventory[0].type==DIAMOND_PICKAXE &&
        inventory[0].damage==424 && chest_slot(&right,26).count==11);
    REQUIRE(chest_move(&right,26,57,0)==CHEST_OK && chests_count_in(OVERWORLD)==0 &&
        inventory[3].type==PLANKS && inventory[3].count==11);
    ChestPos unopened={-18,150,5};
    REQUIRE(set_block(unopened.x,unopened.y,unopened.z,SADDLE_CHEST) &&
        set_block(unopened.x,unopened.y+1,unopened.z,STONE));
    REQUIRE(chest_open(unopened,&right)==CHEST_BLOCKED && block(unopened.x,unopened.y,unopened.z)==SADDLE_CHEST);
    memset(drops,0,sizeof(Drop)*MAXDROP);
    REQUIRE(chest_break(unopened) && drops[1].item.type==SADDLE && drops[1].item.count==1);
    printf("CHEST TEST recipe/pair/seam/ceiling/transfer/backpressure/split/loot/isolation failures=%d\n",failures);
    return failures;
}

static int bed_selftest(void) {
    int failures=0;
    REQUIRE(world_begin_generated(808,GENERATOR_CURRENT));
    generate();
    for (int x=-36;x<=3;++x) for (int z=-6;z<=25;++z) {
        REQUIRE(set_block(x,147,z,GRASS));
    }
    px=-19.5f; py=148.01f; pz=10.5f; selected_slot=0;
    memset(inventory,0,sizeof inventory);
    inventory[0]=(ItemStack){IRON_INGOT,2,0,0,0};
    const int clippers[]={0,-1,-1,0};
    REQUIRE(craft_take(clippers,2) && inventory[0].type==SHEARS);
    REQUIRE(actors_restore(OVERWORLD,(ActorRecord){{-17.5f,148.01f,10.5f},0,ACTOR_SHEEP,8,0}));
    aim_at((Vec3){-17.5f,148.87f,10.5f});
    REQUIRE(set_block(-19,149,10,STONE) && set_block(-19,148,10,STONE));
    REQUIRE(!actors_shear(target()) && !inventory[0].damage);
    REQUIRE(set_block(-19,149,10,AIR) && set_block(-19,148,10,AIR));
    REQUIRE(actors_shear(target()) && inventory[0].damage==1 && drops[0].item.type==WOOL &&
        drops[0].item.count>=1 && drops[0].item.count<=3 && actors_in(OVERWORLD)[0].traits==SHEEP_SHORN);
    REQUIRE(actors_shear(target()) && inventory[0].damage==1 && !drops[1].item.count);
    for (int i=0;i<170;++i) actors_step(.1f);
    REQUIRE(actors_in(OVERWORLD)[0].type==ACTOR_SHEEP && !actors_in(OVERWORLD)[0].traits);
    actors_reset();

    inventory[1]=(ItemStack){STRING,12,0,0,0};
    const int wool[]={1,1,1,1};
    REQUIRE(craft_take(wool,2) && craft_take(wool,2) && craft_take(wool,2) && inventory[2].type==WOOL &&
        inventory[2].count==3);
    inventory[3]=(ItemStack){PLANKS,3,0,0,0};
    const int bed_recipe[]={2,2,2,3,3,3,-1,-1,-1};
    REQUIRE(craft_take(bed_recipe,3) && inventory[1].type==BED && inventory[1].count==1);
    selected_slot=1;
    BedPos f={-17,148,10},h={-16,148,10};
    REQUIRE(set_block(h.x,h.y,h.z,STONE) && !bed_place(f,1) && block(f.x,f.y,f.z)==AIR);
    REQUIRE(set_block(h.x,h.y,h.z,AIR));
    aim_at((Vec3){-16.5f,147.82f,10.5f});
    REQUIRE(place() && !inventory[1].count && world_cell(f.x,f.y,f.z)==BED_BASE+1 &&
        world_cell(h.x,h.y,h.z)==BED_BASE+5);
    REQUIRE(collides(-16.5f,148.2f,10.5f) && !collides(-16.5f,148.57f,10.5f));
    aim_at((Vec3){-16.5f,148.40f,10.5f});
    Hit bed=target();
    REQUIRE(bed.hit && block(bed.x,bed.y,bed.z)==BED && bed_use(bed) && !bed_sleeping() && !bed_anchor().valid);
    world_set_dawn(10);
    player_rules.day_tick=14100;
    REQUIRE(actors_restore(OVERWORLD,(ActorRecord){{-15.5f,148.01f,13.5f},0,ACTOR_ZOMBIE,20,0}));
    REQUIRE(bed_use(bed) && !bed_sleeping());
    actors_reset();
    REQUIRE(set_block(h.x,h.y+1,h.z,STONE) && bed_use(bed) && !bed_sleeping() && !bed_anchor().valid);
    REQUIRE(set_block(h.x,h.y+1,h.z,AIR) && bed_use(bed) && bed_sleeping() && bed_anchor().head.x==h.x);
    float before=px;
    for (int i=0;i<110 && bed_sleeping();++i) game_step(.05f,0,1,0,0);
    REQUIRE(!bed_sleeping() && bed_take_message() && player_rules.day_tick==0 &&
        player_rules.world_day==11 && world_moon_phase()==3 && px==before);
    player_take_damage(DAMAGE_VOID,MAX_HEALTH);
    player_respawn();
    REQUIRE(player_rules.health==MAX_HEALTH && player_rules.world_day==11 &&
        fabsf(px-h.x-.5f)<=1.f && fabsf(pz-h.z-.5f)<=1.f &&
        !collides(px,py,pz));
    for (int i=0;i<MAXDROP;++i) drops[i]=make_drop(px,py,pz,(ItemStack){DIRT,1,0,0,0});
    REQUIRE(bed_break(f) && block(f.x,f.y,f.z)==BED && block(h.x,h.y,h.z)==BED);
    memset(drops,0,sizeof(Drop)*MAXDROP);
    REQUIRE(bed_break(h) && block(f.x,f.y,f.z)==AIR && block(h.x,h.y,h.z)==AIR &&
        drops[0].item.type==BED && !drops[1].item.count);
    player_respawn();
    REQUIRE(!bed_anchor().valid && fabsf(py-148.01f)>4.f);
    REQUIRE(world_select_dimension(NETHER));
    px=-19.5f; py=148.01f; pz=10.5f;
    REQUIRE(set_block(f.x,147,f.z,NETHER_BRICKS) && set_block(h.x,147,h.z,NETHER_BRICKS) && bed_place(f,1));
    REQUIRE(set_block(h.x,148,h.z+1,NETHER_BRICKS) && set_block(h.x,148,h.z-1,CHEST));
    const Hit bad={1,f.x,f.y,f.z,0,0,0};
    REQUIRE(bed_use(bad) && !bed_sleeping() && !bed_anchor().valid && player_rules.health<MAX_HEALTH &&
        block(f.x,f.y,f.z)==AIR && block(h.x,h.y,h.z)==AIR && block(h.x,148,h.z+1)==AIR &&
        block(h.x,148,h.z-1)==CHEST);
    printf("BED TEST wool/shears/regrowth/pair/night/hostile/respawn/fallback/dimension failures=%d\n",failures);
    return failures;
}

static int farming_selftest(void) {
    int failures=0;
    REQUIRE(world_begin_generated(584,GENERATOR_FARM));
    generate();
    int sx=(int)floorf(px),sz=(int)floorf(pz),gx=0,gz=0,gy=0;
    for (int x=sx-20;x<=sx+20 && !gy;++x) for (int z=sz-20;z<=sz+20 && !gy;++z) {
        int y=world_column_top(x,z);
        if (block(x,y,z)==TALL_GRASS) { gx=x; gz=z; gy=y; }
    }
    REQUIRE(gy && block(gx,gy-1,gz)==GRASS);
    REQUIRE(world_begin_generated(584,GENERATOR_EQUINE) && block(gx,gy-1,gz)==GRASS && block(gx,gy,gz)==AIR);
    generate();
    /* Bone meal lets an old world get its first wheat seeds without swapping generators. */
    inventory[0]=(ItemStack){BONE_MEAL,1,0,0,0};
    for (int x=99;x<=103;++x) for (int z=17;z<=21;++z) REQUIRE(set_block(x,148,z,GRASS));
    REQUIRE(farm_use((Hit){1,101,148,19,101,149,19}) && !inventory[0].count && block(101,149,19)==TALL_GRASS);
    REQUIRE(set_block(-17,150,0,DIRT) && set_block(-18,150,0,GRASS) && set_block(-17,150,2,GRASS) &&
        set_block(-13,150,0,WATER));
    Hit wet={1,-17,150,0,-17,151,0},dry={1,-18,150,0,-18,151,0},dark={1,-17,150,2,-17,151,2};
    selected_slot=0;
    inventory[0]=(ItemStack){STONE_HOE,1,0,0,0};
    REQUIRE(farm_use(wet) && farm_use(dry) && farm_use(dark) && inventory[0].damage==3);
    REQUIRE(world_cell(-17,150,0)==FARM_BASE+7 && world_cell(-18,150,0)==FARM_BASE &&
        !set_block(-17,155,0,FARMLAND));
    REQUIRE(collides(-16.5f,150.85f,.5f) && !collides(-16.5f,150.96f,.5f));
    px=-16.5f; py=152.f; pz=.5f; pitch=-1.55f;
    REQUIRE(target().hit && target().x==wet.x && target().y==wet.y);
    inventory[0]=(ItemStack){WHEAT_SEEDS,2,0,0,0};
    REQUIRE(farm_use(target()) && world_cell(-17,151,0)==WHEAT_BASE && inventory[0].count==1);
    inventory[0]=(ItemStack){CARROT,2,0,0,0};
    REQUIRE(farm_use(dry) && world_cell(-18,151,0)==CARROT_BASE && inventory[0].count==1);
    inventory[0]=(ItemStack){POTATO,2,0,0,0};
    REQUIRE(farm_use(dark) && world_cell(-17,151,2)==POTATO_BASE && inventory[0].count==1);
    const BlockEdit far[]={{256,150,0,FARM_BASE+7},{256,151,0,WHEAT_BASE},
        {-17,154,2,STONE},{-16,150,2,STONE}};
    REQUIRE(world_set_blocks(far,4));
    player_rules.day_tick=7000;
    for (int i=0;i<5000;++i) farm_step(.1f);
    REQUIRE(world_cell(-17,151,0)==WHEAT_BASE+7 && world_cell(-18,151,0)>CARROT_BASE &&
        world_cell(-17,151,2)==POTATO_BASE && world_cell(256,151,0)==WHEAT_BASE &&
        world_cell(256,150,0)==FARM_BASE+7);
    REQUIRE(set_block(-16,151,2,TORCH));
    player_rules.day_tick=14000;
    for (int i=0;i<3600;++i) farm_step(.1f);
    REQUIRE(world_cell(-17,151,2)>POTATO_BASE);
    inventory[0]=(ItemStack){BONE_MEAL,3,0,0,0};
    REQUIRE(world_set_farm_stage(-17,151,2,POTATO_BASE+1));
    Hit tuber={1,-17,151,2,-17,152,2};
    REQUIRE(farm_use(tuber) && inventory[0].count==2 && world_cell(-17,151,2)>=POTATO_BASE+3);
    REQUIRE(set_block(-13,150,0,AIR));
    for (int i=0;i<1200;++i) farm_step(.1f);
    REQUIRE(world_cell(-17,150,0)==FARM_BASE && world_cell(-17,151,0)==WHEAT_BASE+7);
    REQUIRE(world_select_dimension(NETHER) && world_farm_batch(-2,0,(BlockEdit[4]){{0}},4)==0);
    REQUIRE(world_select_dimension(OVERWORLD));
    Hit ripe={1,-17,151,0,-17,152,0};
    for (int i=0;i<MAXDROP;++i) drops[i].item=(ItemStack){COAL,1,0,0,0};
    REQUIRE(farm_break(ripe) && world_cell(-17,151,0)==WHEAT_BASE+7);
    memset(drops,0,sizeof dimension_drops[0]);
    REQUIRE(farm_break(ripe) && block(-17,151,0)==AIR && drops[0].item.type==WHEAT_SEEDS && drops[1].item.type==WHEAT);
    REQUIRE(world_set_blocks((BlockEdit[]){{-17,151,0,WHEAT_BASE+1}},1));
    py=152.1f; vy=0; grounded=0; player_rules.fall_origin=py;
    for (int i=0;i<24;++i) game_step(.025f,0,0,0,0);
    REQUIRE(block(-17,150,0)==DIRT && block(-17,151,0)==AIR);

    inventory_reset();
    inventory[0]=(ItemStack){PLANKS,2,0,0,0}; inventory[1]=(ItemStack){STICK,3,0,0,0};
    int hoe[]={-1,0,0,-1,-1,1,-1,-1,1};
    REQUIRE(craft_preview(hoe,3).type==WOOD_HOE && craft_take(hoe,3));
    inventory[3]=(ItemStack){WHEAT,3,0,0,0};
    int bread[]={-1,-1,-1,3,3,3,-1,-1,-1};
    REQUIRE(craft_preview(bread,3).type==BREAD && craft_take(bread,3));
    inventory[4]=(ItemStack){BONE,1,0,0,0};
    REQUIRE(craft_preview((int[]){-1,4,-1,-1},2).type==BONE_MEAL &&
        craft_preview((int[]){-1,4,-1,-1},2).count==3);
    inventory[5]=(ItemStack){POTATO,1,0,0,0}; inventory[6]=(ItemStack){CHARCOAL,1,0,0,0};
    REQUIRE(set_block(-21,150,0,FURNACE) && furnace_deposit(-21,150,0,FURNACE_INPUT,5) &&
        furnace_deposit(-21,150,0,FURNACE_FUEL,6));
    for (int i=0;i<10;++i) furnaces_advance(1.f);
    const Furnace *f=furnace_at(-21,150,0);
    REQUIRE(f && f->slots[FURNACE_OUTPUT].type==BAKED_POTATO);

    actors_reset();
    for (int x=0;x<19;++x) REQUIRE(set_block(x,149,8,STONE));
    px=12.5f; py=150.01f; pz=8.5f;
    player_reset_rules(SURVIVAL); player_rules.day_tick=14000;
    REQUIRE(actors_restore(OVERWORLD,(ActorRecord){{4.5f,150.01f,8.5f},0,ACTOR_SKELETON,20,0}));
    for (int i=0;i<3;++i) REQUIRE(set_block(8,150+i,8,STONE));
    actors_step(.05f);
    REQUIRE(actors_fireballs()[0].seconds<=0.f && player_rules.health==MAX_HEALTH);
    for (int i=0;i<3;++i) REQUIRE(set_block(8,150+i,8,AIR));
    for (int i=0;i<4;++i) actors_step(.025f);
    REQUIRE(actors_fireballs()[0].seconds>0 && actors_fireballs()[0].kind==HOSTILE_ARROW);
    for (int i=0;i<25;++i) actors_step(.025f);
    REQUIRE(player_rules.health==MAX_HEALTH-3);
    printf("FARM TEST seeds/water/light/harvest/trample/skeleton failures=%d\n",failures);
    return failures;
}

static int animal_count(int type,int babies) {
    const ActorRecord *a=actors_in(world_dimension());
    const ActorCare *c=actors_care_in(world_dimension());
    int n=0;
    for (int i=0;i<ACTOR_CAPACITY;++i) if (a[i].type==type && (!babies || c[i].grow)) ++n;
    return n;
}
static int aim_feed(Vec3 animal) { aim_at(animal); return actors_use_livestock(target())!=NULL; }
static int livestock_selftest(void) {
    int failures=0;
    world_begin(885101);
    generate();
    for (int x=-12;x<=12;++x) for (int z=-10;z<=10;++z) REQUIRE(set_block(x,146,z,STONE));
    const ActorRecord *a=actors_in(OVERWORLD);
    const ActorCare *c=actors_care_in(OVERWORLD);
    ActorRecord first={{-1.25f,147.01f,-1.5f},0,ACTOR_COW,10,0};
    ActorRecord second={{1.25f,147.01f,-1.5f},0,ACTOR_COW,10,0};
    REQUIRE(!actors_restore_with_care(OVERWORLD,first,(ActorCare){1,1,0,0,1}));
    REQUIRE(!actors_restore_with_care(OVERWORLD,first,(ActorCare){0,0,0,3,0}));
    REQUIRE(!actors_restore(NETHER,first) && actors_restore(OVERWORLD,first) && actors_restore(OVERWORLD,second));
    px=0; py=147.01f; pz=.5f; selected_slot=0;
    inventory[0]=(ItemStack){CARROT,2,0,0,0};
    Vec3 cow1={first.feet.x,147.80f,first.feet.z},cow2={second.feet.x,147.80f,second.feet.z};
    REQUIRE(!aim_feed(cow1) && inventory[0].count==2);
    inventory[0]=(ItemStack){WHEAT,4,0,0,0};
    REQUIRE(set_block(-1,148,-1,STONE) && !aim_feed(cow1) && inventory[0].count==4);
    REQUIRE(set_block(-1,148,-1,AIR) && aim_feed(cow1) && c[0].love==600 && c[0].managed);
    REQUIRE(aim_feed(cow1) && inventory[0].count==3);
    REQUIRE(aim_feed(cow2) && c[1].love==600 && inventory[0].count==2);
    selected_slot=1;
    for (int z=-3;z<=1;++z) for (int y=147;y<=149;++y) REQUIRE(set_block(0,y,z,STONE));
    for (int i=0;i<90;++i) actors_step(.05f);
    REQUIRE(animal_count(ACTOR_COW,1)==0 && c[0].love && c[1].love);
    for (int z=-3;z<=1;++z) for (int y=147;y<=149;++y) REQUIRE(set_block(0,y,z,AIR));

    unsigned remaining=c[0].love;
    Vec3 sleeping_position=a[0].feet;
    float old_px=px;
    px=180.f;
    actors_step(.05f);
    REQUIRE(animal_count(ACTOR_COW,0)==2 && c[0].love==remaining &&
        a[0].feet.x==sleeping_position.x && a[0].feet.z==sleeping_position.z);
    REQUIRE(world_select_dimension(NETHER) && animal_count(ACTOR_COW,0)==0);
    actors_step(.05f);
    REQUIRE(world_select_dimension(OVERWORLD));
    px=old_px;
    /* Bring the adults back to the center if their random walk moved apart. */
    for (int i=0;i<220 && !animal_count(ACTOR_COW,1);++i) actors_step(.05f);
    REQUIRE(animal_count(ACTOR_COW,0)==3 && animal_count(ACTOR_COW,1)==1 &&
        c[0].cooldown && c[1].cooldown && c[2].managed && !c[0].love);
    REQUIRE(orb_points(OVERWORLD)>=1 && orb_points(OVERWORLD)<=7 && !experience_points());

    actors_reset();
    ActorRecord close=second;
    close.feet.x=-.15f;
    REQUIRE(actors_restore_with_care(OVERWORLD,first,(ActorCare){0,0,600,0,1}) &&
        actors_restore_with_care(OVERWORLD,close,(ActorCare){0,0,600,0,1}));
    ActorRecord resident={{240.5f,147.01f,40.5f},0,ACTOR_PIG,10,0};
    for (int i=2;i<ACTOR_CAPACITY;++i)
        REQUIRE(actors_restore_with_care(OVERWORLD,resident,(ActorCare){0,0,0,0,1}));
    for (int i=0;i<80;++i) actors_step(.05f);
    REQUIRE(animal_count(ACTOR_COW,0)==2 && c[0].love && c[1].love && !c[0].cooldown && !c[1].cooldown);
    REQUIRE(actors_hit_arrow((Vec3){240.5f,147.45f,40.5f},20));
    for (int i=0;i<8;++i) actors_step(.05f);
    REQUIRE(animal_count(ACTOR_COW,1)==1 && c[0].cooldown && c[1].cooldown);

    actors_reset();
    REQUIRE(actors_restore(OVERWORLD,first));
    selected_slot=0;
    for (int i=0;i<SLOT_COUNT;++i) inventory[i]=(ItemStack){STONE,64,0,0,0};
    inventory[0]=(ItemStack){BUCKET,2,0,0,0};
    REQUIRE(aim_feed(cow1) && inventory[0].count==2);
    inventory[2]=(ItemStack){0,0,0,0,0};
    REQUIRE(aim_feed(cow1) && inventory[0].count==1 && inventory[2].type==MILK_BUCKET);
    selected_slot=2; player_effects=(PlayerEffects){.poison=48,.hunger=80};
    REQUIRE(actors_use_livestock(target()) && inventory[2].type==BUCKET &&
        !player_effects.hunger && !player_effects.poison);
    memset(drops,0,sizeof dimension_drops[0]);
    REQUIRE(actors_hit_arrow(cow1,14) && drops[0].item.type==RAW_BEEF);
    REQUIRE(actors_restore_with_care(OVERWORLD,first,(ActorCare){2,0,0,0,1}));
    REQUIRE(!actors_hit_arrow(cow1,14)); /* .79 is above the juvenile collision envelope. */
    REQUIRE(actors_hit_arrow((Vec3){cow1.x,147.3f,cow1.z},14) && !drops[2].item.count);
    REQUIRE(actors_restore_with_care(OVERWORLD,first,(ActorCare){2,0,0,0,1}));
    actors_step(.05f); REQUIRE(c[0].grow==1);
    actors_step(.05f); REQUIRE(c[0].grow==0);

    actors_reset();
    ActorRecord bird={{3.25f,147.01f,1.5f},0,ACTOR_CHICKEN,4,0};
    REQUIRE(actors_restore_with_care(OVERWORLD,bird,(ActorCare){0,0,0,1,1}));
    for (int i=0;i<MAXDROP;++i) drops[i].item=(ItemStack){COAL,1,0,0,0};
    actors_step(.05f); REQUIRE(c[0].egg==1);
    drops[0].item=(ItemStack){0,0,0,0,0};
    actors_step(.05f); REQUIRE(drops[0].item.type==EGG && c[0].egg>=CHICKEN_EGG_MIN_TICKS);
    actors_reset();
    selected_slot=0; yaw=0; pitch=-.52f;
    for (int i=0;i<48;++i) {
        inventory[0]=(ItemStack){EGG,1,0,0,0};
        REQUIRE(actors_use_livestock(target()) && !inventory[0].count);
        for (int t=0;t<22;++t) actors_step(.025f);
    }
    REQUIRE(animal_count(ACTOR_CHICKEN,1)>0 && actors_fireballs()[0].seconds<=0.f);
    REQUIRE(set_block(8,147,5,FURNACE) && set_block(9,147,5,FURNACE));
    selected_slot=0;
    inventory[0]=(ItemStack){RAW_BEEF,1,0,0,0}; inventory[1]=(ItemStack){RAW_CHICKEN,1,0,0,0};
    inventory[2]=(ItemStack){CHARCOAL,2,0,0,0};
    REQUIRE(furnace_deposit(8,147,5,FURNACE_INPUT,0) && furnace_deposit(9,147,5,FURNACE_INPUT,1) &&
        furnace_deposit(8,147,5,FURNACE_FUEL,2));
    inventory[2]=(ItemStack){CHARCOAL,1,0,0,0};
    REQUIRE(furnace_deposit(9,147,5,FURNACE_FUEL,2));
    for (int i=0;i<10;++i) furnaces_advance(1.f);
    const Furnace *beef=furnace_at(8,147,5),*chicken=furnace_at(9,147,5);
    REQUIRE(beef && chicken && beef->slots[FURNACE_OUTPUT].type==COOKED_BEEF &&
        chicken->slots[FURNACE_OUTPUT].type==COOKED_CHICKEN);
    /* Find a deterministic food-poisoning draw instead of sampling the wall clock. */
    inventory[0]=(ItemStack){RAW_CHICKEN,1,0,0,0};
    player_rules.food=12;
    player_rules.day_tick=0;
    while (hash2((int)floorf(px)+(int)player_rules.day_tick,(int)floorf(pz)+14)%10u>=3u &&
        player_rules.day_tick<100) ++player_rules.day_tick;
    REQUIRE(player_eat() && player_rules.food==14 && player_effects.hunger==CHICKEN_HUNGER_TICKS);
    printf("LIVESTOCK TEST feed/mate/occlusion/travel/milk/chicks=%d/cook failures=%d\n",animal_count(ACTOR_CHICKEN,1),failures);
    return failures;
}

static int enclosures_selftest(void) {
    int failures=0;
    world_begin(894501);
    generate();
    const int y=148;
    for (int x=-21;x<=-10;++x) for (int z=-7;z<=8;++z) REQUIRE(set_block(x,y-1,z,STONE));
    REQUIRE(!valid_cell(EGG) && !set_block(-15,y+5,0,STICK) && !valid_cell(FENCE_GATE) && !gate_place(-15,y+5,0,NORTH));
    for (int x=-21;x<=-11;++x) if (x!=-16) REQUIRE(set_block(x,y,0,WOOD_FENCE));
    REQUIRE(gate_place(-16,y,0,NORTH));
    GateState gate={0,0,0};
    REQUIRE(gate_decode(world_cell(-16,y,0),&gate) && gate.facing==NORTH && !gate.open && !gate.powered);
    const int links[]={AIR,world_cell(-16,y,0),AIR,WOOD_FENCE};
    REQUIRE(fence_connections(WOOD_FENCE,links)==((1u<<EAST)|(1u<<WEST)));
    REQUIRE(fence_connections(NETHER_FENCE,links)==(1u<<EAST));
    BlockBox boxes[BARRIER_PARTS];
    REQUIRE(barrier_world_boxes(-17,y,0,WOOD_FENCE,0,boxes)==5);
    REQUIRE(boxes[1].x1==-16.f && boxes[1].y1<y+1.f);
    REQUIRE(barrier_world_boxes(-16,y,0,world_cell(-16,y,0),1,boxes)==1 && boxes[0].y1==y+1.5f);
    REQUIRE(world_point_blocked(-16.05f,y+.46f,.50f,0));
    REQUIRE(!world_point_blocked(-16.05f,y+.64f,.50f,0) && world_point_blocked(-16.05f,y+.64f,.50f,1));
    REQUIRE(collides(-15.50f,y+1.25f,.50f) && !collides(-15.50f,y+1.501f,.50f));
    REQUIRE(!collides(-15.50f,y+.02f,1.4f));
    REQUIRE(set_block(-15,y,3,WOOD_FENCE) && !collides(-15.84f,y+.01f,3.0f));
    REQUIRE(!world_box_blocked((BlockBox){0,249,0,.3f,249.2f,.3f},0));

    px=-15.5f; py=y+.02f; pz=2.8f; yaw=0; pitch=-.29f; grounded=1;
    Hit selected=target();
    REQUIRE(selected.hit && selected.x==-16 && selected.y==y && selected.z==0);
    /* Full movement and jump physics must leave the player on this side. */
    pitch=-.33f;
    for (int i=0;i<120;++i) {
        if (i==22 || i==75) player_jump();
        game_step(.025f,0,1,0,0);
    }
    REQUIRE(pz>.75f && pz<1.10f && player_rules.health==MAX_HEALTH);
    px=-15.5f; py=y+.02f; pz=2.6f;
    aim_at((Vec3){-15.5f,y+.81f,.50f});
    selected=target();
    REQUIRE(selected.hit && player_use_gate(selected) && gate_decode(world_cell(-16,y,0),&gate) && gate.open);
    REQUIRE(!collides(-15.5f,y+.02f,.50f));
    REQUIRE(set_block(-16,y,-3,STONE));
    aim_at((Vec3){-15.5f,y+.02f,-2.7f});
    REQUIRE(target().hit && target().z==-3);
    aim_at((Vec3){-15.08f,y+.8f,.50f});
    REQUIRE(target().hit && target().z==0);
    pz=.50f;
    REQUIRE(player_use_gate(selected) && gate_decode(world_cell(-16,y,0),&gate) && gate.open);
    pz=2.6f; yaw=0;
    for (int i=0;i<48;++i) game_step(.025f,0,1,0,0);
    REQUIRE(pz<0.f);
    REQUIRE(gate_toggle(-16,y,0,SOUTH));

    /* A separate redstone fixture tests multiple live sources, a manual
       override, then both transitions after a new signal edge. */
    REQUIRE(set_block(-12,y-1,-5,REDSTONE_BLOCK) && gate_place(-12,y,-5,EAST));
    REQUIRE(gate_decode(world_cell(-12,y,-5),&gate) && gate.open && gate.powered && gate.facing==EAST);
    REQUIRE(set_block(-11,y,-5,REDSTONE_BLOCK) && gate_toggle(-12,y,-5,WEST));
    REQUIRE(set_block(-12,y-1,-5,STONE));
    REQUIRE(gate_decode(world_cell(-12,y,-5),&gate) && !gate.open && gate.powered);
    REQUIRE(set_block(-11,y,-5,AIR) && gate_decode(world_cell(-12,y,-5),&gate) && !gate.open && !gate.powered);
    const BlockEdit signal[]={{-12,y+1,-5,REDSTONE_BLOCK},{-11,y,2,NETHER_FENCE}};
    REQUIRE(world_set_blocks(signal,2) && gate_decode(world_cell(-12,y,-5),&gate) && gate.open && gate.powered);
    REQUIRE(set_block(-12,y+1,-5,AIR) && gate_decode(world_cell(-12,y,-5),&gate) && !gate.open && !gate.powered);
    REQUIRE(set_block(-12,y-1,-5,REDSTONE_BLOCK));
    px=-11.47f; py=y+.02f; pz=-4.5f; yaw=1.57079633f;
    REQUIRE(!collides(px,py,pz) && set_block(-12,y-1,-5,STONE) && collides(px,py,pz));
    /* The west side is farther away at this position and may not be used to
       walk through the closed gate. Walking back east must escape the overlap. */
    for (int i=0;i<4;++i) game_step(.025f,0,-1,0,0);
    REQUIRE(fabsf(px+11.47f)<.002f);
    for (int i=0;i<14;++i) game_step(.025f,0,1,0,0);
    REQUIRE(px>-10.90f && !collides(px,py,pz));

    actors_reset();
    ActorCare ready={0,0,ANIMAL_LOVE_TICKS,0,1};
    REQUIRE(actors_restore_with_care(OVERWORLD,(ActorRecord){{-15.5f,y+.01f,-1.25f},0,ACTOR_COW,10,0},ready));
    REQUIRE(actors_restore_with_care(OVERWORLD,(ActorRecord){{-15.5f,y+.01f,2.25f},0,ACTOR_COW,10,0},ready));
    px=-12.5f; py=y+.01f; pz=2.5f; selected_slot=0;
    for (int i=0;i<150;++i) actors_step(.05f);
    const ActorRecord *list=actors_in(OVERWORLD);
    REQUIRE(list[0].feet.z<0.f && list[1].feet.z>1.f && animal_count(ACTOR_COW,1)==0);
    REQUIRE(gate_toggle(-16,y,0,NORTH));
    for (int i=0;i<220 && !animal_count(ACTOR_COW,1);++i) actors_step(.05f);
    REQUIRE(animal_count(ACTOR_COW,1)==1);
    inventory_reset();
    inventory[0]=(ItemStack){STICK,12,0,0,0}; inventory[1]=(ItemStack){PLANKS,2,0,0,0};
    inventory[2]=(ItemStack){NETHER_BRICKS,6,0,0,0};
    int fence[]={-1,-1,-1,0,0,0,0,0,0},gate_recipe[]={0,1,0,0,1,0,-1,-1,-1};
    int nether[]={2,2,2,2,2,2,-1,-1,-1};
    REQUIRE(craft_preview(fence,3).count==2 && craft_take(fence,3));
    REQUIRE(craft_preview(gate_recipe,3).type==FENCE_GATE && craft_take(gate_recipe,3));
    REQUIRE(craft_preview(nether,3).count==6 && craft_take(nether,3));
    px=-12.5f; py=y+.01f; pz=5.4f; selected_slot=0;
    inventory[0]=(ItemStack){FENCE_GATE,1,0,0,0};
    aim_at((Vec3){-12.5f,y-.02f,7.5f});
    REQUIRE(place() && !inventory[0].count && gate_decode(world_cell(-13,y,7),&gate) && gate.facing==SOUTH);
    aim_at((Vec3){-12.5f,y+.65f,7.5f});
    REQUIRE(target().hit && block(target().x,target().y,target().z)==FENCE_GATE);
    REQUIRE(mine() && block(-13,y,7)==AIR && drops[0].item.type==FENCE_GATE);
    printf("ENCLOSURE TEST seam/height/aim/interaction/animals/power/recipe failures=%d\n",failures);
    return failures;
}

static int lead_selftest(void) {
    int failures=0;
    world_begin(911);
    generate();
    const int y=150;
    for (int x=-5;x<=10;++x) for (int z=-8;z<=12;++z) REQUIRE(set_block(x,y-1,z,STONE));
    const ActorRecord *list=actors_in(OVERWORLD);

    actors_reset(); horses_reset();
    memset(drops,0,sizeof dimension_drops[0]);
    px=.5f; py=y+.01f; pz=2.6f;
    ActorRecord cow={{.55f,y+.01f,.65f},0,ACTOR_COW,10,0};
    REQUIRE(actors_restore(OVERWORLD,cow));
    selected_slot=0; inventory[0]=(ItemStack){LEAD,2,0,0,0};
    aim_at((Vec3){.55f,y+.9f,.65f});
    REQUIRE(actors_use_lead(target()) && inventory[0].count==1 && actors_lead_at(OVERWORLD,0)->kind==LEAD_PLAYER);
    pz=7.2f;
    for (int i=0;i<28;++i) actors_step(.05f);
    REQUIRE(list[0].feet.z>cow.feet.z+.5f);
    REQUIRE(set_block(0,y,4,WOOD_FENCE));
    Hit knot={1,0,y,4,0,y,5};
    REQUIRE(lead_use_fence(knot) && actors_lead_at(OVERWORLD,0)->kind==LEAD_FENCE && inventory[0].count==1);
    HorseRecord horse={{2.5f,y+.01f,5.1f},0,0,0,0,EQUINE_DONKEY,0,20,20,64,45,0,0,0,{0},0};
    REQUIRE(horse_restore(OVERWORLD,horse));
    px=2.5f; pz=7.2f; aim_at((Vec3){horse.feet.x,y+1.f,horse.feet.z});
    REQUIRE(horse_use() && !inventory[0].count && horses_lead_at(OVERWORLD,0)->kind==LEAD_PLAYER);
    px=.5f; aim_at((Vec3){.5f,y+.8f,4.5f});
    REQUIRE(target().hit && lead_use_fence(target()) && horses_lead_at(OVERWORLD,0)->kind==LEAD_FENCE);
    for (int i=0;i<MAXDROP;++i) drops[i].item=(ItemStack){COAL,1,0,0,0};
    REQUIRE(lead_use_fence(knot) && actors_lead_at(OVERWORLD,0)->kind==LEAD_FENCE &&
        horses_lead_at(OVERWORLD,0)->kind==LEAD_FENCE);
    REQUIRE(set_block(0,y,4,AIR));
    lead_fence_removed(0,y,4);
    REQUIRE(actors_lead_at(OVERWORLD,0)->kind==LEAD_PENDING && horses_lead_at(OVERWORLD,0)->kind==LEAD_PENDING);
    drops[0].item=(ItemStack){0,0,0,0,0};
    actors_step(.05f);
    REQUIRE(drops[0].item.type==LEAD && actors_lead_at(OVERWORLD,0)->kind==LEAD_FREE);
    drops[1].item=(ItemStack){0,0,0,0,0};
    horses_step(.05f,0,0);
    REQUIRE(drops[1].item.type==LEAD && horses_lead_at(OVERWORLD,0)->kind==LEAD_FREE);
    actors_step(.05f); horses_step(.05f,0,0);
    REQUIRE(drops[0].item.count==1 && drops[1].item.count==1);

    actors_reset();
    ActorCare baby={ANIMAL_GROW_TICKS,0,0,0,1};
    ActorRecord calf={{3.5f,y+.01f,2.5f},0,ACTOR_COW,10,0};
    REQUIRE(actors_restore_with_lead(OVERWORLD,calf,baby,(LeadLink){LEAD_PLAYER,0,0,0}));
    Vec3 hit={calf.feet.x,calf.feet.y+.22f,calf.feet.z};
    REQUIRE(actors_hit_arrow(hit,12) && list[0].type==ACTOR_COW);
    drops[2].item=(ItemStack){0,0,0,0,0};
    REQUIRE(actors_hit_arrow(hit,12) && !list[0].type && drops[2].item.type==LEAD);
    REQUIRE(actors_restore_with_lead(OVERWORLD,calf,baby,(LeadLink){LEAD_PLAYER,0,0,0}));
    drops[3].item=(ItemStack){0,0,0,0,0};
    REQUIRE(world_select_dimension(END));
    lead_player_left(OVERWORLD);
    REQUIRE(actors_lead_at(OVERWORLD,0)->kind==LEAD_FREE && !drops[3].item.count &&
        dimension_drops[OVERWORLD][3].item.type==LEAD);
    REQUIRE(world_select_dimension(OVERWORLD));
    printf("LEAD TEST tether/snap/dimension failures=%d\n",failures);
    return failures;
}

static int brewing_selftest(void) {
    int failures=0;
    REQUIRE(world_begin_generated(58100,GENERATOR_FILTER_ARCHIVE) && world_select_dimension(NETHER));
    int wx=0,wy=0,wz=0;
    for (int x=-175;x<=175 && !wy;++x) for (int z=-175;z<=175 && !wy;++z) {
        int floor=nether_fortress_floor(x,z);
        if (floor>0 && block(x,floor+1,z)==FILTER_MAT) { wx=x; wy=floor+1; wz=z; }
    }
    REQUIRE(wy && block(wx,wy-1,wz)==ASH_SAND);
    REQUIRE(world_begin_generated(58100,GENERATOR_FARM));
    generate();
    REQUIRE(world_select_dimension(NETHER) && block(wx,wy,wz)==AIR && block(wx,wy-1,wz)==NETHER_BRICKS);
    REQUIRE(set_block(-10,150,10,SADDLE_CHEST));
    ChestView old;
    REQUIRE(chest_open((ChestPos){-10,150,10},&old)==CHEST_OK && chest_slot(&old,14).type==FILTER_MOSS &&
        chest_slot(&old,14).count==3);

    REQUIRE(world_select_dimension(OVERWORLD));
    const int y=150;
    inventory_reset(); inventory[0]=(ItemStack){STRIATED_GLASS,1,0,0,0};
    inventory[1]=(ItemStack){LIMESTONE_ITEM,1,0,0,0}; inventory[2]=(ItemStack){IRON_INGOT,1,0,0,0};
    inventory[3]=(ItemStack){LAMINATE_ITEM,1,0,0,0};
    int stand[]={0,1,2,3};
    REQUIRE(craft_preview(stand,2).type==BREWING_STAND && craft_take(stand,2));
    inventory[0]=(ItemStack){GLASS,3,0,0,0};
    int bottle[]={0,-1,0,-1,0,-1,-1,-1,-1};
    REQUIRE(craft_preview(bottle,3).type==GLASS_BOTTLE && craft_preview(bottle,3).count==3 && craft_take(bottle,3));
    inventory[0]=(ItemStack){SILT_GEL,1,0,0,0}; inventory[1]=(ItemStack){REED_GRIT,1,0,0,0};
    REQUIRE(!craft_preview((int[]){0,-1,-1,1},2).count);
    REQUIRE(set_block(1,y,0,FURNACE));
    inventory[0]=(ItemStack){SAND,1,0,0,0}; inventory[1]=(ItemStack){COAL,1,0,0,0};
    REQUIRE(furnace_deposit(1,y,0,FURNACE_INPUT,0) && furnace_deposit(1,y,0,FURNACE_FUEL,1));
    for (int i=0;i<10;++i) furnaces_advance(1.f);
    REQUIRE(furnace_at(1,y,0) && furnace_at(1,y,0)->slots[FURNACE_OUTPUT].type==GLASS);

    REQUIRE(set_block(0,y,0,WATER) && set_block(0,y-1,0,STONE));
    px=.5f; py=y+1.01f; pz=2.1f; aim_at((Vec3){.5f,y+.8f,.5f});
    inventory[0]=(ItemStack){GLASS_BOTTLE,1,0,0,0};
    REQUIRE(player_use_bottle() && inventory[0].type==WATER_BOTTLE && world_cell(0,y,0)==WATER);
    REQUIRE(set_block(0,y,2,BREWING_STAND));
    REQUIRE(brew_deposit(0,y,2,0,0));
    inventory[1]=(ItemStack){WATER_BOTTLE,1,0,0,0}; inventory[2]=inventory[1];
    inventory[3]=(ItemStack){LIMESTONE_ITEM,4,0,0,0}; inventory[4]=(ItemStack){FILTER_MOSS,1,0,0,0};
    REQUIRE(!brew_deposit(0,y,2,BREW_INGREDIENT,4));
    REQUIRE(brew_deposit(0,y,2,1,1) && brew_deposit(0,y,2,2,2) && brew_deposit(0,y,2,BREW_INGREDIENT,3));
    for (int i=0;i<5;++i) brewing_advance(1.f);
    const BrewingStand *b=brew_at(0,y,2);
    REQUIRE(b && b->progress==100 && b->slots[0].type==WATER_BOTTLE);
    if (!b) return failures;
    REQUIRE(world_select_dimension(NETHER) && !brew_at(0,y,2));
    brewing_advance(1.f);
    REQUIRE(world_select_dimension(OVERWORLD) && b->progress==100);
    brewing_advance(1.f);
    REQUIRE(b->slots[0].type==POTION_FIRE && b->slots[1].type==WATER_BOTTLE &&
        b->slots[2].type==WATER_BOTTLE && b->slots[BREW_INGREDIENT].count==3 && !b->progress && !b->active);
    for (int i=0;i<14;++i) brewing_advance(1.f);
    REQUIRE(b->slots[1].type==WATER_BOTTLE && b->active==BREW_RESERVE+1 && b->progress==280);
    brewing_advance(1.f);
    REQUIRE(b->slots[1].type==POTION_FIRE_LONG && b->slots[2].type==WATER_BOTTLE &&
        !b->slots[BREW_INGREDIENT].count && !b->progress);
    REQUIRE(!brew_dose(BREW_MIST,LIMESTONE_ITEM).output && !brew_dose(BREW_QUICK,MAGMA_CREAM).output);
    inventory[0]=(ItemStack){POTION_FIRE_LONG,1,0,0,0};
    REQUIRE(player_use_bottle() && inventory[0].type==GLASS_BOTTLE && player_effects.fire==FIRE_POTION_LONG_TICKS);
    REQUIRE(set_block(3,y,4,LAVA) && set_block(3,y-1,4,STONE));
    px=3.5f; py=y+.02f; pz=4.5f;
    for (int i=0;i<14;++i) game_step(.05f,0,0,0,0);
    REQUIRE(player_rules.health==MAX_HEALTH && player_effects.fire<FIRE_POTION_LONG_TICKS);
    player_take_damage(DAMAGE_MELEE,1); REQUIRE(player_rules.health==MAX_HEALTH-1);
    player_effects.fire=1;
    for (int i=0;i<14;++i) game_step(.05f,0,0,0,0);
    REQUIRE(!player_effects.fire && player_rules.health<MAX_HEALTH-1);
    inventory[0]=(ItemStack){POTION_FIRE,1,0,0,0};
    REQUIRE(player_use_bottle() && player_effects.fire==FIRE_POTION_TICKS);
    inventory[0]=(ItemStack){MILK_BUCKET,1,0,0,0};
    REQUIRE(actors_use_livestock(target()) && !player_effects.fire);

    REQUIRE(world_select_dimension(NETHER));

    /* Player-grown remedies do not leak into the opposite dimension's stations. */
    REQUIRE(!brew_at(0,y,2) && world_select_dimension(OVERWORLD));
    px=.5f; py=y+.02f; pz=4.5f; aim_at((Vec3){.5f,y+.45f,2.5f});
    inventory[0]=(ItemStack){WOOD_PICKAXE,1,0,0,0};
    REQUIRE(target().hit && target().z==2);
    for (int i=0;i<MAXDROP;++i) drops[i].item=(ItemStack){COAL,1,0,0,0};
    REQUIRE(mine() && brew_at(0,y,2) && block(0,y,2)==BREWING_STAND);
    memset(drops,0,sizeof dimension_drops[0]);
    REQUIRE(mine() && !brew_at(0,y,2) && block(0,y,2)==AIR && drops[2].item.type==POTION_FIRE_LONG);
    REQUIRE(set_block(0,y,2,BREWING_STAND));
    inventory[0]=(ItemStack){WATER_BOTTLE,1,0,0,0};
    REQUIRE(brew_deposit(0,y,2,0,0) && set_block(0,y,2,STONE));
    REQUIRE(!brew_at(0,y,2) && !brewing_count_in(OVERWORLD));
    printf("FILTER TEST recovery/glass/one-bottle-doses/fire/magma/break failures=%d\n",failures);
    return failures;
}

static int armor_selftest(void) {
    int failures=0, chain_drop=0, natural=0;
    world_begin(19667);
    generate();
    const int materials[]={LEATHER,0,IRON_INGOT,GOLD_INGOT,DIAMOND};
    const int scores[]={7,12,15,11,20};
    const int recipes[ARMOR_SLOTS][9]={
        {0,0,0,0,-1,0,-1,-1,-1}, {0,-1,0,0,0,0,0,0,0},
        {0,0,0,0,-1,0,0,-1,0}, {-1,-1,-1,0,-1,0,0,-1,0}
    };
    for (int m=0;m<ARMOR_MATERIALS;++m) {
        inventory_reset();
        inventory[0]=(ItemStack){(uint16_t)materials[m],(uint8_t)(materials[m] ? 24 : 0),0,0,0};
        for (int i=0;i<ARMOR_SLOTS;++i) {
            int type=armor_material_item((ArmorMaterial)m,i);
            REQUIRE(armor_slot(type)==i && item_stack_valid((ItemStack){(uint16_t)type,1,0,0,0}) &&
                !item_stack_valid((ItemStack){(uint16_t)type,2,0,0,0}));
            if (materials[m]) {
                REQUIRE(craft_preview(recipes[i],3).type==type && craft_take(recipes[i],3));
                int source=-1;
                for (int j=0;j<SLOT_COUNT;++j) if (inventory[j].type==type) { source=j; break; }
                REQUIRE(source>=0 && !armor_equip(source,(i+1)%ARMOR_SLOTS) && armor_equip(source,i));
            } else REQUIRE(armor_restore(i,(ItemStack){(uint16_t)type,1,0,0,0}));
        }
        REQUIRE(armor_points()==scores[m] && !inventory[0].count);
    }
    REQUIRE(!armor_restore(ARMOR_HEAD,(ItemStack){DIAMOND_CHESTPLATE,1,0,0,0}) &&
        !armor_restore(ARMOR_HEAD,(ItemStack){DIAMOND_HELMET,1,363,0,0}));
    for (int i=0;i<5;++i) player_take_damage(DAMAGE_MELEE,1);
    REQUIRE(player_rules.health==19 && !player_rules.health_debt && armor_worn(ARMOR_HEAD).damage==5);
    player_take_damage(DAMAGE_PROJECTILE,10);
    REQUIRE(player_rules.health==17 && armor_worn(ARMOR_HEAD).damage==7);
    player_take_damage(DAMAGE_EXPLOSION,8);
    REQUIRE(player_rules.health==16 && player_rules.health_debt==15 && armor_worn(ARMOR_HEAD).damage==9);
    player_take_damage(DAMAGE_FALL,4);
    player_take_damage(DAMAGE_DROWNING,2);
    player_take_damage(DAMAGE_BURNING,2);
    REQUIRE(player_rules.health==8 && player_rules.health_debt==15 && armor_worn(ARMOR_HEAD).damage==9);
    player_effects.fire=200;
    player_take_damage(DAMAGE_DIRECT_FIRE,4);
    player_take_damage(DAMAGE_BURNING,2);
    REQUIRE(player_rules.health==8 && armor_worn(ARMOR_HEAD).damage==9);
    player_effects.fire=0;
    player_take_damage(DAMAGE_DIRECT_FIRE,4);
    REQUIRE(player_rules.health==7 && player_rules.health_debt==10 && armor_worn(ARMOR_HEAD).damage==10);
    player_take_damage(DAMAGE_STARVATION,40);
    player_take_damage(DAMAGE_POISON,40);
    REQUIRE(player_rules.health==1 && !player_rules.health_debt && armor_worn(ARMOR_HEAD).damage==10);
    REQUIRE(death_set_keep_inventory(1));
    player_take_damage(DAMAGE_VOID,MAX_HEALTH);
    REQUIRE(!player_rules.health && !player_rules.health_debt && armor_worn(ARMOR_HEAD).damage==10);
    player_respawn();
    REQUIRE(player_rules.health==MAX_HEALTH && armor_points()==20);
    REQUIRE(death_set_keep_inventory(0));
    REQUIRE(armor_restore(ARMOR_FEET,(ItemStack){DIAMOND_BOOTS,1,428,0,0}));
    player_take_damage(DAMAGE_PROJECTILE,1);
    REQUIRE(armor_points()==17 && !armor_worn(ARMOR_FEET).count && player_rules.health_debt==5);
    player_reset_rules(CREATIVE);
    player_take_damage(DAMAGE_EXPLOSION,100);
    REQUIRE(player_rules.health==MAX_HEALTH && armor_worn(ARMOR_HEAD).damage==11);
    player_reset_rules(SURVIVAL);

    for (int i=0;i<SLOT_COUNT;++i) inventory[i]=(ItemStack){DIRT,64,0,0,0};
    REQUIRE(!armor_unequip(ARMOR_HEAD) && armor_worn(ARMOR_HEAD).type==DIAMOND_HELMET);
    inventory[6]=(ItemStack){IRON_HELMET,1,141,0,0};
    REQUIRE(armor_equip(6,ARMOR_HEAD) && armor_worn(ARMOR_HEAD).damage==141 &&
        inventory[6].type==DIAMOND_HELMET && inventory[6].damage==11);
    inventory[6]=(ItemStack){IRON_INGOT,24,0,0,0};
    const int filled[]={6,6,6,6,-1,6,-1,-1,-1};
    REQUIRE(!craft_take(filled,3) && inventory[6].count==24);
    inventory[10]=(ItemStack){CHAIN_HELMET,1,150,0,0};
    inventory[11]=(ItemStack){CHAIN_HELMET,1,149,0,0};
    const int repair[]={10,11,-1,-1};
    REQUIRE(craft_preview(repair,2).damage==126 && craft_take(repair,2) &&
        inventory[10].type==CHAIN_HELMET && inventory[10].damage==126 && !inventory[11].count);
    REQUIRE(armor_unequip(ARMOR_HEAD) && inventory[11].type==IRON_HELMET && inventory[11].damage==141);

    /* Natural spawn in the normal world sampler. Kept separate from arena edits. */
    actors_reset();
    player_rules.mode=CREATIVE;
    for (int sample=0;sample<4 && !natural;++sample) {
        px=18.5f+100.f*sample; pz=9.5f-23.f*sample;
        py=terrain_height((int)px,(int)pz)+1.01f;
        player_rules.day_tick=12500+1800*sample;
        for (int i=0;i<1000 && !natural;++i) {
            actors_step(.1f);
            const ActorRecord *a=actors_in(OVERWORLD);
            for (int j=0;j<ACTOR_CAPACITY;++j) if (actors_can_wear_armor(a[j].type) && a[j].traits)
                ++natural;
        }
    }
    REQUIRE(natural>0);

    actors_reset();
    player_reset_rules(SURVIVAL);
    player_rules.day_tick=16000;
    REQUIRE(armor_restore(ARMOR_BODY,(ItemStack){IRON_CHESTPLATE,1,17,0,0}));
    REQUIRE(armor_restore(ARMOR_LEGS,(ItemStack){0,0,0,0,0}));
    const int height=147;
    for (int x=-4;x<=4;++x) for (int z=-10;z<=4;++z) {
        REQUIRE(set_block(x,height-1,z,STONE));
        for (int h=height;h<=height+2;++h) REQUIRE(set_block(x,h,z,AIR));
    }
    px=.5f; py=height+.01f; pz=.5f;
    const Vec3 at={.5f,height+.01f,-.5f};
    uint8_t kit=armor_make_kit(ARMOR_CHAIN,15);
    REQUIRE(!armor_kit_valid(0xf1) && !armor_kit_valid(0x20) && armor_kit_points(kit)==12);
    REQUIRE(actors_restore(OVERWORLD,(ActorRecord){at,0,ACTOR_ZOMBIE,20,kit}));
    actors_step(.05f);
    REQUIRE(player_rules.health==18 && player_rules.health_debt==7 && armor_worn(ARMOR_BODY).damage==18);
    Vec3 aim={at.x,at.y+1.f,at.z};
    REQUIRE(actors_hit_arrow(aim,10) && actors_in(OVERWORLD)[0].health==15 &&
        actors_armor_state(OVERWORLD,0).health_debt==5);
    memset(drops,0,sizeof dimension_drops[0]);
    for (int i=0;i<64 && !chain_drop;++i) {
        REQUIRE(actors_hit_arrow(aim,200));
        for (int s=0;s<MAXDROP;++s) if (drops[s].item.count && drops[s].item.type>=CHAIN_HELMET &&
            drops[s].item.type<=CHAIN_BOOTS && drops[s].item.damage>0 && item_stack_valid(drops[s].item)) ++chain_drop;
        if (!chain_drop) {
            memset(drops,0,sizeof dimension_drops[0]);
            REQUIRE(actors_restore(OVERWORLD,(ActorRecord){at,0,ACTOR_ZOMBIE,20,kit}));
        }
    }
    REQUIRE(chain_drop>0);

    /* Real skeleton projectile reaches the player without a test damage call. */
    actors_reset(); player_reset_rules(SURVIVAL); player_rules.day_tick=16000;
    REQUIRE(actors_restore(OVERWORLD,(ActorRecord){{.5f,py,-7.5f},0,ACTOR_SKELETON,20,0}));
    for (int i=0;i<65 && !player_rules.health_debt;++i) actors_step(.025f);
    REQUIRE(player_rules.health==18 && player_rules.health_debt==7 && armor_worn(ARMOR_BODY).damage==19);

    actors_reset(); player_rules.day_tick=10;
    REQUIRE(actors_restore_snapshot(OVERWORLD,(ActorRecord){at,0,ACTOR_SKELETON,20,
        armor_make_kit(ARMOR_LEATHER,1)},(ActorCare){0,0,0,0,0},(LeadLink){LEAD_FREE,0,0,0},
        (ActorArmorState){.helmet_wear=54}));
    player_rules.mode=CREATIVE;
    for (int i=0;i<20;++i) actors_step(.06f);
    REQUIRE(actors_in(OVERWORLD)[0].health==20 && !actors_in(OVERWORLD)[0].traits &&
        !actors_armor_state(OVERWORLD,0).helmet_wear);
    for (int i=0;i<20;++i) actors_step(.06f);
    REQUIRE(actors_in(OVERWORLD)[0].health==19);
    printf("ARMOR TEST recipes/repair/swap/full-bag/fractions/wear/damage-categories/real-arrows/armored-mobs=%d/loot=%d failures=%d\n",
        natural,chain_drop,failures);
    return failures;
}

static int count_visible_drop(const Drop *d,void *ctx) {
    if (drop_valid(*d)) ++*(int *)ctx;
    return 1;
}
static int death_drop_selftest(void) {
    int failures=0,visible=0;
    world_begin(910545);
    generate();
    REQUIRE(!death_keep_inventory());
    const int y=149;
    for (int x=-19;x<=-12;++x) for (int z=-19;z<=-12;++z)
        REQUIRE(set_block(x,y-1,z,STONE) && set_block(x,y,z,AIR) && set_block(x,y+1,z,AIR));
    const Vec3 death_at={-16.015625f,y+.01f,-16.015625f};
    px=death_at.x; py=death_at.y; pz=death_at.z;
    for (int i=0;i<MAXDROP;++i) drops[i]=make_drop(800.5f,y+.2f,800.5f,(ItemStack){STONE,64,0,0,0});
    for (int i=0;i<SLOT_COUNT;++i) inventory[i]=(ItemStack){(uint16_t)(i==0 ? IRON_SWORD : DIRT),
        (uint8_t)(i==0 ? 1 : 4),(uint16_t)(i==0 ? 41 : 0),0,0};
    for (int i=0;i<ARMOR_SLOTS;++i)
        REQUIRE(armor_restore(i,(ItemStack){(uint16_t)armor_material_item(ARMOR_DIAMOND,i),1,(uint16_t)(10+i),0,0}));
    player_take_damage(DAMAGE_FALL,200);
    REQUIRE(player_rules.health==0 && !inventory[0].count && armor_points()==0 &&
        death_drop_count(OVERWORLD)==SLOT_COUNT+ARMOR_SLOTS);
    REQUIRE(drops[0].item.count==64 && drops[MAXDROP-1].item.count==64);
    REQUIRE(player_respawn()==RESPAWN_OK && player_rules.health==MAX_HEALTH && !inventory[0].count &&
        death_drop_count(OVERWORLD)==SLOT_COUNT+ARMOR_SLOTS);
    px=death_at.x; py=death_at.y; pz=death_at.z;
    drops_visible(-2,-2,count_visible_drop,&visible);
    REQUIRE(visible==SLOT_COUNT+ARMOR_SLOTS);
    for (int i=0;i<DEATH_DROP_PICKUP_TICKS-1;++i) drops_step(.05f,1);
    REQUIRE(!inventory[0].count && death_drop_count(OVERWORLD)==SLOT_COUNT+ARMOR_SLOTS);
    px=3000; pz=3000;
    REQUIRE(!drops_step(.05f,DROP_EXPIRE_TICKS) && death_drop_count(OVERWORLD)==SLOT_COUNT+ARMOR_SLOTS &&
        drops[0].age==0);
    REQUIRE(world_select_dimension(NETHER));
    px=death_at.x; pz=death_at.z;
    REQUIRE(!drops_step(.05f,DROP_EXPIRE_TICKS) && death_drop_count(OVERWORLD)==SLOT_COUNT+ARMOR_SLOTS);
    REQUIRE(world_select_dimension(OVERWORLD));
    REQUIRE(drops_step(.05f,1) && !death_drop_count(OVERWORLD));
    visible=0;
    drops_visible(-2,-2,count_visible_drop,&visible);
    REQUIRE(!visible);
    int dirt=0,sword=0,pieces=0;
    for (int i=0;i<SLOT_COUNT;++i) {
        if (inventory[i].type==DIRT) dirt+=inventory[i].count;
        else if (inventory[i].type==IRON_SWORD && inventory[i].damage==41) ++sword;
        else if (armor_slot(inventory[i].type)>=0 && inventory[i].damage==10+armor_slot(inventory[i].type)) ++pieces;
    }
    REQUIRE(dirt==4*(SLOT_COUNT-1) && sword==1 && pieces==ARMOR_SLOTS);
    REQUIRE(!armor_points());

    /* Pick up only the free capacity and leave the rest in the world. */
    for (int i=0;i<SLOT_COUNT;++i) inventory[i]=(ItemStack){STONE,64,0,0,0};
    inventory[0]=(ItemStack){DIRT,63,0,0,0};
    drops[0]=make_drop(px,py+.14f,pz,(ItemStack){DIRT,7,0,0,0});
    REQUIRE(!drops_step(.05f,DROP_PICKUP_TICKS-1) && inventory[0].count==63);
    REQUIRE(drops_step(.05f,1) && inventory[0].count==64 && drops[0].item.count==6);
    drops[0].age=DROP_EXPIRE_TICKS-1;
    REQUIRE(!drops_step(.05f,1) && !drops[0].item.count);

    /* Actual lava damage and cleanup; the rest of the nearby terrain is dry. */
    REQUIRE(set_block(-14,y,-14,LAVA));
    px=-13.5f; py=y+.01f; pz=-13.5f; vy=0;
    player_rules.health=2;
    memset(inventory,0,sizeof inventory);
    inventory[0]=(ItemStack){GOLD_INGOT,1,0,0,0};
    for (int i=0;i<12 && player_rules.health;++i) game_step(.05f,0,0,0,0);
    REQUIRE(!player_rules.health && !inventory[0].count && death_drop_count(OVERWORLD)==1);
    REQUIRE(player_respawn()==RESPAWN_OK);
    px=-13.5f; py=y+.01f; pz=-13.5f;
    for (int i=0;i<10;++i) drops_step(.05f,1);
    REQUIRE(!death_drop_count(OVERWORLD) && !inventory[0].count);

    REQUIRE(world_select_dimension(END));
    px=500.5f; py=.03f; pz=500.5f; vy=-2.f;
    inventory[0]=(ItemStack){DIAMOND,1,0,0,0};
    game_step(.05f,0,0,0,0);
    REQUIRE(!player_rules.health && !death_drop_count(END) && !inventory[0].count);
    REQUIRE(player_respawn()==RESPAWN_OK && world_dimension()==OVERWORLD);
    REQUIRE(death_set_keep_inventory(1));
    inventory[0]=(ItemStack){DIAMOND,2,0,0,0};
    player_take_damage(DAMAGE_MELEE,100);
    REQUIRE(!player_rules.health && player_respawn()==RESPAWN_OK && inventory[0].count==2 &&
        !death_drop_count(OVERWORLD));

    /* Exhaust the persistence cap. Death and respawn remain lossless. */
    REQUIRE(death_set_keep_inventory(0));
    Drop remote=make_drop(4000.5f,149.25f,4000.5f,(ItemStack){DIRT,1,0,0,0});
    for (int i=0;i<DEATH_DROP_FILE_LIMIT;++i) REQUIRE(death_drop_restore(OVERWORLD,remote));
    REQUIRE(death_drop_count(OVERWORLD)==DEATH_DROP_FILE_LIMIT && !death_drop_restore(OVERWORLD,remote));
    player_take_damage(DAMAGE_PROJECTILE,100);
    REQUIRE(!player_rules.health && inventory[0].type==DIAMOND && inventory[0].count==2);
    REQUIRE(player_respawn()==RESPAWN_RETAINED && player_rules.health==MAX_HEALTH &&
        inventory[0].type==DIAMOND && inventory[0].count==2);
    px=0; py=100; pz=0;
    REQUIRE(!drops_step(.05f,1) && death_drop_count(OVERWORLD)==DEATH_DROP_FILE_LIMIT);
    drops_reset();
    printf("DEATH DROPS TEST full-pool/reclaim/age/chunk/dimension/partial/lava/void/keep/backpressure failures=%d\n",failures);
    return failures;
}

static int experience_selftest(void) {
    int failures=0;
    world_begin(11531);
    generate();
    uint32_t stream=experience_rng();
    REQUIRE(!experience_restore_player(UINT32_MAX,stream) && !experience_restore_player(1,0));
    const uint32_t cases[][4]={
        {0,0,0,17},{16,0,16,17},{17,1,0,17},{271,15,16,17},{272,16,0,20},{291,16,19,20},
        {292,17,0,23},{824,29,58,59},{825,30,0,62},{886,30,61,62},{887,31,0,69}
    };
    for (unsigned i=0;i<sizeof cases/sizeof *cases;++i) {
        REQUIRE(experience_restore_player(cases[i][0],stream));
        XPProgress p=experience_progress();
        REQUIRE(p.points==cases[i][0] && p.level==cases[i][1] && p.within==cases[i][2] && p.next==cases[i][3]);
    }
    REQUIRE(experience_restore_player(XP_MAX_POINTS,stream));
    XPProgress max=experience_progress();
    REQUIRE(max.level>20000 && max.within<max.next);
    REQUIRE(experience_restore_player(0,stream));
    const int y=150;
    for (int x=-5;x<=8;++x) for (int z=-5;z<=6;++z)
        REQUIRE(set_block(x,y-1,z,STONE) && set_block(x,y,z,AIR) && set_block(x,y+1,z,AIR));
    px=.5f; py=y+.01f; pz=.5f;
    REQUIRE(experience_restore_orb(OVERWORLD,(XPOrb){{2.70f,y+.35f,.5f},{0,0,0},20,0}));
    REQUIRE(!experience_restore_orb(OVERWORLD,(XPOrb){{2.70f,y+.35f,.5f},{25,0,0},20,0}));
    for (int i=0;i<240 && !experience_points();++i) experience_step(.05f,1);
    REQUIRE(experience_points()==20 && experience_progress().level==1 && experience_progress().within==3 &&
        !orb_points(OVERWORLD));
    Vec3 near={.5f,y+.6f,.5f};
    REQUIRE(experience_restore_orb(OVERWORLD,(XPOrb){near,{0,0,0},1,0}));
    REQUIRE(experience_restore_orb(OVERWORLD,(XPOrb){near,{0,0,0},1,0}));
    experience_step(.05f,1); experience_step(.05f,1);
    REQUIRE(experience_points()==21 && orb_points(OVERWORLD)==1);
    experience_step(.05f,1); experience_step(.05f,1);
    REQUIRE(experience_points()==22 && !orb_points(OVERWORLD));

    REQUIRE(experience_restore_orb(OVERWORLD,(XPOrb){{5.5f,y+.35f,.5f},{0,0,0},7,5999}));
    px=4000.5f;
    experience_step(.05f,1);
    REQUIRE(orb_points(OVERWORLD)==7);
    px=.5f;
    REQUIRE(world_select_dimension(NETHER));
    experience_step(.05f,1);
    REQUIRE(orb_points(OVERWORLD)==7 && !orb_points(NETHER));
    REQUIRE(world_select_dimension(OVERWORLD));
    experience_step(.05f,1);
    REQUIRE(!orb_points(OVERWORLD));

    REQUIRE(set_block(0,y,0,DIAMOND_ORE));
    pz=2.5f;
    aim_at((Vec3){.5f,y+.5f,.5f});
    inventory[0]=(ItemStack){WOOD_PICKAXE,1,0,0,0};
    selected_slot=0;
    REQUIRE(mine() && !orb_points(OVERWORLD) && block(0,y,0)==AIR);
    REQUIRE(set_block(0,y,0,DIAMOND_ORE));
    inventory[0]=(ItemStack){IRON_PICKAXE,1,0,0,0};
    for (int i=0;i<MAXDROP;++i) drops[i]=make_drop(px,py,pz,(ItemStack){DIRT,1,0,0,0});
    REQUIRE(mine() && block(0,y,0)==DIAMOND_ORE && !orb_points(OVERWORLD));
    drops[0].item=(ItemStack){0,0,0,0,0};
    REQUIRE(mine() && block(0,y,0)==AIR && orb_points(OVERWORLD)>=3 && orb_points(OVERWORLD)<=7);
    const unsigned ore=orb_points(OVERWORLD);

    REQUIRE(set_block(3,y,3,FURNACE));
    inventory[0]=(ItemStack){GOLD_ORE,3,0,0,0}; inventory[1]=(ItemStack){COAL,1,0,0,0};
    REQUIRE(furnace_deposit(3,y,3,FURNACE_INPUT,0) && furnace_deposit(3,y,3,FURNACE_FUEL,1));
    for (int i=0;i<30;++i) furnaces_advance(1);
    REQUIRE(furnace_at(3,y,3) && furnace_at(3,y,3)->slots[FURNACE_OUTPUT].count==3 && orb_points(OVERWORLD)==ore);
    for (int i=0;i<SLOT_COUNT;++i) inventory[i]=(ItemStack){DIRT,64,0,0,0};
    REQUIRE(!furnace_withdraw(3,y,3,FURNACE_OUTPUT) && orb_points(OVERWORLD)==ore);
    inventory[0]=(ItemStack){0,0,0,0,0};
    REQUIRE(furnace_withdraw(3,y,3,FURNACE_OUTPUT) && orb_points(OVERWORLD)==ore+3 &&
        !furnace_withdraw(3,y,3,FURNACE_OUTPUT) && inventory[0].count==3);

    experience_reset();
    REQUIRE(experience_restore_player(887,stream));
    player_take_damage(DAMAGE_FALL,200);
    REQUIRE(!experience_points() && orb_points(OVERWORLD)==100);
    REQUIRE(player_respawn()==RESPAWN_OK && !experience_points() && orb_points(OVERWORLD)==100);
    REQUIRE(death_set_keep_inventory(1) && experience_restore_player(272,stream));
    player_take_damage(DAMAGE_MELEE,200);
    REQUIRE(player_respawn()==RESPAWN_OK && experience_points()==272 && orb_points(OVERWORLD)==100);
    REQUIRE(death_set_keep_inventory(0) && world_select_dimension(END));
    player_take_damage(DAMAGE_VOID,200);
    REQUIRE(!experience_points() && !orb_points(END) && orb_points(OVERWORLD)==100);
    REQUIRE(player_respawn()==RESPAWN_OK);
    player_reset_rules(CREATIVE);
    experience_reward(near,999);
    REQUIRE(!experience_points() && orb_points(OVERWORLD)==100);
    experience_reset();
    player_reset_rules(SURVIVAL);
    XPOrb distant={{2000.5f,150.5f,2000.5f},{0,0,0},1,300};
    for (int i=0;i<XP_ORB_LIMIT;++i) REQUIRE(experience_restore_orb(OVERWORLD,distant));
    REQUIRE(!experience_restore_orb(OVERWORLD,distant));
    experience_reward(near,5);
    REQUIRE(experience_points()==5 && orb_points(OVERWORLD)==XP_ORB_LIMIT);
    experience_reward(distant.point,17);
    REQUIRE(experience_points()==5 && orb_points(OVERWORLD)==XP_ORB_LIMIT+17);
    experience_reset();
    printf("EXPERIENCE TEST curve/orbs/cooldown/expiry/ore/furnace/death failures=%d\n",failures);
    return failures;
}

static int enchanting_selftest(void) {
    int failures=0;
    REQUIRE(world_begin_generated(602013,GENERATOR_BOOKS));
    generate();
    int cane_x=0,cane_z=0,cane_y=0;
    int shore=world_sea_level();
    for (int cx=-11;cx<=11 && !cane_y;++cx) for (int cz=-11;cz<=11 && !cane_y;++cz) {
        ChunkView v=chunk_view(cx,cz);
        for (int x=0;x<CHUNK_SIZE && !cane_y;++x) for (int z=0;z<CHUNK_SIZE && !cane_y;++z) {
            if (v.cells[(x*CHUNK_SIZE+z)*WY+shore+1]==SUGAR_CANE) {
                cane_x=cx*CHUNK_SIZE+x; cane_z=cz*CHUNK_SIZE+z; cane_y=shore+1;
            }
        }
    }
    REQUIRE(cane_y==shore+1 && !water_strength(SUGAR_CANE) && !solid(SUGAR_CANE));
    REQUIRE(world_begin_generated(602013,GENERATOR_FILTER_ARCHIVE));
    generate();
    REQUIRE(block(cane_x,cane_y,cane_z)!=SUGAR_CANE);
    int y=150;
    for (int x=-8;x<=7;++x) for (int z=-8;z<=7;++z)
        REQUIRE(set_block(x,y-1,z,STONE) && set_block(x,y,z,AIR) && set_block(x,y+1,z,AIR));
    px=.5f;py=y+.01f;pz=4.5f;
    REQUIRE(set_block(-4,y,-1,SAND));
    inventory[0]=(ItemStack){SUGAR_CANE,4,0,0,0}; selected_slot=0;
    Hit soil={1,-4,y,-1,-4,y+1,-1};
    REQUIRE(farm_use(soil) && inventory[0].count==4 && block(-4,y+1,-1)==AIR);
    REQUIRE(set_block(-5,y,-1,WATER));
    REQUIRE(farm_use(soil) && inventory[0].count==3 && block(-4,y+1,-1)==SUGAR_CANE);
    for (int i=0;i<3400;++i) farm_step(.1f);
    REQUIRE(block(-4,y+3,-1)==SUGAR_CANE && block(-4,y+4,-1)==AIR);
    REQUIRE(farm_break((Hit){1,-4,y+2,-1,0,0,0}) && block(-4,y+1,-1)==SUGAR_CANE &&
        block(-4,y+2,-1)==AIR && drops[0].item.type==SUGAR_CANE && drops[0].item.count==2);
    const int paper[]={0,0,0,-1,-1,-1,-1,-1,-1};
    REQUIRE(craft_take(paper,3) && inventory[0].type==PAPER && inventory[0].count==3);
    inventory[2]=(ItemStack){LEATHER,1,0,0,0};
    REQUIRE(craft_take((int[]){0,0,0,2},2) && inventory[0].type==BOOK);
    inventory[1]=(ItemStack){DIAMOND,2,0,0,0}; inventory[2]=(ItemStack){OBSIDIAN,4,0,0,0};
    const int table[]={-1,0,-1,1,2,1,2,2,2};
    REQUIRE(!craft_preview(table,3).count);
    inventory[0]=(ItemStack){LAMINATE_ITEM,1,0,0,0};
    inventory[1]=(ItemStack){IRON_INGOT,1,0,0,0};
    inventory[2]=(ItemStack){SLATE_ITEM,1,0,0,0};
    inventory[3]=(ItemStack){PLANKS,1,0,0,0};
    REQUIRE(craft_take((int[]){0,1,2,3},2) && inventory[0].type==ENCHANTING_TABLE);
    REQUIRE(set_block(0,y,0,ENCHANTING_TABLE));
    inventory[0]=(ItemStack){PLANKS,6,0,0,0}; inventory[1]=(ItemStack){BOOK,3,0,0,0};
    REQUIRE(craft_take((int[]){0,0,0,1,1,1,0,0,0},3) && inventory[0].type==BOOKSHELF);
    inventory[0]=(ItemStack){STONE_PICKAXE,1,22,enchant_code(ENCH_SILK_TOUCH,1),0};
    inventory[1]=(ItemStack){0};
    REQUIRE(experience_restore_player(897,0x734228a2u));
    REQUIRE(calibration_preview(-1,CALIBRATE_CORE)==CALIBRATION_SELECT &&
        calibration_preview(0,CALIBRATE_LAYER)==CALIBRATION_MISSING &&
        !calibration_apply(0,y,0,0,CALIBRATE_LAYER));
    inventory[1]=(ItemStack){SLATE_ITEM,2,0,0,0};
    inventory[2]=(ItemStack){LIMESTONE_ITEM,1,0,0,0};
    inventory[3]=(ItemStack){LAMINATE_ITEM,2,0,0,0};
    REQUIRE(calibration_preview(0,CALIBRATE_LAYER)==CALIBRATION_READY &&
        !calibration_apply(1,y,0,0,CALIBRATE_LAYER));
    REQUIRE(set_block(0,y,2,SLATE_ROCK));
    aim_at((Vec3){.5f,y+.55f,2.5f});
    REQUIRE(mine_hold_milliseconds()==1030);
    REQUIRE(calibration_apply(0,y,0,0,CALIBRATE_LAYER) && inventory[1].count==1 &&
        enchant_level(inventory[0].enchants,ENCH_LAYER_CLEAVER) && mine_hold_milliseconds()==772);
    REQUIRE(calibration_preview(0,CALIBRATE_LAYER)==CALIBRATION_UNNEEDED &&
        !calibration_apply(0,y,0,0,CALIBRATE_LAYER) && inventory[1].count==1);
    REQUIRE(calibration_apply(0,y,0,0,CALIBRATE_CORE) && !inventory[2].count &&
        !enchant_level(inventory[0].enchants,ENCH_LAYER_CLEAVER) &&
        enchant_level(inventory[0].enchants,ENCH_CORE_CLAMP) && mine_hold_milliseconds()==1030);
    REQUIRE(mine() && world_cell(0,y,2)==AIR && drops[1].item.type==SLATE_ITEM &&
        drops[1].item.count==1 && inventory[0].damage==23); /* Placed stone is not a core. */
    drops[1]=(Drop){0};
    REQUIRE(calibration_apply(0,y,0,0,CALIBRATE_LINER) && inventory[0].damage==7 &&
        inventory[3].count==1 && enchant_level(inventory[0].enchants,ENCH_SILK_TOUCH) &&
        enchant_level(inventory[0].enchants,ENCH_CORE_CLAMP));
    REQUIRE(calibration_apply(0,y,0,0,CALIBRATE_LINER) && !inventory[0].damage &&
        !inventory[3].count && calibration_preview(0,CALIBRATE_LINER)==CALIBRATION_UNNEEDED &&
        experience_points()==897 && experience_rng()==0x734228a2u);
    char description[130]; enchant_describe(description,sizeof description,inventory[0].enchants);
    REQUIRE(*description && !item_stack_valid((ItemStack){DIRT,1,0,inventory[0].enchants,0}) &&
        !item_stack_valid((ItemStack){ENCHANTED_BOOK,1,0,0,0}));
    REQUIRE(!item_stack_valid((ItemStack){IRON_SWORD,1,0,enchant_code(ENCH_SMITE,2)|enchant_code(ENCH_SHARPNESS,2),0}));
    const uint64_t layer=enchant_code(ENCH_LAYER_CLEAVER,1),core=enchant_code(ENCH_CORE_CLAMP,1);
    REQUIRE(!item_stack_valid((ItemStack){STONE_PICKAXE,1,0,layer|core,0}) &&
        !item_stack_valid((ItemStack){ENCHANTED_BOOK,1,0,layer,0}) &&
        !item_stack_valid((ItemStack){WOOD_PICKAXE,1,0,layer,0}));

    const uint64_t lure=enchant_code(ENCH_LURE,2),luck=enchant_code(ENCH_LUCK_OF_THE_SEA,1);
    inventory[0]=(ItemStack){FISHING_ROD,1,7,0,0};
    int rolled_fishing=0;
    for (uint32_t seed=1;seed<=64 && !rolled_fishing;++seed) {
        uint32_t random=seed;
        uint64_t bits=enchant_fishing_treasure(FISHING_ROD,&random);
        REQUIRE(!(bits&(layer|core)) && item_stack_valid((ItemStack){FISHING_ROD,1,0,bits,0}));
        rolled_fishing=enchant_level(bits,ENCH_LURE) || enchant_level(bits,ENCH_LUCK_OF_THE_SEA);
    }
    REQUIRE(rolled_fishing);
    inventory[1]=(ItemStack){ENCHANTED_BOOK,1,0,lure|luck,0};
    AnvilPlan rod;
    REQUIRE(anvil_preview(0,1,NULL,&rod)==ANVIL_READY && rod.item.type==FISHING_ROD &&
        rod.item.damage==7 && rod.item.enchants==(lure|luck) && rod.materials==1);
    inventory[0]=(ItemStack){BOW,1,7,0,0};
    REQUIRE(anvil_preview(0,1,NULL,&rod)==ANVIL_INCOMPATIBLE);

    inventory_reset();
    armor_restore(ARMOR_FEET,(ItemStack){IRON_BOOTS,1,71,enchant_code(ENCH_FEATHER_FALLING,4),0});
    player_take_damage(DAMAGE_FALL,6);
    REQUIRE(player_rules.health>14 && armor_worn(ARMOR_FEET).damage==71);

    /* These effects go through the same samples that accept touch actions. */
    inventory[0]=(ItemStack){IRON_PICKAXE,1,0,enchant_code(ENCH_SILK_TOUCH,1),0};
    REQUIRE(set_block(0,y,2,DIAMOND_ORE));
    aim_at((Vec3){.5f,y+.55f,2.5f});
    unsigned before=orb_points(OVERWORLD);
    REQUIRE(mine() && block(0,y,2)==AIR && drops[1].item.type==DIAMOND_ORE && orb_points(OVERWORLD)==before);
    inventory[0].enchants=enchant_code(ENCH_FORTUNE,3)|enchant_code(ENCH_UNBREAKING,3);
    int bonus=0;
    for (int i=0;i<20;++i) {
        memset(drops,0,sizeof dimension_drops[0]);
        REQUIRE(set_block(0,y,2,DIAMOND_ORE) && mine() && drops[0].item.type==DIAMOND &&
            drops[0].item.count>=1 && drops[0].item.count<=4);
        bonus+=drops[0].item.count>1;
    }
    REQUIRE(bonus>0 && inventory[0].damage<21 && orb_points(OVERWORLD)>before);
    inventory[0]=(ItemStack){IRON_SWORD,1,0,enchant_code(ENCH_SMITE,3),0};
    REQUIRE(actors_restore(OVERWORLD,(ActorRecord){{.5f,y+.01f,2.5f},0,ACTOR_ZOMBIE,20,0}));
    aim_at((Vec3){.5f,y+1.f,2.5f});
    REQUIRE(actors_attack(target()) && actors_in(OVERWORLD)[0].health==6 && inventory[0].damage==1);
    inventory[0]=(ItemStack){BOW,1,0,enchant_code(ENCH_POWER,5)|enchant_code(ENCH_INFINITY,1),0};
    inventory[1]=(ItemStack){ARROW,1,0,0,0};
    REQUIRE(bow_use());
    for (int i=0;i<21;++i) combat_step(.05f);
    REQUIRE(bow_use() && inventory[1].count==1 && combat_arrows()[0].damage==12 && inventory[0].damage==1);
    printf("ENCHANT TEST cane/recipe/shelves/cost/books/armor/melee/bow/mining failures=%d\n",failures);
    return failures;
}

static int name_tag_selftest(void) {
    int failures=0;
    GameDifficulty previous=game_selected_difficulty();
    world_begin(33);
    generate();
    player_reset_rules(SURVIVAL);
    REQUIRE(game_set_difficulty(NORMAL));
    px=.5f; py=150.01f; pz=.5f; yaw=0; pitch=-.36f;
    for (int z=-4;z<=0;++z) REQUIRE(set_block(0,149,z,STONE));
    const Vec3 animal={.5f,150.01f,-2.05f};
    REQUIRE(actors_restore(OVERWORLD,(ActorRecord){animal,0,ACTOR_PIG,10,0}));
    selected_slot=0;
    inventory[0]=(ItemStack){NAME_TAG,2,0,0,0};
    REQUIRE(game_tap().message && !actors_name_id(OVERWORLD,0) && inventory[0].count==2);
    REQUIRE(set_block(2,149,0,STONE) && anvil_place(2,150,0,1,0) && experience_restore_player(650,7));
    REQUIRE(anvil_apply(2,150,0,0,-1,"  小野 🐾  ")==ANVIL_READY);
    REQUIRE(!strcmp(item_override(inventory[0]),"小野 🐾"));
    REQUIRE(set_block(0,151,-1,STONE) && !actors_use_name_tag(target()) &&
        !actors_name_id(OVERWORLD,0) && inventory[0].count==2);
    REQUIRE(set_block(0,151,-1,AIR) && game_tap().message && inventory[0].count==1);
    Vec3 head;
    REQUIRE(!strcmp(actors_aimed_name(&head),"小野 🐾") && head.y>animal.y &&
        !actors_name_id(NETHER,0));
    uint16_t id=actors_name_id(OVERWORLD,0);
    REQUIRE(id && actors_restore(OVERWORLD,(ActorRecord){{10.5f,150.01f,-4.f},0,ACTOR_ZOMBIE,20,0}) &&
        actors_set_name(OVERWORLD,1,id));
    REQUIRE(actors_restore(OVERWORLD,(ActorRecord){{11.5f,150.01f,-4.f},0,ACTOR_SQUID,10,0}) &&
        actors_set_name(OVERWORLD,2,id));
    REQUIRE(actors_restore(OVERWORLD,(ActorRecord){{13.5f,150.01f,-4.f},0,ACTOR_PIG,10,0}));
    px=200.5f;
    actors_step(.05f);
    REQUIRE(actors_in(OVERWORLD)[0].type==ACTOR_PIG && actors_name_id(OVERWORLD,1)==id &&
        actors_name_id(OVERWORLD,2)==id && !actors_in(OVERWORLD)[3].type);
    REQUIRE(game_set_difficulty(PEACEFUL));
    actors_apply_difficulty();
    REQUIRE(!actors_name_id(OVERWORLD,1) && !actors_in(OVERWORLD)[1].type);
    REQUIRE(actors_restore(OVERWORLD,(ActorRecord){{12.5f,150.01f,-4.f},0,ACTOR_PIG,10,0}) &&
        !actors_name_id(OVERWORLD,1));
    player_reset_rules(CREATIVE);
    REQUIRE(set_block(0,149,-2,STONE));
    px=.5f; py=150.01f; pz=.5f; pitch=0.f;
    HorseRecord h={.feet={.5f,150.01f,-1.9f},.type=EQUINE_HORSE,.max_health=25,.health=25};
    REQUIRE(horse_restore(OVERWORLD,h) && game_tap().message && horse_name_id(OVERWORLD,0)==id &&
        inventory[0].count==1 && horse_rider()<0 && !strcmp(actors_aimed_name(&head),"小野 🐾"));
    REQUIRE(game_set_difficulty(previous));
    printf("NAME TAG TEST anvil/occlusion/consumption/despawn/peaceful/horse failures=%d\n",failures);
    return failures;
}
static int calibration_core_selftest(void) {
    int failures=0;
    REQUIRE(world_begin_generated(56182,GENERATOR_CURRENT));
    actors_reset();
    player_reset_rules(SURVIVAL);
    int sx=0,sy=0,sz=0;
    for (int x=1;x<15 && !sy;++x) for (int z=1;z<15 && !sy;++z)
        for (int y=14;y<34;++y) if (world_cell(x,y,z)==SLATE_ROCK) { sx=x;sy=y;sz=z; break; }
    REQUIRE(sy && !world_has_edit(sx,sy,sz));
    if (!sy) return failures;
    for (int y=sy+1;y<=sy+4;++y) REQUIRE(set_block(sx,y,sz,AIR));
    px=sx+.5f; py=sy+1.01f; pz=sz+.5f;
    aim_at((Vec3){sx+.5f,sy+.5f,sz+.5f});
    memset(drops,0,sizeof dimension_drops[0]);
    selected_slot=0;
    inventory[0]=(ItemStack){STONE_PICKAXE,1,0,enchant_code(ENCH_CORE_CLAMP,1),0};
    REQUIRE(mine() && world_cell(sx,sy,sz)==AIR && drops[0].item.type==SLATE_ITEM &&
        drops[0].item.count==2 && inventory[0].damage==2);
    memset(drops,0,sizeof dimension_drops[0]);
    REQUIRE(set_block(sx,sy,sz,SLATE_ROCK) && world_has_edit(sx,sy,sz));
    REQUIRE(mine() && world_cell(sx,sy,sz)==AIR && drops[0].item.type==SLATE_ITEM &&
        drops[0].item.count==1 && inventory[0].damage==3);
    world_reset();
    printf("CALIBRATION TEST natural cores, placed stone and wear failures=%d\n",failures);
    return failures;
}

static int anvil_selftest(void) {
    int failures=0,y=150;
    world_begin(57169);
    generate();
    memset(inventory,0,sizeof inventory);
    inventory[0]=(ItemStack){IRON_INGOT,31,0,0,0};
    const int compress[]={0,0,0,0,0,0,0,0,0};
    for (int i=0;i<3;++i) REQUIRE(craft_take(compress,3));
    REQUIRE(inventory[0].count==4 && inventory[1].type==IRON_BLOCK && inventory[1].count==3);
    REQUIRE(craft_take((int[]){1,1,1,-1,0,-1,0,0,0},3) && inventory[0].type==ANVIL && !inventory[1].count);
    REQUIRE(set_block(0,y-1,0,STONE) && set_block(0,y,0,AIR) && anvil_place(0,y,0,1,1) &&
        block(0,y,0)==ANVIL && world_cell(0,y,0)==ANVIL_BASE+3);
    REQUIRE(world_point_blocked(.5f,y+.4f,.5f,1) && !world_point_blocked(.08f,y+.4f,.08f,1));
    REQUIRE(world_point_blocked(.08f,y+.8f,.5f,1) && !world_point_blocked(.5f,y+.8f,.08f,1));
    const uint64_t fortune=enchant_code(ENCH_FORTUNE,2);
    inventory[0]=(ItemStack){IRON_PICKAXE,1,123,0,0};
    inventory[1]=(ItemStack){ENCHANTED_BOOK,1,0,fortune,0};
    AnvilPlan plan;
    uint32_t rng=experience_rng();
    REQUIRE(anvil_preview(0,1,"  深谷 ⛏️  ",&plan)==ANVIL_READY && plan.cost==11 &&
        plan.item.damage==123 && plan.item.enchants==fortune && plan.work==2 && plan.materials==1 &&
        !strcmp(plan.name,"深谷 ⛏️") && !inventory[0].custom && !inventory[0].enchants && experience_rng()==rng);
    REQUIRE(anvil_apply(0,y,0,0,1,"深谷 ⛏️")==ANVIL_NEED_LEVELS && inventory[1].count==1 &&
        !inventory[0].custom && world_cell(0,y,0)==ANVIL_BASE+3 && experience_rng()==rng);
    REQUIRE(experience_restore_player(1000,0x12345678u));
    unsigned level=experience_progress().level;
    REQUIRE(anvil_apply(0,y,0,0,1,"  深谷 ⛏️  ")==ANVIL_READY && !inventory[1].count &&
        experience_progress().level==level-11 && item_prior_work(inventory[0])==2 &&
        !strcmp(item_display_name(inventory[0]),"深谷 ⛏️"));
    inventory[1]=(ItemStack){IRON_INGOT,6,0,0,0};
    REQUIRE(anvil_preview(0,1,NULL,&plan)==ANVIL_READY && plan.materials==2 &&
        !plan.item.damage && plan.item.enchants==fortune && plan.cost==6);
    inventory[1]=(ItemStack){IRON_PICKAXE,1,206,fortune,0};
    REQUIRE(anvil_preview(0,1,NULL,&plan)==ANVIL_READY && plan.item.damage==49 &&
        enchant_level(plan.item.enchants,ENCH_FORTUNE)==3 && plan.materials==1);
    inventory[0]=(ItemStack){IRON_SWORD,1,0,enchant_code(ENCH_SHARPNESS,2),0};
    inventory[1]=(ItemStack){ENCHANTED_BOOK,1,0,enchant_code(ENCH_SMITE,2),0};
    REQUIRE(anvil_preview(0,1,NULL,&plan)==ANVIL_INCOMPATIBLE && anvil_preview(0,0,NULL,&plan)==ANVIL_MISSING);
    REQUIRE(anvil_preview(0,-1,"bad\nname",&plan)==ANVIL_BAD_NAME);
    REQUIRE(anvil_preview(0,-1,"012345678901234567890123456789! ",&plan)==ANVIL_BAD_NAME);
    REQUIRE(anvil_preview(0,-1,"\xED\xA0\x80",&plan)==ANVIL_BAD_NAME);
    inventory[0]=(ItemStack){IRON_PICKAXE,1,105,0,0};
    inventory[1]=(ItemStack){IRON_INGOT,8,0,0,0};
    REQUIRE(item_set_custom(&inventory[0],38,NULL) && anvil_preview(0,1,NULL,&plan)==ANVIL_TOO_EXPENSIVE);
    REQUIRE(anvil_preview(0,-1,"采集镐",&plan)==ANVIL_READY && plan.cost==39);
    REQUIRE(item_set_custom(&inventory[0],8,NULL) && anvil_preview(0,1,NULL,&plan)==ANVIL_READY &&
        plan.cost==10 && plan.work==10);
    REQUIRE(set_block(0,y,0,AIR) && anvil_place(0,y,0,2,1));
    uint32_t wear_seed=1;
    for (;wear_seed<1000;++wear_seed) {
        REQUIRE(experience_restore_player(1500,wear_seed));
        if (experience_roll(0,99)<12u) break;
    }
    REQUIRE(wear_seed<1000 && experience_restore_player(1500,wear_seed));
    REQUIRE(anvil_apply(0,y,0,0,1,NULL)==ANVIL_READY && block(0,y,0)==AIR &&
        inventory[1].count==6 && !inventory[0].damage);
    REQUIRE(anvil_place(0,y,0,2,1));
    px=.5f;py=y+.01f;pz=2.5f;yaw=0;pitch=-.43f;
    selected_slot=0;
    REQUIRE(target().hit && target().z==0);
    REQUIRE(mine() && block(0,y,0)==AIR && drops[0].item.type==ANVIL && drops[0].item.damage==2);
    REQUIRE(inventory_add_stack(drops[0].item) && inventory[2].type==ANVIL && inventory[2].damage==2);
    player_reset_rules(CREATIVE);
    REQUIRE(anvil_place(0,y,0,2,1));
    REQUIRE(experience_restore_player(1500,wear_seed));
    REQUIRE(anvil_apply(0,y,0,0,-1,"创造镐")==ANVIL_READY && experience_points()==1500 &&
        world_cell(0,y,0)==ANVIL_BASE+5 && experience_rng()==wear_seed);
    printf("ANVIL TEST craft/shape/repair/books/rename/atomic costs/wear/mine failures=%d\n",failures);
    return failures;
}

static int gravity_selftest(void) {
    int failures=0,y=150;
    REQUIRE(world_begin_generated(7651,GENERATOR_BOOKS));
    generate();
    px=-15.5f;py=y+.01f;pz=.5f;
    REQUIRE(set_block(-16,y-1,0,STONE));
    REQUIRE(set_block(-17,y-1,1,STONE) && set_block(-17,y+3,1,STONE));
    REQUIRE(set_block(-17,y+4,1,SAND) && set_block(-17,y+5,1,RED_SAND));
    gravity_step(1);
    REQUIRE(block(-17,y+4,1)==SAND && !gravity_pending_count(OVERWORLD));
    REQUIRE(set_block(-17,y+3,1,AIR));
    game_step(.05f,0,0,0,0);
    size_t n=0;
    gravity_falling(OVERWORLD,&n);
    REQUIRE(n>0 && block(-17,y+4,1)==AIR);
    gravity_step(75);
    REQUIRE(block(-17,y,1)==SAND && block(-17,y+1,1)==RED_SAND && block(-17,y+5,1)==AIR);
    gravity_falling(OVERWORLD,&n);
    REQUIRE(!n && !gravity_pending_count(OVERWORLD));
    REQUIRE(set_block(-18,y-1,0,STONE) && set_block(-18,y,0,WATER) && set_block(-18,y+4,0,GRAVEL));
    gravity_step(75);
    REQUIRE(block(-18,y,0)==GRAVEL && block(-18,y+4,0)==AIR);
    REQUIRE(set_block(-13,y-1,0,STONE) && set_block(-13,y,0,TORCH) && set_block(-13,y+5,0,GRAVEL));
    for (int i=0;i<MAXDROP;++i) drops[i]=make_drop(8.5f,y+.5f,7.5f,(ItemStack){STONE,1,0,0,0});
    gravity_step(75);
    gravity_falling(OVERWORLD,&n);
    REQUIRE(n==1 && block(-13,y,0)==TORCH && block(-13,y+5,0)==AIR);
    drops[27].item=(ItemStack){0,0,0,0,0};
    gravity_step(3);
    gravity_falling(OVERWORLD,&n);
    REQUIRE(!n && drops[27].item.type==GRAVEL && drops[27].item.count==1 && block(-13,y,0)==TORCH);
    REQUIRE(set_block(-16,y,0,AIR) && anvil_place(-16,y+7,0,0,1));
    gravity_step(75);
    REQUIRE(player_rules.health==8 && block(-16,y,0)==ANVIL &&
        ((world_cell(-16,y,0)-ANVIL_BASE)&1)==1 && anvil_stage(world_cell(-16,y,0))<=1);
    REQUIRE(world_move_blocked(body_box(px,py,pz,.28f,1.72f),body_box(px,py,pz+.12f,.28f,1.72f),0)==0);
    REQUIRE(set_block(-16,y,0,AIR) && anvil_place(-16,y+7,0,0,1));
    player_reset_rules(SURVIVAL);
    REQUIRE(armor_restore(ARMOR_HEAD,(ItemStack){IRON_HELMET,1,0,0,0}));
    gravity_step(75);
    REQUIRE(player_rules.health>8 && player_rules.health<MAX_HEALTH && armor_worn(ARMOR_HEAD).damage>30);
    actors_reset();
    REQUIRE(actors_restore(OVERWORLD,(ActorRecord){{-10.5f,y+.01f,.5f},0,ACTOR_PIG,10,0}));
    REQUIRE(set_block(-11,y-1,0,STONE) && anvil_place(-11,y+7,0,0,0));
    memset(drops,0,MAXDROP*sizeof *drops);
    drops[0]=make_drop(-10.5f,y+.45f,.5f,(ItemStack){DIAMOND,1,0,0,0});
    REQUIRE(death_drop_restore(OVERWORLD,make_drop(-10.5f,y+.45f,.5f,(ItemStack){IRON_INGOT,1,0,0,0})));
    REQUIRE(death_drop_restore(NETHER,make_drop(-10.5f,y+.45f,.5f,(ItemStack){IRON_INGOT,1,0,0,0})));
    gravity_step(75);
    REQUIRE(!actors_in(OVERWORLD)[0].type && !experience_points() &&
        drops[0].item.type!=DIAMOND && death_drop_count(OVERWORLD)==0 && death_drop_count(NETHER)==1);
    /* Enough checks in a single chunk to exercise the duplicate index,
       spreading the burst over ticks without dropping a pending block. */
    for (int x=16;x<30;++x) for (int z=0;z<10;++z)
        REQUIRE(set_block(x,y-1,z,STONE) && set_block(x,y+2,z,SAND) && set_block(x,y+2,z,SAND));
    REQUIRE(gravity_pending_count(OVERWORLD)==140);
    gravity_step(80);
    for (int x=16;x<30;++x) for (int z=0;z<10;++z)
        REQUIRE(block(x,y,z)==SAND && block(x,y+2,z)==AIR);
    REQUIRE(set_block(800,y,800,SAND) && gravity_pending_count(OVERWORLD)==1);
    gravity_step(200);
    REQUIRE(block(800,y,800)==SAND && gravity_pending_count(OVERWORLD)==1);
    REQUIRE(world_select_dimension(END) && set_block(0,6,0,SAND));
    px=.5f;py=y+.01f;pz=.5f;
    /* Look for an actual void column instead of relying on the center island. */
    REQUIRE(set_block(999,6,999,GRAVEL));
    px=999.5f;pz=999.5f;
    gravity_step(60);
    gravity_falling(END,&n);
    REQUIRE(!n && block(999,6,999)==AIR && gravity_pending_count(OVERWORLD)==1);
    printf("GRAVITY TEST support/stack/seam/fluid/fragile/retry/impact/armor/mobs/void failures=%d\n",failures);
    return failures;
}

static int test_gate_power(int x,int y,int z) {
    GateState gate;
    return gate_decode(world_cell(x,y,z),&gate) && gate.powered && gate.open;
}
static int redstone_selftest(void) {
    int failures=0,y=150;
    world_begin(9015);
    generate();
    px=-15.5f;py=y+.01f;pz=3.5f;
    memset(inventory,0,sizeof inventory);
    inventory[0]=(ItemStack){STICK,1,0,0,0}; inventory[1]=(ItemStack){COBBLESTONE,1,0,0,0};
    REQUIRE(craft_take((int[]){0,-1,1,-1},2) && inventory[0].type==LEVER_ITEM);
    for (int x=-23;x<=-2;++x) for (int z=-8;z<=14;++z)
        REQUIRE(set_block(x,y-1,z,STONE));
    REQUIRE(redstone_place_lever((CircuitPos){-21,y,0},(CircuitPos){-21,y-1,0},1));
    REQUIRE(redstone_place_lever((CircuitPos){-3,y,0},(CircuitPos){-3,y-1,0},0));
    REQUIRE(!redstone_place_lever((CircuitPos){-4,y+9,0},(CircuitPos){-5,y+9,0},0));
    for (int x=-20;x<=-4;++x) REQUIRE(redstone_place_wire(x,y,0));
    REQUIRE(redstone_place_wire(-13,y,-1) && gate_place(-13,y,-2,EAST));
    REQUIRE(gate_place(-6,y,1,EAST)); /* Side of a straight, not an output. */
    REQUIRE(redstone_place_wire(-12,y,1));
    for (int z=2;z<=4;++z) REQUIRE(rail_place((RailCell){-12,y,z},RAIL_BOOSTER,0));
    REQUIRE(redstone_toggle_lever(-21,y,0)==2);
    for (int i=0;i<7;++i) redstone_step(-16,0);
    for (int i=0;i<17;++i) REQUIRE(redstone_wire_power(-20+i,y,0)==(i<15 ? 15-i : 0));
    REQUIRE(test_gate_power(-13,y,-2) && !test_gate_power(-6,y,1));
    REQUIRE(rail_is_energized((RailCell){-12,y,4}));
    REQUIRE(!collides(-12.5f,y+.01f,-1.5f) && !solid(REDSTONE_WIRE) && !solid(LEVER));
    REQUIRE(redstone_toggle_lever(-3,y,0)==2 && redstone_toggle_lever(-21,y,0)==1);
    for (int i=0;i<7;++i) redstone_step(-16,0);
    REQUIRE(redstone_wire_power(-4,y,0)==15 && !redstone_wire_power(-20,y,0) &&
        test_gate_power(-13,y,-2) && rail_is_energized((RailCell){-12,y,4}));
    REQUIRE(redstone_toggle_lever(-3,y,0)==1);
    for (int i=0;i<10;++i) redstone_step(-16,0);
    REQUIRE(!test_gate_power(-13,y,-2) && !rail_is_energized((RailCell){-12,y,4}));
    REQUIRE(set_block(-19,y,-6,REDSTONE_BLOCK) && set_block(-17,y,-6,STONE) && set_block(-16,y,-6,STONE));
    REQUIRE(redstone_place_wire(-18,y,-6) && redstone_place_wire(-17,y+1,-6) && redstone_place_wire(-16,y+1,-6));
    for (int i=0;i<7;++i) redstone_step(-16,-6);
    REQUIRE(redstone_wire_power(-18,y,-6)==15 && redstone_wire_power(-17,y+1,-6)==14 &&
        redstone_wire_power(-16,y+1,-6)==13 && (redstone_wire_shape(-18,y,-6).raised&(1<<EAST)));
    REQUIRE(set_block(-18,y+1,-6,STONE));
    for (int i=0;i<8;++i) redstone_step(-16,-6);
    REQUIRE(!redstone_wire_power(-17,y+1,-6) && !redstone_wire_power(-16,y+1,-6));
    /* Loop with no alternate source must not sustain its previous output. */
    REQUIRE(redstone_place_lever((CircuitPos){-18,y,10},(CircuitPos){-18,y-1,10},0));
    for (int x=-17;x<=-13;++x) {
        REQUIRE(redstone_place_wire(x,y,10) && redstone_place_wire(x,y,14));
    }
    for (int z=11;z<=13;++z) {
        REQUIRE(redstone_place_wire(-17,y,z) && redstone_place_wire(-13,y,z));
    }
    REQUIRE(redstone_toggle_lever(-18,y,10)==2);
    for (int i=0;i<7;++i) redstone_step(-16,10);
    REQUIRE(redstone_wire_power(-13,y,14)==7);
    REQUIRE(redstone_toggle_lever(-18,y,10)==1);
    for (int i=0;i<12;++i) redstone_step(-16,10);
    REQUIRE(!redstone_wire_power(-13,y,14) && !redstone_wire_power(-17,y,14) && !redstone_wire_power(-17,y,10));
    REQUIRE(set_block(-12,y,7,STONE) && gate_place(-11,y,7,NORTH));
    REQUIRE(redstone_place_lever((CircuitPos){-13,y,7},(CircuitPos){-12,y,7},0));
    REQUIRE(redstone_toggle_lever(-13,y,7)==2);
    redstone_step(-12,7);
    REQUIRE(test_gate_power(-11,y,7));
    REQUIRE(set_block(-12,y,7,AIR));
    redstone_step(-12,7);
    REQUIRE(block(-13,y,7)==AIR && !test_gate_power(-11,y,7) && drops[0].item.type==LEVER_ITEM);
    REQUIRE(redstone_place_wire(-7,y,8) && set_block(-7,y+1,8,WATER));
    for (int i=0;i<20 && block(-7,y,8)!=WATER;++i) water_step();
    int washed=0;
    for (int i=0;i<MAXDROP;++i) if (drops[i].item.type==REDSTONE_DUST &&
        fabsf(drops[i].x+6.5f)<.001f && fabsf(drops[i].z-8.5f)<.001f) ++washed;
    REQUIRE(block(-7,y,8)==WATER && washed==1);
    /* The real game step, not just a test-inserted signal, runs the solver. */
    REQUIRE(redstone_toggle_lever(-21,y,0)==2);
    px=-15.5f;py=y+.01f;pz=3.5f;
    for (int i=0;i<7;++i) game_step(.05f,0,0,0,0);
    REQUIRE(redstone_wire_power(-20,y,0)==15 && test_gate_power(-13,y,-2));
    printf("REDSTONE TEST recipes/range/gates/rails/slope/loop/multisource/mount/water/game failures=%d\n",failures);
    return failures;
}

/* Run with --world-test. This intentionally exercises eviction and negative
   chunk boundaries without creating a second main in the project. */
static int difficulty_selftest(void) {
    int failures=0;
    REQUIRE(world_begin_generated(24921,GENERATOR_TEMPLES));
    generate();
    const int damage[]={0,4,6,9};
    const int starvation_floors[]={20,10,1,0};
    for (int i=0;i<=HARD;++i) {
        REQUIRE(game_set_difficulty((GameDifficulty)i));
        player_reset_rules(SURVIVAL);
        player_take_mob_damage(DAMAGE_MELEE,6);
        REQUIRE(player_rules.health==MAX_HEALTH-damage[i]);
        player_take_damage(DAMAGE_FALL,2);
        REQUIRE(player_rules.health==MAX_HEALTH-damage[i]-2);
        player_reset_rules(SURVIVAL);
        player_take_damage(DAMAGE_STARVATION,25);
        REQUIRE(player_rules.health==starvation_floors[i]);
    }
    REQUIRE(!game_set_difficulty((GameDifficulty)4) && game_selected_difficulty()==HARD);
    player_reset_rules(SURVIVAL);
    REQUIRE(game_set_difficulty(PEACEFUL));
    Vec3 at={px,py,pz};
    REQUIRE(actors_restore(OVERWORLD,(ActorRecord){at,0,ACTOR_PIG,10,0}));
    REQUIRE(actors_restore(OVERWORLD,(ActorRecord){at,0,ACTOR_ZOMBIE,20,0}));
    REQUIRE(actors_restore(NETHER,(ActorRecord){at,0,ACTOR_REEDLIZARD,20,0}));
    REQUIRE(actors_restore(END,(ActorRecord){at,0,ACTOR_TIDERAY,TIDERAY_HEALTH,0}));
    REQUIRE(!blast_restore(OVERWORLD,(PrimedBlast){at,{0,0,0},1,BLAST_ARCHIVE_SMALL}));
    REQUIRE(blast_restore(OVERWORLD,(PrimedBlast){at,{0,0,0},80,BLAST_TNT}));
    actors_apply_difficulty();
    size_t n;
    const PrimedBlast *charges=blasts_in(OVERWORLD,&n);
    REQUIRE(n==1 && charges[0].kind==BLAST_TNT);
    REQUIRE(actors_in(OVERWORLD)[0].type==ACTOR_PIG && actors_in(OVERWORLD)[1].type==ACTOR_EMPTY);
    REQUIRE(actors_in(NETHER)[0].type==ACTOR_EMPTY && actors_in(END)[0].type==ACTOR_TIDERAY);
    player_rules.health=13;
    player_rules.food=6;
    player_rules.exhaustion=12.f;
    for (int i=0;i<22;++i) game_step(.05f,0,0,0,0);
    REQUIRE(player_rules.health==14 && player_rules.food==6 && player_rules.exhaustion==0.f);
    player_rules.hardcore=1;
    REQUIRE(game_effective_difficulty()==HARD);
    player_take_damage(DAMAGE_STARVATION,30);
    REQUIRE(player_rules.health==0 && player_respawn()==RESPAWN_HARDCORE && player_rules.health==0);
    REQUIRE(game_set_difficulty(NORMAL));
    player_reset_rules(SURVIVAL);
    printf("DIFFICULTY TEST health/mob damage/peaceful preservation/hardcore failures=%d\n",failures);
    return failures;
}

static int zombie_door_fixture(int iron,int capable,int blocker,GameDifficulty difficulty) {
    const int y=150,x=8,z=10;
    if (!game_set_difficulty(difficulty) || !world_begin_generated(41921,GENERATOR_TEMPLES)) return 0;
    generate();
    for (int i=x-2;i<=x+2;++i) for (int k=z-6;k<=z+6;++k)
        if (!set_block(i,y-1,k,STONE)) return 0;
    if (!door_place(x,y,z,NORTH,0,iron) ||
        (blocker && (!set_block(x,y,z-1,STONE) || !set_block(x,y+1,z-1,STONE)))) return 0;
    px=x+.5f; py=y+.01f; pz=z+3.5f;
    player_rules.day_tick=15000;
    return actors_restore_snapshot(OVERWORLD,
        (ActorRecord){{x+.5f,y+.01f,z-2.5f},0,ACTOR_ZOMBIE,20,0},
        (ActorCare){0},(LeadLink){LEAD_FREE,0,0,0},(ActorArmorState){.door_breaker=(uint8_t)capable});
}
static unsigned knock_until(int ticks) {
    unsigned max=0;
    for (int i=0;i<ticks;++i) {
        actors_step(.05f);
        unsigned stage=actors_door_attack(OVERWORLD,0).stage;
        if (stage>max) max=stage;
    }
    return max;
}
static int zombie_door_selftest(void) {
    int failures=0;
    const int x=8,y=150,z=10;
    REQUIRE(zombie_door_fixture(0,1,0,HARD));
    unsigned stage=knock_until(72);
    REQUIRE(stage>=2 && stage<=4 && door_cell(world_cell(x,y,z)));
    REQUIRE(door_toggle(x,y+1,z) && !knock_until(1));
    REQUIRE(door_toggle(x,y,z) && knock_until(1)==1);
    REQUIRE(knock_until(68)>=2);
    int damage[ACTOR_CAPACITY]={1};
    actors_explosion_hits(damage);
    REQUIRE(!actors_door_attack(OVERWORLD,0).stage && !knock_until(5));
    REQUIRE(knock_until(58)>=2);
    pz=z-7.f;
    REQUIRE(!knock_until(1) && door_cell(world_cell(x,y,z)));
    pz=z+3.5f;
    REQUIRE(knock_until(58)>=2 && door_cell(world_cell(x,y,z)));
    REQUIRE(knock_until(238)>=8 && block(x,y,z)==AIR && block(x,y+1,z)==AIR);
    REQUIRE(!actors_door_attack(OVERWORLD,0).stage && block(x,y-1,z)==STONE);
    for (int i=0;i<MAXDROP;++i) REQUIRE(drops[i].item.type!=WOOD_DOOR_ITEM);

    REQUIRE(zombie_door_fixture(0,1,0,NORMAL) && knock_until(310)>=8 && door_cell(world_cell(x,y+1,z)));
    REQUIRE(zombie_door_fixture(1,1,0,HARD) && !knock_until(310) && door_cell(world_cell(x,y,z)));
    REQUIRE(zombie_door_fixture(0,0,0,HARD) && !knock_until(310) && door_cell(world_cell(x,y,z)));
    REQUIRE(zombie_door_fixture(0,1,1,HARD) && !knock_until(310) && door_cell(world_cell(x,y,z)));
    REQUIRE(game_set_difficulty(NORMAL));
    printf("ZOMBIE DOOR TEST cracks/interruption/hard/normal/iron/capable/wall failures=%d\n",failures);
    return failures;
}

static int rescue_selftest(void) {
    int failures=0;
    const int x=8,y=150,z=10;
    REQUIRE(world_begin_generated(41921,GENERATOR_CURRENT));
    generate();
    REQUIRE(game_set_difficulty(HARD));
    player_rules.day_tick=15100;
    for (int i=5;i<=11;++i) for (int k=5;k<=16;++k) REQUIRE(set_block(i,y-1,k,STONE));
    for (int i=7;i<=9;++i) REQUIRE(set_block(i,y,z,NETHER_FENCE));
    VillagerSave identity={x,y-1,z-2,VILLAGER_SMITH,1,2,{1,0,0},0};
    REQUIRE(actors_restore_roaming(OVERWORLD,
        (ActorRecord){{x+.5f,y+.01f,z-1.15f},0,ACTOR_VILLAGER,1,ROAMING_VILLAGER|VILLAGER_SMITH},identity));
    REQUIRE(actors_restore(OVERWORLD,(ActorRecord){{x+.5f,y+.01f,z-2.19f},0,ACTOR_ZOMBIE,20,0}));
    px=x+.5f; py=y+.01f; pz=z+3.f; yaw=0; pitch=.025f; vy=0.f;
    for (int i=0;i<4;++i) actors_step(.05f);
    REQUIRE(actors_in(OVERWORLD)[0].type==ACTOR_STRANDED_GUIDE && !villagers_roaming_market(0,NULL));
    const StrandedGuideState source=actors_stranded_state(OVERWORLD,0);
    REQUIRE(source.origin.market.home==VILLAGER_SMITH && source.origin.market.uses[0]==1);
    /* Clear the attacker and aim at the seated resident through the real interaction path. */
    actors_reset();
    for (int i=x-1;i<=x+1;++i) for (int j=z-2;j<=z+2;++j)
        REQUIRE(set_block(i,y,j,AIR) && set_block(i,y+1,j,AIR));
    const ActorRecord guide={{x+.5f,y+.01f,z+.5f},0,ACTOR_STRANDED_GUIDE,STRANDED_HEALTH,0};
    REQUIRE(actors_restore_stranded(OVERWORLD,guide,source));
    px=x+.5f; pz=z+3.25f; yaw=0.f; pitch=-.30f;
    memset(inventory,0,sizeof inventory);
    selected_slot=0;
    inventory[0]=(ItemStack){FIELD_FLASK_TWO,1,0,0,0};
    const char *note=game_tap().message;
    REQUIRE(note && strstr(note,"回声路标") && inventory[0].type==FIELD_FLASK_TWO);
    REQUIRE(set_block(x+1,y,z,RESONANCE_MARKER) && set_block(x,y+1,z,STONE));
    note=game_tap().message;
    REQUIRE(note && strstr(note,"暂停") && inventory[0].type==FIELD_FLASK_TWO);
    REQUIRE(set_block(x,y+1,z,AIR));
    REQUIRE(game_tap().message && inventory[0].type==FIELD_FLASK_ONE &&
        actors_stranded_state(OVERWORLD,0).rest_ticks==GUIDE_REST_TICKS);
    REQUIRE(game_tap().message && inventory[0].type==FIELD_FLASK_ONE);
    for (int i=0;i<20;++i) actors_step(.05f);
    const unsigned elapsed_rest=actors_stranded_state(OVERWORLD,0).rest_ticks;
    REQUIRE(elapsed_rest>0 && elapsed_rest<GUIDE_REST_TICKS);
    REQUIRE(set_block(x+1,y,z,AIR));
    for (int i=0;i<25;++i) actors_step(.05f);
    REQUIRE(actors_stranded_state(OVERWORLD,0).rest_ticks==elapsed_rest &&
        inventory[0].type==FIELD_FLASK_ONE);
    REQUIRE(set_block(x+1,y,z,RESONANCE_MARKER));
    for (int i=0;i<GUIDE_REST_TICKS+2 && actors_in(OVERWORLD)[0].type==ACTOR_STRANDED_GUIDE;++i)
        actors_step(.05f);
    REQUIRE(actors_in(OVERWORLD)[0].type==ACTOR_VILLAGER && inventory[0].type==FIELD_FLASK_ONE);
    VillagerSession trade=villagers_open(0);
    VillagerSave restored={0};
    REQUIRE(trade && villagers_role(trade)==VILLAGER_SMITH && villagers_count(trade)==2 &&
        villagers_roaming_market(0,&restored) && restored.uses[0]==1);
    villagers_close(trade);
    REQUIRE(game_set_difficulty(NORMAL));
    printf("RESCUE TEST live injury, no marker, clearance, one sip, paused timer and market failures=%d\n",failures);
    return failures;
}

static int navigation_selftest(void) {
    int failures=0;
    REQUIRE(world_begin_generated(992382978u,GENERATOR_CURRENT));
    generate();
    player_reset_rules(SURVIVAL);
    memset(inventory,0,sizeof inventory);
    inventory[0]=(ItemStack){IRON_INGOT,4,0,0,0};
    inventory[1]=(ItemStack){REDSTONE_DUST,2,0,0,0};
    const int dial[]={-1,0,-1,0,1,0,-1,0,-1};
    REQUIRE(craft_preview(dial,3).type==COMPASS_ITEM && craft_take(dial,3) && inventory[0].type==COMPASS_ITEM);
    inventory[2]=(ItemStack){GOLD_INGOT,4,0,0,0};
    const int clock[]={-1,2,-1,2,1,2,-1,2,-1};
    REQUIRE(craft_take(clock,3) && inventory[1].type==CLOCK_ITEM);
    inventory[2]=(ItemStack){PAPER,8,0,0,0};
    const int empty[]={2,2,2,2,0,2,2,2,2};
    REQUIRE(craft_take(empty,3) && inventory[0].type==EMPTY_MAP);
    Vec3 spawn=player_world_spawn();
    px=spawn.x; pz=spawn.z+24.f; yaw=0.f;
    REQUIRE(navigation_compass_step()==0);
    yaw=1.5707963f;
    REQUIRE(navigation_compass_step()==6);
    player_rules.day_tick=6000;
    REQUIRE(navigation_clock_minutes()==12*60);
    px=-127.5f; py=150.01f; pz=-127.5f; yaw=0.f;
    for (int a=-6;a<=8;++a) for (int b=-6;b<=8;++b) {
        (void)world_cell(-128+a,149,-128+b);
        REQUIRE(set_block(-128+a,149,-128+b,a>=0 ? SANDSTONE : STONE));
    }
    selected_slot=0;
    REQUIRE(game_tap().message && inventory[0].type==FILLED_MAP && map_item_id(inventory[0])==1);
    for (int i=0;i<35;++i) navigation_step(.05f);
    MapView m={0},copy={0};
    REQUIRE(map_view(1,&m) && m.x==-128 && m.z==-128 && m.scale==0 && m.explored>200);
    uint8_t center=m.colors ? m.colors[64*MAP_SIDE+64] : 0;
    REQUIRE(center && map_rgb(center)!=map_rgb(0) && map_marker(m).heading==0);
    pz=-122.5f; yaw=1.5707963f;
    REQUIRE(map_marker(m).z==69 && map_marker(m).heading==4);
    REQUIRE(world_select_dimension(NETHER) && !map_marker(m).visible && navigation_clock_minutes()==-1);
    for (int i=0;i<8;++i) navigation_step(.05f);
    REQUIRE(map_view(1,&copy) && copy.explored==m.explored && copy.revision==m.revision && world_select_dimension(OVERWORLD));
    inventory[2]=(ItemStack){EMPTY_MAP,2,0,0,0};
    const int clone[]={0,2,2,-1};
    REQUIRE(craft_preview(clone,2).count==3 && craft_take(clone,2) && inventory[0].count==3 &&
        map_item_id(inventory[0])==1 && map_count()==1);
    inventory[2]=(ItemStack){PAPER,8,0,0,0};
    const int scale[]={2,2,2,2,0,2,2,2,2};
    REQUIRE(craft_preview(scale,3).type==FILLED_MAP && map_count()==1);
    REQUIRE(craft_take(scale,3) && inventory[0].count==2 && map_item_id(inventory[0])==1 &&
        inventory[2].count==1 && map_item_id(inventory[2])==2);
    REQUIRE(map_view(2,&copy) && copy.x==m.x && copy.z==m.z && copy.scale==1 && !copy.explored);
    for (int s=2;s<=4;++s) REQUIRE(map_zoom((unsigned)s)==(unsigned)(s+1));
    REQUIRE(!map_can_zoom(5));
    for (int i=0;i<SLOT_COUNT;++i) inventory[i]=(ItemStack){STONE,64,0,0,0};
    inventory[0]=(ItemStack){EMPTY_MAP,2,0,0,0};
    REQUIRE(game_tap().message && inventory[0].type==EMPTY_MAP && inventory[0].count==2 && map_count()==5);
    uint8_t bad[MAP_PIXELS]={1};
    REQUIRE(!map_restore(0,0,OVERWORLD,0,bad) && !map_restore(1,0,OVERWORLD,0,bad));
    navigation_reset();
    printf("NAVIGATION TEST recipes/projection/survey/copy/zoom/atomic/dimension failures=%d\n",failures);
    return failures;
}

static int mushroom_selftest(void) {
    int failures=0;
    REQUIRE(world_begin_generated(335517,GENERATOR_MUSHROOM_ISLAND));
    generate();
    for (int x=-6;x<=11;++x) for (int z=-5;z<=6;++z) REQUIRE(set_block(x,146,z,MYCELIUM));
    px=0.f; py=147.01f; pz=2.5f;
    player_reset_rules(SURVIVAL);
    REQUIRE(set_block(-5,147,-1,RED_MUSHROOM) && set_block(8,147,-1,BROWN_MUSHROOM));
    REQUIRE(set_block(-5,149,-1,STONE));
    REQUIRE(farm_bonemeal(-5,147,-1)<0 && block(-5,147,-1)==RED_MUSHROOM && block(-5,148,-1)==AIR);
    REQUIRE(set_block(-5,149,-1,AIR));
    player_reset_rules(CREATIVE);
    REQUIRE(farm_bonemeal(-5,147,-1)>0 && farm_bonemeal(8,147,-1)>0);
    REQUIRE(block(-5,147,-1)==MUSHROOM_STEM && block(8,147,-1)==MUSHROOM_STEM);
    int red=0,brown=0;
    for (int h=147;h<155;++h) for (int x=-8;x<=11;++x) for (int z=-4;z<=2;++z) {
        int b=block(x,h,z);
        red+=b==RED_MUSHROOM_BLOCK;
        brown+=b==BROWN_MUSHROOM_BLOCK;
    }
    REQUIRE(red==45 && brown==45);
    REQUIRE(!valid_cell(EGG) && !set_block(4,147,0,EGG));
    player_reset_rules(SURVIVAL);
    memset(inventory,0,sizeof inventory);
    inventory[0]=(ItemStack){PLANKS,3,0,0,0};
    const int bowl_recipe[]={0,-1,0,-1,0,-1,-1,-1,-1};
    REQUIRE(craft_take(bowl_recipe,3) && inventory[0].type==BOWL && inventory[0].count==4);
    inventory[1]=(ItemStack){RED_MUSHROOM_ITEM,1,0,0,0};
    inventory[2]=(ItemStack){BROWN_MUSHROOM_ITEM,1,0,0,0};
    const int soup_recipe[]={1,0,-1,2};
    REQUIRE(craft_take(soup_recipe,2) && inventory[1].type==MUSHROOM_STEW && inventory[0].count==3);
    selected_slot=1;
    player_rules.food=11;
    REQUIRE(player_eat() && inventory[1].type==BOWL && player_rules.food==17);
    printf("FUNGAL TEST atomic growth, cooking and container return failures=%d\n",failures);
    return failures;
}

static int tide_crab_selftest(void) {
    int failures=0;
    enum { Y=145 };
    REQUIRE(world_begin_generated(65871,GENERATOR_CURRENT));
    generate();
    actors_reset();
    player_reset_rules(SURVIVAL);
    px=.5f; py=Y+.01f; pz=3.5f; selected_slot=0;
    for (int z=-3;z<=4;++z) {
        REQUIRE(set_block(0,Y-1,z,JOINT_BASALT) && set_block(0,Y,z,AIR));
        REQUIRE(set_block(1,Y-2,z,STONE) && set_block(1,Y-1,z,z<=1 ? WATER : STONE));
    }
    ActorRecord crab={{.5f,Y+.01f,.5f},0,ACTOR_TIDE_CRAB,8,0};
    REQUIRE(!actors_restore(NETHER,crab) && !actors_restore_with_care(OVERWORLD,crab,
        (ActorCare){.cooldown=TIDE_CRAB_WORK_TICKS+1}));
    REQUIRE(actors_restore(OVERWORLD,crab));
    for (int i=0;i<SLOT_COUNT;++i) inventory[i]=(ItemStack){STONE,64,0,0,0};
    inventory[0]=(ItemStack){GRAVEL,2,0,0,0};
    aim_at((Vec3){.5f,Y+.17f,.5f});
    const char *note=actors_use_tide_crab(target());
    REQUIRE(note && strstr(note,"背包放不下") && inventory[0].count==2 && !actors_care_in(OVERWORLD)[0].cooldown);
    inventory[2]=(ItemStack){0};
    Interaction traded=game_tap();
    REQUIRE(traded.message && strstr(traded.message,"换得潮磨砂") && inventory[0].count==1 && inventory[2].type==TIDE_GRIT &&
        actors_care_in(OVERWORLD)[0].cooldown==TIDE_CRAB_WORK_TICKS);
    note=actors_use_tide_crab(target());
    REQUIRE(note && strstr(note,"还在筛砂") && inventory[0].count==1);
    inventory[3]=(ItemStack){JOINT_BASALT_ITEM,1,0,0,0};
    inventory[4]=(ItemStack){0};
    REQUIRE(craft_take((int[]){3,2,-1,-1},2) && inventory[2].type==BASALT_PAVING_ITEM && inventory[2].count==3);
    float travel=0.f;
    for (int step=0;step<120;++step) {
        actors_step(.05f);
        ActorRecord a=actors_in(OVERWORLD)[0];
        REQUIRE(a.type==ACTOR_TIDE_CRAB && fabsf(a.feet.x-.5f)<.001f && fabsf(a.feet.y-crab.feet.y)<.001f);
        travel=fmaxf(travel,fabsf(a.feet.z-.5f));
    }
    REQUIRE(travel>.15f && actors_care_in(OVERWORLD)[0].cooldown==TIDE_CRAB_WORK_TICKS-120);
    for (int z=-3;z<=4;++z) REQUIRE(set_block(1,Y-1,z,STONE));
    ActorRecord a=actors_in(OVERWORLD)[0];
    aim_at((Vec3){a.feet.x,a.feet.y+.17f,a.feet.z});
    note=actors_use_tide_crab(target());
    REQUIRE(note && strstr(note,"只在水边") && inventory[0].count==1);
    printf("TIDE CRAB TEST wet-rock gait, dry habitat, atomic exchange, timer and paving failures=%d\n",failures);
    return failures;
}

static int outlet_fixture_selftest(void) {
    int failures=0;
    enum { Y=145 };
    REQUIRE(world_begin_generated(58100,GENERATOR_CURRENT) && world_select_dimension(NETHER));
    player_reset_rules(SURVIVAL);
    for (int x=-3;x<=3;++x) for (int z=-5;z<=3;++z)
        REQUIRE(set_block(x,Y-1,z,STONE));
    px=.5f; py=Y+.01f; pz=.5f; yaw=0.f;
    selected_slot=0;
    drops_reset();
    inventory[0]=(ItemStack){AIR_OUTLET_ITEM,2,0,0,0};
    REQUIRE(set_block(0,Y,-2,NETHER_BRICKS));
    pitch=-.55f;
    REQUIRE(place() && world_cell(0,Y,-1)==AIR_OUTLET_WALL+SOUTH && inventory[0].count==1);
    py=Y+1.01f; pitch=-.66f;
    REQUIRE(place() && world_cell(0,Y+1,-2)==AIR_OUTLET_BASE+8 && !inventory[0].count);
    REQUIRE(world_point_blocked(.5f,Y+.5f,-.75f,1) && !world_point_blocked(.5f,Y+.5f,-.1f,1));
    REQUIRE(light_opacity(AIR_OUTLET_BASE)<16);
    py=Y+.01f; pitch=-.70f;
    REQUIRE(mine() && block(0,Y,-1)==AIR && drops[0].item.type==AIR_OUTLET_ITEM);
    REQUIRE(game_set_difficulty(NORMAL));
    printf("OUTLET FIXTURE TEST wall/floor/loot failures=%d\n",failures);
    return failures;
}

int world_selftest(void) {
    extern int faultback_selftest(void);
    extern int shaft_selftest(void);
    extern int build_blocks_selftest(void);
    extern int architecture_selftest(void);
    extern int item_frame_selftest(void);
    extern int seep_selftest(void);
    extern int settling_mold_selftest(void);
    extern int creative_effects_selftest(void);
    extern int lily_pad_selftest(void);
    extern int actor_water_selftest(void);
    extern int axe_selftest(void);
    extern int world_profiles_selftest(void);
    extern int survey_selftest(void);
    extern int aquifer_selftest(void);
    extern int shellback_selftest(void);
    extern int tide_selftest(void);
    extern int streaming_selftest(void);
    extern int redstone_timing_selftest(void);
    extern int block_light_selftest(void);
    extern int comparator_selftest(void);
    extern int pressure_selftest(void);
    extern int button_selftest(void);
    extern int interaction_selftest(void);
    extern int hopper_selftest(void);
    extern int dropper_selftest(void);
    extern int dispenser_selftest(void);
    extern int blast_selftest(void);
    extern int fire_selftest(void);
    extern int weather_selftest(void);
    extern int piston_selftest(void);
    extern int lava_selftest(void);
    extern int village_selftest(void);
    extern int air_station_selftest(void);
    extern int air_relay_selftest(void);
    extern int dungeon_selftest(void);
    extern int strata_gallery_selftest(void);
    extern int spillway_selftest(void);
    extern int water_wait_selftest(void);
    extern int lantern_moth_selftest(void);
    extern int kiln_mole_selftest(void);
    extern int mob_contact_selftest(void);
    int failures = world_profiles_selftest()+axe_selftest()+survey_selftest()+aquifer_selftest()+shellback_selftest()+tide_selftest(),
        lo = WY, hi = 0, river = 0, caves = 0, ores = 0;
    world_begin(0);
    for (int x = -2048; x <= 2048; x += 32) for (int z = -2048; z <= 2048; z += 32) {
        int h = terrain_height(x, z);
        if (h < lo) lo = h;
        if (h > hi) hi = h;
        if (h <= 25) river++;
    }
    /* Neighbouring cells straddle both the origin and negative chunk seams. */
    const int xs[] = {-513, -512, -17, -16, -1, 0, 15, 16, 513, 400000};
    for (unsigned i = 0; i < sizeof xs / sizeof xs[0]; i++) {
        int x = xs[i], z = -x - 1, h = terrain_height(x, z);
        if (!solid(block(x, h, z)) || block(x, 0, z) != BEDROCK) failures++;
        if (!set_block(x, h, z, AIR) || !set_block(x, h + 2, z, SNOW)) failures++;
    }
    /* Access more distinct chunks than the cache can hold. */
    for (int i = 0; i < CHUNK_CACHE + 16; i++) {
        int x = 20000 + i * CHUNK_SIZE, z = -30000;
        int h = terrain_height(x, z);
        for (int y = 4; y < h - 4; y++) {
            int t = block(x, y, z);
            if (t == AIR) caves++;
            if (t == COAL_ORE || t == IRON_ORE) ores++;
        }
    }
    for (unsigned i = 0; i < sizeof xs / sizeof xs[0]; i++) {
        int x = xs[i], z = -x - 1, h = terrain_height(x, z);
        if (block(x, h, z) != AIR || block(x, h + 2, z) != SNOW) failures++;
    }
    if (lo > 25 || hi < 55 || !river || !caves || !ores) failures++;
    printf("WORLD TEST heights=%d..%d river_samples=%d cave_cells=%d ores=%d failures=%d\n",
           lo, hi, river, caves, ores, failures);
    failures += water_selftest();
    failures += water_wait_selftest();
    failures += game_selftest();
    failures += crafting_selftest();
    failures += furnace_selftest();
    failures += climate_selftest();
    failures += portal_selftest();
    failures += actors_selftest();
    failures += nether_survival_selftest();
    failures += archery_selftest();
    failures += difficulty_selftest();
    failures += zombie_door_selftest();
    failures += rescue_selftest();
    failures += mushroom_selftest();
    failures += tide_crab_selftest();
    failures += navigation_selftest();
    failures += boat_selftest();
    failures += lily_pad_selftest();
    failures += railway_selftest();
    failures += detector_rail_selftest();
    failures += activator_rail_selftest();
    failures += chest_cart_selftest();
    failures += hopper_cart_selftest();
    failures += furnace_cart_selftest();
    failures += rail_junction_selftest();
    failures += free_cart_selftest();
    failures += tnt_cart_fall_selftest();
    failures += ender_chest_selftest();
    failures += faultback_selftest();
    failures += outlet_fixture_selftest();
    failures += air_station_selftest();
    failures += air_relay_selftest();
    failures += dungeon_selftest();
    failures += strata_gallery_selftest();
    failures += spillway_selftest();
    failures += lantern_moth_selftest();
    failures += kiln_mole_selftest();
    failures += mineshaft_selftest();
    failures += pyramid_selftest();
    failures += windcut_selftest();
    failures += rain_garden_selftest();
    failures += field_supplies_selftest();
    failures += hydraulic_sorter_selftest();
    failures += heat_bench_selftest();
    failures += joint_coast_selftest();
    failures += mob_contact_selftest();
    failures += horse_selftest();
    failures += fishing_selftest();
    failures += chest_selftest();
    failures += bed_selftest();
    failures += farming_selftest();
    failures += livestock_selftest();
    failures += enclosures_selftest();
    failures += lead_selftest();
    extern int silt_bed_selftest(void);
    failures += silt_bed_selftest();
    failures += brewing_selftest();
    failures += filter_bed_selftest();
    failures += water_breathing_selftest();
    failures += night_vision_selftest();
    failures += mining_press_selftest();
    failures += armor_selftest();
    failures += death_drop_selftest();
    failures += experience_selftest();
    failures += enchanting_selftest();
    failures += calibration_core_selftest();
    failures += anvil_selftest();
    failures += name_tag_selftest();
    failures += gravity_selftest();
    failures += redstone_selftest();
    failures += redstone_timing_selftest();
    failures += block_light_selftest();
    failures += comparator_selftest();
    failures += pressure_selftest();
    failures += button_selftest();
    failures += interaction_selftest();
    failures += hopper_selftest();
    failures += dropper_selftest();
    failures += dispenser_selftest();
    failures += blast_selftest();
    failures += fire_selftest();
    failures += weather_selftest();
    failures += piston_selftest();
    failures += lava_selftest();
    failures += village_selftest();
    failures += door_selftest();
    failures += shaft_selftest();
    failures += build_blocks_selftest();
    failures += architecture_selftest();
    failures += item_frame_selftest();
    failures += seep_selftest();
    failures += settling_mold_selftest();
    failures += creative_effects_selftest();
    failures += villager_selftest();
    failures += village_ai_selftest();
    failures += treasure_selftest();
    failures += horse_armor_selftest();
    failures += forestry_selftest();
    failures += leaf_decay_selftest();
    failures += golem_selftest();
    failures += snow_selftest();
    failures += wolf_selftest();
    failures += cat_selftest();
    failures += squid_selftest();
    failures += actor_water_selftest();
    failures += pane_selftest();
    failures += trapped_chest_selftest();
    failures += tripwire_selftest();
    extern int temple_selftest(void);
    failures += temple_selftest();
    failures += dye_selftest();
    failures += streaming_selftest();
    item_names_reset();
    chests_reset();
    brewing_reset();
    drops_reset();
    experience_reset();
    world_reset();
    return failures ? 1 : 0;
}
