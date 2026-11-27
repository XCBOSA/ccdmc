#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#include "save_test.h"
#include "../game/field_supplies.h"
#include "../game/item_frames.h"
#include "../game/fireworks.h"
#include "../world/pressure_layout.h"
#include "../game/game.h"
#include "../save/seed.h"
#include "../game/navigation.h"
#include "../game/item_custom.h"
#include "../game/furnace.h"
#include "../game/brewing.h"
#include "../game/armor.h"
#include "../game/drops.h"
#include "../game/experience.h"
#include "../game/enchanting.h"
#include "../game/anvil.h"
#include "../game/gravity.h"
#include "../world/redstone.h"
#include "../world/light.h"
#include "../world/lava.h"
#include "../world/villages.h"
#include "../world/temples.h"
#include "../world/dungeons.h"
#include "../world/mineshafts.h"
#include "../world/spillways.h"
#include "../game/villagers.h"
#include "../game/portals.h"
#include "../game/actors.h"
#include "../world/aquifer_layout.h"
#include "../game/button_arrows.h"
#include "../game/hoppers.h"
#include "../game/blast.h"
#include "../game/air_station.h"
#include "../game/dungeon.h"
#include "../game/fire.h"
#include "../game/weather.h"
#include "../game/pistons.h"
#include "../world/rails.h"
#include "../game/droppers.h"
#include "../game/boats.h"
#include "../game/minecarts.h"
#include "../game/horses.h"
#include "../game/leaf_decay.h"
#include "../game/fishing.h"
#include "../game/chests.h"
#include "../game/ender_chest.h"
#include "../game/tripwire.h"
#include "../game/beds.h"
#include "../world/barriers.h"
#include "../world/build_blocks.h"
#include "../save/save.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CHECK(condition) do { if (!(condition)) { printf("SAVE TEST failed line %d: %s (%s)\n", __LINE__, #condition, save_error()); failures++; } } while (0)

static int count_far_water(void *ctx,int x,int y,int z,int raw) {
    (void)raw;
    if (x==-6504 && y==170 && z==3208) ++*(int *)ctx;
    return 1;
}

static int save_test_farmer(VillagePlan plan) {
    int x,z;
    if (!village_home_entry(&plan,VILLAGER_FARMER,&x,&z)) return -1;
    (void)world_cell(x,plan.floor,z);
    px=x+.5f; py=plan.floor+1.01f; pz=z+.5f; vy=0.f;
    villagers_step(20);
    const ActorRecord *a=actors_in(OVERWORLD);
    for (int i=0;i<ACTOR_CAPACITY;++i) if (a[i].type==ACTOR_VILLAGER && a[i].traits==VILLAGER_FARMER) return i;
    return -1;
}

static int snapshot_timer(const RedstoneTiming *record,void *ctx) {
    RedstoneTiming *wanted=(RedstoneTiming *)ctx;
    if (wanted->position.x==record->position.x && wanted->position.y==record->position.y &&
        wanted->position.z==record->position.z) *wanted=*record;
    return 1;
}
static int snapshot_fire(const FireCell *record,void *ctx) {
    FireCell *wanted=(FireCell *)ctx;
    if (record->x==wanted->x && record->y==wanted->y && record->z==wanted->z) *wanted=*record;
    return 1;
}
static int temple_roundtrip(void) {
    int failures=0;
    if (!save_create_with_generation("丛林神庙 · 存档",SURVIVAL,GENERATOR_TEMPLES)) { CHECK(0); return failures; }
    TemplePlan temple={0};
    int found=0;
    for (int x=-4608;!found && x<=4608;x+=192) for (int z=-4608;z<=4608;z+=192) {
        if (temple_plan_for(x,z,&temple)) { found=1; break; }
    }
    CHECK(found && world_generation()==GENERATOR_TEMPLES);
    if (!found) { save_leave(); return failures; }
    BlockEdit trap=temple_position(temple,7,-4,4,DISPENSER_BASE+SOUTH),
        second=temple_position(temple,7,-4,1,DISPENSER_BASE+SOUTH),
        hidden=temple_position(temple,3,-4,2,0);
    ChestPos chest={hidden.x,hidden.y,hidden.z};
    ChestView v={OVERWORLD,{{0,0,0},{0,0,0}},0,0};
    (void)world_cell(trap.x,trap.y,trap.z);
    (void)world_cell(second.x,second.y,second.z);
    CHECK(world_cell(chest.x,chest.y,chest.z)==TEMPLE_CHEST_CELL);
    for (int i=0;i<8;++i) redstone_step(temple.x,temple.z);
    const Dropper *record=dropper_at(trap.x,trap.y,trap.z);
    CHECK(record!=NULL);
    if (record) {
        for (int i=0;i<DROPPER_SLOTS;++i)
            for (unsigned n=record->slots[i].count;n>0;--n) CHECK(dropper_extract(trap.x,trap.y,trap.z,i));
    }
    CHECK(set_block(second.x,second.y,second.z,AIR));
    CHECK(chest_open(chest,&v)==CHEST_OK);
    for (int i=0;i<CHEST_SLOTS;++i)
        for (int n=chest_slot(&v,i).count;n>0;--n) chest_automation_remove(&v,i);
    CHECK(save_write());
    uint32_t id=save_current()->id;
    save_leave();
    CHECK(save_load(id) && world_generation()==GENERATOR_TEMPLES && save_current()->format==CURRENT_FORMAT);
    CHECK(world_cell(trap.x,trap.y,trap.z)==trap.cell && world_cell(second.x,second.y,second.z)==AIR);
    for (int i=0;i<8;++i) redstone_step(temple.x,temple.z);
    record=dropper_at(trap.x,trap.y,trap.z);
    CHECK(record && !dropper_at(second.x,second.y,second.z));
    if (record) for (int i=0;i<DROPPER_SLOTS;++i) CHECK(!record->slots[i].count);
    CHECK(world_cell(chest.x,chest.y,chest.z)==CHEST && chest_open(chest,&v)==CHEST_OK);
    for (int i=0;i<CHEST_SLOTS;++i) CHECK(!chest_slot(&v,i).count);
    save_leave();
    return failures;
}

/* This fixture has no drops, only the old IDs and some running water. */
static int write_legacy_fixture(const char *path, int version) {
    unsigned char data[4096];
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    size_t count = fread(data, 1, sizeof data, fp);
    fclose(fp);
    enum { ARMOR_TRAILER=1+5*ARMOR_SLOTS, DROP_TRAILER=1+4*DIMENSION_COUNT, XP_TRAILER=8+4*DIMENSION_COUNT,
           FALL_TRAILER=8*DIMENSION_COUNT, TIMED_TRAILER=4*DIMENSION_COUNT, ARROW_TRAILER=DIMENSION_COUNT,
           BLAST_TRAILER=2*DIMENSION_COUNT, BURN_TRAILER=3 };
    if (count<147+ARMOR_TRAILER+DROP_TRAILER+XP_TRAILER+FALL_TRAILER+TIMED_TRAILER+ARROW_TRAILER+
            BLAST_TRAILER+BURN_TRAILER+WEATHER_FILE_BYTES+EMPTY_HORSE_GEAR_BYTES+EMPTY_LEAF_WORK_BYTES+6+2*DIMENSION_COUNT ||
            count==sizeof data || data[7]!=CURRENT_FORMAT || count<FIELD_SAVE_BYTES) return 0;
    if (version<100) count-=FIELD_SAVE_BYTES;
    if (version<86) {
        if (count<EMPTY_ITEM_FRAMES_BYTES) return 0;
        for (int i=0;i<EMPTY_ITEM_FRAMES_BYTES;++i) if (data[--count]) return 0;
    }
    if (version<85) count-=4; /* v84 does not have a day counter. */
    if (version<84 && (data[--count] || data[--count])) return 0; /* Empty v84 night vision. */
    if (version<83 && (data[--count] || data[--count])) return 0; /* Empty v83 water breathing. */
    if (version<81 && (data[--count] || data[--count])) return 0; /* Empty v81 regeneration. */
    if (version<80) for (int i=0;i<2*DIMENSION_COUNT;++i) if (data[--count]) return 0; /* Empty v80 entity names. */
    if (version<79 && (data[--count] || data[--count])) return 0; /* Empty v79 spawner timers. */
    if (version<78 && data[--count]) return 0; /* Empty v78 retired field. */
    /* The synthetic older defaults never had the new terrain generators. */
    unsigned generation_offset=18u+data[16];
    if (version<79 && generation_offset<count && data[generation_offset]>=GENERATOR_DUNGEONS)
        data[generation_offset]=GENERATOR_MUSHROOM_ISLAND;
    else if (version<91 && generation_offset<count && data[generation_offset]>=GENERATOR_PRESSURE_WORKS)
        data[generation_offset]=GENERATOR_AQUIFER;
    else if (version<92 && generation_offset<count && data[generation_offset]>=GENERATOR_SHELL_HABITATS)
        data[generation_offset]=GENERATOR_PRESSURE_WORKS;
    else if (version<98 && generation_offset<count && data[generation_offset]>=GENERATOR_WINDCUT)
        data[generation_offset]=GENERATOR_SEEP_GARDENS;
    else if (version<99 && generation_offset<count && data[generation_offset]>=GENERATOR_RAIN_GARDEN)
        data[generation_offset]=GENERATOR_WINDCUT;
    else if (version<106 && generation_offset<count && data[generation_offset]>=GENERATOR_JOINT_COAST)
        data[generation_offset]=GENERATOR_RAIN_GARDEN;
    else if (version<111 && generation_offset<count && data[generation_offset]>=GENERATOR_STRATA_GALLERIES)
        data[generation_offset]=GENERATOR_JOINT_COAST;
    else if (version<112 && generation_offset<count && data[generation_offset]>=GENERATOR_SPILLWAYS)
        data[generation_offset]=GENERATOR_STRATA_GALLERIES;
    if (version<77) for (int i=0;i<2*DIMENSION_COUNT;++i) if (data[--count]) return 0;
    if (version<75 && data[--count]) return 0; /* Empty v75 Ender inventory. */
    if (version==52 || version==53 || version==55 || version==65 || version>=74) {
        data[7]=(unsigned char)version;
        fp=fopen(path,"wb");
        if (!fp) return 0;
        int ok=fwrite(data,1,count,fp)==count;
        return fclose(fp)==0 && ok;
    }
    for (int i=0;i<EMPTY_LEAF_WORK_BYTES;++i) if (data[count-1-i]) return 0;
    count-=EMPTY_LEAF_WORK_BYTES;
    for (int i=0;i<EMPTY_HORSE_GEAR_BYTES;++i) if (data[count-1-i]) return 0;
    count-=EMPTY_HORSE_GEAR_BYTES;
    if (version==42) {
        data[7]=42;
        fp=fopen(path,"wb");
        if (!fp) return 0;
        int ok=fwrite(data,1,count,fp)==count;
        return fclose(fp)==0 && ok;
    }
    count-=WEATHER_FILE_BYTES;
    if (version==36) {
        data[7]=36;
        fp=fopen(path,"wb");
        if (!fp) return 0;
        int ok=fwrite(data,1,count,fp)==count;
        return fclose(fp)==0 && ok;
    }
    for (int i=0;i<BURN_TRAILER;++i) if (data[count-1-i]) return 0;
    count-=BURN_TRAILER;
    for (int i=0;i<BLAST_TRAILER;++i) if (data[count-1-i]) return 0;
    count-=BLAST_TRAILER;
    for (int i=0;i<ARROW_TRAILER;++i) if (data[count-1-i]) return 0;
    count-=ARROW_TRAILER;
    if (version==30) {
        data[7]=30;
        fp=fopen(path,"wb");
        if (!fp) return 0;
        int ok=fwrite(data,1,count,fp)==count;
        return fclose(fp)==0 && ok;
    }
    for (int i=0;i<TIMED_TRAILER;++i) if (data[count-1-i]) return 0;
    count-=TIMED_TRAILER;
    for (int i=0;i<FALL_TRAILER;++i) if (data[count-1-i]) return 0;
    count-=FALL_TRAILER;
    if (version==22) {
        data[7]=22;
        fp=fopen(path,"wb");
        if (!fp) return 0;
        int ok=fwrite(data,1,count,fp)==count;
        return fclose(fp)==0 && ok;
    }
    for (int i=0;i<4*DIMENSION_COUNT;++i) if (data[count-1-i]) return 0;
    count-=XP_TRAILER;
    if (memcmp(data+count,"\0\0\0\0",4)) return 0;
    if (version==21) {
        data[7]=21;
        fp=fopen(path,"wb");
        if (!fp) return 0;
        int ok=fwrite(data,1,count,fp)==count;
        return fclose(fp)==0 && ok;
    }
    for (int i=0;i<DROP_TRAILER;++i) if (data[count-1-i]) return 0;
    count-=DROP_TRAILER;
    if (version==20) {
        data[7]=20;
        fp=fopen(path,"wb");
        if (!fp) return 0;
        int ok=fwrite(data,1,count,fp)==count;
        return fclose(fp)==0 && ok;
    }
    for (int i=0;i<ARMOR_TRAILER;++i) if (data[count-1-i]) return 0;
    count-=ARMOR_TRAILER;
    /* These fixtures contain no zombies or skeletons (no v20 actor fields). */
    if (version==19) {
        data[7]=19;
        fp=fopen(path,"wb");
        if (!fp) return 0;
        int ok=fwrite(data,1,count,fp)==count;
        return fclose(fp)==0 && ok;
    }
    if (data[count-1] || data[count-2] || data[count-3]) return 0;
    count-=2; /* No fire-resistance effect for pre-v19 fixtures. */
    /* These header-only downgrades don't contain actor/horse additions. */
    if (version>=14 && version<=18) {
        data[7]=(unsigned char)version;
        fp=fopen(path,"wb");
        if (!fp) return 0;
        int ok=fwrite(data,1,count,fp)==count;
        return fclose(fp)==0 && ok;
    }
    count--; /* Fixtures being downgraded have no bed anchor or sheep. */
    if (version==13) {
        data[7]=13;
        fp=fopen(path,"wb");
        if (!fp) return 0;
        int ok=fwrite(data,1,count,fp)==count;
        return fclose(fp)==0 && ok;
    }
    if (version==12) {
        data[7]=12;
        fp=fopen(path,"wb");
        if (!fp) return 0;
        int ok=fwrite(data,1,count,fp)==count;
        return fclose(fp)==0 && ok;
    }
    if (memcmp(data+count-4,"\0\0\0\0",4)) return 0;
    count -= 4; /* Empty poison and hunger effects. */
    if (version==11) {
        data[7]=11;
        fp=fopen(path,"wb");
        if (!fp) return 0;
        int ok=fwrite(data,1,count,fp)==count;
        return fclose(fp)==0 && ok;
    }
    if (memcmp(data+count-4,"\0\0\0\0",4)) return 0;
    count -= 4; /* Three empty horse banks and no rider. */
    if (version==10) {
        data[7]=10;
        fp=fopen(path,"wb");
        if (!fp) return 0;
        int ok=fwrite(data,1,count,fp)==count;
        return fclose(fp)==0 && ok;
    }
    if (memcmp(data+count-4,"\0\0\0\0",4)) return 0;
    count -= 4; /* Three empty minecart banks and no passenger. */
    if (version==9) {
        data[7]=9;
        fp=fopen(path,"wb");
        if (!fp) return 0;
        int ok=fwrite(data,1,count,fp)==count;
        return fclose(fp)==0 && ok;
    }
    /* This fixture has three empty boat banks and no rider. */
    if (memcmp(data+count-4,"\0\0\0\0",4)) return 0;
    count -= 4;
    if (version==8) {
        data[7] = 8;
        fp = fopen(path,"wb");
        if (!fp) return 0;
        int ok=fwrite(data,1,count,fp)==count;
        return fclose(fp)==0 && ok;
    }
    count -= 8; /* The v8 End battle tail is not part of older formats. */
    size_t mode = 17u + data[16];
    size_t generation = mode + 1;
    size_t dimension = generation + 1, pose = dimension + 1;
    if (data[generation] != GENERATOR_LEGACY || data[dimension] != OVERWORLD) return 0;
    if (version == 6 || version == 7) {
        if (version == 6) {
            /* This fixture contains no actors in any of its three sections. */
            if (data[count-1] || data[count-2] || data[count-3]) return 0;
            count -= 3;
        }
        data[7] = (unsigned char)version;
        fp = fopen(path, "wb");
        if (!fp) return 0;
        int ok = fwrite(data,1,count,fp) == count;
        return fclose(fp) == 0 && ok;
    }
    size_t rules = pose + 6 * 4 + 1;
    size_t stacks = rules + 24, drops_offset = stacks + 36 * 5;
    if (drops_offset + 5 > count || memcmp(data + drops_offset, "\0\0\0\0", 4)) return 0;
    size_t end = drops_offset + 4;
    while (end < count && data[end]) {
        if (data[end] == 1) end += 11;
        else if (data[end] == 2) end += 10;
        else if (data[end] == 3 && version >= 4) end += 37;
        else return 0;
    }
    if (end >= count) return 0;
    count = end + 1;
    data[7] = (unsigned char)version;
    fp = fopen(path, "wb");
    if (!fp) return 0;
    if (version >= 3) {
        size_t prefix = version >= 5 ? dimension : generation;
        int ok = fwrite(data, 1, prefix, fp) == prefix &&
            fwrite(data + pose, 1, count - pose, fp) == count - pose;
        return fclose(fp) == 0 && ok;
    }
    int ok;
    if (version == 2) ok = fwrite(data, 1, generation, fp) == generation &&
        fwrite(data + pose, 1, stacks - pose, fp) == stacks - pose;
    else ok = fwrite(data, 1, mode, fp) == mode &&
        fwrite(data + pose, 1, rules - pose, fp) == rules - pose;
    for (size_t i = stacks; ok && i < drops_offset; i += 5)
        ok = !data[i+1] && !data[i+3] && !data[i+4] &&
            fputc(data[i], fp) != EOF && fputc(data[i+2], fp) != EOF;
    for (size_t i = drops_offset + 4; ok && i < count && data[i];) {
        if (data[i] == 1 && i + 11 <= count) {
            int cell = data[i + 10];
            if (cell >= 240 && cell < 248) data[i + 10] = (unsigned char)(32 + cell - 240);
            i += 11;
        } else if (data[i] == 2 && i + 10 <= count) i += 10;
        else ok = 0;
    }
    if (ok) ok = fwrite(data + drops_offset, 1, count - drops_offset, fp) == count - drops_offset;
    return fclose(fp) == 0 && ok;
}

static int write_retired_bovine_fixture(const char *path) {
    /* Cow and retired variant had identical v106 payloads. Locate the
       unique cow record by its four exact little-endian float values. */
    const unsigned char marker[]={ACTOR_COW,9,0,0,8,65,0,0,22,67,0,0,56,65,0,0,0,0};
    unsigned char data[4096];
    FILE *file=fopen(path,"rb+");
    if (!file) return 0;
    size_t n=fread(data,1,sizeof data,file),at=0;
    unsigned matches=0;
    for (size_t i=8;i+sizeof marker<=n;++i) if (!memcmp(data+i,marker,sizeof marker)) {
        at=i; ++matches;
    }
    int ok=n<sizeof data && matches==1 && data[7]==CURRENT_FORMAT &&
        !fseek(file,7,SEEK_SET) && fputc(106,file)!=EOF &&
        !fseek(file,(long)at,SEEK_SET) && fputc(ACTOR_TIDE_CRAB,file)!=EOF;
    return fclose(file)==0 && ok;
}
static int mark_archived_pack_member(const char *path) {
    /* After converting the envelope to v82, fill the historical age and
       action fields of the named actor at (2.5, 190, -10.5). */
    static const unsigned char marker[]={ACTOR_KILN_MOLE,11,0,0,0x20,0x40,0,0,0x3e,0x43,
        0,0,0x28,0xc1,0,0,0,0,0,0,0};
    const unsigned char replaced[]={ACTOR_KILN_MOLE,19,0,0,0x20,0x40,0,0,0x3e,0x43,
        0,0,0x28,0xc1,0,0,0,0,1,6,2};
    unsigned char data[4096];
    FILE *fp=fopen(path,"rb+");
    if (!fp) return 0;
    size_t n=fread(data,1,sizeof data,fp),at=0;
    unsigned found=0;
    for (size_t i=8;i+sizeof marker<=n;++i) if (!memcmp(data+i,marker,sizeof marker)) { ++found; at=i; }
    const int ok=found==1 && n<sizeof data && data[7]==82 &&
        !fseek(fp,(long)at,SEEK_SET) && fwrite(replaced,1,sizeof replaced,fp)==sizeof replaced;
    return fclose(fp)==0 && ok;
}

/* Corrupt the first position component in one isolated, current-format save.
   Write bytes rather than doing math on the values under -Ofast. */
static int replace_saved_player_x(const char *path, uint32_t bits) {
    FILE *fp=fopen(path,"r+b");
    if (!fp) return 0;
    int len=fseek(fp,16,SEEK_SET)==0 ? fgetc(fp) : EOF;
    const unsigned char bytes[]={
        (unsigned char)bits,(unsigned char)(bits>>8),(unsigned char)(bits>>16),(unsigned char)(bits>>24)
    };
    int ok=len>=1 && len<=SAVE_NAME_MAX && fseek(fp,len+3,SEEK_CUR)==0 &&
        fwrite(bytes,1,sizeof bytes,fp)==sizeof bytes;
    return fclose(fp)==0 && ok;
}

int save_selftest(void) {
    int failures = 0;
    const int x = -17, y = LEGACY_WY - 4, z = 15;
    char dir[] = ".voxel-test-XXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    if (!save_open_directory(dir) || !save_create_with_generation("施工世界",SURVIVAL,GENERATOR_GEOLOGY)) {
        puts(save_error()); rmdir(dir); return 1;
    }
    uint64_t seed1 = world_seed();
    CHECK(world_generation() == GENERATOR_GEOLOGY);
    int heights[32];
    for (int i = 0; i < 32; i++) heights[i] = terrain_height(i * 43, -i * 19);
    CHECK(set_block(x, y, z, WATER));
    CHECK(set_block(x + 1, y, z, STONE));
    CHECK(set_block(x, WY - 1, z, PACKED_ICE));
    water_step();
    CHECK(block(x, y - 1, z) == WATER && block(x, y - 2, z) == AIR);
    int surface = terrain_height(0, 0);
    CHECK(set_block(0, surface, 0, AIR));
    inventory[9] = (ItemStack){DIRT, 60, 0,0,0};
    inventory_move(0, 9);
    CHECK(inventory[9].count == 64 && inventory[0].count == 6);
    inventory_move(1, 0);
    CHECK(inventory[0].type == WATER && inventory[1].type == DIRT);
    inventory_move(9, 10);
    CHECK(inventory[9].count == 0 && inventory[10].count == 64 && inventory[10].type == DIRT);
    inventory_reset();
    for (int i = 0; i < SLOT_COUNT; i++) inventory[i] = (ItemStack){STONE, STACK_MAX, 0,0,0};
    CHECK(!inventory_add(STONE));
    inventory[33].count--;
    CHECK(inventory_add(STONE) && inventory[33].count == STACK_MAX);
    inventory_reset();
    inventory_move(0, 15);
    CHECK(inventory_add(DIRT) && inventory[15].count == 11 && inventory[0].type == AIR);
    selected_slot = 8;
    px = -1.25f; py = 37.25f; pz = 11.5f; yaw = 1.125f; pitch = -.5f; vy = 1.25f;
    drops[7] = (Drop){-.5f,33.5f,.5f,{DIAMOND_PICKAXE,1,1400,0,0},4122,7};
    inventory[14] = (ItemStack){IRON_PICKAXE, 1, 135,0,0};
    CHECK(set_block(x + 2, y, z, FURNACE));
    inventory[19] = (ItemStack){IRON_ORE, 2, 0,0,0};
    inventory[20] = (ItemStack){COAL, 1, 0,0,0};
    CHECK(furnace_deposit(x + 2, y, z, FURNACE_INPUT, 19) &&
        furnace_deposit(x + 2, y, z, FURNACE_FUEL, 20));
    for (int i = 0; i < 4; i++) furnaces_advance(1);
    const Furnace *old_furnace = furnace_at(x + 2, y, z);
    CHECK(old_furnace && old_furnace->cook == 80 && old_furnace->burn == 1520);
    player_rules.health = 13;
    player_rules.food = 10;
    player_rules.air = 7;
    player_rules.day_tick = 18300;
    CHECK(actors_restore(OVERWORLD,(ActorRecord){{-.5f,33.01f,11.5f},.2f,ACTOR_ZOMBIE,17,0}));
    CHECK(save_write());
    save_leave();
    CHECK(save_create_with_generation("", CREATIVE, GENERATOR_LEGACY));
    player_toggle_flight();
    CHECK(inventory_creative_pick(IRON_ORE));
    CHECK(save_write());
    uint64_t seed2 = world_seed();
    CHECK(seed1 != seed2);
    int different = 0;
    for (int i = 0; i < 32; i++) different |= heights[i] != terrain_height(i * 43, -i * 19);
    CHECK(different && block(x, y, z) != WATER && inventory[15].count == 0 && furnaces_count() == 0);
    save_leave();
    SaveInfo *items;
    size_t n;
    CHECK(save_list(&items, &n) && n == 2 && items[0].mode == CREATIVE &&
        items[0].seed == seed2 && items[1].seed == seed1 && !strcmp(items[1].name, "施工世界") &&
        items[0].generation == GENERATOR_LEGACY && items[1].generation == GENERATOR_GEOLOGY);
    free(items);
    CHECK(save_load(1) && world_seed() == seed1);
    CHECK(actors_in(OVERWORLD)[0].type == ACTOR_ZOMBIE && actors_in(OVERWORLD)[0].health == 17 &&
        actors_in(OVERWORLD)[0].feet.x == -.5f && actors_in(OVERWORLD)[0].heading == .2f);
    CHECK(world_generation() == GENERATOR_GEOLOGY && block(x, WY - 1, z) == PACKED_ICE);
    for (int i = 0; i < 32; i++) CHECK(heights[i] == terrain_height(i * 43, -i * 19));
    CHECK(world_cell(x, y, z) == WATER && block(x + 1, y, z) == STONE && block(0, surface, 0) == AIR);
    CHECK(block(x, y - 1, z) == WATER && block(x, y - 2, z) == AIR);
    water_step();
    CHECK(block(x, y - 2, z) == WATER);
    CHECK(px == -1.25f && py == 37.25f && pz == 11.5f && yaw == 1.125f && pitch == -.5f && vy == 1.25f);
    CHECK(selected_slot == 8 && inventory[15].type == DIRT && inventory[15].count == 11 &&
        inventory[14].type == IRON_PICKAXE && inventory[14].damage == 135 &&
        drops[0].item.count==1 && drops[0].item.type==DIAMOND_PICKAXE && drops[0].item.damage==1400 &&
        drops[0].age==4122 && drops[0].pickup_delay==7);
    CHECK(player_rules.mode == SURVIVAL && player_rules.health == 13 &&
        player_rules.food == 10 && player_rules.air == 7 && player_rules.day_tick == 18300);
    const Furnace *loaded_furnace = furnace_at(x + 2, y, z);
    CHECK(loaded_furnace && loaded_furnace->cook == 80 && loaded_furnace->burn == 1520 &&
        loaded_furnace->slots[FURNACE_INPUT].count == 2 &&
        loaded_furnace->slots[FURNACE_OUTPUT].count == 0);
    for (int i = 0; i < 6; i++) furnaces_advance(1);
    CHECK(loaded_furnace && loaded_furnace->slots[FURNACE_OUTPUT].type == IRON_INGOT &&
        loaded_furnace->slots[FURNACE_OUTPUT].count == 1 && loaded_furnace->slots[FURNACE_INPUT].count == 1);
    CHECK(world_select_dimension(NETHER));
    CHECK(set_block(x,y,z,NETHERRACK) && set_block(x+2,y,z,FURNACE));
    inventory[21] = (ItemStack){GOLD_ORE,1,0,0,0};
    CHECK(furnace_deposit(x+2,y,z,FURNACE_INPUT,21));
    CHECK(actors_restore(NETHER,(ActorRecord){{x+.5f,y+.01f,z+.5f},.3f,ACTOR_REEDLIZARD,12,REED_HARVESTED}));
    CHECK(actors_restore_reed_cooldown(NETHER,0,180) &&
        !actors_restore_reed_cooldown(NETHER,0,REED_COOL_TICKS+1));
    drops[0] = make_drop(x+.5f,y+.5f,z+.5f,(ItemStack){QUARTZ,1,0,0,0});
    CHECK(world_select_dimension(END));
    CHECK(set_block(x,y,z,PLANKS));
    px=100.5f; py=49.01f; pz=.5f;
    portals_set_end_return((EndReturn){1,{-2.5f,70.01f,-4.5f}});
    CHECK(boat_restore(OVERWORLD,(BoatRecord){{1.5f,62.1f,55.5f},.9f,1}));
    CHECK(boat_restore(END,(BoatRecord){{100.5f,48.83f,.5f},0,1}));
    CHECK(boat_restore_rider(0));
    CHECK(save_write() && save_current()->dimension==END);
    save_leave();
    CHECK(save_load(1) && save_current()->dimension==OVERWORLD && world_dimension()==OVERWORLD &&
        save_current()->returned_from_retired_site && !collides(px,py,pz) && !player_rules.flying &&
        player_rules.health==13 && player_rules.food==10);
    CHECK(save_list(&items,&n));
    int disk_site=-1;
    for (size_t i=0;i<n;++i) if (items[i].id==1) disk_site=items[i].dimension;
    free(items);
    CHECK(disk_site==END); /* Landing alone must not rewrite the old file. */
    CHECK(!actors_in(END)[0].type && actors_in(OVERWORLD)[0].type == ACTOR_ZOMBIE);
    CHECK(portals_end_return().valid && portals_end_return().position.x==-2.5f);
    CHECK(boat_rider()<0 && boats_in(END)[0].position.x==100.5f &&
        boats_in(OVERWORLD)[0].heading==.9f && !boats_in(NETHER)[0].occupied);
    CHECK(world_select_dimension(END) && block(x,y,z)==PLANKS && !drops[0].item.count &&
        world_select_dimension(OVERWORLD));
    CHECK(save_write());
    save_leave();
    CHECK(save_load(1) && !save_current()->returned_from_retired_site);
    CHECK(world_select_dimension(NETHER) && block(x,y,z)==NETHERRACK && drops[0].item.type==QUARTZ &&
        actors_in(NETHER)[0].type == ACTOR_REEDLIZARD && actors_in(NETHER)[0].health == 12 &&
        actors_in(NETHER)[0].traits == REED_HARVESTED && actors_reed_phase(NETHER,0)==REED_COOLED &&
        actors_reed_cooldown(NETHER,0)==180 &&
        furnace_at(x+2,y,z) && furnace_at(x+2,y,z)->slots[FURNACE_INPUT].type==GOLD_ORE);
    CHECK(world_select_dimension(OVERWORLD) && block(x,y,z)==WATER && drops[0].item.type==DIAMOND_PICKAXE &&
        furnace_at(x+2,y,z) && furnace_at(x+2,y,z)->slots[FURNACE_OUTPUT].type==IRON_INGOT);
    save_leave();
    CHECK(save_load(2) && world_seed() == seed2 && block(x, y, z) != WATER);
    CHECK(player_rules.mode == CREATIVE && player_rules.flying && inventory[0].type == IRON_ORE &&
        world_generation() == GENERATOR_LEGACY);
    CHECK(set_block(x, y, z, WATER));
    water_step();
    int old_flow = world_cell(x, y - 1, z);
    CHECK(old_flow >= FLOW_BASE);
    CHECK(save_write());
    save_leave();
    char path[100];
    snprintf(path, sizeof path, "%s/00000002.vxl", dir);
    CHECK(write_legacy_fixture(path,11));
    CHECK(save_load(2) && !player_effects.poison && !player_effects.hunger);
    CHECK(save_write());
    save_leave();
    CHECK(write_legacy_fixture(path,10));
    CHECK(save_load(2) && horse_rider()==-1 && !horses_in(OVERWORLD)[0].type);
    CHECK(save_write());
    save_leave();
    CHECK(write_legacy_fixture(path,9));
    CHECK(save_load(2) && cart_rider()==-1 && !carts_in(OVERWORLD)[0].occupied);
    CHECK(save_write());
    save_leave();
    CHECK(write_legacy_fixture(path, 8));
    CHECK(save_load(2) && boat_rider()==-1 && !boats_in(OVERWORLD)[0].occupied);
    CHECK(save_write());
    save_leave();
    CHECK(write_legacy_fixture(path, 7));
    CHECK(save_load(2) && world_dimension()==OVERWORLD);
    CHECK(save_write());
    save_leave();
    CHECK(write_legacy_fixture(path, 6));
    CHECK(save_load(2) && world_dimension() == OVERWORLD && !actors_in(OVERWORLD)[0].type);
    CHECK(save_write());
    save_leave();
    CHECK(write_legacy_fixture(path, 5));
    CHECK(save_load(2) && world_dimension() == OVERWORLD && save_current()->generation == GENERATOR_LEGACY);
    CHECK(save_write());
    save_leave();
    CHECK(write_legacy_fixture(path, 4));
    CHECK(save_load(2) && save_current()->generation == GENERATOR_LEGACY &&
        world_cell(x, y - 1, z) == old_flow);
    CHECK(save_write());
    save_leave();
    CHECK(write_legacy_fixture(path, 3));
    CHECK(save_load(2) && player_rules.mode == CREATIVE && world_cell(x, y - 1, z) == old_flow);
    CHECK(save_write());
    save_leave();
    CHECK(write_legacy_fixture(path, 2));
    CHECK(save_load(2) && player_rules.mode == CREATIVE && player_rules.flying &&
        inventory[0].type == IRON_ORE && world_cell(x, y - 1, z) == old_flow);
    CHECK(save_write());
    save_leave();
    CHECK(write_legacy_fixture(path, 1));
    CHECK(save_load(2) && player_rules.mode == SURVIVAL && !player_rules.flying &&
        player_rules.health == MAX_HEALTH && inventory[0].type == IRON_ORE &&
        world_cell(x, y - 1, z) == old_flow);
    CHECK(save_write());
    save_leave();
    snprintf(path, sizeof path, "%s/00000002.vxl", dir);
    FILE *fp = fopen(path, "wb");
    if (fp) { fputs("truncated fixture", fp); fclose(fp); }
    CHECK(fp != NULL && !save_load(2) && save_current() == NULL);
    CHECK(save_create("第三个世界") && save_current()->id == 3 && world_seed() != seed1 && world_seed() != seed2);
    CHECK(set_block(-17,124,-16,REDSTONE_BLOCK) && set_block(-16,124,-16,STONE));
    CHECK(rail_place((RailCell){-17,125,-16},RAIL_BOOSTER,1) && rail_place((RailCell){-16,125,-16},RAIL_BOOSTER,1));
    CHECK(cart_restore(OVERWORLD,(CartRecord){{-16.5f,125.045f,-15.5f},{-17,125,-16},.5f,3.f,1.57f,1,CART_RIDEABLE,0,0,{0,0,0},0}));
    CHECK(cart_restore(OVERWORLD,(CartRecord){{-19.5f,125.6f,-15.5f},{0,0,0},0,0,1.57f,1,CART_CHEST,0,1,{2.f,-3.f,1.f},0}));
    CHECK(cart_container_insert(1,(ItemStack){STONE,1,0,0,0})==1);
    px=-16.5f; py=125.295f; pz=-15.5f;
    CHECK(cart_restore_rider(0) && save_write());
    save_leave();
    CHECK(save_load(3) && cart_rider()==0 && carts_in(OVERWORLD)[0].speed==3.f &&
        world_cell(-17,125,-16)==TRACK_POWERED+STRAIGHT_EW &&
        rail_is_energized((RailCell){-16,125,-16}) && !carts_in(NETHER)[0].occupied &&
        carts_in(OVERWORLD)[1].off_track && carts_in(OVERWORLD)[1].free_velocity.y==-3.f &&
        carts_in(OVERWORLD)[1].free_velocity.x==2.f && cart_cargo_in(OVERWORLD,1)[0].count==1);
    cart_unseat();
    CHECK(set_block(-22,139,-21,STONE) && set_block(-20,140,-21,CHEST));
    HorseRecord horse={{-21.5f,140.01f,-20.5f},.4f,0,720,0,EQUINE_HORSE,2,24,19,71,88,95,1,1,{0},0};
    HorseRecord mule={{-23.5f,140.01f,-22.5f},-.7f,24000,0,0,EQUINE_MULE,1,23,23,55,61,0,0,0,{0},0};
    CHECK(horse_restore(OVERWORLD,horse) && horse_restore(NETHER,mule));
    px=horse.feet.x; py=141.05f; pz=horse.feet.z;
    CHECK(horse_restore_rider(0) && save_write());
    save_leave();
    CHECK(save_load(3) && horse_rider()==0 && cart_rider()==-1 && boat_rider()==-1 &&
        horses_in(OVERWORLD)[0].health==19 && horses_in(OVERWORLD)[0].saddle==1 &&
        horses_in(OVERWORLD)[0].cooldown==720 && horses_in(NETHER)[0].age==24000 &&
        horses_in(NETHER)[0].type==EQUINE_MULE && block(-20,140,-21)==CHEST);
    horse_unseat();
    CHECK(set_block(-24,140,-22,FURNACE));
    inventory[6]=(ItemStack){RAW_SALMON,2,0,0,0};
    inventory[7]=(ItemStack){COAL,1,0,0,0};
    CHECK(furnace_deposit(-24,140,-22,FURNACE_INPUT,6));
    CHECK(furnace_deposit(-24,140,-22,FURNACE_FUEL,7));
    for (int i=0;i<10;++i) furnaces_advance(1);
    const Furnace *salmon=furnace_at(-24,140,-22);
    CHECK(salmon && salmon->slots[FURNACE_OUTPUT].type==COOKED_SALMON);
    inventory[0]=(ItemStack){FISHING_ROD,1,7,0,0}; selected_slot=0;
    player_effects=(PlayerEffects){.poison=992,.hunger=141};
    CHECK(fishing_use() && fishing_active() && save_write());
    save_leave();
    CHECK(save_load(3) && !fishing_active() && inventory[0].type==FISHING_ROD &&
        inventory[0].damage==7 && player_effects.poison==992 && player_effects.hunger==141);
    salmon=furnace_at(-24,140,-22);
    CHECK(salmon && salmon->slots[FURNACE_OUTPUT].type==COOKED_SALMON &&
        salmon->slots[FURNACE_INPUT].type==RAW_SALMON);
    ChestPos west={-33,145,8},east={-32,145,8};
    ChestView v;
    memset(&v,0,sizeof v);
    CHECK(set_block(west.x,west.y,west.z,CHEST) && set_block(east.x,east.y,east.z,CHEST));
    CHECK(chest_open(east,&v)==CHEST_OK && v.count==2);
    inventory[9]=(ItemStack){COOKED_SALMON,12,0,0,0};
    inventory[10]=(ItemStack){IRON_PICKAXE,1,143,0,0};
    CHECK(chest_move(&v,63,25,0)==CHEST_OK && chest_move(&v,64,53,0)==CHEST_OK);
    CHECK(world_select_dimension(NETHER) && set_block(west.x,west.y,west.z,CHEST) &&
        chest_open(west,&v)==CHEST_OK && v.count==1);
    inventory[9]=(ItemStack){QUARTZ,23,0,0,0};
    CHECK(chest_move(&v,63,25,0)==CHEST_OK);
    CHECK(world_select_dimension(OVERWORLD) && save_write());
    save_leave();
    CHECK(save_load(3) && chests_count_in(OVERWORLD)==2 && chests_count_in(NETHER)==1 &&
        chest_open(west,&v)==CHEST_OK && chest_slot(&v,25).count==12 &&
        chest_slot(&v,25).type==COOKED_SALMON && chest_slot(&v,53).type==IRON_PICKAXE &&
        chest_slot(&v,53).damage==143);
    CHECK(world_select_dimension(NETHER) && chest_open(west,&v)==CHEST_OK &&
        chest_slot(&v,25).type==QUARTZ && chest_slot(&v,25).count==23);
    save_leave();
    CHECK(save_create_with_generation("v12 empties",SURVIVAL,GENERATOR_EQUINE));
    CHECK(set_block(30,145,0,CHEST) && set_block(32,145,0,SADDLE_CHEST) && save_write());
    save_leave();
    snprintf(path,sizeof path,"%s/00000004.vxl",dir);
    CHECK(write_legacy_fixture(path,12) && save_load(4) && chests_count_in(OVERWORLD)==0);
    CHECK(chest_open((ChestPos){30,145,0},&v)==CHEST_OK && !chest_slot(&v,13).count);
    CHECK(chest_open((ChestPos){32,145,0},&v)==CHEST_OK && chest_slot(&v,13).type==SADDLE);
    CHECK(chest_slot(&v,14).type==SUGAR_CANE && chest_move(&v,14,64,0)==CHEST_OK &&
        inventory[10].type==SUGAR_CANE);
    /* Subsequent synthetic v13..v22 fixtures must not include v23-only loot. */
    inventory[10]=(ItemStack){0,0,0,0,0};
    CHECK(chest_move(&v,13,63,0)==CHEST_OK && save_write());
    save_leave();
    CHECK(save_load(4) && chest_open((ChestPos){32,145,0},&v)==CHEST_OK &&
        !chest_slot(&v,13).count && inventory[9].type==SADDLE && chests_count_in(OVERWORLD)==0);
    save_leave();
    CHECK(write_legacy_fixture(path,13) && save_load(4) && !bed_anchor().valid);
    CHECK(save_write());
    save_leave();
    CHECK(write_legacy_fixture(path,14) && save_load(4) && world_generation()==GENERATOR_EQUINE);
    CHECK(save_write());
    save_leave();
    CHECK(write_legacy_fixture(path,15) && save_load(4) && world_generation()==GENERATOR_EQUINE);
    CHECK(save_write());
    save_leave();
    CHECK(write_legacy_fixture(path,16) && save_load(4) && world_generation()==GENERATOR_EQUINE);
    CHECK(save_write());
    save_leave();
    CHECK(write_legacy_fixture(path,17) && save_load(4) && world_generation()==GENERATOR_EQUINE);
    CHECK(save_write());
    save_leave();
    CHECK(write_legacy_fixture(path,18) && save_load(4) && world_generation()==GENERATOR_EQUINE && !player_effects.fire);
    CHECK(save_write());
    save_leave();
    CHECK(write_legacy_fixture(path,19) && save_load(4) && !armor_worn(ARMOR_HEAD).count &&
        !player_rules.health_debt && world_generation()==GENERATOR_EQUINE);
    CHECK(save_write());
    save_leave();
    CHECK(write_legacy_fixture(path,20) && save_load(4) && !death_keep_inventory() &&
        !death_drop_count(OVERWORLD));
    CHECK(save_write());
    save_leave();
    CHECK(write_legacy_fixture(path,21) && save_load(4) && !experience_points());
    CHECK(save_write());
    save_leave();
    CHECK(write_legacy_fixture(path,22) && save_load(4) && !experience_points() &&
        world_generation()==GENERATOR_EQUINE);
    px=-19.5f; py=148.01f; pz=18.5f;
    for (int x=-19;x<=-14;++x) for (int z=17;z<=20;++z)
        CHECK(set_block(x,147,z,GRASS));
    BedPos foot={-17,148,18},head={-16,148,18};
    CHECK(bed_place(foot,1));
    player_rules.day_tick=14500;
    CHECK(bed_use((Hit){1,foot.x,foot.y,foot.z,0,0,0}) && bed_sleeping());
    CHECK(actors_restore(OVERWORLD,(ActorRecord){{-16.5f,148.01f,20.5f},0,ACTOR_SHEEP,8,SHEEP_SHORN}));
    inventory[6]=(ItemStack){SHEARS,1,37,0,0};
    CHECK(save_write());
    save_leave();
    CHECK(save_load(4) && !bed_sleeping() && bed_anchor().valid && bed_anchor().head.x==head.x &&
        world_cell(foot.x,foot.y,foot.z)==BED_BASE+1 && world_cell(head.x,head.y,head.z)==BED_BASE+5 &&
        actors_in(OVERWORLD)[0].type==ACTOR_SHEEP && actors_in(OVERWORLD)[0].traits==SHEEP_SHORN &&
        inventory[6].type==SHEARS && inventory[6].damage==37);
    player_respawn();
    CHECK(fabsf(px-head.x-.5f)<=1.f && fabsf(pz-head.z-.5f)<=1.f);
    const BlockEdit crops[]={{-17,148,1,FARM_BASE+7},{-17,149,1,WHEAT_BASE+3},
        {-18,148,1,FARM_BASE+1},{-18,149,1,CARROT_BASE+7},
        {-19,148,1,FARM_BASE+5},{-19,149,1,POTATO_BASE+1}};
    CHECK(world_set_blocks(crops,6));
    CHECK(set_block(-16,148,1,STONE) && set_block(-16,149,1,TORCH));
    inventory[5]=(ItemStack){IRON_HOE,1,21,0,0};
    CHECK(actors_restore(OVERWORLD,(ActorRecord){{-9.5f,145.1f,2.5f},1,ACTOR_SKELETON,6,0}));
    CHECK(save_write());
    save_leave();
    CHECK(save_load(4) && save_current()->format==CURRENT_FORMAT && world_generation()==GENERATOR_EQUINE &&
        inventory[5].type==IRON_HOE && inventory[5].damage==21 && block(-16,149,1)==TORCH &&
        actors_in(OVERWORLD)[1].type==ACTOR_SKELETON && actors_in(OVERWORLD)[1].health==6);
    BlockEdit samples[8];
    for (int i=0;i<6;++i) CHECK(world_cell(crops[i].x,crops[i].y,crops[i].z)==crops[i].cell);
    CHECK(world_farm_batch(-2,0,samples,8)==6);
    ActorRecord calf={{-17.5f,148.01f,18.5f},1,ACTOR_COW,10,0};
    ActorRecord chicken={{-17.5f,148.01f,16.5f},1,ACTOR_CHICKEN,4,0};
    CHECK(actors_restore_with_care(OVERWORLD,calf,(ActorCare){12345,0,0,0,1}));
    CHECK(actors_restore_with_care(OVERWORLD,chicken,(ActorCare){0,36,0,6402,1}));
    CHECK(actors_restore_with_care(OVERWORLD,(ActorRecord){{-18.5f,148.01f,18.5f},0,ACTOR_PIG,10,0},
        (ActorCare){0,0,119,0,1}));
    inventory[10]=(ItemStack){MILK_BUCKET,1,0,0,0};
    player_effects.hunger=CHICKEN_HUNGER_TICKS;
    CHECK(save_write());
    save_leave();
    CHECK(save_load(4) && inventory[10].type==MILK_BUCKET && player_effects.hunger==CHICKEN_HUNGER_TICKS);
    const ActorRecord *farm=actors_in(OVERWORLD);
    const ActorCare *care=actors_care_in(OVERWORLD);
    CHECK(farm[2].type==ACTOR_COW && care[2].managed && care[2].grow==12345 &&
        farm[3].type==ACTOR_CHICKEN && care[3].managed && care[3].egg==6402 && care[3].cooldown==36 &&
        farm[4].type==ACTOR_PIG && care[4].love==119 && care[4].managed);
    CHECK(set_block(-17,147,6,STONE) && gate_place(-17,148,6,EAST) && gate_toggle(-17,148,6,WEST));
    CHECK(set_block(-17,148,5,WOOD_FENCE) && set_block(-17,148,7,WOOD_FENCE));
    CHECK(set_block(-19,147,6,REDSTONE_BLOCK) && gate_place(-19,148,6,SOUTH));
    CHECK(world_select_dimension(NETHER) && set_block(-17,148,6,NETHER_FENCE) && world_select_dimension(OVERWORLD));
    inventory[11]=(ItemStack){FENCE_GATE,5,0,0,0};
    CHECK(save_write());
    save_leave();
    GateState gate={0,0,0};
    CHECK(save_load(4) && inventory[11].type==FENCE_GATE && inventory[11].count==5 &&
        block(-17,148,5)==WOOD_FENCE && block(-17,148,7)==WOOD_FENCE &&
        gate_decode(world_cell(-17,148,6),&gate) && gate.facing==WEST && gate.open && !gate.powered);
    CHECK(gate_decode(world_cell(-19,148,6),&gate) && gate.facing==SOUTH && gate.open && gate.powered);
    CHECK(world_select_dimension(NETHER) && block(-17,148,6)==NETHER_FENCE && world_select_dimension(OVERWORLD));
    lead_set(actors_lead_at(OVERWORLD,2),(LeadLink){LEAD_FENCE,-17,148,7});
    lead_set(actors_lead_at(OVERWORLD,3),(LeadLink){LEAD_PENDING,0,0,0});
    CHECK(actors_restore(OVERWORLD,(ActorRecord){{-17.5f,148.01f,15.5f},0,ACTOR_SILTBACK,3,0}));
    HorseRecord donkey={{-17.5f,148.01f,9.5f},0,0,0,0,EQUINE_DONKEY,0,20,20,60,50,30,1,0,{0},0};
    CHECK(horse_restore_with_lead(OVERWORLD,donkey,(LeadLink){LEAD_PLAYER,0,0,0}));
    inventory[12]=(ItemStack){SILT_GEL,6,0,0,0};
    inventory[13]=(ItemStack){LEAD,2,0,0,0};
    CHECK(save_write());
    save_leave();
    CHECK(save_load(4) && farm[5].type==ACTOR_SILTBACK && farm[5].traits==0 && farm[5].health==3 &&
        inventory[12].type==SILT_GEL && inventory[13].type==LEAD);
    LeadLink saved=*actors_lead_at(OVERWORLD,2);
    CHECK(saved.kind==LEAD_FENCE && saved.x==-17 && saved.y==148 && saved.z==7 &&
        actors_lead_at(OVERWORLD,3)->kind==LEAD_PENDING && horses_in(OVERWORLD)[0].type==EQUINE_DONKEY &&
        horses_lead_at(OVERWORLD,0)->kind==LEAD_PLAYER && !actors_lead_at(NETHER,2)->kind);
    CHECK(set_block(-15,149,5,GLASS) && set_block(-15,149,6,ASH_SAND));
    const BlockEdit filter={-15,150,6,FILTER_MAT_TWO};
    CHECK(world_set_blocks(&filter,1) && set_block(-15,150,7,BREWING_STAND));
    inventory[12]=(ItemStack){WATER_BOTTLE,1,0,0,0}; inventory[13]=(ItemStack){LIMESTONE_ITEM,4,0,0,0};
    CHECK(brew_deposit(-15,150,7,1,12) && brew_deposit(-15,150,7,BREW_INGREDIENT,13));
    for (int i=0;i<7;++i) brewing_advance(1.f);
    inventory[14]=(ItemStack){WATER_BOTTLE,1,0,0,0};
    CHECK(brew_deposit(-15,150,7,0,14));
    CHECK(brew_at(-15,150,7) && brew_at(-15,150,7)->active==BREW_RESERVE+1 &&
        brew_at(-15,150,7)->progress==140);
    CHECK(world_select_dimension(NETHER) && set_block(-15,150,7,BREWING_STAND));
    inventory[15]=(ItemStack){WATER_BOTTLE,1,0,0,0}; inventory[16]=(ItemStack){LIMESTONE_ITEM,1,0,0,0};
    CHECK(brew_deposit(-15,150,7,0,15) && brew_deposit(-15,150,7,BREW_INGREDIENT,16));
    brewing_advance(.5f);
    CHECK(actors_restore(NETHER,(ActorRecord){{-13.5f,69.01f,4.5f},0,ACTOR_REEDLIZARD,2,0}));
    CHECK(world_select_dimension(OVERWORLD));
    player_effects.fire=8102;
    CHECK(armor_restore(ARMOR_BODY,(ItemStack){IRON_CHESTPLATE,1,211,0,0}));
    CHECK(armor_restore(ARMOR_FEET,(ItemStack){GOLD_BOOTS,1,87,0,0}));
    CHECK(!armor_restore(ARMOR_HEAD,(ItemStack){GOLD_BOOTS,1,1,0,0}));
    player_take_damage(DAMAGE_PROJECTILE,3);
    const int armor_health=player_rules.health,debt=player_rules.health_debt;
    CHECK(debt>0 && armor_points()==7);
    Vec3 zombie_feet={-22.5f,149.01f,8.5f};
    uint8_t kit=armor_make_kit(ARMOR_CHAIN,3u);
    CHECK(actors_restore_snapshot(OVERWORLD,(ActorRecord){zombie_feet,1,ACTOR_ZOMBIE,17,kit},(ActorCare){0,0,0,0,0},
        (LeadLink){LEAD_FREE,0,0,0},(ActorArmorState){.helmet_wear=11,.health_debt=17,.door_breaker=1}));
    CHECK(save_write());
    save_leave();
    CHECK(save_load(4) && player_effects.fire==8102 && world_cell(filter.x,filter.y,filter.z)==FILTER_MAT_TWO &&
        block(-15,149,5)==GLASS && player_rules.health==armor_health && player_rules.health_debt==debt &&
        armor_worn(ARMOR_BODY).type==IRON_CHESTPLATE && armor_worn(ARMOR_BODY).damage==212 &&
        armor_worn(ARMOR_FEET).type==GOLD_BOOTS && armor_worn(ARMOR_FEET).damage==88);
    int armored=0;
    for (int i=0;i<ACTOR_CAPACITY;++i) if (actors_in(OVERWORLD)[i].type==ACTOR_ZOMBIE &&
        actors_in(OVERWORLD)[i].traits==kit) {
        ++armored;
        ActorArmorState a=actors_armor_state(OVERWORLD,i);
        CHECK(a.health_debt==17 && a.helmet_wear==11 && a.door_breaker==1 &&
              !actors_door_attack(OVERWORLD,i).stage && actors_in(OVERWORLD)[i].health==17);
    }
    CHECK(armored==1);
    const BrewingStand *stand=brew_at(-15,150,7);
    CHECK(stand && stand->progress==140 && stand->active==BREW_RESERVE+1 &&
        stand->slots[1].type==WATER_BOTTLE && stand->slots[BREW_INGREDIENT].count==4);
    for (int i=0;i<8;++i) brewing_advance(1.f);
    CHECK(stand && stand->slots[1].type==POTION_FIRE_LONG && stand->slots[0].type==WATER_BOTTLE &&
        stand->slots[BREW_INGREDIENT].count==1);
    CHECK(world_select_dimension(NETHER) && actors_in(NETHER)[0].type==ACTOR_REEDLIZARD &&
        actors_in(NETHER)[0].traits==0 && actors_in(NETHER)[0].health==2);
    CHECK(brew_at(-15,150,7) && brew_at(-15,150,7)->progress==10 && brew_at(-15,150,7)->slots[0].type==WATER_BOTTLE);
    save_leave();

    CHECK(save_create("死亡物品 · 跨维度") && save_current()->id==5);
    CHECK(death_set_keep_inventory(1) && save_write());
    save_leave();
    CHECK(save_load(5) && death_keep_inventory() && !death_set_keep_inventory(2));
    CHECK(death_set_keep_inventory(0) && world_select_dimension(NETHER));
    const int death_x=-65,death_y=147,death_z=-16;
    CHECK(set_block(death_x,death_y-1,death_z,STONE));
    px=death_x+.5f; py=death_y+.01f; pz=death_z+.5f;
    memset(inventory,0,sizeof inventory);
    inventory[0]=(ItemStack){GOLD_SWORD,1,12,0,0};
    CHECK(armor_restore(ARMOR_FEET,(ItemStack){CHAIN_BOOTS,1,157,0,0}));
    CHECK(death_drop_restore(OVERWORLD,(Drop){.5f,149.25f,.5f,{DIAMOND,3,0,0,0},5998,0}));
    player_take_damage(DAMAGE_MELEE,100);
    CHECK(!player_rules.health && !inventory[0].count && !armor_points() &&
        death_drop_count(NETHER)==2 && death_drop_count(OVERWORLD)==1 && save_write());
    save_leave();
    CHECK(save_load(5) && !player_rules.health && !death_keep_inventory() && world_dimension()==NETHER &&
        death_drop_count(NETHER)==2 && death_drop_count(OVERWORLD)==1 && !inventory[0].count);
    CHECK(player_respawn()==RESPAWN_OK && world_dimension()==OVERWORLD && death_drop_count(NETHER)==2);
    CHECK(world_select_dimension(NETHER));
    px=death_x+.5f; py=death_y+.01f; pz=death_z+.5f;
    CHECK(!drops_step(.05f,DEATH_DROP_PICKUP_TICKS-1) && death_drop_count(NETHER)==2);
    CHECK(save_write());
    save_leave();
    CHECK(save_load(5) && world_dimension()==NETHER && death_drop_count(NETHER)==2);
    CHECK(drops_step(.05f,1) && !death_drop_count(NETHER) && death_drop_count(OVERWORLD)==1 &&
        inventory[0].type==GOLD_SWORD && inventory[0].damage==12 && inventory[1].type==CHAIN_BOOTS &&
        inventory[1].damage==182);
    CHECK(world_select_dimension(OVERWORLD));
    CHECK(set_block(0,148,0,STONE));
    px=.5f; py=146.f; pz=2.f;
    CHECK(!drops_step(.05f,1) && death_drop_count(OVERWORLD)==1);
    CHECK(!drops_step(.05f,1) && !death_drop_count(OVERWORLD));
    save_leave();
    CHECK(save_create("经验 · 跨维度") && save_current()->id==6);
    CHECK(experience_restore_player(824,0x12345678u));
    const XPOrb over={{.5f,152.20f,.5f},{0,.25f,0},1,621};
    const XPOrb nether={{-64.5f,151.85f,-15.5f},{.125f,0,-.25f},37,5999};
    CHECK(experience_restore_orb(OVERWORLD,over) && experience_restore_orb(NETHER,nether));
    CHECK(set_block(0,151,0,STONE));
    px=.5f; py=152.01f; pz=.5f;
    CHECK(save_write());
    save_leave();
    CHECK(save_load(6) && experience_points()==824 && experience_rng()==0x12345678u &&
        experience_progress().level==29 && experience_progress().within==58);
    size_t count=0;
    const XPOrb *list=experience_orbs(OVERWORLD,&count);
    CHECK(count==1 && list[0].age==621 && list[0].value==1 && list[0].velocity.y==.25f);
    list=experience_orbs(NETHER,&count);
    CHECK(count==1 && list[0].age==5999 && list[0].value==37 && list[0].point.x==-64.5f && list[0].velocity.z==-.25f);
    experience_step(.05f,1);
    CHECK(experience_points()==825 && experience_progress().level==30);
    CHECK(world_select_dimension(NETHER));
    px=-64.5f; py=151.01f; pz=-15.5f;
    experience_step(.05f,1);
    experience_orbs(NETHER,&count);
    CHECK(!count && experience_points()==825);
    player_take_damage(DAMAGE_FALL,200);
    CHECK(!player_rules.health && !experience_points() && save_write());
    save_leave();
    CHECK(save_load(6) && !player_rules.health && !experience_points() && world_dimension()==NETHER);
    list=experience_orbs(NETHER,&count);
    uint32_t xp=0;
    for (size_t i=0;i<count;++i) xp+=list[i].value;
    CHECK(xp==100 && player_respawn()==RESPAWN_OK && world_dimension()==OVERWORLD && !experience_points());
    save_leave();
    CHECK(save_create_with_generation("附魔的物品",SURVIVAL,GENERATOR_BOOKS) && save_current()->id==7);
    const uint64_t sharp=enchant_code(ENCH_SHARPNESS,4)|enchant_code(ENCH_UNBREAKING,2);
    const uint64_t shielding=enchant_code(ENCH_FEATHER_FALLING,4)|enchant_code(ENCH_PROTECTION,3);
    const uint64_t book=enchant_code(ENCH_FORTUNE,3);
    const uint64_t calibrated=enchant_code(ENCH_SILK_TOUCH,1)|enchant_code(ENCH_CORE_CLAMP,1);
    inventory[6]=(ItemStack){STONE_PICKAXE,1,9,calibrated,0};
    inventory[7]=(ItemStack){IRON_SWORD,1,38,sharp,0};
    inventory[8]=(ItemStack){ENCHANTED_BOOK,1,0,book,0};
    CHECK(item_set_custom(&inventory[7],8,"洞穴远征") && item_set_custom(&inventory[8],6,""));
    ItemStack boots={IRON_BOOTS,1,145,shielding,0};
    CHECK(item_set_custom(&boots,4,"靴子 🥾") && armor_restore(ARMOR_FEET,boots));
    CHECK(set_block(-5,148,0,ENCHANTING_TABLE) && set_block(-7,148,0,BOOKSHELF) &&
        set_block(-4,148,0,CHEST) && set_block(-8,148,0,SUGAR_CANE));
    ChestView magic;
    CHECK(chest_open((ChestPos){-4,148,0},&magic)==CHEST_OK &&
        chest_move(&magic,DOUBLE_CHEST_SLOTS+7,0,1)==CHEST_OK &&
        chest_slot(&magic,0).enchants==sharp && !inventory[7].count);
    drops[0]=make_drop(-4.5f,148.4f,1.5f,(ItemStack){DIAMOND_PICKAXE,1,1510,enchant_code(ENCH_SILK_TOUCH,1),0});
    CHECK(item_set_custom(&drops[0].item,2,"掉在地上的镐") && set_block(-11,148,0,IRON_BLOCK) &&
        anvil_place(-10,148,0,2,1));
    CHECK(experience_restore_player(292,0x89abcdefu) && save_write());
    save_leave();
    CHECK(save_load(7) && save_current()->format==CURRENT_FORMAT && world_generation()==GENERATOR_BOOKS &&
        block(-5,148,0)==ENCHANTING_TABLE && block(-7,148,0)==BOOKSHELF &&
        block(-8,148,0)==SUGAR_CANE && experience_points()==292 &&
        inventory[6].type==STONE_PICKAXE && inventory[6].enchants==calibrated && inventory[6].damage==9 &&
        armor_worn(ARMOR_FEET).enchants==shielding && armor_worn(ARMOR_FEET).damage==145 &&
        inventory[8].type==ENCHANTED_BOOK && inventory[8].enchants==book && item_prior_work(inventory[8])==6 &&
        !*item_override(inventory[8]) && !strcmp(item_override(armor_worn(ARMOR_FEET)),"靴子 🥾") &&
        item_prior_work(armor_worn(ARMOR_FEET))==4 &&
        block(-11,148,0)==IRON_BLOCK && world_cell(-10,148,0)==ANVIL_BASE+5);
    CHECK(chest_open((ChestPos){-4,148,0},&magic)==CHEST_OK &&
        chest_move(&magic,0,DOUBLE_CHEST_SLOTS+7,0)==CHEST_OK &&
        inventory[7].enchants==sharp && inventory[7].damage==38 && item_prior_work(inventory[7])==8 &&
        !strcmp(item_override(inventory[7]),"洞穴远征") &&
        drops[0].item.enchants==enchant_code(ENCH_SILK_TOUCH,1) && drops[0].item.damage==1510 &&
        !strcmp(item_override(drops[0].item),"掉在地上的镐") && item_prior_work(drops[0].item)==2);
    Vec3 end_drop={-15.5f,148.01f,1.5f};
    px=end_drop.x;py=end_drop.y;pz=end_drop.z;
    for (int x=-17;x<=-15;++x) for (int z=0;z<=2;++z) CHECK(set_block(x,147,z,STONE));
    player_take_damage(DAMAGE_FALL,200);
    CHECK(!player_rules.health && death_drop_count(OVERWORLD)>=3 && save_write());
    save_leave();
    CHECK(save_load(7) && !player_rules.health && player_respawn()==RESPAWN_OK);
    px=end_drop.x;py=end_drop.y;pz=end_drop.z;
    for (int i=0;i<40;++i) drops_step(.05f,1);
    int found=0;
    for (int i=0;i<SLOT_COUNT;++i) found+=inventory[i].type==IRON_SWORD && inventory[i].enchants==sharp &&
        inventory[i].damage==38 && item_prior_work(inventory[i])==8 &&
        !strcmp(item_override(inventory[i]),"洞穴远征");
    CHECK(found==1 && !death_drop_count(OVERWORLD));
    save_leave();
    CHECK(save_create("重力 · 持久化") && save_current()->id==8);
    px=-15.5f;py=150.01f;pz=0.6f;
    CHECK(set_block(-16,149,0,STONE) && set_block(-17,150,1,STONE) &&
        set_block(-17,157,1,SAND) && set_block(-17,151,1,AIR));
    gravity_step(4);
    size_t flying=0;
    const FallingBlock *inflight=gravity_falling(OVERWORLD,&flying);
    float mid=flying==1 ? inflight[0].bottom : 0.f;
    CHECK(flying==1 && mid>151.f && mid<157.f && block(-17,157,1)==AIR);
    CHECK(world_select_dimension(NETHER) && set_block(-17,150,1,STONE) &&
        set_block(-17,155,1,GRAVEL) && set_block(-17,151,1,AIR) && gravity_pending_count(NETHER)==1);
    CHECK(world_select_dimension(OVERWORLD) && save_write());
    save_leave();
    CHECK(save_load(8) && world_dimension()==OVERWORLD && save_current()->format==CURRENT_FORMAT);
    inflight=gravity_falling(OVERWORLD,&flying);
    CHECK(flying==1 && inflight[0].bottom==mid && gravity_pending_count(NETHER)==1);
    gravity_step(60);
    CHECK(block(-17,151,1)==SAND && !gravity_pending_count(OVERWORLD));
    CHECK(world_select_dimension(NETHER) && block(-17,155,1)==GRAVEL && block(-17,151,1)==AIR);
    gravity_step(60);
    CHECK(block(-17,151,1)==GRAVEL && block(-17,155,1)==AIR && save_write());
    save_leave();
    CHECK(save_load(8) && world_dimension()==NETHER && block(-17,151,1)==GRAVEL);
    gravity_falling(NETHER,&flying);
    CHECK(!flying && world_select_dimension(OVERWORLD) && block(-17,151,1)==SAND);
    save_leave();
    CHECK(save_create("红石 · 跨维度与待更新") && save_current()->id==9);
    px=-15.5f;py=150.01f;pz=3.5f;
    for (int dim=0;dim<2;++dim) {
        CHECK(world_select_dimension((Dimension)dim));
        for (int x=-20;x<=-13;++x) CHECK(set_block(x,149,0,STONE));
        CHECK(redstone_place_lever((CircuitPos){-20,150,0},(CircuitPos){-20,149,0},1));
        for (int x=-19;x<=-14;++x) CHECK(redstone_place_wire(x,150,0));
        CHECK(gate_place(-13,150,0,EAST));
        CHECK(redstone_toggle_lever(-20,150,0)==2);
        redstone_step(-16,0);
        GateState g;
        CHECK(redstone_wire_power(-14,150,0)==10 && gate_decode(world_cell(-13,150,0),&g) && g.open && g.powered);
    }
    CHECK(world_select_dimension(OVERWORLD) && redstone_toggle_lever(-20,150,0)==1 && save_write());
    save_leave();
    CHECK(save_load(9) && save_current()->format==CURRENT_FORMAT && world_dimension()==OVERWORLD &&
        redstone_wire_power(-14,150,0)==10);
    for (int i=0;i<7;++i) redstone_step(-16,0);
    GateState g;
    CHECK(!redstone_wire_power(-14,150,0) && gate_decode(world_cell(-13,150,0),&g) && !g.open && !g.powered);
    CHECK(world_select_dimension(NETHER));
    redstone_step(-16,0);
    CHECK(redstone_wire_power(-14,150,0)==10 && gate_decode(world_cell(-13,150,0),&g) && g.open && g.powered);
    save_leave();
    CHECK(save_create("红石 · 恢复剩余计时与烧毁冷却") && save_current()->id==10);
    px=-15.5f;py=150.01f;pz=3.5f;
    for (int x=-20;x<=-8;++x) CHECK(set_block(x,149,0,STONE) && set_block(x,149,8,STONE));
    CHECK(set_block(-10,150,8,STONE) &&
        redstone_place_torch((CircuitPos){-9,150,8},(CircuitPos){-10,150,8}) &&
        redstone_place_lever((CircuitPos){-10,151,8},(CircuitPos){-10,150,8},0));
    for (int edge=0;edge<8;++edge) {
        CHECK(redstone_toggle_lever(-10,151,8)==2);
        for (int tick=0;tick<3;++tick) redstone_step(-16,8);
        CHECK(redstone_toggle_lever(-10,151,8)==1);
        for (int tick=0;tick<3;++tick) redstone_step(-16,8);
    }
    for (int d=0;d<2;++d) {
        CHECK(world_select_dimension((Dimension)d));
        for (int x=-17;x<=-14;++x) CHECK(set_block(x,149,0,STONE));
        CHECK(redstone_place_lever((CircuitPos){-17,150,0},(CircuitPos){-17,149,0},1) &&
            redstone_place_repeater(-16,150,0,EAST) && redstone_place_wire(-15,150,0));
        int desired=d==OVERWORLD ? 4 : 2;
        for (int delay=2;delay<=desired;++delay) CHECK(redstone_next_delay(-16,150,0)==delay);
        CHECK(redstone_toggle_lever(-17,150,0)==2);
        redstone_step(-16,0);
        if (d==OVERWORLD) for (int i=0;i<2;++i) redstone_step(-16,0);
    }
    RedstoneTiming burned={{-9,150,8},0,0,0,0,{0},0};
    CHECK(redstone_visit_timing(OVERWORLD,snapshot_timer,&burned) && burned.cooldown==154 && burned.count==8);
    CHECK(world_select_dimension(OVERWORLD) && save_write());
    save_leave();
    CHECK(save_load(10) && save_current()->format==CURRENT_FORMAT && world_dimension()==OVERWORLD);
    CHECK(redstone_timing_count(OVERWORLD)==2 && redstone_timing_count(NETHER)==1);
    RedstoneTiming restored_burn={{-9,150,8},0,0,0,0,{0},0}, pending={{-16,150,0},0,0,0,0,{0},0};
    CHECK(redstone_visit_timing(OVERWORLD,snapshot_timer,&restored_burn) &&
        restored_burn.cooldown==burned.cooldown && restored_burn.count==burned.count &&
        !memcmp(restored_burn.ages,burned.ages,8));
    CHECK(redstone_visit_timing(OVERWORLD,snapshot_timer,&pending) && pending.remaining==6 && pending.target==1);
    for (int i=0;i<5;++i) redstone_step(-16,0);
    CHECK(!redstone_wire_power(-15,150,0));
    redstone_step(-16,0);
    CHECK(redstone_wire_power(-15,150,0)==15);
    CHECK(world_select_dimension(NETHER) && !redstone_wire_power(-15,150,0));
    for (int i=0;i<3;++i) redstone_step(-16,0);
    CHECK(!redstone_wire_power(-15,150,0));
    redstone_step(-16,0);
    CHECK(redstone_wire_power(-15,150,0)==15 && world_select_dimension(OVERWORLD));
    for (int i=0;i<149;++i) redstone_step(-16,8);
    CHECK(world_cell(-9,150,8)<TORCH_POWER_BASE+TORCH_POWER_ON);
    redstone_step(-16,8);
    CHECK(world_cell(-9,150,8)>=TORCH_POWER_BASE+TORCH_POWER_ON && save_write());
    save_leave();
    CHECK(save_create("Lamp light test"));
    CHECK(set_block(-17,150,9,REDSTONE_LAMP) && set_block(-18,150,9,REDSTONE_BLOCK));
    redstone_step(-16,9);
    CHECK(block_light_at(-16,150,9)==14 && set_block(-18,150,9,AIR));
    redstone_step(-16,9); redstone_step(-16,9);
    inventory[0]=(ItemStack){REDSTONE_LAMP_ITEM,3,0,0,0};
    CHECK(save_write());
    save_leave();
    CHECK(save_load(11) && save_current()->format==CURRENT_FORMAT && inventory[0].type==REDSTONE_LAMP_ITEM &&
        inventory[0].count==3 && block_light_at(-16,150,9)==14);
    RedstoneTiming lamp={{-17,150,9},0,0,0,0,{0},0};
    CHECK(redstone_visit_timing(OVERWORLD,snapshot_timer,&lamp) && lamp.remaining==3 && lamp.target==0);
    for (int i=0;i<2;++i) redstone_step(-16,9);
    CHECK(block_light_at(-16,150,9)==14);
    redstone_step(-16,9);
    CHECK(world_cell(-17,150,9)==LAMP_OFF && block_light_at(-16,150,9)==0);
    save_leave();
    CHECK(save_create("比较器 · 模式和未完成的模拟信号"));
    for (int x=-18;x<=-16;++x) CHECK(set_block(x,149,0,STONE));
    CHECK(set_block(-18,150,0,CHEST) && redstone_place_comparator(-17,150,0,EAST) &&
        redstone_place_wire(-16,150,0) && redstone_toggle_comparator(-17,150,0)==2);
    ChestView analogue={OVERWORLD,{{0,0,0},{0,0,0}},0,0};
    CHECK(chest_open((ChestPos){-18,150,0},&analogue)==CHEST_OK);
    for (int i=0;i<14;++i) {
        inventory[4]=(ItemStack){STONE,64,0,0,0};
        CHECK(chest_move(&analogue,DOUBLE_CHEST_SLOTS+4,i,0)==CHEST_OK);
        if (i==6) for (int j=0;j<3;++j) redstone_step(-16,0);
    }
    redstone_step(-16,0);
    redstone_step(-16,0);
    CHECK(redstone_comparator_power(-17,150,0)==4 && redstone_wire_power(-16,150,0)==4 && save_write());
    save_leave();
    CHECK(save_load(12) && save_current()->format==CURRENT_FORMAT && world_cell(-17,150,0)==(COMPARATOR_BASE+COMPARATOR_SUBTRACT+EAST));
    RedstoneTiming compare={{-17,150,0},0,0,0,0,{0},0};
    CHECK(redstone_visit_timing(OVERWORLD,snapshot_timer,&compare) && compare.output==4 && compare.target==8 &&
        compare.remaining==1 && redstone_comparator_power(-17,150,0)==4);
    redstone_step(-16,0);
    CHECK(redstone_comparator_power(-17,150,0)==8 && redstone_wire_power(-16,150,0)==8 && save_write());
    save_leave();
    CHECK(save_load(12) && redstone_comparator_power(-17,150,0)==8 && redstone_wire_power(-16,150,0)==8);
    save_leave();
    /* This checkpoint is also rewritten as v30 below; pin its old terrain. */
    CHECK(save_create_with_generation("压力板 · 恢复释放计时",SURVIVAL,GENERATOR_BOOKS));
    CHECK(set_block(-17,149,0,STONE) && redstone_place_plate(-17,150,0,PRESSURE_GOLD) &&
        set_block(-16,149,0,STONE) && redstone_place_wire(-16,150,0));
    drops[0]=make_drop(-16.5f,150.13f,.5f,(ItemStack){STONE,64,0,0,0});
    redstone_step(-16,0);
    CHECK(redstone_plate_power(-17,150,0)==1 && redstone_wire_power(-16,150,0)==1);
    drops[0].item=(ItemStack){0,0,0,0,0};
    for (int j=0;j<3;++j) redstone_step(-16,0);
    inventory[0]=(ItemStack){GOLD_PLATE_ITEM,3,0,0,0};
    CHECK(save_write());
    save_leave();
    snprintf(path,sizeof path,"%s/%08x.vxl",dir,13);
    CHECK(write_legacy_fixture(path,30));
    CHECK(save_load(13) && save_current()->format==CURRENT_FORMAT && inventory[0].type==GOLD_PLATE_ITEM &&
        inventory[0].count==3);
    RedstoneTiming plate={{-17,150,0},0,0,0,0,{0},0};
    CHECK(redstone_visit_timing(OVERWORLD,snapshot_timer,&plate) && plate.output==1 && plate.remaining==7);
    for (int j=0;j<6;++j) redstone_step(-16,0);
    CHECK(redstone_plate_power(-17,150,0)==1 && redstone_wire_power(-16,150,0)==1);
    redstone_step(-16,0);
    CHECK(redstone_plate_power(-17,150,0)==0 && redstone_wire_power(-16,150,0)==0);
    save_leave();
    CHECK(save_create("按钮 · 宽编号、箭矢与剩余脉冲"));
    CHECK(set_block(-16,150,-1,STONE) && set_block(-16,150,4,STONE) &&
        redstone_place_button((CircuitPos){-16,150,0},(CircuitPos){-16,150,-1},0) &&
        redstone_place_button((CircuitPos){-16,150,5},(CircuitPos){-16,150,4},1));
    inventory[0]=(ItemStack){STONE_BUTTON_ITEM,2,0,0,0};
    inventory[1]=(ItemStack){WOOD_BUTTON_ITEM,3,0,0,0};
    CHECK(redstone_press_button(-16,150,0)==1 &&
        button_arrow_impact((Vec3){-15.5f,150.5f,5.20f},(Vec3){-15.5f,150.5f,5.05f},1));
    for (int j=0;j<3;++j) redstone_step(-16,0);
    CHECK(save_write());
    save_leave();
    CHECK(save_load(14) && save_current()->format==CURRENT_FORMAT && inventory[0].type==STONE_BUTTON_ITEM &&
        inventory[0].count==2 && inventory[1].type==WOOD_BUTTON_ITEM &&
        world_cell(-16,150,0)==BUTTON_BASE && world_cell(-16,150,5)==BUTTON_WOOD &&
        redstone_button_power(-16,150,0)==15 && redstone_button_power(-16,150,5)==15);
    size_t arrow_count=0;
    const ButtonArrow *arrows=button_arrows_in(OVERWORLD,&arrow_count);
    CHECK(arrow_count==1 && arrows[0].ticks==BUTTON_ARROW_LIFE-3 && arrows[0].recoverable &&
        arrows[0].z==5 && arrows[0].direction.z== -1.f);
    for (int j=0;j<17;++j) redstone_step(-16,0);
    CHECK(redstone_button_power(-16,150,0)==0 && redstone_button_power(-16,150,5)==15);
    for (int j=0;j<16;++j) redstone_step(-16,0);
    CHECK(button_arrow_present(-16,150,5) && redstone_button_power(-16,150,5)==15);
    save_leave();
    CHECK(save_create("漏斗 · 容量、剩余搬运计时与维度"));
    CHECK(set_block(-16,150,0,CHEST) && hopper_place((Hit){1,-16,150,0,-17,150,0}));
    inventory[0]=(ItemStack){IRON_INGOT,3,0,0,0};
    CHECK(hopper_move(-17,150,0,HOPPER_SLOTS,0,0));
    for (int j=0;j<4;++j) hoppers_step(-16,0);
    const Hopper *h=hopper_at(-17,150,0);
    CHECK(h && h->cooldown==5 && h->slots[0].count==2);
    CHECK(world_select_dimension(NETHER) && set_block(-16,150,0,STONE) &&
        hopper_place((Hit){1,-16,150,0,-17,150,0}));
    inventory[0]=(ItemStack){QUARTZ,1,0,0,0};
    CHECK(hopper_move(-17,150,0,HOPPER_SLOTS,0,0));
    CHECK(world_select_dimension(OVERWORLD) && save_write());
    save_leave();
    CHECK(save_load(15) && save_current()->format==CURRENT_FORMAT && world_cell(-17,150,0)==HOPPER_BASE+EAST);
    h=hopper_at(-17,150,0);
    CHECK(h && h->cooldown==5 && h->slots[0].count==2 && h->slots[0].type==IRON_INGOT);
    CHECK(world_select_dimension(NETHER));
    h=hopper_at(-17,150,0);
    CHECK(h && !h->cooldown && h->slots[0].type==QUARTZ);
    CHECK(world_select_dimension(OVERWORLD));
    ChestView delivery;
    CHECK(chest_open((ChestPos){-16,150,0},&delivery)==CHEST_OK);
    for (int j=0;j<4;++j) hoppers_step(-16,0);
    CHECK(chest_slot(&delivery,0).count==1);
    hoppers_step(-16,0);
    CHECK(chest_slot(&delivery,0).count==2);
    save_leave();
    CHECK(save_create("投掷器 · 尚未触发与持续高电平"));
    CHECK(dropper_place(-17,150,0,EAST) && set_block(-16,150,0,CHEST));
    inventory[0]=(ItemStack){ARROW,3,0,0,0};
    CHECK(dropper_move(-17,150,0,DROPPER_SLOTS,0,0) && set_block(-17,150,1,REDSTONE_BLOCK));
    redstone_step(-17,0); redstone_step(-17,0);
    CHECK(world_select_dimension(NETHER) && dropper_place(-17,150,0,DROPPER_UP-DROPPER_BASE));
    inventory[0]=(ItemStack){QUARTZ,4,0,0,0};
    CHECK(dropper_move(-17,150,0,DROPPER_SLOTS,8,0) && world_select_dimension(OVERWORLD) && save_write());
    save_leave();
    CHECK(save_load(16) && save_current()->format==CURRENT_FORMAT && world_cell(-17,150,0)==DROPPER_BASE+EAST);
    RedstoneTiming dropper_pulse={0}; dropper_pulse.position=(CircuitPos){-17,150,0};
    CHECK(redstone_visit_timing(OVERWORLD,snapshot_timer,&dropper_pulse) &&
        dropper_pulse.remaining==3 && dropper_pulse.target==1);
    CHECK(world_select_dimension(NETHER) && world_cell(-17,150,0)==DROPPER_UP);
    const Dropper *d=dropper_at(-17,150,0);
    CHECK(d && d->slots[8].type==QUARTZ && d->slots[8].count==4);
    CHECK(world_select_dimension(OVERWORLD) && chest_open((ChestPos){-16,150,0},&delivery)==CHEST_OK);
    redstone_step(-17,0); redstone_step(-17,0);
    CHECK(!chest_slot(&delivery,0).count);
    redstone_step(-17,0);
    CHECK(chest_slot(&delivery,0).type==ARROW && chest_slot(&delivery,0).count==1);
    CHECK(save_write());
    save_leave();
    CHECK(save_load(16) && chest_open((ChestPos){-16,150,0},&delivery)==CHEST_OK);
    for (int j=0;j<15;++j) redstone_step(-17,0);
    d=dropper_at(-17,150,0);
    CHECK(d && d->slots[0].count==2 && d->rolls==1 && chest_slot(&delivery,0).count==1);
    save_leave();
    CHECK(save_create("发射器 · 桶、朝向和待触发脉冲"));
    CHECK(ejector_place(-17,150,0,EAST,DISPENSER) && set_block(-18,150,0,REDSTONE_BLOCK));
    inventory[0]=(ItemStack){WATER_BUCKET,1,0,0,0};
    CHECK(dropper_move(-17,150,0,DROPPER_SLOTS,6,0));
    redstone_step(-17,0); redstone_step(-17,0);
    CHECK(save_write());
    save_leave();
    CHECK(save_load(17) && save_current()->format==CURRENT_FORMAT && world_cell(-17,150,0)==DISPENSER_BASE+EAST);
    d=dropper_at(-17,150,0);
    CHECK(d && d->slots[6].type==WATER_BUCKET && d->rolls==0);
    for (int j=0;j<3;++j) redstone_step(-17,0);
    CHECK(d && d->slots[6].type==BUCKET && d->rolls==1 && world_cell(-16,150,0)==WATER);
    CHECK(save_write());
    save_leave();
    CHECK(save_load(17) && world_cell(-16,150,0)==WATER);
    for (int j=0;j<10;++j) redstone_step(-17,0);
    d=dropper_at(-17,150,0);
    CHECK(d && d->rolls==1 && d->slots[6].type==BUCKET && world_cell(-16,150,0)==WATER);
    save_leave();
    CHECK(save_create("TNT · 两个维度的待爆实体"));
    CHECK(blast_restore(OVERWORLD,(PrimedBlast){{-16.5f,150.3f,.5f},{.2f,1.5f,0},25,BLAST_TNT}));
    CHECK(blast_restore(NETHER,(PrimedBlast){{-16.5f,150.f,.5f},{0,0,0},0,BLAST_TNT}));
    CHECK(set_block(-18,150,0,TNT_BLOCK) && actors_restore(OVERWORLD,(ActorRecord){{-22.5f,150.f,3.5f},0,ACTOR_SHELLBACK,SHELLBACK_HEALTH,0}));
    inventory[0]=(ItemStack){GUNPOWDER,4,0,0,0};
    CHECK(save_write());
    save_leave();
    CHECK(save_load(18) && save_current()->format==CURRENT_FORMAT && world_cell(-18,150,0)==TNT_BLOCK &&
        inventory[0].type==GUNPOWDER && actors_in(OVERWORLD)[0].type==ACTOR_SHELLBACK);
    size_t charges=0;
    const PrimedBlast *active_blasts=blasts_in(OVERWORLD,&charges);
    CHECK(charges==1 && active_blasts[0].ticks==25 && active_blasts[0].point.y==150.3f &&
        active_blasts[0].speed.y==1.5f && active_blasts[0].speed.x==.2f);
    CHECK(world_select_dimension(NETHER));
    active_blasts=blasts_in(NETHER,&charges);
    CHECK(charges==1 && active_blasts[0].ticks==0);
    blasts_step(1); blasts_in(NETHER,&charges);
    CHECK(charges==0 && world_select_dimension(OVERWORLD));
    active_blasts=blasts_in(OVERWORLD,&charges);
    CHECK(charges==1 && active_blasts[0].ticks==25);
    save_leave();
    CHECK(save_create_with_generation("火焰 · 计划和着火状态",SURVIVAL,GENERATOR_BOOKS));
    CHECK(set_block(-17,149,0,NETHERRACK) && fire_ignite(-17,150,0));
    for (int j=0;j<54;++j) fires_step(-17,0,1);
    FireCell persisted={-17,150,0,0,0,0};
    CHECK(fire_visit(OVERWORLD,snapshot_fire,&persisted) && persisted.attempts==1 && persisted.delay>0);
    CHECK(world_select_dimension(NETHER) && set_block(-17,149,0,STONE) && fire_ignite(-17,150,0) &&
        world_select_dimension(OVERWORLD) && player_restore_burn((PlayerBurn){37,11}) && save_write());
    save_leave();
    CHECK(save_load(19) && save_current()->format==CURRENT_FORMAT && world_cell(-17,150,0)==FIRE_CELL);
    FireCell loaded={-17,150,0,0,0,0};
    CHECK(fire_visit(OVERWORLD,snapshot_fire,&loaded) && loaded.age==persisted.age &&
        loaded.delay==persisted.delay && loaded.attempts==persisted.attempts);
    CHECK(player_burn_state().remaining==37 && player_burn_state().due==11);
    CHECK(world_select_dimension(NETHER) && world_cell(-17,150,0)==FIRE_CELL);
    for (int j=0;j<200;++j) fires_step(-17,0,1);
    CHECK(world_cell(-17,150,0)==AIR && world_select_dimension(OVERWORLD) &&
        world_cell(-17,150,0)==FIRE_CELL && player_burn_state().remaining==37);
    save_leave();
    snprintf(path,sizeof path,"%s/%08x.vxl",dir,19);
    CHECK(write_legacy_fixture(path,36));
    CHECK(save_load(19) && weather_kind()==WEATHER_CLEAR && world_cell(-17,150,0)==FIRE_CELL &&
        player_burn_state().remaining==37);
    save_leave();
    CHECK(save_create("雷暴 · 雪与成熟壳片"));
    CHECK(weather_restore((WeatherSave){0x3981ae22,835,321,2,WEATHER_THUNDER}));
    CHECK(set_block(-18,150,0,STONE) && set_block(-18,151,0,SNOW_LAYER));
    CHECK(actors_restore(OVERWORLD,(ActorRecord){{-17.5f,150.f,.5f},0,ACTOR_SHELLBACK,SHELLBACK_HEALTH,SHELLBACK_RIPE}));
    CHECK(blast_restore(OVERWORLD,(PrimedBlast){{-2.5f,150,.5f},{0,0,0},1,BLAST_TNT}));
    CHECK(world_select_dimension(NETHER) && save_write());
    save_leave();
    CHECK(save_load(20) && world_dimension()==NETHER && weather_kind()==WEATHER_CLEAR);
    CHECK(world_select_dimension(OVERWORLD) && weather_kind()==WEATHER_THUNDER && block(-18,151,0)==SNOW_LAYER);
    WeatherSave storm=weather_record();
    CHECK(storm.random==0x3981ae22 && storm.remaining==835 && storm.strike==321 && storm.cover==2 &&
        actors_in(OVERWORLD)[0].traits==SHELLBACK_RIPE);
    active_blasts=blasts_in(OVERWORLD,&charges);
    CHECK(charges==1 && active_blasts[0].kind==BLAST_TNT);
    save_leave();
    CHECK(save_create_in_mode("活塞 · 跨区块保存伸长状态",SURVIVAL));
    CHECK(piston_place(-17,150,0,EAST,1) && set_block(-18,150,0,REDSTONE_BLOCK) && set_block(-16,150,0,GLASS));
    for (int j=0;j<6;++j) redstone_step(-17,0);
    CHECK(piston_extended(world_cell(-17,150,0)) && piston_head_cell(world_cell(-16,150,0)) &&
        world_cell(-15,150,0)==GLASS);
    inventory[0]=(ItemStack){STICKY_PISTON_ITEM,4,0,0,0};
    CHECK(world_select_dimension(NETHER) && piston_place(-17,150,0,WEST,0) && save_write());
    save_leave();
    CHECK(save_load(21) && world_dimension()==NETHER && world_cell(-17,150,0)==PISTON_BASE+WEST);
    CHECK(inventory[0].type==STICKY_PISTON_ITEM && inventory[0].count==4);
    CHECK(world_select_dimension(OVERWORLD) && world_cell(-17,150,0)==PISTON_BASE+PISTON_STICKY+PISTON_EXTENDED+EAST &&
        world_cell(-16,150,0)==PISTON_HEAD_BASE+PISTON_STICKY+EAST && world_cell(-15,150,0)==GLASS);
    CHECK(set_block(-18,150,0,AIR));
    for (int j=0;j<6;++j) redstone_step(-17,0);
    CHECK(world_cell(-17,150,0)==PISTON_BASE+PISTON_STICKY+EAST && world_cell(-16,150,0)==GLASS &&
        world_cell(-15,150,0)==AIR && save_write());
    save_leave();
    CHECK(save_create("无基座的损坏活塞存档"));
    CHECK(world_restore_edit(-16,150,0,PISTON_HEAD_BASE+EAST) && save_write());
    save_leave();
    CHECK(!save_load(22));
    save_leave();
    CHECK(save_create_in_mode("熔岩 · 跨维度保留未到期的流动",SURVIVAL));
    CHECK(set_block(-17,149,3,STONE) && set_block(-16,149,3,STONE) && set_block(-17,150,3,LAVA));
    CHECK(set_block(-18,150,3,STONE) && set_block(-17,150,2,STONE) && set_block(-17,150,4,STONE));
    for (int j=0;j<33;++j) lava_step();
    CHECK(lava_flow_cell(world_cell(-16,150,3)) && world_cell(-16,149,3)==STONE);
    CHECK(world_select_dimension(NETHER) && set_block(-17,150,3,LAVA));
    for (int j=0;j<7;++j) lava_step();
    CHECK(world_cell(-17,149,3)==AIR && save_write());
    save_leave();
    CHECK(save_load(23) && save_current()->format==CURRENT_FORMAT && world_dimension()==NETHER);
    (void)world_cell(-16,149,3); /* Bring the edge chunk into the active view. */
    (void)world_cell(-17,149,3);
    lava_step(); lava_step();
    CHECK(world_cell(-17,149,3)==AIR);
    lava_step();
    CHECK(world_cell(-17,149,3)==LAVA_FALLING);
    CHECK(world_select_dimension(OVERWORLD) && lava_flow_cell(world_cell(-16,150,3)));
    CHECK(set_block(-17,150,3,AIR));
    for (int j=0;j<180;++j) lava_step();
    CHECK(world_cell(-16,150,3)==AIR && world_select_dimension(NETHER) && world_cell(-17,149,3)==LAVA_FALLING);
    save_leave();
    CHECK(save_create_with_generation("村庄 · 生成器和农田保留",SURVIVAL,GENERATOR_VILLAGES) && world_generation()==GENERATOR_VILLAGES);
    VillagePlan village={0};
    int village_found=0;
    for (int rx=-10;rx<=10 && !village_found;++rx) for (int rz=-10;rz<=10 && !village_found;++rz)
        village_found=village_plan_for(rx*VILLAGE_REGION_EDGE,rz*VILLAGE_REGION_EDGE,&village);
    CHECK(village_found);
    if (village_found) {
        int x,z;
        village_global(&village,1,12,&x,&z);
        CHECK(world_cell(x,village.floor+1,z)==WHEAT_BASE+7 && set_block(x,village.floor+1,z,AIR));
        int farmer=save_test_farmer(village),initial_stock=0,ok=0;
        CHECK(farmer>=0);
        if (farmer>=0) {
            uint16_t name;
            CHECK(item_name_intern("田边的阿禾",&name) && actors_set_name(OVERWORLD,farmer,name));
            VillagerSession s=villagers_open(farmer);
            VillagerOffer offer={0};
            CHECK(villagers_offer(s,0,&offer));
            initial_stock=offer.remaining;
            inventory[0]=offer.cost[0];
            inventory[1]=offer.cost[1];
            (void)villagers_exchange(s,0,&ok);
            CHECK(ok && inventory[0].type==FRUIT_RATION && inventory[0].count==3);
            villagers_close(s);
            int hurt[ACTOR_CAPACITY]={0}; hurt[farmer]=4;
            actors_explosion_hits(hurt);
            CHECK(actors_in(OVERWORLD)[farmer].health==VILLAGER_HEALTH-4);
        }
        CHECK(save_write()); save_leave();
        CHECK(save_load(24) && save_current()->generation==GENERATOR_VILLAGES &&
            world_cell(x,village.floor+1,z)==AIR && world_cell(x,village.floor,z)==FARM_BASE+7);
        farmer=save_test_farmer(village);
        CHECK(farmer>=0 && inventory[0].type==FRUIT_RATION && inventory[0].count==3);
        if (farmer>=0) {
            VillagerSession s=villagers_open(farmer);
            VillagerOffer offer={0};
            CHECK(actors_in(OVERWORLD)[farmer].health==VILLAGER_HEALTH-4 &&
                !strcmp(item_name_for_id(actors_name_id(OVERWORLD,farmer)),"田边的阿禾") &&
                villagers_count(s)==1 && villagers_offer(s,0,&offer) && offer.remaining==initial_stock-1);
            villagers_close(s);
            villagers_step(VILLAGER_RESTOCK_TICKS+1);
            s=villagers_open(farmer);
            CHECK(villagers_count(s)==1 && villagers_handoffs_needed(s)==1 && villagers_offer(s,0,&offer));
            inventory[2]=offer.cost[0];
            inventory[3]=offer.cost[1];
            (void)villagers_exchange(s,0,&ok);
            CHECK(ok && villagers_count(s)==2 && villagers_offer(s,1,&offer) && offer.result.type==FILTER_MOSS);
            villagers_close(s);

            /* The linked-village ledger must not spawn another farmer
               while its existing resident is waiting for help. */
            VillagerOrigin origin={0};
            uint16_t name=actors_name_id(OVERWORLD,farmer);
            CHECK(villagers_mark_stranded(farmer,&origin) && origin.linked && origin.market.uses[0]==2);
            ActorRecord patient=actors_in(OVERWORLD)[farmer];
            patient.type=ACTOR_STRANDED_GUIDE;
            patient.health=STRANDED_HEALTH;
            patient.traits=0;
            actors_reset();
            int gx=(int)floorf(patient.feet.x),gy=(int)floorf(patient.feet.y+.06f),gz=(int)floorf(patient.feet.z);
            CHECK(set_block(gx,gy-1,gz,STONE) && set_block(gx,gy,gz,AIR) &&
                set_block(gx,gy+1,gz,AIR) && set_block(gx+1,gy,gz,RESONANCE_MARKER));
            CHECK(actors_restore_stranded(OVERWORLD,patient,
                (StrandedGuideState){.origin=origin,.rest_ticks=4}) && actors_set_name(OVERWORLD,0,name));
            CHECK(save_write()); save_leave();
            CHECK(save_load(24) && actors_stranded_state(OVERWORLD,0).origin.linked);
            for (int n=0;n<6;++n) actors_step(.05f);
            int farmers=0;
            for (int i=0;i<ACTOR_CAPACITY;++i) {
                ActorRecord a=actors_in(OVERWORLD)[i];
                farmers+=a.type==ACTOR_VILLAGER && a.traits==VILLAGER_FARMER;
            }
            CHECK(farmers==1 && !villagers_roaming_market(0,NULL) &&
                !strcmp(item_name_for_id(actors_name_id(OVERWORLD,0)),"田边的阿禾"));
            s=villagers_open(0);
            CHECK(villagers_count(s)==2 && villagers_offer(s,0,&offer) && offer.remaining==initial_stock-2);
            villagers_close(s);
        }
    }
    save_leave();
    CHECK(save_create_with_generation("无村庄的原有世界",SURVIVAL,GENERATOR_BOOKS));
    CHECK(set_block(-18,149,29,TORCH) && save_write());
    save_leave();
    CHECK(save_load(25) && save_current()->generation==GENERATOR_BOOKS &&
        world_cell(-18,149,29)==TORCH && !village_plan_for(0,0,&village));
    save_leave();
    CHECK(save_create_with_generation("木门和铁门 · 存档",SURVIVAL,GENERATOR_DOORS) && world_generation()==GENERATOR_DOORS);
    CHECK(set_block(-16,149,-17,STONE) && door_place(-16,150,-17,EAST,1,0) && door_toggle(-16,151,-17));
    CHECK(set_block(-16,149,-16,STONE) && door_place(-16,150,-16,NORTH,0,1));
    CHECK(set_block(-15,151,-16,REDSTONE_BLOCK));
    for (int i=0;i<10;++i) redstone_step(-16,-16);
    int wood=world_cell(-16,150,-17),iron=world_cell(-16,150,-16);
    CHECK(door_cell(wood) && door_cell(iron) && (door_bits(wood)&DOOR_OPEN) && (door_bits(iron)&DOOR_POWERED));
    inventory[8]=(ItemStack){WOOD_DOOR_ITEM,1,0,0,0};
    CHECK(save_write()); save_leave();
    CHECK(save_load(26) && save_current()->generation==GENERATOR_DOORS && inventory[8].type==WOOD_DOOR_ITEM);
    CHECK(world_cell(-16,150,-17)==wood && world_cell(-16,151,-17)==door_peer(wood) &&
        world_cell(-16,150,-16)==iron && world_cell(-16,151,-16)==door_peer(iron));
    CHECK(set_block(-15,151,-16,AIR));
    for (int i=0;i<10;++i) redstone_step(-16,-16);
    CHECK(!(door_bits(world_cell(-16,150,-16))&DOOR_OPEN) && (door_bits(world_cell(-16,150,-17))&DOOR_OPEN));
    CHECK(set_block(-16,149,-17,AIR) && world_cell(-16,150,-17)==AIR && world_cell(-16,151,-17)==AIR);
    save_leave();
    CHECK(save_create_with_generation("铁匠箱与矿石 · 存档",SURVIVAL,GENERATOR_TREASURE));
    village_found=0;
    for (int rx=-10;rx<=10 && !village_found;++rx) for (int rz=-10;rz<=10 && !village_found;++rz)
        village_found=village_plan_for(rx*VILLAGE_REGION_EDGE,rz*VILLAGE_REGION_EDGE,&village);
    ChestPos smith;
    CHECK(village_found && village_smith_chest(&village,&smith.x,&smith.y,&smith.z));
    if (village_found && village_smith_chest(&village,&smith.x,&smith.y,&smith.z)) {
        ItemStack preview[CHEST_SLOTS+2],rest[CHEST_SLOTS+2];
        int total=chest_blast_contents(smith,preview);
        CHECK(total>=3 && total<=8 && block(smith.x,smith.y,smith.z)==SMITH_CHEST);
        CHECK(save_write()); save_leave();
        CHECK(save_load(27) && save_current()->generation==GENERATOR_TREASURE &&
            block(smith.x,smith.y,smith.z)==SMITH_CHEST && chest_blast_contents(smith,rest)==total);
        for (int i=0;i<total;++i) CHECK(item_same_kind(preview[i],rest[i]) && preview[i].count==rest[i].count);
        ChestView stash;
        if (chest_open(smith,&stash)==CHEST_OK) {
            for (int s=0;s<CHEST_SLOTS;++s) {
                if (!chest_slot(&stash,s).count) continue;
                CHECK(chest_move(&stash,s,DOUBLE_CHEST_SLOTS+10,0)==CHEST_OK);
                break;
            }
        } else CHECK(0 && "smith chest must open");
        inventory[0]=(ItemStack){EMERALD_ORE_ITEM,1,0,0,0};
        CHECK(set_block(-18,149,26,EMERALD_ORE) && set_block(-18,149,27,EMERALD_BLOCK) && save_write());
        save_leave();
        CHECK(save_load(27) && block(smith.x,smith.y,smith.z)==CHEST && chest_blast_contents(smith,rest)==total-1 &&
            inventory[0].type==EMERALD_ORE_ITEM && world_cell(-18,149,26)==EMERALD_ORE_CELL &&
            world_cell(-18,149,27)==EMERALD_BLOCK_CELL);
        if (chest_open(smith,&stash)==CHEST_OK) {
            for (int s=0;s<CHEST_SLOTS;++s)
                while (chest_slot(&stash,s).count) chest_automation_remove(&stash,s);
        } else CHECK(0 && "partially looted smith chest must open");
        CHECK(save_write()); save_leave();
        CHECK(save_load(27) && chest_open(smith,&stash)==CHEST_OK && chest_blast_contents(smith,rest)==0 &&
            block(smith.x,smith.y,smith.z)==CHEST);
    }
    save_leave();
    CHECK(save_create_with_generation("马铠 · 旧世界",SURVIVAL,GENERATOR_TREASURE));
    HorseRecord mount={.feet={-2.5f,150.01f,2.5f},.type=EQUINE_HORSE,
        .max_health=24,.health=24,.tamed=1,.saddle=1};
    HorseRecord other=mount; other.feet.x+=5.f;
    CHECK(horse_restore(OVERWORLD,mount) && horse_restore(OVERWORLD,other));
    px=mount.feet.x; py=mount.feet.y+1.04f; pz=mount.feet.z;
    CHECK(set_block(-3,149,2,STONE) && horse_restore_rider(0) && save_write());
    save_leave();
    snprintf(path,sizeof path,"%s/%08x.vxl",dir,28);
    CHECK(write_legacy_fixture(path,42) && save_load(28) && save_current()->generation==GENERATOR_TREASURE &&
        horse_rider()==0 && !horse_mounted_armor().count && !horses_in(OVERWORLD)[0].health_debt);
    inventory[0]=(ItemStack){DIAMOND_HORSE_ARMOR,1,0,0,0};
    CHECK(item_set_custom(&inventory[0],0,"远征马铠") && horse_equip_mounted(0) &&
        horse_hit_hostile((Vec3){px,mount.feet.y+.8f,pz},1,DAMAGE_PROJECTILE) &&
        horses_in(OVERWORLD)[0].health==24 && horses_in(OVERWORLD)[0].health_debt==14);
    CHECK(horse_restore(NETHER,other) && horse_restore_equipment(NETHER,0,(ItemStack){GOLD_HORSE_ARMOR,1,0,0,0},18));
    CHECK(save_write()); save_leave();
    CHECK(save_load(28) && horse_rider()==0 && horse_mounted_armor().type==DIAMOND_HORSE_ARMOR &&
        !strcmp(item_display_name(horse_mounted_armor()),"远征马铠") &&
        horses_in(OVERWORLD)[0].health_debt==14 && !horses_in(OVERWORLD)[1].armor.count &&
        horses_in(NETHER)[0].armor.type==GOLD_HORSE_ARMOR && horses_in(NETHER)[0].health_debt==18 &&
        world_generation()==GENERATOR_TREASURE);
    save_leave();
    CHECK(save_create_with_generation("种植林场 · 存档",SURVIVAL,GENERATOR_FORESTRY));
    BlockEdit trees[]={{-18,189,-6,DIRT},{-18,190,-6,SAPLING_BASE+2*TREE_DARK_OAK+1},
        {-17,190,-6,PLACED_LEAF_BASE+TREE_JUNGLE},{-16,190,-6,TREE_LEAF_BASE}};
    inventory[3]=(ItemStack){ACACIA_SAPLING,5,0,0,0};
    inventory[4]=(ItemStack){DARK_OAK_LEAF_ITEM,3,0,0,0};
    CHECK(world_set_blocks(trees,sizeof trees/sizeof *trees) && world_select_dimension(END));
    BlockEdit remote[]={{21,160,1,DIRT},{21,161,1,SAPLING_BASE+2*TREE_SPRUCE}};
    CHECK(world_set_blocks(remote,2) && world_select_dimension(OVERWORLD) && save_write());
    save_leave();
    CHECK(save_load(29) &&
        world_cell(-18,190,-6)==SAPLING_BASE+2*TREE_DARK_OAK+1 &&
        world_cell(-17,190,-6)==PLACED_LEAF_BASE+TREE_JUNGLE &&
        world_cell(-16,190,-6)==natural_leaf(TREE_BIRCH) &&
        inventory[3].type==ACACIA_SAPLING && inventory[3].count==5 &&
        inventory[4].type==DARK_OAK_LEAF_ITEM && inventory[4].count==3 &&
        world_select_dimension(END) && world_cell(21,161,1)==SAPLING_BASE+2*TREE_SPRUCE);
    save_leave();
    CHECK(save_create_with_generation("砍树后落叶 · 存档",SURVIVAL,GENERATOR_FORESTRY));
    BlockEdit tree[]={{15,199,6,SPRUCE_LOG},{15,200,6,TREE_LEAF_BASE+TREE_SPRUCE-1},
        {16,200,6,TREE_LEAF_BASE+TREE_SPRUCE-1},{17,200,6,PLACED_LEAF_BASE+TREE_SPRUCE}};
    CHECK(world_set_blocks(tree,4) && set_block(15,199,6,AIR));
    size_t overworld_pending=leaves_pending(OVERWORLD);
    CHECK(overworld_pending>0 && world_select_dimension(END) &&
        set_block(-1,177,7,BIRCH_LOG) && set_block(-1,177,7,AIR));
    size_t end_pending=leaves_pending(END);
    CHECK(end_pending>0 && world_select_dimension(OVERWORLD) && save_write());
    save_leave();
    CHECK(save_load(30) && leaves_pending(OVERWORLD)==overworld_pending && leaves_pending(END)==end_pending &&
        world_cell(15,199,6)==AIR && block(16,200,6)==LEAF);
    (void)chunk_view(0,0); (void)chunk_view(1,0);
    for (int i=0;i<180;++i) leaves_step(16,6);
    CHECK(block(15,200,6)==AIR && block(16,200,6)==AIR &&
        world_cell(17,200,6)==PLACED_LEAF_BASE+TREE_SPRUCE && leaves_pending(OVERWORLD)==0 &&
        leaves_pending(END)==end_pending && save_write());
    save_leave();
    CHECK(save_create_with_generation("巡护机与瓜田 · 存档",SURVIVAL,GENERATOR_GOLEMS) &&
        world_generation()==GENERATOR_GOLEMS);
    BlockEdit gourds[]={{-17,150,7,FARM_BASE+7},{-17,151,7,PUMPKIN_STEM_BASE+6},
        {-16,151,7,PUMPKIN_FACE_BASE+WEST},{-16,151,8,POPPY_CELL}};
    CHECK(world_set_blocks(gourds,4) &&
        actors_restore(OVERWORLD,(ActorRecord){{-10.5f,151.01f,8.5f},.5f,ACTOR_IRON_GOLEM,78,GOLEM_PLAYER_BUILT}) &&
        actors_restore(NETHER,(ActorRecord){{12.5f,142.01f,2.5f},0.f,ACTOR_IRON_GOLEM,89,GOLEM_PLAYER_BUILT}));
    inventory[2]=(ItemStack){PUMPKIN_SEEDS,4,0,0,0};
    inventory[3]=(ItemStack){CRAWLER_KIT,1,0,0,0};
    CHECK(save_write());
    save_leave();
    CHECK(save_load(31) && actors_in(OVERWORLD)[0].type==ACTOR_IRON_GOLEM &&
        actors_in(OVERWORLD)[0].health==78 && actors_in(OVERWORLD)[0].traits==GOLEM_PLAYER_BUILT &&
        actors_in(NETHER)[0].health==89 && inventory[2].type==PUMPKIN_SEEDS &&
        inventory[3].type==CRAWLER_KIT && inventory[3].count==1 &&
        world_cell(-17,151,7)==PUMPKIN_STEM_BASE+6 && world_cell(-16,151,7)==PUMPKIN_FACE_BASE+WEST &&
        world_cell(-16,151,8)==POPPY_CELL);
    save_leave();
    CHECK(save_create_in_mode("水冷脉冲机与雪球 · 存档",SURVIVAL));
    inventory[0]=(ItemStack){SNOWBALL,16,0,0,0};
    inventory[1]=(ItemStack){DIAMOND_SHOVEL,1,37,enchant_code(ENCH_SILK_TOUCH,1),0};
    CHECK(set_block(24,150,4,STONE) && set_block(24,151,4,SNOW_LAYER) &&
        actors_restore(OVERWORLD,(ActorRecord){{24.5f,151.01f,4.5f},.5f,ACTOR_SNOW_GOLEM,3,0}) &&
        actors_restore(END,(ActorRecord){{31.5f,145.01f,4.5f},0.f,ACTOR_SNOW_GOLEM,2,0}) && save_write());
    save_leave();
    CHECK(save_load(32) && save_current()->format==CURRENT_FORMAT &&
        inventory[0].type==SNOWBALL && inventory[0].count==16 && inventory[1].damage==37 &&
        enchant_level(inventory[1].enchants,ENCH_SILK_TOUCH)==1 &&
        actors_in(OVERWORLD)[0].type==ACTOR_SNOW_GOLEM && actors_in(OVERWORLD)[0].health==3 &&
        actors_in(END)[0].type==ACTOR_SNOW_GOLEM && actors_in(END)[0].health==2 &&
        world_cell(24,151,4)==SNOW_LAYER);
    save_leave();
    CHECK(save_create_in_mode("同行与守候 · 狼群",SURVIVAL));
    LeadLink wolf_lead={LEAD_FENCE,-17,151,8};
    ActorCare pup={12000,0,0,0,1};
    CHECK(set_block(-17,150,8,STONE) && set_block(-17,151,8,WOOD_FENCE) &&
        actors_restore_with_lead(OVERWORLD,(ActorRecord){{-16.5f,151.01f,8.5f},.5f,ACTOR_WOLF,13,
            WOLF_TAMED|WOLF_SITTING},(ActorCare){0,800,0,0,1},wolf_lead) &&
        actors_restore(OVERWORLD,(ActorRecord){{-18.5f,151.01f,6.5f},0,ACTOR_WOLF,WOLF_WILD_HEALTH,0}) &&
        actors_restore_with_care(END,(ActorRecord){{31.5f,145.01f,4.5f},0.f,ACTOR_WOLF,20,WOLF_TAMED},pup) &&
        save_write());
    save_leave();
    CHECK(save_load(33) && save_current()->format==CURRENT_FORMAT &&
        actors_in(OVERWORLD)[0].type==ACTOR_WOLF && actors_in(OVERWORLD)[0].health==13 &&
        actors_in(OVERWORLD)[0].traits==(WOLF_TAMED|WOLF_SITTING) &&
        actors_lead_at(OVERWORLD,0)->kind==LEAD_FENCE && actors_lead_at(OVERWORLD,0)->x==-17 &&
        actors_care_in(OVERWORLD)[0].cooldown==800 && actors_in(OVERWORLD)[1].traits==0 &&
        actors_in(END)[0].type==ACTOR_WOLF && actors_in(END)[0].traits==WOLF_TAMED &&
        actors_care_in(END)[0].grow==12000);
    save_leave();
    CHECK(save_create_in_mode("丛林豹猫与花色 · 存档",SURVIVAL));
    CHECK(actors_restore(OVERWORLD,(ActorRecord){{-18.5f,151.01f,6.5f},0,ACTOR_OCELOT,9,PET_TAMED|PET_SITTING|CAT_TUXEDO}) &&
        actors_restore_with_care(OVERWORLD,(ActorRecord){{-16.5f,151.01f,8.5f},0,ACTOR_OCELOT,10,0},
            (ActorCare){8000,0,0,0,0}) &&
        actors_restore_with_care(NETHER,(ActorRecord){{12.5f,142.01f,2.5f},0,ACTOR_OCELOT,8,PET_TAMED|CAT_TABBY},
            (ActorCare){111,0,0,0,1}) && save_write());
    save_leave();
    CHECK(save_load(34) && save_current()->format==CURRENT_FORMAT &&
        actors_in(OVERWORLD)[0].type==ACTOR_OCELOT && actors_in(OVERWORLD)[0].health==9 &&
        actors_in(OVERWORLD)[0].traits==(PET_TAMED|PET_SITTING|CAT_TUXEDO) &&
        actors_cat_sitting(OVERWORLD,0) && !actors_in(OVERWORLD)[1].traits &&
        actors_care_in(OVERWORLD)[1].grow==8000 && !actors_care_in(OVERWORLD)[1].managed &&
        actors_in(NETHER)[0].traits==(PET_TAMED|CAT_TABBY) && actors_care_in(NETHER)[0].grow==111);
    save_leave();
    CHECK(save_create_in_mode("鱿鱼与四色玻璃 · 存档",SURVIVAL));
    inventory[0]=(ItemStack){INK_SAC,7,0,0,0};
    inventory[1]=(ItemStack){LIGHT_GRAY_GLASS_ITEM,4,0,0,0};
    CHECK(set_block(-17,158,-15,STAINED_GLASS_BASE) && set_block(-16,158,-15,STAINED_GLASS_BASE+3) &&
        actors_restore(OVERWORLD,(ActorRecord){{-17.5f,60.01f,-15.5f},0,ACTOR_SQUID,6,0}));
    CHECK(world_select_dimension(END) && set_block(-17,158,-15,STAINED_GLASS_BASE+1) &&
        world_select_dimension(OVERWORLD) && save_write());
    save_leave();
    CHECK(save_load(35) && save_current()->format==CURRENT_FORMAT && inventory[0].type==INK_SAC && inventory[0].count==7 &&
        inventory[1].type==LIGHT_GRAY_GLASS_ITEM && actors_in(OVERWORLD)[0].type==ACTOR_SQUID &&
        actors_in(OVERWORLD)[0].health==6 && !actors_in(END)[0].type &&
        world_cell(-17,158,-15)==STAINED_GLASS_BASE && world_cell(-16,158,-15)==STAINED_GLASS_BASE+3 &&
        world_select_dimension(END) && world_cell(-17,158,-15)==STAINED_GLASS_BASE+1);
    save_leave();
    CHECK(save_create_with_generation("十六色玻璃与可可 · 存档",SURVIVAL,GENERATOR_DYES) &&
        world_generation()==GENERATOR_DYES);
    inventory[0]=(ItemStack){ORANGE_GLASS_ITEM,19,0,0,0};
    inventory[1]=(ItemStack){COCOA_BEANS,11,0,0,0};
    BlockEdit dye_edits[]={{8,155,8,STAINED_GLASS_END-1},{10,155,8,LAPIS_ORE_CELL},
        {11,154,8,SAND},{11,155,8,CACTUS_BASE+11},{13,155,8,JUNGLE_LOG},
        {14,155,8,COCOA_BASE+4+WEST},{16,155,8,LAPIS_BLOCK_CELL}};
    CHECK(world_set_blocks(dye_edits,sizeof dye_edits/sizeof *dye_edits) && world_select_dimension(NETHER) &&
        set_block(8,155,8,STAINED_GLASS_BASE+7) && world_select_dimension(OVERWORLD) && save_write());
    save_leave();
    CHECK(save_load(36) && world_generation()==GENERATOR_DYES && save_current()->format==CURRENT_FORMAT &&
        inventory[0].type==ORANGE_GLASS_ITEM && inventory[0].count==19 && inventory[1].type==COCOA_BEANS &&
        world_cell(8,155,8)==STAINED_GLASS_END-1 && world_cell(10,155,8)==LAPIS_ORE_CELL &&
        world_cell(11,155,8)==CACTUS_BASE+11 && world_cell(14,155,8)==COCOA_BASE+4+WEST &&
        world_cell(16,155,8)==LAPIS_BLOCK_CELL && world_select_dimension(NETHER) &&
        world_cell(8,155,8)==STAINED_GLASS_BASE+7);
    save_leave();
    CHECK(save_create_with_generation("玻璃板与栏杆 · 存档",SURVIVAL,GENERATOR_DYES));
    inventory[0]=(ItemStack){ORANGE_PANE_ITEM,19,0,0,0};
    inventory[1]=(ItemStack){GLASS_PANE_ITEM,10,0,0,0};
    inventory[2]=(ItemStack){IRON_BARS_ITEM,8,0,0,0};
    CHECK(set_block(-17,159,-15,STAINED_PANE_END-1) && set_block(-16,159,-15,IRON_BARS_CELL) &&
        world_select_dimension(END) && set_block(-17,159,-15,GLASS_PANE_CELL) &&
        world_select_dimension(OVERWORLD) && save_write());
    snprintf(path,sizeof path,"%s/%08x.vxl",dir,37);
    CHECK(write_legacy_fixture(path,52));
    save_leave();
    CHECK(save_load(37) && save_current()->format==CURRENT_FORMAT && world_generation()==GENERATOR_DYES &&
        inventory[0].type==ORANGE_PANE_ITEM && inventory[0].count==19 && inventory[1].type==GLASS_PANE_ITEM &&
        inventory[2].type==IRON_BARS_ITEM && world_cell(-17,159,-15)==STAINED_PANE_END-1 &&
        world_cell(-16,159,-15)==IRON_BARS_CELL && world_select_dimension(END) &&
        world_cell(-17,159,-15)==GLASS_PANE_CELL && world_cell(-16,159,-15)==AIR);
    save_leave();
    CHECK(save_create_with_generation("陷阱箱 · 存档",SURVIVAL,GENERATOR_DYES));
    ChestPos trapped={-17,159,-15},half={-16,159,-15};
    ChestView chest={OVERWORLD,{{0,0,0},{0,0,0}},0,0};
    inventory[0]=(ItemStack){TRIPWIRE_HOOK_ITEM,3,0,0,0};
    inventory[1]=(ItemStack){TRAPPED_CHEST_ITEM,1,0,0,0};
    inventory[2]=(ItemStack){DIAMOND,2,0,0,0};
    CHECK(set_block(trapped.x,trapped.y,trapped.z,TRAPPED_CHEST) &&
        set_block(half.x,half.y,half.z,TRAPPED_CHEST) && chest_open(half,&chest)==CHEST_OK &&
        chest.count==2 && chest.kind==TRAPPED_CHEST &&
        chest_move(&chest,DOUBLE_CHEST_SLOTS+2,29,0)==CHEST_OK);
    CHECK(world_select_dimension(NETHER) && set_block(half.x,half.y,half.z,CHEST) &&
        world_select_dimension(OVERWORLD));
    chest_access_begin(&chest);
    CHECK(chest_trap_power(trapped) && save_write());
    snprintf(path,sizeof path,"%s/%08x.vxl",dir,38);
    CHECK(write_legacy_fixture(path,53));
    save_leave();
    CHECK(save_load(38) && save_current()->format==CURRENT_FORMAT &&
        inventory[0].type==TRIPWIRE_HOOK_ITEM && inventory[0].count==3 && inventory[1].type==TRAPPED_CHEST_ITEM &&
        !chest_trap_power(trapped) && !chest_trap_power(half));
    CHECK(chest_open(trapped,&chest)==CHEST_OK && chest.count==2 && chest.kind==TRAPPED_CHEST &&
        chest_slot(&chest,29).type==DIAMOND && chest_slot(&chest,29).count==2);
    CHECK(world_select_dimension(NETHER) && chest_open(half,&chest)==CHEST_OK &&
        chest.count==1 && chest.kind==CHEST && !chest_slot(&chest,2).count);
    save_leave();
    CHECK(save_create_with_generation("绊线 · 存档",SURVIVAL,GENERATOR_DYES));
    const CircuitPos a={-17,173,0},b={-15,173,0},line={-16,173,0};
    CHECK(set_block(-18,173,0,STONE) && set_block(-14,173,0,STONE) &&
        tripwire_place_hook(a,(CircuitPos){-18,173,0}) && tripwire_place_hook(b,(CircuitPos){-14,173,0}) &&
        tripwire_place_string(line));
    tripwire_body_contact(line,(BlockBox){-16.f,173.2f,0.f,-15.f,173.4f,1.f});
    CHECK(redstone_tripwire_power(a.x,a.y,a.z)==15 && redstone_tripwire_power(b.x,b.y,b.z)==15 && save_write());
    save_leave();
    CHECK(save_load(39) && save_current()->format==CURRENT_FORMAT && world_cell(line.x,line.y,line.z)==TRIPWIRE_CELL &&
        world_cell(a.x,a.y,a.z)==TRIPWIRE_HOOK_BASE+WEST && world_cell(b.x,b.y,b.z)==TRIPWIRE_HOOK_BASE+EAST &&
        redstone_tripwire_power(a.x,a.y,a.z)==15 && redstone_tripwire_power(b.x,b.y,b.z)==15);
    CHECK(world_select_dimension(END) && !redstone_tripwire_power(a.x,a.y,a.z) &&
        world_select_dimension(OVERWORLD));
    for (int i=0;i<TRIPWIRE_PULSE_TICKS+2;++i) redstone_step(-16,0);
    CHECK(!redstone_tripwire_power(a.x,a.y,a.z) && !redstone_tripwire_power(b.x,b.y,b.z));
    save_leave();
    failures+=temple_roundtrip();
    snprintf(path,sizeof path,"%s/%08x.vxl",dir,40);
    const uint32_t bad_x[] = {UINT32_C(0x7fc00000), UINT32_C(0x7f800000), UINT32_C(0xff800000)};
    for (size_t i=0;i<sizeof bad_x/sizeof *bad_x;++i) {
        CHECK(replace_saved_player_x(path,bad_x[i]));
        CHECK(!save_load(40));
        save_leave();
    }
    CHECK(save_create_with_generation("旧远征记录",SURVIVAL,GENERATOR_TEMPLES));
    CHECK(world_select_dimension(END) && set_block(0,148,0,PLANKS) && save_write());
    save_leave();
    snprintf(path,sizeof path,"%s/%08x.vxl",dir,41);
    CHECK(write_legacy_fixture(path,55));
    CHECK(save_load(41) && save_current()->returned_from_retired_site && world_dimension()==OVERWORLD);
    CHECK(world_select_dimension(END) && world_cell(0,148,0)==PLANKS && world_select_dimension(OVERWORLD));
    CHECK(save_write());
    save_leave();
    CHECK(save_load(41) && !save_current()->returned_from_retired_site && world_dimension()==OVERWORLD);
    save_leave();
    CHECK(!save_load(40));
    save_leave();
    CHECK(game_selected_difficulty()==NORMAL && save_set_difficulty(EASY));
    CHECK(save_open_directory(dir) && game_selected_difficulty()==EASY);
    CHECK(save_create_hardcore("仅一命") && player_rules.hardcore && game_effective_difficulty()==HARD);
    CHECK(save_current()->hardcore && !save_current()->hardcore_dead && !save_set_difficulty(NORMAL));
    player_take_damage(DAMAGE_VOID,MAX_HEALTH);
    CHECK(!player_rules.health && player_respawn()==RESPAWN_HARDCORE && save_write());
    CHECK(save_current()->hardcore_dead);
    save_leave();
    CHECK(save_open_directory(dir) && game_selected_difficulty()==EASY);
    CHECK(save_load(42) && save_current()->hardcore_dead && game_effective_difficulty()==HARD &&
        !player_rules.health && player_respawn()==RESPAWN_HARDCORE);
    player_rules.health=MAX_HEALTH;
    CHECK(!save_write()); /* A dead Hardcore slot cannot be rewritten as alive. */
    save_leave();
    CHECK(save_load(42) && !player_rules.health);
    save_leave();
    CHECK(save_load(41) && !player_rules.hardcore && game_effective_difficulty()==EASY);
    save_leave();
    CHECK(save_set_difficulty(NORMAL));
    snprintf(path,sizeof path,"%s/.difficulty",dir);
    CHECK(unlink(path)==0);
    CHECK(save_advanced_lighting());
    CHECK(!save_set_advanced_lighting(2) && save_advanced_lighting());
    CHECK(save_set_advanced_lighting(0) && !save_advanced_lighting());
    CHECK(save_open_directory(dir) && !save_advanced_lighting());
    CHECK(save_set_advanced_lighting(1) && save_open_directory(dir) && save_advanced_lighting());
    snprintf(path,sizeof path,"%s/.graphics",dir);
    CHECK(unlink(path)==0);
    CHECK(save_open_directory(dir) && save_advanced_lighting());
    CHECK(save_create_with_generation("接应 · 往返",SURVIVAL,GENERATOR_FUNGI));
    px=8.5f; py=150.01f; pz=12.25f;
    CHECK(set_block(9,149,9,MYCELIUM) && set_block(9,150,9,BROWN_MUSHROOM));
    VillagerSave market={8,149,8,VILLAGER_LIBRARIAN,0,2,{2,1,0},0};
    CHECK(actors_restore_stranded(OVERWORLD,
        (ActorRecord){{8.5f,150.01f,8.5f},0,ACTOR_STRANDED_GUIDE,STRANDED_HEALTH,0},
        (StrandedGuideState){{market,0},119}));
    VillagerSave roaming={7,149,10,VILLAGER_FARMER,17,2,{2,1,0},0};
    CHECK(actors_restore_roaming(OVERWORLD,
        (ActorRecord){{7.5f,150.01f,10.5f},0,ACTOR_VILLAGER,17,ROAMING_VILLAGER|VILLAGER_FARMER},roaming));
    player_effects.weakness=2037;
    inventory[2]=(ItemStack){SPLASH_WEAKNESS_LONG,1,0,0,0};
    CHECK(save_write());
    save_leave();
    CHECK(save_load(43) && player_effects.weakness==2037 && inventory[2].type==SPLASH_WEAKNESS_LONG &&
        world_cell(9,150,9)==BROWN_MUSHROOM && !actors_armor_state(OVERWORLD,0).door_breaker);
    StrandedGuideState restored=actors_stranded_state(OVERWORLD,0);
    CHECK(restored.rest_ticks==119 && restored.origin.market.home==VILLAGER_LIBRARIAN &&
          restored.origin.market.uses[0]==2);
    VillagerSave existing={0};
    CHECK(villagers_roaming_market(1,&existing) && existing.health==17 && existing.uses[1]==1);
    CHECK(villagers_role(villagers_open(1))==VILLAGER_FARMER);
    save_leave();

    CHECK(save_create_with_generation("蘑菇 · 往返",SURVIVAL,GENERATOR_MUSHROOM_ISLAND) &&
        world_generation()==GENERATOR_MUSHROOM_ISLAND);
    CHECK(set_block(8,149,9,MYCELIUM) && set_block(8,150,9,RED_MUSHROOM) &&
        set_block(9,150,9,BROWN_MUSHROOM_BLOCK));
    uint16_t bovine_name=0;
    CHECK(actors_restore_with_lead(OVERWORLD,(ActorRecord){{8.5f,150.f,11.5f},0,ACTOR_COW,9,0},
        (ActorCare){.grow=160,.managed=1},(LeadLink){LEAD_FENCE,8,150,10}) &&
        item_name_intern("岩岸小牛",&bovine_name) && actors_set_name(OVERWORLD,0,bovine_name));
    inventory[0]=(ItemStack){MUSHROOM_STEW,1,0,0,0};
    inventory[1]=(ItemStack){BOWL,8,0,0,0};
    CHECK(save_write()); save_leave();
    snprintf(path,sizeof path,"%s/%08x.vxl",dir,44);
    CHECK(write_retired_bovine_fixture(path) && save_load(44) && save_current()->format==CURRENT_FORMAT &&
        world_generation()==GENERATOR_MUSHROOM_ISLAND);
    CHECK(world_cell(8,150,9)==RED_MUSHROOM && world_cell(9,150,9)==BROWN_MUSHROOM_BLOCK &&
        inventory[0].type==MUSHROOM_STEW && inventory[1].type==BOWL && inventory[1].count==8);
    const char *restored_bovine_name=item_name_for_id(actors_name_id(OVERWORLD,0));
    CHECK(actors_in(OVERWORLD)[0].type==ACTOR_COW && actors_in(OVERWORLD)[0].health==9 &&
        actors_care_in(OVERWORLD)[0].grow==160 && actors_lead_at(OVERWORLD,0)->kind==LEAD_FENCE &&
        restored_bovine_name && !strcmp(restored_bovine_name,"岩岸小牛"));
    save_leave();
    CHECK(save_create("地图 · 往返"));
    memset(inventory,0,sizeof inventory);
    uint8_t explored_map[MAP_PIXELS]={0};
    explored_map[8008]=6;
    explored_map[8012]=50;
    CHECK(map_restore(-128,256,OVERWORLD,0,explored_map));
    CHECK(map_zoom(1)==2);
    inventory[0]=(ItemStack){FILLED_MAP,2,0,0,1};
    inventory[1]=(ItemStack){FILLED_MAP,1,0,0,2};
    CHECK(item_set_custom(&inventory[0],21,"探索日志"));
    CHECK(save_write()); save_leave();
    CHECK(save_load(45) && map_count()==2);
    MapView chart={0},wider={0};
    CHECK(map_view(1,&chart) && chart.x==-128 && chart.z==256 && chart.explored==2 &&
        chart.colors[8008]==6 && chart.colors[8012]==50);
    CHECK(map_view(2,&wider) && wider.scale==1 && wider.explored==0 &&
        inventory[0].count==2 && map_item_id(inventory[0])==1 && map_item_id(inventory[1])==2 &&
        !strcmp(item_display_name(inventory[0]),"探索日志") && item_prior_work(inventory[0])==21);
    save_leave();
    CHECK(save_load(44) && map_count()==0);
    save_leave();

    uint64_t custom=1;
    CHECK(world_seed_parse("",&custom)==SEED_AUTOMATIC && custom==1);
    CHECK(world_seed_parse("0",&custom)==SEED_EXPLICIT && custom==0);
    CHECK(world_seed_parse("+9223372036854775807",&custom)==SEED_EXPLICIT &&
        custom==INT64_MAX && world_seed_signed(custom)==INT64_MAX);
    CHECK(world_seed_parse("-9223372036854775808",&custom)==SEED_EXPLICIT &&
        custom==(UINT64_C(1)<<63) && world_seed_signed(custom)==INT64_MIN);
    CHECK(world_seed_parse("9223372036854775808",&custom)==SEED_EXPLICIT &&
        world_seed_signed(custom)==-1773151197);
    CHECK(world_seed_parse("方块😀",&custom)==SEED_EXPLICIT && custom==799047329);
    CHECK(world_seed_parse("\xed\xa0\x80",&custom)==SEED_INVALID);
    CHECK(world_seed_parse("\xf0\x9f",&custom)==SEED_INVALID);
    char long_seed[WORLD_SEED_TEXT_MAX+2];
    memset(long_seed,'x',sizeof long_seed-1); long_seed[sizeof long_seed-1]=0;
    CHECK(world_seed_parse(long_seed,&custom)==SEED_INVALID);
    long_seed[WORLD_SEED_TEXT_MAX]=0;
    CHECK(world_seed_parse(long_seed,&custom)==SEED_EXPLICIT);

    /* Matching seed, different IDs and edits. Verify generated cells rather
       than just checking the saved number. Use a negative wide value to
       exercise the signed UI value as well as both stored words. */
    const uint64_t wide=(UINT64_C(1)<<63)|1u;
    CHECK(save_create_seeded("Seed A",SURVIVAL,0,wide) && save_current()->id==46 &&
        save_current()->has_explicit_seed && world_seed()==wide);
    uint32_t fingerprints[DIMENSION_COUNT]={0};
    const int cell_count=CHUNK_SIZE*CHUNK_SIZE*WY;
    for (int d=0;d<DIMENSION_COUNT;++d) {
        CHECK(world_select_dimension((Dimension)d));
        const unsigned char *cells=chunk_view(3,-2).cells;
        CHECK(cells!=NULL);
        unsigned h=2166136261u;
        if (cells) for (int i=0;i<cell_count;++i) h=(h^cells[i])*16777619u;
        fingerprints[d]=h;
    }
    CHECK(world_select_dimension(OVERWORLD));
    const int distinct=block(3,150,3)==LAPIS_BLOCK ? IRON_BLOCK : LAPIS_BLOCK;
    CHECK(set_block(3,150,3,distinct) && save_write());
    save_leave();
    CHECK(save_create_seeded("Seed B",CREATIVE,0,wide) && save_current()->id==47 &&
        world_seed()==wide && block(3,150,3)!=distinct);
    for (int d=0;d<DIMENSION_COUNT;++d) {
        CHECK(world_select_dimension((Dimension)d));
        const unsigned char *cells=chunk_view(3,-2).cells;
        CHECK(cells!=NULL);
        unsigned h=2166136261u;
        if (cells) for (int i=0;i<cell_count;++i) h=(h^cells[i])*16777619u;
        CHECK(h==fingerprints[d]);
    }
    CHECK(world_select_dimension(OVERWORLD) && save_write());
    save_leave();
    CHECK(save_load(46) && world_seed()==wide && block(3,150,3)==distinct);
    save_leave();
    CHECK(save_load(47) && world_seed()==wide && block(3,150,3)!=distinct);
    save_leave();
    CHECK(save_create_seeded("Low bits",SURVIVAL,1,1) && save_current()->id==48 && player_rules.hardcore);
    const unsigned char *cells=chunk_view(3,-2).cells;
    unsigned low_fingerprint=2166136261u;
    CHECK(cells!=NULL);
    if (cells) for (int i=0;i<cell_count;++i) low_fingerprint=(low_fingerprint^cells[i])*16777619u;
    CHECK(low_fingerprint!=fingerprints[OVERWORLD]);
    save_leave();
    CHECK(save_load(48) && player_rules.hardcore && world_seed()==1 && save_current()->has_explicit_seed);
    save_leave();
    CHECK(save_create_seeded("Zero",SURVIVAL,0,0) && world_seed()==0);
    save_leave();
    CHECK(save_load(49) && world_seed()==0 && save_current()->has_explicit_seed);
    save_leave();

    CHECK(save_create_seeded("Truncated",SURVIVAL,0,wide));
    save_leave();
    snprintf(path,sizeof path,"%s/%08x.vxl",dir,50);
    CHECK(truncate(path,7+1+4+4+1+strlen("Truncated")+3+2)==0);
    CHECK(!save_load(50) && !save_current());
    printf("WORLD SEEDS: signed bounds, Unicode, seed reuse, three dimensions, corruption\n");

    const uint64_t profile_seed=UINT64_C(0x8000000000000001);
    CHECK(!save_create_profiled("Invalid Profile",CREATIVE,0,(WorldProfile)99,&profile_seed));
    CHECK(save_create_profiled("Classic Flat",CREATIVE,0,WORLD_SUPERFLAT,&profile_seed));
    CHECK(save_current()->id==51 && save_current()->profile==WORLD_SUPERFLAT && world_seed()==profile_seed);
    CHECK(set_block(28,4,-15,IRON_BLOCK) && map_restore(0,0,OVERWORLD,0,explored_map));
    CHECK(world_select_dimension(NETHER) && save_write());
    save_leave();
    CHECK(save_load(51) && world_profile()==WORLD_SUPERFLAT && world_seed()==profile_seed &&
        world_dimension()==NETHER && map_count()==1);
    CHECK(world_select_dimension(OVERWORLD) && block(28,4,-15)==IRON_BLOCK && block(28,0,-15)==BEDROCK);
    save_leave();
    CHECK(save_create_profiled("Large Biomes",SURVIVAL,1,WORLD_LARGE_BIOMES,NULL));
    save_leave();
    CHECK(save_load(52) && world_profile()==WORLD_LARGE_BIOMES && player_rules.hardcore &&
        !save_current()->has_explicit_seed);
    save_leave();
    CHECK(save_create_profiled("Amplified",CREATIVE,0,WORLD_AMPLIFIED,&profile_seed));
    save_leave();
    CHECK(save_load(53) && world_profile()==WORLD_AMPLIFIED && world_seed()==profile_seed && !map_count());
    save_leave();
    CHECK(save_load(46) && world_profile()==WORLD_STANDARD && world_seed()==profile_seed);
    save_leave();
    snprintf(path,sizeof path,"%s/%08x.vxl",dir,53);
    FILE *invalid_profile=fopen(path,"r+b");
    CHECK(invalid_profile!=NULL);
    if (invalid_profile) {
        CHECK(!fseek(invalid_profile,7+1+4+4+1+strlen("Amplified")+3+4,SEEK_SET) &&
            fputc(0xff,invalid_profile)!=EOF);
        CHECK(!fclose(invalid_profile));
    }
    CHECK(!save_load(53) && !save_current());

    const uint64_t bonus_seed=100;
    CHECK(!save_create_options("No hardcore reward",SURVIVAL,1,WORLD_SUPERFLAT,&bonus_seed,0,1));
    CHECK(save_create_options("Flat Start",SURVIVAL,0,WORLD_SUPERFLAT,&bonus_seed,1,1));
    const ChestRecord *initial_bonus=chest_record_in(OVERWORLD,0);
    ChestPos bonus_pos={0,4,-3};
    CHECK(save_current()->id==54 && save_current()->bonus_chest && world_structures_disabled() &&
        chests_count_in(OVERWORLD)==1 && initial_bonus && initial_bonus->at.x==bonus_pos.x &&
        initial_bonus->at.y==bonus_pos.y && initial_bonus->at.z==bonus_pos.z);
    CHECK(block(0,4,-3)==CHEST && block(0,4,-2)==TORCH && block(0,4,-4)==TORCH &&
        block(1,4,-3)==TORCH && block(-1,4,-3)==TORCH);
    ChestView bonus_view={OVERWORLD,{{0,0,0},{0,0,0}},0,0};
    int bonus_tool=-1,bonus_axe=-1;
    CHECK(chest_open(bonus_pos,&bonus_view)==CHEST_OK);
    for (int i=0;i<CHEST_SLOTS;++i) {
        ItemStack s=chest_slot(&bonus_view,i);
        if (s.count && (s.type==WOOD_PICKAXE || s.type==STONE_PICKAXE)) bonus_tool=i;
        if (s.count==1 && (s.type==WOOD_AXE || s.type==STONE_AXE)) bonus_axe=i;
    }
    CHECK(bonus_tool>=0 && bonus_axe>=0);
    save_leave();
    CHECK(save_load(54) && world_structures_disabled() && save_current()->bonus_chest &&
        chest_open(bonus_pos,&bonus_view)==CHEST_OK && chest_slot(&bonus_view,bonus_tool).count==1 &&
        is_axe(chest_slot(&bonus_view,bonus_axe).type));
    CHECK(chest_move(&bonus_view,bonus_tool,DOUBLE_CHEST_SLOTS+2,0)==CHEST_OK &&
        !chest_slot(&bonus_view,bonus_tool).count && save_write());
    save_leave();
    CHECK(save_load(54) && chest_open(bonus_pos,&bonus_view)==CHEST_OK &&
        !chest_slot(&bonus_view,bonus_tool).count && inventory[2].count==1);
    CHECK(chest_break(bonus_pos)!=NULL && block(0,4,-3)==AIR && save_write());
    save_leave();
    CHECK(save_load(54) && save_current()->bonus_chest && world_structures_disabled() &&
        block(0,4,-3)==AIR && !chests_count_in(OVERWORLD));
    save_leave();

    CHECK(save_create_profiled("Previous Format",CREATIVE,0,WORLD_SUPERFLAT,&bonus_seed));
    save_leave();
    snprintf(path,sizeof path,"%s/%08x.vxl",dir,55);
    FILE *legacy_options=fopen(path,"rb");
    CHECK(legacy_options!=NULL);
    if (legacy_options) {
        unsigned char blob[4096];
        size_t len=fread(blob,1,sizeof blob,legacy_options);
        CHECK(!fclose(legacy_options));
        if (len>=FIELD_SAVE_BYTES && blob[7]==CURRENT_FORMAT) len-=FIELD_SAVE_BYTES;
        const size_t at=17+strlen("Previous Format")+3+4+1;
        const size_t tail=14+4*DIMENSION_COUNT+EMPTY_ITEM_FRAMES_BYTES; /* Empty late fields. */
        int empty=len>at+2+tail && len<sizeof blob;
        for (size_t i=0;empty && i<tail;++i) empty=blob[len-1-i]==0;
        CHECK(empty && blob[at]==0 && blob[at+1]==0);
        if (empty) {
            len-=tail;
            blob[7]=64;
            blob[18+strlen("Previous Format")]=GENERATOR_MUSHROOM_ISLAND;
            memmove(blob+at,blob+at+2,len-at-2);
            legacy_options=fopen(path,"wb");
            CHECK(legacy_options!=NULL);
            if (legacy_options) CHECK(fwrite(blob,1,len-2,legacy_options)==len-2 && !fclose(legacy_options));
        }
    }
    CHECK(save_load(55) && !world_structures_disabled() && !save_current()->bonus_chest &&
        world_profile()==WORLD_SUPERFLAT && !chests_count_in(OVERWORLD) && block(0,4,-3)==AIR);
    save_leave();
    CHECK(save_create("Damaged axe"));
    inventory[2]=(ItemStack){IRON_AXE,1,19,enchant_code(ENCH_UNBREAKING,2),0};
    CHECK(save_write());
    save_leave();
    CHECK(save_load(56) && inventory[2].type==IRON_AXE && inventory[2].damage==19 &&
        enchant_level(inventory[2].enchants,ENCH_UNBREAKING)==2);
    save_leave();
    snprintf(path,sizeof path,"%s/%08x.vxl",dir,56);
    CHECK(write_legacy_fixture(path,65));
    CHECK(!save_load(56) && !save_current());
    FlatLayout flat;
    CHECK(!flat_parse("bedrock,6*sandstone,2*oxide-sand | dryland",&flat));
    CHECK(save_create_layout("Custom layers",CREATIVE,0,WORLD_SUPERFLAT,&bonus_seed,0,0,&flat) &&
        py>9.f && py<9.1f && block(0,8,0)==RED_SAND);
    CHECK(set_block(-2,9,1,PLANKS) && save_write());
    save_leave();
    CHECK(save_load(57) && flat_equal(&flat,&save_current()->flat) &&
        block(-2,9,1)==PLANKS && block(200,8,-107)==RED_SAND && terrain_biome(10,10)==BIOME_DESERT);
    save_leave();
    snprintf(path,sizeof path,"%s/%08x.vxl",dir,57);
    FILE *bad_flat=fopen(path,"r+b");
    CHECK(bad_flat!=NULL);
    if (bad_flat) {
        const long height_at=17+strlen("Custom layers")+3+4+1;
        CHECK(!fseek(bad_flat,height_at+2,SEEK_SET) && fputc(BIOME_END,bad_flat)!=EOF && !fclose(bad_flat));
    }
    CHECK(!save_load(57) && !save_current());
    CHECK(save_create_options("Detector rail",CREATIVE,0,WORLD_SUPERFLAT,&bonus_seed,1,0));
    CHECK(rail_place((RailCell){0,4,-2},RAIL_DETECTOR,0) && set_block(1,4,-2,REDSTONE_LAMP) &&
        cart_launch_on_rail((RailCell){0,4,-2}));
    inventory[3]=(ItemStack){DETECTOR_RAIL_ITEM,6,0,0,0};
    for (int i=0;i<5;++i) redstone_step(0,0);
    CHECK(world_cell(1,4,-2)==LAMP_ON && save_write());
    save_leave();
    CHECK(save_load(58) && redstone_detector_power(0,4,-2)==15 && world_cell(1,4,-2)==LAMP_ON &&
        cart_on_track((RailCell){0,4,-2}) && inventory[3].count==6 &&
        inventory[3].type==DETECTOR_RAIL_ITEM && world_cell(0,4,-2)==DETECTOR_TRACK_BASE);
    carts_reset();
    for (int i=0;i<24;++i) redstone_step(0,0);
    CHECK(!redstone_detector_power(0,4,-2) && world_cell(1,4,-2)==LAMP_OFF);
    save_leave();
    CHECK(save_create_options("TNT cart",CREATIVE,0,WORLD_SUPERFLAT,&bonus_seed,1,0));
    CHECK(set_block(0,3,-2,REDSTONE_BLOCK) && rail_place((RailCell){0,4,-2},RAIL_ACTIVATOR,0) &&
        set_block(1,4,-2,GLASS) && cart_launch_tnt_on_rail((RailCell){0,4,-2}));
    inventory[3]=(ItemStack){TNT_MINECART,1,0,0,0};
    for (int i=0;i<17;++i) carts_tick_fuses();
    CHECK(carts_in(OVERWORLD)[0].fuse==63 && save_write());
    save_leave();
    CHECK(save_load(59) && carts_in(OVERWORLD)[0].kind==CART_TNT && carts_in(OVERWORLD)[0].fuse==63 &&
        inventory[3].type==TNT_MINECART && world_cell(0,4,-2)==ACTIVATOR_TRACK_BASE);
    for (int i=0;i<63;++i) { carts_tick_fuses(); blasts_step(1); }
    CHECK(!carts_in(OVERWORLD)[0].occupied && world_cell(1,4,-2)==AIR &&
        world_cell(0,4,-2)==ACTIVATOR_TRACK_BASE && world_cell(0,3,-2)==REDSTONE_BLOCK && save_write());
    save_leave();
    CHECK(save_load(59) && !carts_in(OVERWORLD)[0].occupied && world_cell(1,4,-2)==AIR);
    save_leave();
    CHECK(save_create_options("Chest cart",CREATIVE,0,WORLD_SUPERFLAT,&bonus_seed,1,0));
    CHECK(rail_place((RailCell){0,4,-2},RAIL_DETECTOR,0) && cart_launch_chest_on_rail((RailCell){0,4,-2}));
    CartStorageView cart={OVERWORLD,0,0};
    CHECK(cart_open_storage(0,&cart));
    inventory[3]=(ItemStack){IRON_PICKAXE,1,19,enchant_code(ENCH_UNBREAKING,2),0};
    CHECK(item_set_custom(&inventory[3],7,"货车里的镐") && cart_storage_move(&cart,CART_STORAGE_SLOTS+3,26,0));
    CHECK(save_write()); save_leave();
    CHECK(save_load(60) && !cart_storage_valid(&cart) && cart_open_storage(0,&cart));
    ItemStack carried=cart_storage_item(&cart,26);
    CHECK(carried.type==IRON_PICKAXE && carried.damage==19 && !strcmp(item_display_name(carried),"货车里的镐") &&
        item_prior_work(carried)==7 && !inventory[3].count && cart_detector_signal((RailCell){0,4,-2})==1);
    save_leave();
    CHECK(save_create_options("Hopper cart",CREATIVE,0,WORLD_SUPERFLAT,&bonus_seed,1,0));
    CHECK(rail_place((RailCell){0,4,-2},RAIL_DETECTOR,0) && cart_launch_hopper_on_rail((RailCell){0,4,-2}));
    CHECK(cart_open_storage(0,&cart));
    inventory[3]=(ItemStack){HOPPER_MINECART,1,0,0,0};
    inventory[4]=(ItemStack){COBBLESTONE,64,0,0,0};
    CHECK(cart_storage_move(&cart,HOPPER_CART_SLOTS+4,4,0) && cart_restore_hopper_cooldown(OVERWORLD,0,3));
    CHECK(save_write()); save_leave();
    CHECK(save_load(61) && !cart_storage_valid(&cart) && cart_open_storage(0,&cart));
    CHECK(cart_cargo_count(OVERWORLD,0)==HOPPER_CART_SLOTS && inventory[3].type==HOPPER_MINECART &&
        !inventory[4].count && cart_storage_item(&cart,4).count==64 && !cart_storage_item(&cart,5).count &&
        cart_hopper_cooldown(OVERWORLD,0)==3 && cart_detector_signal((RailCell){0,4,-2})==3);
    save_leave();
    CHECK(save_create_options("Furnace cart",CREATIVE,0,WORLD_SUPERFLAT,&bonus_seed,1,0));
    CHECK(rail_place((RailCell){0,4,-2},RAIL_NORMAL,0) && cart_launch_furnace_on_rail((RailCell){0,4,-2}));
    inventory[5]=(ItemStack){FURNACE_MINECART,1,0,0,0};
    CHECK(cart_restore_furnace_drive(OVERWORLD,0,(CartFurnaceDrive){2274,0,-1}) && save_write());
    save_leave();
    CartFurnaceDrive motor;
    CHECK(save_load(62) && inventory[5].type==FURNACE_MINECART &&
        cart_furnace_drive(OVERWORLD,0,&motor) && motor.fuel==2274 && motor.x==0.f && motor.z==-1.f);
    save_leave();
    CHECK(save_create_options("Junction",CREATIVE,0,WORLD_SUPERFLAT,&bonus_seed,1,0));
    CHECK(rail_place((RailCell){0,4,0},RAIL_NORMAL,0) &&
        rail_place((RailCell){0,4,-1},RAIL_NORMAL,0) &&
        rail_place((RailCell){0,4,1},RAIL_NORMAL,0) &&
        rail_place((RailCell){1,4,0},RAIL_NORMAL,1));
    CHECK(world_cell(0,4,0)==TRACK_BASE+BEND_ES &&
        redstone_place_lever((CircuitPos){-1,4,0},(CircuitPos){-1,3,0},0));
    CHECK(cart_launch_on_rail((RailCell){0,4,0}) && redstone_toggle_lever(-1,4,0)==2);
    redstone_step(0,0);
    CHECK(world_cell(0,4,0)==TRACK_BASE+BEND_ES && save_write());
    save_leave();
    CHECK(save_load(63));
    for (int i=0;i<8;++i) redstone_step(0,0);
    CHECK(world_cell(0,4,0)==TRACK_BASE+BEND_ES && cart_on_track((RailCell){0,4,0}));
    carts_reset();
    for (int i=0;i<8;++i) redstone_step(0,0);
    CHECK(world_cell(0,4,0)==TRACK_BASE+BEND_NE);
    save_leave();
    CHECK(save_create_options("TNT cart impact",CREATIVE,0,WORLD_SUPERFLAT,&bonus_seed,1,0));
    CHECK(cart_restore(OVERWORLD,(CartRecord){{.5f,9.f,.5f},{0,0,0},0,0,0,1,CART_TNT,60,1,{0,-4.f,0},14.5f}));
    CHECK(blast_queue_cart((Vec3){1.5f,4.f,.5f},72.f,0) && save_write());
    save_leave();
    CHECK(save_load(64));
    const CartRecord *falling=&carts_in(OVERWORLD)[0];
    size_t queued_count=0;
    const PrimedBlast *queued=blasts_in(OVERWORLD,&queued_count);
    CHECK(falling->occupied && falling->off_track && falling->fuse==60 && falling->fall_distance==14.5f &&
        falling->free_velocity.y==-4.f && queued_count==1 && queued[0].kind==BLAST_CART_IMPACT && queued[0].speed.x==72.f);
    for (int i=1;i<BLAST_CAP;++i)
        CHECK(blast_restore(OVERWORLD,(PrimedBlast){{240.f,140.f,240.f},{0.f,0.f,0.f},TNT_FUSE,BLAST_TNT}));
    unsigned step=0;
    while (step++<80 && falling->fuse!=CART_TNT_PROTECTED_PENDING) carts_step(.05f,0,0);
    CHECK(falling->occupied && falling->off_track && falling->fuse==CART_TNT_PROTECTED_PENDING &&
        falling->speed>38.f && falling->speed<40.f && !falling->fall_distance &&
        !falling->free_velocity.y && save_write());
    save_leave();
    CHECK(save_load(64) && falling->fuse==CART_TNT_PROTECTED_PENDING && falling->speed>38.f);
    /* The original pending power and protection survive a second chain hit. */
    cart_blast_hit(0);
    CHECK(falling->occupied && falling->speed>38.f && falling->fuse==CART_TNT_PROTECTED_PENDING);
    blasts_reset();
    for (int i=0;i<2;++i) carts_tick_fuses();
    queued=blasts_in(OVERWORLD,&queued_count);
    CHECK(queued_count==1 && !falling->occupied && queued[0].kind==BLAST_CART_TRACK && queued[0].speed.x>38.f);
    save_leave();
    CHECK(save_create_options("Personal Ender storage",SURVIVAL,0,WORLD_SUPERFLAT,&bonus_seed,1,0));
    EnderChestView private_box={OVERWORLD,{0,0,0},0};
    CHECK(set_block(2,4,-2,ENDER_CHEST) && ender_chest_open((ChestPos){2,4,-2},&private_box)==CHEST_OK);
    inventory[1]=(ItemStack){DIAMOND_PICKAXE,1,42,enchant_code(ENCH_SILK_TOUCH,1),0};
    CHECK(item_set_custom(&inventory[1],6,"跨维度的镐") &&
        ender_chest_move(&private_box,DOUBLE_CHEST_SLOTS+1,26,0)==CHEST_OK);
    CHECK(world_select_dimension(NETHER) && set_block(0,120,-1,AIR) && set_block(0,119,-1,ENDER_CHEST));
    px=.5f; py=119.01f; pz=1.5f; player_rules.fall_origin=py;
    CHECK(!ender_chest_valid(&private_box) && ender_chest_open((ChestPos){0,119,-1},&private_box)==CHEST_OK);
    CHECK(!inventory[1].count && ender_chest_item(&private_box,26).damage==42 && save_write());
    save_leave();
    CHECK(save_load(65) && world_dimension()==NETHER && !ender_chest_valid(&private_box) &&
        ender_chest_open((ChestPos){0,119,-1},&private_box)==CHEST_OK);
    ItemStack private_pick=ender_chest_item(&private_box,26);
    CHECK(private_pick.type==DIAMOND_PICKAXE && private_pick.damage==42 &&
        enchant_level(private_pick.enchants,ENCH_SILK_TOUCH)==1 && item_prior_work(private_pick)==6 &&
        !strcmp(item_display_name(private_pick),"跨维度的镐"));
    CHECK(world_select_dimension(OVERWORLD) && block(2,4,-2)==ENDER_CHEST &&
        ender_chest_open((ChestPos){2,4,-2},&private_box)==CHEST_OK &&
        ender_chest_move(&private_box,26,DOUBLE_CHEST_SLOTS+1,0)==CHEST_OK && inventory[1].damage==42 &&
        !ender_chest_item(&private_box,26).count && save_write());
    save_leave();
    CHECK(save_create_options("Another world",CREATIVE,0,WORLD_SUPERFLAT,&bonus_seed,1,0));
    CHECK(set_block(2,4,-2,ENDER_CHEST) && !ender_chest_valid(&private_box) &&
        !ender_chest_items()[26].count);
    save_leave();
    CHECK(save_create_options("Legacy empty pockets",CREATIVE,0,WORLD_SUPERFLAT,&bonus_seed,1,0));
    save_leave();
    snprintf(path,sizeof path,"%s/%08x.vxl",dir,67);
    CHECK(write_legacy_fixture(path,74) && save_load(67) && !ender_chest_items()[26].count);
    save_leave();
    CHECK(save_create_options("Mounted outlets and effects",SURVIVAL,0,WORLD_SUPERFLAT,&bonus_seed,1,0));
    CHECK(world_select_dimension(NETHER));
    px=.5f; py=149.01f; pz=2.5f; player_rules.fall_origin=py;
    CHECK(actors_restore(NETHER,(ActorRecord){{2.5f,149.01f,5.5f},0,ACTOR_FAULTBACK,FAULTBACK_HEALTH,0}));
    BlockEdit heads[]={{2,149,-2,AIR_OUTLET_WALL+SOUTH},{3,149,-2,AIR_OUTLET_BASE+13}};
    CHECK(world_set_blocks(heads,2));
    player_effects.weakness=183;
    player_rules.health_debt=13;
    inventory[0]=(ItemStack){AIR_OUTLET_ITEM,3,0,0,0};
    CHECK(save_write());
    save_leave();
    CHECK(save_load(68) && save_current()->format==CURRENT_FORMAT && world_dimension()==NETHER &&
        player_effects.weakness==183 && player_rules.health_debt==13 &&
        inventory[0].type==AIR_OUTLET_ITEM && inventory[0].count==3 &&
        actors_in(NETHER)[0].type==ACTOR_FAULTBACK && actors_in(NETHER)[0].health==FAULTBACK_HEALTH &&
        world_cell(2,149,-2)==heads[0].cell && world_cell(3,149,-2)==heads[1].cell);
    save_leave();
    CHECK(save_create_options("Archived burst validation",SURVIVAL,0,WORLD_SUPERFLAT,&bonus_seed,1,0));
    CHECK(!blast_restore(OVERWORLD,(PrimedBlast){{2.f,12.f,3.f},{0,0,0},0,BLAST_ARCHIVE_BOSS_FIRST}) &&
        blast_restore(OVERWORLD,(PrimedBlast){{2.f,12.f,3.f},{0,0,0},80,BLAST_TNT}));
    CHECK(save_write());
    save_leave();
    size_t pending_count=0;
    CHECK(save_load(69) &&
        blasts_in(OVERWORLD,&pending_count)[0].kind==BLAST_TNT && pending_count==1);
    save_leave();
    CHECK(save_create_options("Pressurized station and pipe",SURVIVAL,0,WORLD_SUPERFLAT,&bonus_seed,1,0));
    BlockEdit stations[]={{0,17,0,AIR_STATION_BASE+4},{1,17,0,AIR_PIPE},{2,17,0,AIR_OUTLET_BASE},
        {0,16,0,GOLD_BLOCK_CELL},{1,16,0,DIAMOND_BLOCK_CELL}};
    CHECK(world_set_blocks(stations,5) && world_select_dimension(NETHER));
    px=.5f; py=149.01f; pz=2.5f; player_rules.fall_origin=py;
    inventory[4]=(ItemStack){AIR_STATION_ITEM,1,0,0,0};
    inventory[5]=(ItemStack){AIR_PIPE_ITEM,17,0,0,0};
    CHECK(save_write());
    save_leave();
    CHECK(save_load(70) && world_dimension()==NETHER &&
        inventory[4].type==AIR_STATION_ITEM && inventory[5].type==AIR_PIPE_ITEM && inventory[5].count==17);
    CHECK(world_recorded_cell(NETHER,0,17,0)==-1 && world_select_dimension(OVERWORLD) &&
        world_cell(0,17,0)==AIR_STATION_BASE+4 && block(1,17,0)==AIR_PIPE && block(2,17,0)==AIR_OUTLET &&
        block(0,16,0)==GOLD_BLOCK && block(1,16,0)==DIAMOND_BLOCK);
    save_leave();
    CHECK(save_create_options("Legacy v77",SURVIVAL,0,WORLD_SUPERFLAT,&bonus_seed,1,0));
    save_leave();
    snprintf(path,sizeof path,"%s/%08x.vxl",dir,71);
    CHECK(write_legacy_fixture(path,77) && save_load(71));
    save_leave();
    CHECK(save_create_seeded_generation("Archived timer and loot",SURVIVAL,GENERATOR_JOINT_COAST,1712));
    DungeonPlan room={0};
    for (int x=-10;!room.spawner && x<=10;++x) for (int z=-10;z<=10;++z)
        if (dungeon_plan_for(x*DUNGEON_REGION,z*DUNGEON_REGION,&room)) break;
    CHECK(room.spawner && save_current()->id==72 && world_generation()==GENERATOR_JOINT_COAST);
    if (room.spawner) {
        DungeonClock clock={room.x,room.floor+1,room.z,UINT32_C(0x89abcde0),157};
        ChestPos place={room.x+room.radius_x-1,clock.y,room.z+room.radius_z-1};
        ChestView view={OVERWORLD,{{0,0,0},{0,0,0}},0,0};
        CHECK(world_cell(clock.x,clock.y,clock.z)==room.spawner && dungeon_restore_clock(clock) &&
            !dungeon_restore_clock(clock) && chest_open(place,&view)==CHEST_OK);
        for (int i=0;i<CHEST_SLOTS;++i)
            for (int count=chest_slot(&view,i).count;count>0;--count) chest_automation_remove(&view,i);
        CHECK(set_block(5,220,6,COBWEB));
        inventory[0]=(ItemStack){COBWEB_ITEM,3,0,0,0};
        CHECK(game_set_difficulty(NORMAL));
        player_afflict_cave_spider_poison();
        Vec3 spider={room.x+.5f,clock.y+.01f,room.z+.5f};
        CHECK(actors_restore(OVERWORLD,(ActorRecord){spider,0,ACTOR_CAVE_SPIDER,12,0}));
        uint16_t name,horse_name;
        CHECK(item_name_intern("矿道蜘蛛",&name) && actors_set_name(OVERWORLD,0,name));
        HorseRecord abyss={.feet={3.5f,190.01f,-12.5f},.type=EQUINE_HORSE,.max_health=24,.health=24};
        CHECK(horse_restore(NETHER,abyss) && item_name_intern("驿马",&horse_name) && horse_set_name(NETHER,0,horse_name));

        TerrainKey key=TERRAIN_KEY(1712,GENERATOR_JOINT_COAST,OVERWORLD,WORLD_STANDARD,0);
        MineshaftPlan plan={0};
        CHECK(mineshaft_plan(key,-307,435,&plan) && !plan.rotated);
        RailCell track={plan.x+plan.right-4,plan.floor+1,plan.z};
        CHECK(mine_cart_track(world_cell(track.x,track.y,track.z)));
        Vec3 near_cart={track.x+.5f,track.y+.01f,track.z+1.5f};
        carts_activate_mineshaft(near_cart);
        CHECK(cart_on_track(track));
        int loot_slot=-1;
        for (int i=0;i<CART_STORAGE_SLOTS;++i) {
            const ItemStack *items=cart_cargo_in(OVERWORLD,0);
            if (items && items[i].count) { loot_slot=i; break; }
        }
        CHECK(loot_slot>=0);
        /* Take everything before saving. Loading must not regenerate loot. */
        for (int i=0;i<CART_STORAGE_SLOTS;++i)
            while (cart_container_extract(0,i)) {}
        CHECK(save_write());
        save_leave();
        CHECK(save_load(72) && save_current()->format==CURRENT_FORMAT &&
            dungeon_clocks()[0].random==clock.random && dungeon_clocks()[0].delay==157 &&
            world_cell(clock.x,clock.y,clock.z)==room.spawner && chest_open(place,&view)==CHEST_OK &&
            world_cell(5,220,6)==COBWEB && inventory[0].type==COBWEB_ITEM && inventory[0].count==3 &&
            player_effects.poison_slow && player_effects.poison==CAVE_POISON_NORMAL &&
            actors_in(OVERWORLD)[0].type==ACTOR_CAVE_SPIDER &&
            !strcmp(item_name_for_id(actors_name_id(OVERWORLD,0)),"矿道蜘蛛") &&
            !strcmp(item_name_for_id(horse_name_id(NETHER,0)),"驿马") && !horse_name_id(OVERWORLD,0));
        for (int i=0;i<CHEST_SLOTS;++i) CHECK(!chest_slot(&view,i).count);
        CartStorageView cart={OVERWORLD,0,0};
        CHECK(cart_on_track(track) && cart_open_storage(0,&cart) &&
            world_cell(track.x,track.y,track.z)==TRACK_BASE+STRAIGHT_EW);
        for (int i=0;i<CART_STORAGE_SLOTS;++i) CHECK(!cart_storage_item(&cart,i).count);
        cart_blast_hit(0);
        CHECK(!cart_on_track(track) && save_write());
        save_leave();
        CHECK(save_load(72) && world_cell(track.x,track.y,track.z)==TRACK_BASE+STRAIGHT_EW);
        carts_activate_mineshaft(near_cart);
        CHECK(!cart_on_track(track));
    }
    save_leave();
    CHECK(save_create_seeded("Windcut excavation",SURVIVAL,0,1712));
    WindcutPlan windcut={0};
    CHECK(windcut_plan_for(-4495,3775,&windcut) && windcut.x==-4495 && windcut.z==3775);
    if (windcut.y) {
        int top=windcut.y-7;
        const int west_x[]={-1,0,1,0},west_z[]={0,-1,0,1};
        const int x=windcut.x+west_x[windcut.rotation&3u],z=windcut.z+west_z[windcut.rotation&3u];
        CHECK(world_cell(x,top,z)==SLATE_ROCK && world_cell(x,top+1,z)==AIR);
        CHECK(set_block(x,top,z,AIR));
        CHECK(set_block(5,220,6,ORANGE_STAINED_CLAY));
        inventory[0]=(ItemStack){BLUE_CLAY_ITEM,3,0,0,0};
        CHECK(save_current()->id==73 && save_write());
        save_leave();
        CHECK(save_load(73) && world_generation()==GENERATOR_CURRENT &&
            world_cell(x,top,z)==AIR && world_cell(5,220,6)==ORANGE_STAINED_CLAY &&
            inventory[0].type==BLUE_CLAY_ITEM && inventory[0].count==3);
    }
    save_leave();
    CHECK(save_create_seeded("Unnamed entities before v80",SURVIVAL,0,1712));
    CHECK(save_current()->id==74 && actors_restore(OVERWORLD,(ActorRecord){{3,190,-9},0,ACTOR_COW,10,0}));
    HorseRecord unnamed={.feet={3,190,-9},.type=EQUINE_HORSE,.max_health=24,.health=24};
    CHECK(horse_restore(END,unnamed) && save_write());
    save_leave();
    snprintf(path,sizeof path,"%s/%08x.vxl",dir,74);
    CHECK(write_legacy_fixture(path,79) && save_load(74) &&
        actors_in(OVERWORLD)[0].type==ACTOR_COW && horses_in(END)[0].type==EQUINE_HORSE &&
        !actors_name_id(OVERWORLD,0) && !horse_name_id(END,0));
    save_leave();
    CHECK(save_create_seeded("Archived cavern resident",SURVIVAL,0,1712));
    CHECK(save_current()->id==75 && world_select_dimension(NETHER) && game_set_difficulty(NORMAL));
    px=2.5f; py=158.01f; pz=.5f; player_rules.fall_origin=py; player_rules.health=11;
    CHECK(set_block(2,157,0,NETHERRACK));
    inventory[0]=(ItemStack){POTION_REGEN_II,1,0,0,0};
    player_effects.regeneration=38; player_effects.regeneration_ii=1;
    uint16_t moth_name=0;
    CHECK(actors_restore(NETHER,(ActorRecord){{2.5f,159,-19.5f},.8f,ACTOR_LANTERN_MOTH,7,0}) &&
        item_name_intern("远处的引光蛾",&moth_name) && actors_set_name(NETHER,0,moth_name) &&
        !blast_restore(NETHER,(PrimedBlast){{2.5f,159.25f,-9.5f},{0,0,0},0,BLAST_ARCHIVE_VENT}) && save_write());
    save_leave();
    CHECK(save_load(75) && world_dimension()==NETHER && actors_in(NETHER)[0].type==ACTOR_LANTERN_MOTH &&
        actors_in(NETHER)[0].health==7 && !strcmp(item_name_for_id(actors_name_id(NETHER,0)),"远处的引光蛾") &&
        player_effects.regeneration==38 && player_effects.regeneration_ii && inventory[0].type==POTION_REGEN_II);
    size_t archived_blasts=0;
    blasts_in(NETHER,&archived_blasts);
    CHECK(!archived_blasts);
    save_leave();
    snprintf(path,sizeof path,"%s/%08x.vxl",dir,75);
    CHECK(write_legacy_fixture(path,81) && save_load(75) && player_effects.regeneration==38 &&
        actors_in(NETHER)[0].type==ACTOR_LANTERN_MOTH && !actors_mole_warm(NETHER,0));
    save_leave();
    CHECK(save_create_seeded("Kiln residents",SURVIVAL,0,1712));
    CHECK(save_current()->id==76 && game_set_difficulty(NORMAL) &&
        actors_restore(OVERWORLD,(ActorRecord){{.5f,190,8.5f},0,ACTOR_KILN_MOLE,8,0}) &&
        actors_restore(NETHER,(ActorRecord){{.5f,190,-11.5f},0,ACTOR_KILN_MOLE,12,0}) &&
        actors_restore(NETHER,(ActorRecord){{2.5f,190,-10.5f},0,ACTOR_KILN_MOLE,11,0}));
    uint16_t mole_name=0;
    CHECK(item_name_intern("北炉的访客",&mole_name) && actors_set_name(NETHER,1,mole_name));
    inventory[0]=(ItemStack){GOLD_NUGGET,8,0,0,0};
    inventory[1]=(ItemStack){GOLDEN_CARROT,1,0,0,0};
    CHECK(save_write()); save_leave();
    CHECK(save_load(76) && world_dimension()==OVERWORLD && actors_in(OVERWORLD)[0].type==ACTOR_KILN_MOLE &&
        actors_in(OVERWORLD)[0].health==8 && !actors_mole_warm(OVERWORLD,0) &&
        !actors_mole_warm(NETHER,0) && !actors_mole_warm(NETHER,1) &&
        !actors_in(NETHER)[1].traits && actors_in(NETHER)[1].health==11 &&
        actors_name_id(NETHER,1) && !strcmp(item_name_for_id(actors_name_id(NETHER,1)),"北炉的访客") &&
        inventory[0].type==GOLD_NUGGET && inventory[0].count==8 && inventory[1].type==GOLDEN_CARROT);
    save_leave();
    snprintf(path,sizeof path,"%s/%08x.vxl",dir,76);
    CHECK(write_legacy_fixture(path,82) && mark_archived_pack_member(path) &&
        save_load(76) && !player_effects.water_breathing &&
        !actors_in(NETHER)[1].traits && actors_in(NETHER)[1].type==ACTOR_KILN_MOLE &&
        actors_in(NETHER)[1].health==KILN_MOLE_HEALTH && actors_name_id(NETHER,1) &&
        !strcmp(item_name_for_id(actors_name_id(NETHER,1)),"北炉的访客"));
    save_leave();
    CHECK(save_create_seeded("Underwater with potion",SURVIVAL,0,1712));
    CHECK(save_current()->id==77);
    player_effects.water_breathing=5321;
    player_rules.air=9;
    inventory[0]=(ItemStack){SPLASH_WATER_BREATHING_LONG,1,0,0,0};
    CHECK(save_write()); save_leave();
    CHECK(save_load(77) && player_rules.air==9 && player_effects.water_breathing==5321 &&
        inventory[0].type==SPLASH_WATER_BREATHING_LONG);
    player_effects.water_breathing=WATER_BREATHING_LONG_TICKS+1;
    CHECK(!save_write());
    save_leave();
    snprintf(path,sizeof path,"%s/%08x.vxl",dir,77);
    CHECK(write_legacy_fixture(path,83) && save_load(77) && player_effects.water_breathing==5321 &&
        !player_effects.night_vision);
    save_leave();
    FILE *bad_breath=fopen(path,"r+b");
    CHECK(bad_breath!=NULL);
    if (bad_breath) {
        CHECK(!fseek(bad_breath,-2,SEEK_END) && fputc(0xff,bad_breath)!=EOF && fputc(0xff,bad_breath)!=EOF);
        CHECK(!fclose(bad_breath) && !save_load(77));
    }
    CHECK(save_create_seeded("Night vision",SURVIVAL,0,1713));
    CHECK(save_current()->id==78);
    player_effects.night_vision=7981;
    player_effects.water_breathing=54;
    inventory[0]=(ItemStack){SPLASH_NIGHT_VISION_LONG,1,0,0,0};
    CHECK(save_write()); save_leave();
    CHECK(save_load(78) && player_effects.night_vision==7981 && player_effects.water_breathing==54 &&
        inventory[0].type==SPLASH_NIGHT_VISION_LONG);
    player_effects.night_vision=NIGHT_VISION_LONG_TICKS+1;
    CHECK(!save_write());
    save_leave();
    snprintf(path,sizeof path,"%s/%08x.vxl",dir,78);
    FILE *bad_vision=fopen(path,"r+b");
    CHECK(bad_vision!=NULL);
    if (bad_vision) {
        CHECK(!fseek(bad_vision,-6-EMPTY_ITEM_FRAMES_BYTES-FIELD_SAVE_BYTES,SEEK_END) &&
            fputc(0xff,bad_vision)!=EOF && fputc(0xff,bad_vision)!=EOF);
        CHECK(!fclose(bad_vision) && !save_load(78));
    }
    CHECK(save_create_seeded("Moon phases",SURVIVAL,0,1713) && save_current()->id==79);
    player_rules.world_day=1365;
    player_rules.day_tick=19303;
    CHECK(save_write()); save_leave();
    CHECK(save_load(79) && player_rules.world_day==1365 && world_moon_phase()==5 && player_rules.day_tick==19303);
    save_leave();
    snprintf(path,sizeof path,"%s/%08x.vxl",dir,79);
    CHECK(write_legacy_fixture(path,84) && save_load(79) && player_rules.world_day==0 &&
        world_moon_phase()==0 && player_rules.day_tick==19303);
    save_leave();
    CHECK(save_create_seeded("沼泽睡莲",SURVIVAL,0,196) && save_current()->id==80 &&
        world_generation()==GENERATOR_CURRENT);
    CHECK(set_block(-17,190,0,WATER) && set_block(-17,191,0,LILY_PAD));
    inventory[3]=(ItemStack){LILY_PAD_ITEM,7,0,0,0};
    CHECK(save_write()); save_leave();
    CHECK(save_load(80) && world_generation()==GENERATOR_CURRENT &&
        world_cell(-17,190,0)==WATER && world_cell(-17,191,0)==LILY_PAD &&
        inventory[3].type==LILY_PAD_ITEM && inventory[3].count==7);
    save_leave();
    CHECK(save_create_options("梯井存档",SURVIVAL,0,WORLD_SUPERFLAT,&bonus_seed,1,0) && save_current()->id==81);
    CHECK(set_block(-17,160,0,STONE) && set_block(-17,161,0,STONE));
    CHECK(wall_attachment_place(-16,160,0,WEST,1,0) && wall_attachment_place(-16,161,0,WEST,0,1));
    CHECK(set_block(-16,161,1,REDSTONE_BLOCK));
    for (int n=0;n<6;++n) redstone_step(-16,0);
    const uint16_t powered_hatch=WOOD_TRAPDOOR+WEST+TRAPDOOR_TOP+TRAPDOOR_OPEN+TRAPDOOR_POWERED;
    CHECK(world_cell(-16,161,0)==powered_hatch);
    inventory[2]=(ItemStack){LADDER_ITEM,3,0,0,0};
    inventory[3]=(ItemStack){WOOD_TRAPDOOR_ITEM,2,0,0,0};
    CHECK(save_write()); save_leave();
    CHECK(save_load(81) && world_cell(-16,160,0)==LADDER+WEST && world_cell(-16,161,0)==powered_hatch &&
        inventory[2].type==LADDER_ITEM && inventory[2].count==3 &&
        inventory[3].type==WOOD_TRAPDOOR_ITEM && inventory[3].count==2);
    CHECK(set_block(-16,161,1,AIR));
    for (int n=0;n<6;++n) redstone_step(-16,0);
    CHECK(world_cell(-16,161,0)==WOOD_TRAPDOOR+WEST+TRAPDOOR_TOP);
    CHECK(set_block(-17,160,0,AIR) && world_cell(-16,160,0)==AIR);
    save_leave();
    CHECK(save_create_options("台阶与楼梯",SURVIVAL,0,WORLD_SUPERFLAT,&bonus_seed,1,0) && save_current()->id==82);
    const int lower=build_slab_raw(BUILD_STONE,BUILD_SLAB_BOTTOM),upper=build_slab_raw(BUILD_QUARTZ,BUILD_SLAB_TOP);
    const int double_wood=build_slab_raw(BUILD_ACACIA,BUILD_SLAB_DOUBLE),steps=build_stair_raw(BUILD_DARK_OAK,WEST,1);
    const BlockEdit builder[]={{-2,149,0,lower},{-1,149,0,upper},{0,149,0,double_wood},{1,149,0,steps}};
    (void)world_cell(-2,149,0);
    CHECK(world_set_blocks(builder,4));
    inventory[0]=(ItemStack){BUILD_SLAB_ITEMS+BUILD_ACACIA,17,0,0,0};
    inventory[1]=(ItemStack){(uint16_t)building_stair_item(BUILD_DARK_OAK),6,0,0,0};
    CHECK(save_write()); save_leave();
    CHECK(save_load(82) && world_cell(-2,149,0)==lower && world_cell(-1,149,0)==upper &&
        world_cell(0,149,0)==double_wood && world_cell(1,149,0)==steps &&
        inventory[0].type==BUILD_SLAB_ITEMS+BUILD_ACACIA && inventory[0].count==17 &&
        inventory[1].type==building_stair_item(BUILD_DARK_OAK) && inventory[1].count==6);
    save_leave();
    CHECK(save_create_with_generation("原有建筑布局",CREATIVE,GENERATOR_WETLANDS) && save_current()->id==83);
    CHECK(save_write()); save_leave();
    CHECK(save_load(83) && world_generation()==GENERATOR_WETLANDS);
    CHECK(save_write()); save_leave();
    CHECK(save_load(83) && world_generation()==GENERATOR_WETLANDS);
    save_leave();
    snprintf(path,sizeof path,"%s/%08x.vxl",dir,83);
    CHECK(write_legacy_fixture(path,85) && save_load(83) && world_generation()==GENERATOR_WETLANDS &&
          !item_frames_in(OVERWORLD)[0].present);
    save_leave();
    CHECK(save_create_seeded("展示框与地图",SURVIVAL,0,1712) && save_current()->id==84);
    unsigned char pixels[MAP_PIXELS]={0};
    pixels[7]=9;
    CHECK(map_restore(0,0,OVERWORLD,1,pixels));
    ItemStack framed={IRON_AXE,1,25,enchant_code(ENCH_UNBREAKING,2),0};
    CHECK(item_set_custom(&framed,2,"墙上工具"));
    CHECK(set_block(1,201,0,STONE) && set_block(2,201,0,STONE));
    CHECK(item_frame_restore(OVERWORLD,(ItemFrame){.x=1,.y=201,.z=0,.item=framed,.face=SOUTH,.rotation=3,.present=1}));
    CHECK(item_frame_restore(OVERWORLD,(ItemFrame){.x=2,.y=201,.z=0,.item={FILLED_MAP,1,0,0,1},.face=SOUTH,.rotation=1,.present=1}));
    CHECK(item_frame_restore(NETHER,(ItemFrame){.x=1,.y=201,.z=0,.item=framed,.face=WEST,.rotation=2,.present=1,.detached=1}));
    CHECK(save_write()); save_leave();
    CHECK(save_load(84));
    const ItemFrame *local=item_frames_in(OVERWORLD),*nether_frames=item_frames_in(NETHER);
    MapView mural;
    CHECK(local[0].x==1 && local[0].y==201 && local[0].face==SOUTH && local[0].rotation==3 &&
          local[0].item.damage==25 && local[0].item.enchants==framed.enchants &&
          !strcmp(item_override(local[0].item),"墙上工具"));
    CHECK(local[1].x==2 && local[1].rotation==1 && map_view(map_item_id(local[1].item),&mural) && mural.colors[7]==9);
    CHECK(nether_frames[0].face==WEST && nether_frames[0].detached && nether_frames[0].item.count==1);
    save_leave();
    CHECK(save_create_seeded("渗光陶砖",SURVIVAL,0,1712) && save_current()->id==85);
    CHECK(set_block(16,165,-14,SEEP_TILE_BRIGHT) &&
        actors_restore(OVERWORLD,(ActorRecord){{16.5f,165.f,-15.5f},.5f,ACTOR_SEEP_BEETLE,10,0}));
    inventory[2]=(ItemStack){FIREWORK_ROCKET,3,0,0,0};
    inventory[3]=(ItemStack){EGG_OCELOT,12,0,0,0};
    inventory[4]=(ItemStack){SLATE_ITEM,7,0,0,0};
    inventory[5]=(ItemStack){SURVEY_TAPPER,1,0,0,0};
    CHECK(set_block(3,165,3,LAMINATED_MASONRY));
    CHECK(firework_intern(&inventory[2],(FireworkSpec){3,2,{{112,17,FW_BURST,FW_TRAIL},{9,0,FW_STAR,FW_TWINKLE}}},NULL));
    CHECK(save_write()); save_leave();
    CHECK(save_load(85) && world_cell(16,165,-14)==SEEP_TILE_BRIGHT &&
        actors_in(OVERWORLD)[0].type==ACTOR_SEEP_BEETLE && actors_in(OVERWORLD)[0].health==10);
    const FireworkSpec *saved_firework=firework_spec(inventory[2]);
    CHECK(saved_firework && saved_firework->flight==3 && saved_firework->count==2 &&
        saved_firework->star[0].shape==FW_STAR && saved_firework->star[0].flags==FW_TWINKLE &&
        saved_firework->star[1].shape==FW_BURST && saved_firework->star[1].fade==17 &&
        inventory[3].type==EGG_OCELOT && inventory[3].count==12);
    CHECK(world_cell(3,165,3)==LAMINATED_MASONRY && inventory[4].type==SLATE_ITEM && inventory[4].count==7);
    CHECK(inventory[5].type==SURVEY_TAPPER && inventory[5].count==1 && !inventory[5].damage);
    save_leave();
    CHECK(save_create_seeded("含水带勘测",SURVIVAL,0,89223) && save_current()->id==86);
    CHECK(world_select_dimension(END) && inside_aquifer() && portals_restore_aquifer(AQUIFER_SAMPLE_BRINE));
    portals_set_end_return((EndReturn){1,player_world_spawn()});
    px=.5f; py=AQUIFER_FLOOR+1.01f; pz=-.5f;
    inventory[5]=(ItemStack){BRINE_CORE,1,0,0,0};
    CHECK(save_write());
    save_leave();
    CHECK(save_load(86) && inside_aquifer() && portals_aquifer_samples()==AQUIFER_SAMPLE_BRINE &&
        inventory[5].type==BRINE_CORE && block(0,AQUIFER_FLOOR+1,AQUIFER_DOCK_Z)==AQUIFER_DOCK &&
        portals_end_return().valid);
    save_leave();
    CHECK(save_create_with_generation("旧高热区 · 返程",SURVIVAL,GENERATOR_AQUIFER) && save_current()->id==87);
    CHECK(world_select_dimension(NETHER) && set_block(14,161,11,CLAY) && set_block(14,160,11,PLANKS));
    px=14.5f; py=161.01f; pz=11.5f;
    inventory[3]=(ItemStack){IRON_INGOT,2,0,0,0};
    CHECK(save_write());
    save_leave();
    snprintf(path,sizeof path,"%s/%08x.vxl",dir,87);
    CHECK(write_legacy_fixture(path,90));
    FILE *retired=fopen(path,"r+b");
    CHECK(retired!=NULL);
    if (retired) {
        const unsigned char record[]={1,14,0,0,0,161,11,0,0,0,CLAY};
        unsigned char buf[4096];
        size_t n=fread(buf,1,sizeof buf,retired);
        long index=-1;
        for (size_t i=0;i+sizeof record<=n;++i) if (!memcmp(buf+i,record,sizeof record)) index=(long)(i+sizeof record-1);
        CHECK(index>=0 && !fseek(retired,index,SEEK_SET) && fputc(NETHER_PORTAL_X,retired)!=EOF);
        CHECK(!fclose(retired));
    }
    CHECK(save_load(87) && world_dimension()==OVERWORLD && save_current()->returned_from_retired_site &&
        !collides(px,py,pz) && inventory[3].type==IRON_INGOT && inventory[3].count==2);
    CHECK(world_select_dimension(NETHER) && block(14,161,11)==AIR && block(14,160,11)==PLANKS);
    save_leave();
    CHECK(save_create_seeded("压力岩廊 · 双阀存档",SURVIVAL,0,89223) && save_current()->id==88);
    portals_set_end_return((EndReturn){1,player_world_spawn()});
    CHECK(world_select_dimension(NETHER) && inside_pressure_works() && portals_restore_aquifer(AQUIFER_SAMPLES));
    for (int side=-1;side<=1;side+=2) {
        const int x=side*PRESSURE_VALVE_X, y=PRESSURE_FLOOR+1, z=PRESSURE_VALVE_Z;
        CHECK(block(x,y,z)==PRESSURE_CLOSED && portals_use_aquifer((Hit){1,x,y,z,0,0,0}) &&
            world_cell(x,y,z)==PRESSURE_OPEN);
    }
    px=.5f; py=PRESSURE_FLOOR+1.01f; pz=1.5f;
    inventory[1]=(ItemStack){SINTERED_TILE_ITEM,2,0,0,0};
    CHECK(set_block(-1,PRESSURE_FLOOR+1,0,SINTERED_TILE) && save_write());
    save_leave();
    CHECK(save_load(88) && inside_pressure_works() && portals_pressure_relief()==3 &&
        inventory[1].type==SINTERED_TILE_ITEM && inventory[1].count==2 &&
        block(-1,PRESSURE_FLOOR+1,0)==SINTERED_TILE);
    CHECK(block(0,PRESSURE_FLOOR+1,PRESSURE_LIFT_Z)==PRESSURE_LIFT &&
        portals_use_aquifer((Hit){1,0,PRESSURE_FLOOR+1,PRESSURE_LIFT_Z,0,0,0}) &&
        portals_step(.05f) && inside_aquifer());
    save_leave();
    /* Mixed-width edits must cross multiple output batches without losing
       the final partial batch or spilling into the next dimension. */
    CHECK(save_create_seeded("批量施工存档",CREATIVE,0,89223) && save_current()->id==89);
    enum { BATCH_EDIT_COUNT=803, BATCH_X=-8100, BATCH_Y=237 };
    int batch_ok=1;
    for (int d=OVERWORLD;d<=NETHER && batch_ok;++d) {
        batch_ok=world_select_dimension((Dimension)d);
        for (int i=0;i<BATCH_EDIT_COUNT && batch_ok;++i) {
            int cell=i%3==0 ? SEEP_TILE_BRIGHT : i%3==1 ? AIR : PLANKS;
            batch_ok=world_restore_edit(BATCH_X+i/19,BATCH_Y+i%19,-7500+i%11,cell);
        }
    }
    CHECK(batch_ok && world_select_dimension(OVERWORLD) && save_write());
    save_leave();
    CHECK(save_load(89) && save_current()->format==CURRENT_FORMAT);
    for (int d=OVERWORLD;d<=NETHER;++d) {
        batch_ok=1;
        for (int i=0;i<BATCH_EDIT_COUNT && batch_ok;++i) {
            int cell=i%3==0 ? SEEP_TILE_BRIGHT : i%3==1 ? AIR : PLANKS;
            batch_ok=world_recorded_cell((Dimension)d,BATCH_X+i/19,BATCH_Y+i%19,-7500+i%11)==cell;
        }
        CHECK(batch_ok);
    }
    save_leave();
    CHECK(save_create_seeded("模床中间状态",SURVIVAL,0,56182) && save_current()->id==90);
    CHECK(set_block(0,220,-18,SETTLING_LIME) && set_block(1,220,-18,SETTLING_PACKED) &&
        set_block(2,220,-18,SETTLING_MOLD));
    inventory[0]=(ItemStack){SETTLING_MOLD_ITEM,1,0,0,0};
    CHECK(save_write());
    save_leave();
    CHECK(save_load(90) && world_recorded_cell(OVERWORLD,0,220,-18)==SETTLING_LIME &&
        world_recorded_cell(OVERWORLD,1,220,-18)==SETTLING_PACKED &&
        world_recorded_cell(OVERWORLD,2,220,-18)==SETTLING_MOLD &&
        inventory[0].type==SETTLING_MOLD_ITEM && inventory[0].count==1);
    save_leave();
    CHECK(save_create_seeded("Field supplies",SURVIVAL,0,56182) && save_current()->id==91);
    CHECK(field_restore((FieldSave){33,60,399}));
    inventory[0]=(ItemStack){FIELD_FLASK_TWO,1,0,0,0};
    CHECK(save_write());
    save_leave();
    CHECK(save_load(91) && field_record().water==33 && field_record().heat==60 && field_record().drain==399 &&
        inventory[0].type==FIELD_FLASK_TWO && inventory[0].count==1);
    save_leave();
    snprintf(path,sizeof path,"%s/%08x.vxl",dir,91);
    FILE *bad_water=fopen(path,"r+b");
    CHECK(bad_water!=NULL);
    if (bad_water) {
        CHECK(!fseek(bad_water,-FIELD_SAVE_BYTES,SEEK_END) && fputc(101,bad_water)!=EOF);
        CHECK(!fclose(bad_water) && !save_load(91));
    }
    CHECK(save_create_seeded("Field supplies v99 fallback",SURVIVAL,0,56182) && save_current()->id==92);
    save_leave();
    snprintf(path,sizeof path,"%s/%08x.vxl",dir,92);
    CHECK(write_legacy_fixture(path,99) && save_load(92) && field_record().water==FIELD_WATER_MAX &&
        field_record().heat==0 && field_record().drain==0 && world_generation()==GENERATOR_RAIN_GARDEN);
    save_leave();
    CHECK(save_create_seeded("Rain collector",SURVIVAL,0,56182) && save_current()->id==93);
    CHECK(set_block(-1,220,1,CISTERN_EMPTY) && set_block(-2,220,1,CISTERN_EMPTY+1) &&
        set_block(-3,220,1,CISTERN_EMPTY+2) && set_block(-4,220,1,CISTERN_FULL));
    inventory[0]=(ItemStack){CISTERN_ITEM,2,0,0,0};
    CHECK(save_write());
    save_leave();
    CHECK(save_load(93) && world_has_cisterns() &&
        inventory[0].type==CISTERN_ITEM && inventory[0].count==2);
    for (int i=0;i<=CISTERN_MAX;++i)
        CHECK(world_recorded_cell(OVERWORLD,-1-i,220,1)==CISTERN_EMPTY+i);
    save_leave();
    snprintf(path,sizeof path,"%s/%08x.vxl",dir,93);
    CHECK(write_legacy_fixture(path,100) && !save_load(93));
    CHECK(save_create_seeded("Tidal shore",SURVIVAL,0,91825) && save_current()->id==94);
    CHECK(actors_restore_with_care(OVERWORLD,(ActorRecord){{8.5f,76.f,11.5f},0,ACTOR_TIDE_CRAB,8,0},
        (ActorCare){.cooldown=171}));
    inventory[0]=(ItemStack){TIDE_GRIT,2,0,0,0};
    inventory[1]=(ItemStack){WARM_CORE,1,0,0,0};
    inventory[2]=(ItemStack){CERAMIC_CORE,3,0,0,0};
    CHECK(save_write());
    save_leave();
    CHECK(save_load(94) && actors_in(OVERWORLD)[0].type==ACTOR_TIDE_CRAB &&
        actors_care_in(OVERWORLD)[0].cooldown==171 && inventory[0].type==TIDE_GRIT && inventory[0].count==2 &&
        inventory[1].type==WARM_CORE && inventory[2].type==CERAMIC_CORE && inventory[2].count==3);
    save_leave();
    /* Large enough to cross the autosave stdio staging boundary. The
       worker must publish the old image, never the subsequently edited one. */
    CHECK(save_create_seeded("Async checkpoint ownership",SURVIVAL,0,56182) && save_current()->id==95);
    const int nx=272,nz=-363,checkpoint_y=161;
    CHECK(set_block(nx-2,checkpoint_y,nz,CISTERN_EMPTY));
    for (int x=nx;x<nx+160;++x) for (int z=nz;z<nz+152;++z)
        CHECK(world_restore_edit(x,checkpoint_y,z,((x^z)&1) ? BASALT_PAVING : STONE_BRICKS));
    const ItemStack old_sample={GRAVEL,13,0,0,0},new_sample={TIDE_GRIT,19,0,0,0};
    inventory[0]=old_sample;
    CHECK(save_autosave_begin()==SAVE_CHECKPOINT_PENDING);
    inventory[0]=new_sample;
    CHECK(world_restore_edit(nx,checkpoint_y,nz,AIR));
    CHECK(save_autosave_begin()==SAVE_CHECKPOINT_PENDING); /* Cannot replace staged bytes. */
    save_leave();
    CHECK(save_load(95) && inventory[0].type==GRAVEL && inventory[0].count==13 &&
        world_recorded_cell(OVERWORLD,nx,checkpoint_y,nz)==(((nx^nz)&1) ? BASALT_PAVING : STONE_BRICKS));
    CHECK(world_cell(nx-2,checkpoint_y,nz)==CISTERN_EMPTY &&
        world_set_cistern_level(nx-2,checkpoint_y,nz,CISTERN_EMPTY,CISTERN_FULL));
    CHECK(save_autosave_begin()==SAVE_CHECKPOINT_PENDING);
    inventory[0]=new_sample;
    CHECK(world_restore_edit(nx,checkpoint_y,nz,JOINT_BASALT) && save_write());
    save_leave();
    CHECK(save_load(95) && inventory[0].type==TIDE_GRIT && inventory[0].count==19 &&
        world_recorded_cell(OVERWORLD,nx,checkpoint_y,nz)==JOINT_BASALT &&
        world_recorded_cell(OVERWORLD,nx-2,checkpoint_y,nz)==CISTERN_FULL &&
        save_autosave_poll()==SAVE_CHECKPOINT_IDLE);
    save_leave();
    /* Make replacing the destination fail in this private fixture.
       Background errors must surface once and never overwrite the
       last good file or get reported as successful staging. */
    CHECK(save_create_seeded("Async checkpoint error",SURVIVAL,0,56182) && save_current()->id==96);
    snprintf(path,sizeof path,"%s/%08x.vxl",dir,96);
    char keep[sizeof path+6];
    snprintf(keep,sizeof keep,"%s.keep",path);
    CHECK(!rename(path,keep) && !mkdir(path,0700));
    CHECK(save_autosave_begin()==SAVE_CHECKPOINT_PENDING);
    SaveCheckpoint result=SAVE_CHECKPOINT_PENDING;
    const struct timespec short_wait={0,1000000};
    for (int lap=0;lap<5000 && result==SAVE_CHECKPOINT_PENDING;++lap) {
        result=save_autosave_poll();
        if (result==SAVE_CHECKPOINT_PENDING) (void)nanosleep(&short_wait,NULL);
    }
    CHECK(result==SAVE_CHECKPOINT_FAILED && save_autosave_poll()==SAVE_CHECKPOINT_IDLE);
    save_leave();
    CHECK(!rmdir(path) && !rename(keep,path) && save_load(96));
    save_leave();
    CHECK(save_create_seeded("Active pressure relay",SURVIVAL,0,56182) && save_current()->id==97);
    enum { RELAY_X=4, RELAY_Y=121, RELAY_Z=4 };
    const BlockEdit relay_parts[]={
        {RELAY_X-1,RELAY_Y,RELAY_Z,AIR_STATION_BASE+4},
        {RELAY_X,RELAY_Y,RELAY_Z,AIR_RELAY_BASE+EAST},
        {RELAY_X+1,RELAY_Y,RELAY_Z,LAMP_OFF},
    };
    CHECK(world_set_blocks(relay_parts,3));
    inventory[0]=(ItemStack){AIR_RELAY_ITEM,1,0,0,0};
    CHECK(redstone_press_air_relay(RELAY_X,RELAY_Y,RELAY_Z,NULL)==AIR_RELAY_STARTED);
    for (int i=0;i<6;++i) redstone_step(RELAY_X,RELAY_Z);
    CHECK(world_cell(RELAY_X+1,RELAY_Y,RELAY_Z)==LAMP_ON && save_write());
    save_leave();
    CHECK(save_load(97) && save_current()->format==CURRENT_FORMAT && inventory[0].type==AIR_RELAY_ITEM &&
        world_recorded_cell(OVERWORLD,RELAY_X,RELAY_Y,RELAY_Z)==AIR_RELAY_BASE+EAST &&
        world_recorded_cell(OVERWORLD,RELAY_X-1,RELAY_Y,RELAY_Z)==AIR_STATION_BASE+2);
    CHECK(redstone_timing_count(OVERWORLD)==1 && redstone_receives(RELAY_X+1,RELAY_Y,RELAY_Z));
    for (int i=0;i<AIR_RELAY_TICKS-7;++i) redstone_step(RELAY_X,RELAY_Z);
    CHECK(redstone_timing_count(OVERWORLD)==1 && redstone_receives(RELAY_X+1,RELAY_Y,RELAY_Z));
    for (int i=0;i<6;++i) redstone_step(RELAY_X,RELAY_Z);
    CHECK(!redstone_timing_count(OVERWORLD) && world_cell(RELAY_X+1,RELAY_Y,RELAY_Z)==LAMP_OFF);
    save_leave();
    snprintf(path,sizeof path,"%s/%08x.vxl",dir,97);
    CHECK(write_legacy_fixture(path,108) && !save_load(97));
    CHECK(save_create_seeded("Cured silt bed",SURVIVAL,0,91825) && save_current()->id==98);
    const BlockEdit fiber={16,200,-5,FIBROUS_CLAY};
    CHECK(world_set_blocks(&fiber,1) && save_write());
    save_leave();
    CHECK(save_load(98) && world_recorded_cell(OVERWORLD,16,200,-5)==FIBROUS_CLAY);
    save_leave();
    snprintf(path,sizeof path,"%s/%08x.vxl",dir,98);
    CHECK(write_legacy_fixture(path,109) && !save_load(98));
    CHECK(save_create_seeded_generation("Sampling gallery",SURVIVAL,GENERATOR_STRATA_GALLERIES,1712) &&
        save_current()->id==99);
    DungeonPlan new_gallery={0};
    for (int rx=-10;!new_gallery.entrances && rx<=10;++rx) for (int rz=-10;rz<=10;++rz)
        if (strata_gallery_plan_for(rx*DUNGEON_REGION,rz*DUNGEON_REGION,&new_gallery)) break;
    CHECK(new_gallery.entrances);
    if (new_gallery.entrances) {
        int x,y,z; strata_gallery_sample(new_gallery,0,&x,&y,&z);
        CHECK(world_cell(x,y,z)==STRATA_SAMPLE_SHALE && set_block(x,y,z,SLATE_ROCK));
        inventory[0]=(ItemStack){SHALE_SECTION,1,0,0,0};
        inventory[1]=(ItemStack){STRATA_GAUGE,1,0,0,0};
        CHECK(save_write());
        save_leave();
        CHECK(save_load(99) && world_generation()==GENERATOR_STRATA_GALLERIES &&
            world_cell(x,y,z)==SLATE_ROCK && inventory[0].type==SHALE_SECTION && inventory[1].type==STRATA_GAUGE);
    }
    save_leave();
    CHECK(save_create_seeded("Spillway fittings",SURVIVAL,0,1712) && save_current()->id==100);
    const BlockEdit sieve={11,210,-2,FALL_SIEVE};
    CHECK(set_block(sieve.x,sieve.y,sieve.z,sieve.cell));
    inventory[0]=(ItemStack){FALL_SIEVE_ITEM,2,0,0,0};
    inventory[1]=(ItemStack){TIDE_GRIT,2,0,0,0};
    CHECK(save_write());
    save_leave();
    CHECK(save_load(100) && world_generation()==GENERATOR_SPILLWAYS &&
          world_recorded_cell(OVERWORLD,sieve.x,sieve.y,sieve.z)==FALL_SIEVE &&
          inventory[0].type==FALL_SIEVE_ITEM && inventory[0].count==2 &&
          inventory[1].type==TIDE_GRIT && inventory[1].count==2);
    save_leave();
    CHECK(save_create_seeded("Suspended waterfall",SURVIVAL,0,1712) && save_current()->id==101);
    CHECK(world_stream_start());
    if (world_streaming_active()) world_set_interactive(0);
    CHECK(world_restore_edit(-6504,171,3208,WATER) && water_restore_pending(-6504,170,3208));
    world_set_interactive(1);
    water_step();
    CHECK(save_write());
    world_set_interactive(0);
    save_leave();
    CHECK(save_load(101));
    int saved_water=0,raw;
    CHECK(water_visit_dimension_pending(OVERWORLD,count_far_water,&saved_water) && saved_water==1);
    CHECK(!world_resident_cell(-6504,170,3208,&raw));
    (void)world_cell(-6504,170,3208);  /* Revisit, installing the edited chunk. */
    world_set_interactive(1);
    water_step();
    CHECK(world_resident_cell(-6504,170,3208,&raw) && water_strength(raw)==WATER_LEVELS);
    CHECK(world_resident_cell(-6504,169,3208,&raw) && raw==AIR);
    world_stream_stop();
    save_leave();
    CHECK(save_create_seeded("Rain probe modes",SURVIVAL,0,1712) && save_current()->id==102);
    const BlockEdit probes[]={{-17,130,0,CISTERN_EMPTY},{-16,130,0,CISTERN_PROBE_BASE+CISTERN_PROBE_EMPTY+EAST},
        {-15,130,0,LAMP_OFF},{-18,130,0,CISTERN_PROBE_BASE+WEST},{-19,130,0,LAMP_OFF}};
    CHECK(world_set_blocks(probes,5));
    inventory[0]=(ItemStack){CISTERN_PROBE_ITEM,1,0,0,0};
    for (int t=0;t<24;++t) redstone_step(-17,0);
    CHECK(world_cell(-15,130,0)==LAMP_ON && world_cell(-19,130,0)==LAMP_OFF && save_write());
    save_leave();
    CHECK(save_load(102) && inventory[0].type==CISTERN_PROBE_ITEM &&
        world_recorded_cell(OVERWORLD,-16,130,0)==CISTERN_PROBE_BASE+CISTERN_PROBE_EMPTY+EAST &&
        world_recorded_cell(OVERWORLD,-18,130,0)==CISTERN_PROBE_BASE+WEST);
    CHECK(world_cell(-17,130,0)==CISTERN_EMPTY &&
        world_set_cistern_level(-17,130,0,CISTERN_EMPTY,CISTERN_EMPTY+2));
    for (int t=0;t<24;++t) redstone_step(-17,0);
    CHECK(world_cell(-15,130,0)==LAMP_OFF && world_cell(-19,130,0)==LAMP_ON &&
        !redstone_timing_count(OVERWORLD));
    save_leave();
    for (int i = 1; i <= 102; i++) {
        snprintf(path, sizeof path, "%s/%08x.vxl", dir, i);
        CHECK(unlink(path) == 0);
    }
    CHECK(rmdir(dir) == 0);
    world_reset();
    furnaces_reset();
    chests_reset();
    brewing_reset();
    drops_reset();
    experience_reset();
    item_names_reset();
    printf("SAVE/BAG TEST seed/isolation/edits/water/player/stacks/furnace/corruption failures=%d\n", failures);
    return failures != 0;
}
