#define _POSIX_C_SOURCE 200809L
#include "save.h"
#include "../game/field_supplies.h"
#include "../world/pressure_layout.h"
#include "seed.h"
#include <inttypes.h>
#include "../game/game.h"
#include "../game/furnace.h"
#include "../game/brewing.h"
#include "../game/armor.h"
#include "../game/drops.h"
#include "../game/experience.h"
#include "../game/portals.h"
#include "../game/actors.h"
#include "../game/button_arrows.h"
#include "../game/hoppers.h"
#include "../game/droppers.h"
#include "../game/blast.h"
#include "../game/dungeon.h"
#include "../game/fire.h"
#include "../game/weather.h"
#include "../game/pistons.h"
#include "../game/boats.h"
#include "../game/minecarts.h"
#include "../game/horses.h"
#include "../game/chests.h"
#include "../game/ender_chest.h"
#include "../game/beds.h"
#include "../game/item_custom.h"
#include "../game/item_frames.h"
#include "../game/fireworks.h"
#include "../game/navigation.h"
#include "../game/gravity.h"
#include "../world/redstone.h"
#include "../world/lava.h"
#include "../game/villagers.h"
#include "../game/leaf_decay.h"
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* v1: header, player, 36 stacks, drops, sparse edits, pending water, end byte.
   v2 adds mode to the header and player rules before the stacks.
   v3 adds independent item IDs, stack counts and durability to inventory and
   drops; flowing-water cells move from 32..39 to a reserved byte range.
   v4 appends coordinate-keyed furnace state after terrain and flow records.
   v5 pins a per-world terrain generator version after the mode byte.
   v6 adds active dimension and a bounded section for each dimension.
   v7 appends the live creatures from each dimension after the End return point.
   v8 appends two words for the retired encounter.
   v9 appends dimension-local boats and the active rider.
   v10 reserves track shape bytes and appends dimension-local carts and rider.
   v11 appends horses, donkeys and mules, including breeding, saddle and rider.
   v12 appends the remaining poison and hunger ticks; active casts are transient.
   v13 adds sparse inventory records for nonempty chest halves to each dimension.
   v14 adds oriented bed cells, sheep fleece, and an Overworld bed anchor.
   v15 adds farmland moisture, crop stages and the skeleton actor.
   v16 adds cows and fixed-width care fields for the farm animals.
   v17 reserves fence and oriented gate cells, including an independent redstone latch.
   v18 adds slime size and per-entity links for animals and equines.
   v19 adds brewing stands, Nether crops, magma cubes and fire resistance.
   v20 adds player and mob armor; v21 appends drop clocks and death policy.
   v22 appends player experience and the three dimension orb banks.
   v23 stores optional enchantment bits on each item and adds cane/bookshelves.
   v24 extends the optional per-stack data with repair work and UTF-8 names.
   v25 stores support checks and in-flight falling blocks for each dimension.
   v26 defines raw redstone wire/lever cells; pending work is rebuilt on load.
   v27 stores repeater and torch pending transitions and burnout history.
   v28 adds lamps, v29 analogue outputs, and v30 pressure plates.
   v31 adds a two-byte edit record for wide IDs and embedded wooden-button arrows.
   v32 adds dimension-local hopper inventories and remaining transfer cooldowns.
   v33 adds dropper inventories, random selection state, and redstone scheduling.
   v34 shares the same inventory record with dispensers; raw cells identify their actions.
   v35 adds TNT, a now-retired explosive species and per-dimension blasts.
   v36 stores fire-cell scheduling and the player's remaining burn.
   v37 adds weather; v38 adds piston cells; v39 stores moving lava and its queue.
   v40 stores the persistent ledger for villagers and their opened trades.
   v41 permits paired wide door cells and their inventory items.
   v42 permits treasure generation and emerald ore/storage blocks.
   v53 permits trapped chest cells and the two related inventory IDs; viewing
   sessions are transient and add no saved fields.
   v54 permits oriented tripwire hooks, placed string, and hook countdowns
   in the existing redstone timer records.
   v55 adds optional generation 17 with jungle temples and their loot marker.
   Activated redstone and the consumed inventories use the existing records.
   v56 permits dragon eggs. The unused bit above the destroyed crystal mask
   records whether the one-time trophy still needs to be placed; falling
   blocks store a two-byte cell ID for the egg.
   v57 stores the post-dragon ending state in the next two bits of the End mask.
   v58 reserves the two next bits of the world mode for Hardcore and its death.
   v59 reserves the high bit of zombie fractional health for door breaking.
   v60 adds the fungi generator and the retired resident state layout,
   and an optional player Weakness timer flagged in the health-debt byte.
   v61 stores new fungal cells, foods, biome generation and the retired bovine variant.
   v62 can store a shared-map registry before the first inventory stack.
   v63 adds an optional explicit 64-bit seed. Its low word occupies the
   historic slot; the flagged high word follows the dimension byte.
   v64 can store a nondefault world profile after the seed extension.
   v65 uses two mode bits for the structures and initial bonus chest settings.
   v66 adds the five axe item IDs; all storage uses the same stack encoding.
   v73 records whether each cart is off its rails and its free velocity if so.
   v74 persists TNT cart fall distance and accepts a landing-power equivalent
   in both the pending cart and deferred explosion records.
   v75 appends the local player's shared Ender Chest inventory after world data.
   v76 adds actor slot 21, decorative block orientations and a retired damage effect.
   v77 adds Wither II encoding and appends boss and projectile banks.
   v78 keeps beacon settings in edited cells and appends active player buffs.
   v79 enables dungeon/mineshaft terrain, spawner timers and low-strength poison.
   All integers and IEEE-754 floats are written little-endian, never raw structs. */
static const char magic[] = {'V','X','W','O','R','L','D'};
enum { LEGACY_BLOCKS = 18, LEGACY_FLOW_BASE = 32, WIDE_EDIT_TAG=0x81 };
enum { HEADER_HARDCORE=2, HEADER_HARDCORE_DEAD=4, HEADER_MAPS=8, HEADER_EXPLICIT_SEED=16,
    HEADER_WORLD_PROFILE=32, HEADER_NO_STRUCTURES=64, HEADER_BONUS_CHEST=128, STACK_MAP=8 };
enum { ZOMBIE_DOOR_FLAG=128, PLAYER_WEAKNESS_FLAG=128, PLAYER_RETIRED_EFFECT_FLAG=64,
       RETIRED_EFFECT_TIER_FLAG=0x8000, POISON_SLOW_FLAG=0x8000 };
_Static_assert((int)HEALTH_UNITS<=(int)PLAYER_RETIRED_EFFECT_FLAG,"Health debt must not overlap optional effect flags");
_Static_assert(HEALTH_UNITS<128,"Actor health debt must not use the door capability bit");
static char folder[768];
static const char *error_text;
static SaveInfo active;
static int advanced_lighting = 1;
static SaveCheckpoint finish_autosave(int wait);
static int drain_autosave(void);
static void clear_edit_memo(void);

static int fail(const char *message) {
    error_text = message;
    return 0;
}

const char *save_error(void) { return error_text ? error_text : "存档操作失败"; }
const SaveInfo *save_current(void) { return active.id ? &active : NULL; }
void save_leave(void) {
    if (!drain_autosave()) return;
    active.id=0;
    clear_edit_memo();
}

static void preference_path(char out[800],const char *name) { snprintf(out,800,"%s/%s",folder,name); }

static int preference_read(const char *name) {
    char path[800];
    preference_path(path,name);
    FILE *fp=fopen(path,"rb");
    if (!fp) return -1;
    int value=fgetc(fp)-'0';
    if (fgetc(fp)!=EOF || ferror(fp)) value=-1;
    fclose(fp);
    return value;
}

static int preference_write(const char *name,int value) {
    char path[800],temp[824];
    preference_path(path,name);
    snprintf(temp,sizeof temp,"%s.tmp.XXXXXX",path);
    int fd=mkstemp(temp);
    if (fd<0) return 0;
    const char encoded=(char)('0'+value);
    int ok=write(fd,&encoded,1)==1 && fsync(fd)==0;
    if (close(fd)!=0) ok=0;
    if (ok) ok=rename(temp,path)==0;
    if (!ok) unlink(temp);
    return ok;
}

int save_open_directory(const char *directory) {
    if (!directory || !*directory || strlen(directory) >= sizeof folder)
        return fail("存档路径无效");
    if (!drain_autosave()) return 0;
    if (mkdir(directory, 0700) != 0 && errno != EEXIST) {
        fprintf(stderr, "mkdir %s: %s\n", directory, strerror(errno));
        return fail("无法创建存档目录");
    }
    DIR *dir = opendir(directory);
    if (!dir) {
        fprintf(stderr, "opendir %s: %s\n", directory, strerror(errno));
        return fail("无法打开存档目录");
    }
    closedir(dir);
    strcpy(folder, directory);
    int selected=preference_read(".difficulty");
    game_set_difficulty(game_difficulty_valid(selected) ? (GameDifficulty)selected : NORMAL);
    advanced_lighting=preference_read(".graphics")!=0; // Missing/corrupt settings default to on.
    return 1;
}

int save_set_difficulty(GameDifficulty value) {
    if (!*folder || !game_difficulty_valid(value)) return fail("无效的难度设置");
    if (active.id && active.hardcore) return fail("极限世界的难度锁定为困难");
    if (value==game_selected_difficulty()) return 1;
    if (!preference_write(".difficulty",value)) return fail("无法保存难度，设置未更改");
    game_set_difficulty(value);
    if (active.id) actors_apply_difficulty();
    return 1;
}

int save_advanced_lighting(void) { return advanced_lighting; }

int save_set_advanced_lighting(int enabled) {
    if (!*folder || (enabled!=0 && enabled!=1)) return fail("无效的光影设置");
    if (enabled==advanced_lighting) return 1;
    if (!preference_write(".graphics",enabled)) return fail("无法保存光影设置，设置未更改");
    advanced_lighting=enabled;
    return 1;
}

static void save_path(char out[800], uint32_t id) {
    snprintf(out, 800, "%s/%08lx.vxl", folder, (unsigned long)id);
}

static uint32_t seed_for(uint32_t id) {
    /* The legacy automatic assignment stays stable; explicitly seeded
       worlds are allowed to share terrain, not their ID or saved state. */
    return (id * UINT32_C(0x9E3779B1)) ^ UINT32_C(0xA511E9B3);
}

static int put_u32(FILE *fp, uint32_t n) {
    unsigned char b[4] = {(unsigned char)n, (unsigned char)(n >> 8), (unsigned char)(n >> 16), (unsigned char)(n >> 24)};
    return fwrite(b, 1, sizeof b, fp) == sizeof b;
}
static int get_u32(FILE *fp, uint32_t *n) {
    unsigned char b[4];
    if (fread(b, 1, sizeof b, fp) != sizeof b) return 0;
    *n = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return 1;
}
static int put_short(FILE *fp,uint16_t n) {
    return fputc(n&255,fp)!=EOF && fputc(n>>8,fp)!=EOF;
}
static int get_short(FILE *fp,uint16_t *n) {
    int a=fgetc(fp),b=fgetc(fp);
    if (a<0 || b<0) return 0;
    *n=(uint16_t)(a|(b<<8));
    return 1;
}
static int put_stack(FILE *fp, ItemStack item) {
    if (!item_stack_valid(item)) return 0;
    const char *name=item_override(item);
    size_t length=strlen(name);
    unsigned work=item_prior_work(item);
    unsigned id=map_item_id(item);
    const FireworkSpec *pyro=firework_spec(item);
    unsigned damage=pyro ? 0 : item.damage;
    unsigned flags=(item.enchants ? 1u : 0u)|(work ? 2u : 0u)|(length ? 4u : 0u)|(id ? STACK_MAP : 0u);
    unsigned char bytes[] = {
        (unsigned char)item.type, (unsigned char)(item.type >> 8), (unsigned char)(item.count|(flags ? 128u : 0u)),
        (unsigned char)damage, (unsigned char)(damage >> 8)
    };
    if (!(fwrite(bytes,1,sizeof bytes,fp)==sizeof bytes && (!flags ||
        (fputc((int)flags,fp)!=EOF &&
         (!id || fputc((int)id,fp)!=EOF) &&
         (!(flags&1) || (put_u32(fp,(uint32_t)item.enchants) && put_u32(fp,(uint32_t)(item.enchants>>32)))) &&
         (!(flags&2) || put_short(fp,(uint16_t)work)) &&
         (!(flags&4) || (fputc((int)length,fp)!=EOF && fwrite(name,1,length,fp)==length)))))) return 0;
    /* A stack carries its own payload. Interning IDs are never serialized. */
    if (pyro) {
        if (fputc(pyro->flight,fp)==EOF || fputc(pyro->count,fp)==EOF) return 0;
        for (unsigned i=0;i<pyro->count;++i) {
            FireworkStar s=pyro->star[i];
            if (!put_short(fp,s.colors) || !put_short(fp,s.fade) || fputc(s.shape*4+s.flags,fp)==EOF) return 0;
        }
    }
    return 1;
}
static int get_stack(FILE *fp, unsigned format, int drop, ItemStack *out) {
    ItemStack item = {0, 0, 0,0,0};
    if (format < 3) {
        int id = fgetc(fp), count = drop ? 1 : fgetc(fp);
        if (id < 0 || id >= LEGACY_BLOCKS || count < 0 || count > STACK_MAX) return 0;
        item.type = (uint16_t)id;
        item.count = (uint8_t)count;
    } else {
        unsigned char bytes[5];
        if (fread(bytes, 1, sizeof bytes, fp) != sizeof bytes) return 0;
        item.type = (uint16_t)(bytes[0] | (bytes[1] << 8));
        if (item.type==ARCHIVE_PALE_BLOCK || item.type==ARCHIVE_FRAME || item.type==ARCHIVE_BEARING_ITEM) {
            if (format<6) return 0;
            item.type=item.type==ARCHIVE_PALE_BLOCK ? STONE : item.type==ARCHIVE_FRAME ? STONE_BRICKS : QUARTZ;
        } else if (item.type==ARCHIVE_REWARD_ITEM) {
            if (format<56) return 0;
            /* Keep count and any name, but restore a supported material. */
            item.type=GRAVEL;
        }
        item.count = bytes[2];
        item.damage = (uint16_t)(bytes[3] | (bytes[4] << 8));
        if (format>=23 && (item.count&128u)) {
            item.count&=127u;
            int flags=format==23 ? 1 : fgetc(fp);
            if (flags<1 || flags>(format>=62 ? 15 : 7) || !item.count || !item_def(item.type) ||
                !!(flags&STACK_MAP)!=(item.type==FILLED_MAP)) return 0;
            if (flags&STACK_MAP) {
                int id=fgetc(fp);
                if (id<1 || !map_view((unsigned)id,NULL)) return 0;
                item.custom=(unsigned)id;
            }
            if (flags&1) {
                uint32_t lo,hi;
                if (!get_u32(fp,&lo) || !get_u32(fp,&hi) || !(lo|hi)) return 0;
                item.enchants=(uint64_t)lo|((uint64_t)hi<<32);
            }
            uint16_t work=0;
            if ((flags&2) && (!get_short(fp,&work) || !work)) return 0;
            char name[ITEM_NAME_BYTES+1],normalized[ITEM_NAME_BYTES+1];
            if (flags&4) {
                int n=fgetc(fp);
                if (n<1 || n>ITEM_NAME_BYTES || fread(name,1,(size_t)n,fp)!=(size_t)n ||
                    memchr(name,0,(size_t)n)) return 0;
                name[n]=0;
                if (!item_name_normalize(name,normalized) || strcmp(name,normalized)) return 0;
            }
            if ((flags&6) && !item_set_custom(&item,work,flags&4 ? name : NULL)) return 0;
        }
    }
    if (format<88 && (item.type==FIRE_CHARGE || firework_item(item.type) || spawn_egg_item(item.type))) return 0;
    if (format<97 && item.type==EGG_FAULTBACK) return 0;
    if (format<100 && field_flask_item(item.type)) return 0;
    if (format<101 && item.type==CISTERN_ITEM) return 0;
    if (format<104 && item.type==HYDRAULIC_SORTER_ITEM) return 0;
    if (format<105 && item.type==HEAT_BENCH_ITEM) return 0;
    if (format<109 && item.type==AIR_RELAY_ITEM) return 0;
    if (format<111 && (item.type==SHALE_SECTION || item.type==LIME_SECTION || item.type==STRATA_GAUGE)) return 0;
    if (format<112 && item.type==FALL_SIEVE_ITEM) return 0;
    if (format<114 && item.type==CISTERN_PROBE_ITEM) return 0;
    if (format<108 && (item.type==CERAMIC_CORE || item.type==WARM_CORE)) return 0;
    if (format<107 && item.type==TIDE_GRIT) return 0;
    if (format<106 && (item.type==JOINT_BASALT_ITEM || item.type==BASALT_PAVING_ITEM)) return 0;
    if (format<89 && geology_item(item.type)) return 0;
    if (format<90 && item.type==SURVEY_TAPPER) return 0;
    if (aquifer_item(item.type) && !uses_aquifer()) return 0;
    if (item.type==SINTERED_TILE_ITEM && (format<91 || world_generation()<GENERATOR_PRESSURE_WORKS)) return 0;
    if ((item.type==SHELL_FLAKE || item.type==RESONANCE_MARKER_ITEM) && format<92) return 0;
    if (item.type==TIDE_GUIDE && format<93) return 0;
    if (item.type==AIR_PIPE_ITEM && format<94) return 0;
    if (item.type>=SEEP_BED_ITEM && item.type<=SEEP_BRIGHT_ITEM && format<95) return 0;
    if (firework_item(item.type)) {
        int flight=fgetc(fp),count=fgetc(fp);
        if (item.damage || flight<0 || flight>3 || count<0 || count>FW_MAX_STARS) return 0;
        FireworkSpec pyro={(uint8_t)flight,(uint8_t)count,{{0}}};
        for (int i=0;i<count;++i) {
            FireworkStar *s=&pyro.star[i];
            int shape_flags;
            if (!get_short(fp,&s->colors) || !get_short(fp,&s->fade) || (shape_flags=fgetc(fp))<0 ||
                shape_flags>=FW_SHAPES*4) return 0;
            s->shape=(uint8_t)(shape_flags>>2);
            s->flags=(uint8_t)(shape_flags&3);
        }
        if (!firework_intern(&item,pyro,NULL)) return 0;
    }
    if (!item_stack_valid(item) || (drop && !item.count) || (format<86 && item.type==ITEM_FRAME_ITEM) ||
        (format<78 && (item.type==AIR_STATION_ITEM || item.type==GOLD_BLOCK_ITEM || item.type==DIAMOND_BLOCK_ITEM)) ||
        (format<77 && item.type==PUMP_GASKET) ||
        (format<76 && item.type==AIR_OUTLET_ITEM) ||
        (format<75 && item.type==ENDER_CHEST_ITEM) ||
        (format<72 && item.type==FURNACE_MINECART) ||
        (format<71 && item.type==HOPPER_MINECART) ||
        (format<70 && item.type==CHEST_MINECART) ||
        (format<69 && (item.type==ACTIVATOR_RAIL_ITEM || item.type==TNT_MINECART)) ||
        (format<68 && item.type==DETECTOR_RAIL_ITEM) || (format<66 && is_axe(item.type)) ||
        (format<60 && item.type>=SUGAR) ||
        (format<61 && item.type>=RED_MUSHROOM_ITEM) || (format<62 && item.type>=COMPASS_ITEM) ||
        (format<12 && item.type>=FISHING_ROD) ||
        (format<14 && (item.type==WOOL || item.type==BED || item.type==SHEARS)) ||
        (format<15 && (item.type==TORCH || item.type>=WOOD_HOE)) ||
        (format<16 && item.type>=RAW_BEEF) || (format<17 && barrier_type(item.type)) ||
        (format<18 && item.type>=EXTENDED_ITEM_BASE) ||
        (format<19 && (brewing_block(item.type) || item.type>=MAGMA_CREAM)) ||
        (format<20 && armor_slot(item.type)>=0) ||
        (format<23 && (book_block(item.type) || item.type>=PAPER)) ||
        (format<24 && (item.type==IRON_BLOCK || item.type==ANVIL)) ||
        (format<26 && item.type==LEVER_ITEM) ||
        (format<27 && (item.type==REDSTONE_TORCH_ITEM || item.type==REPEATER_ITEM)) ||
        (format<28 && item.type==REDSTONE_LAMP_ITEM) || (format<29 && item.type==COMPARATOR_ITEM) ||
        (format<30 && item.type>=STONE_PLATE_ITEM && item.type<=IRON_PLATE_ITEM) ||
        (format<31 && (item.type==STONE_BUTTON_ITEM || item.type==WOOD_BUTTON_ITEM)) ||
        (format<32 && item.type==HOPPER_ITEM) || (format<33 && item.type==DROPPER_ITEM) ||
        (format<34 && item.type==DISPENSER_ITEM) ||
        (format<35 && (item.type==TNT_ITEM || item.type==GUNPOWDER)) ||
        (format<38 && (item.type==PISTON_ITEM || item.type==STICKY_PISTON_ITEM)) ||
        (format<41 && (item.type==WOOD_DOOR_ITEM || item.type==IRON_DOOR_ITEM)) ||
        (format<42 && (item.type==EMERALD_ORE_ITEM || item.type==EMERALD_BLOCK_ITEM)) ||
        (format<43 && horse_armor_points(item.type)) ||
        (format<44 && (sapling_item(item.type) || typed_leaf_item(item.type))) ||
        (format<46 && item.type>=PUMPKIN_ITEM) || (format<47 && item.type>=SNOWBALL) ||
        (format<50 && item.type>=INK_SAC) || (format<51 && item.type>=RED_GLASS_ITEM) ||
        (format<52 && item.type>=GLASS_PANE_ITEM) || (format<53 && item.type>=TRIPWIRE_HOOK_ITEM)) return 0;
    if (item.type==EGG_ARCHIVE_MAGMA) item.type=EGG_REEDLIZARD;
    *out = item;
    return 1;
}
static int put_float(FILE *fp, float x) {
    uint32_t u;
    memcpy(&u, &x, sizeof u);
    return put_u32(fp, u);
}
static int get_float(FILE *fp, float *x, float lo, float hi) {
    uint32_t u;
    /* The file stores IEEE binary32. Reject non-finite bit patterns before
       constructing a float: -Ofast can assume isfinite(x) is always true. */
    if (!get_u32(fp, &u) || (u & UINT32_C(0x7f800000)) == UINT32_C(0x7f800000)) return 0;
    memcpy(x, &u, sizeof u);
    return *x >= lo && *x <= hi;
}
static int signed_coord(uint32_t n, int *v) {
    int64_t x = n;
    if (n > INT32_MAX) x -= INT64_C(4294967296);
    if (x < -WORLD_SAVE_LIMIT || x > WORLD_SAVE_LIMIT) return 0;
    *v = (int)x;
    return 1;
}
static int valid_name(const char *name, size_t len) {
    if (!len || len > SAVE_NAME_MAX) return 0;
    for (size_t i = 0; i < len; i++)
        if ((unsigned char)name[i] < 32 || (unsigned char)name[i] == 127) return 0;
    return 1;
}
static int read_header(FILE *fp, uint32_t id, SaveInfo *info) {
    char got[sizeof magic];
    uint32_t disk_id, low, high=0;
    if (fread(got, 1, sizeof got, fp) != sizeof got || memcmp(got, magic, sizeof got) ||
        (info->format = (unsigned)fgetc(fp)) < 1 || info->format > CURRENT_FORMAT ||
        !get_u32(fp, &disk_id) || disk_id != id || !get_u32(fp, &low)) return 0;
    int len = fgetc(fp);
    if (len < 1 || len > SAVE_NAME_MAX || fread(info->name, 1, len, fp) != (size_t)len ||
        !valid_name(info->name, len)) return 0;
    info->name[len] = 0;
    int mode = info->format == 1 ? SURVIVAL : fgetc(fp);
    int has_profile=0;
    if (mode>=0 && info->format>=65) {
        info->structures_disabled=!!(mode&HEADER_NO_STRUCTURES);
        info->bonus_chest=!!(mode&HEADER_BONUS_CHEST);
        mode&=~(HEADER_NO_STRUCTURES|HEADER_BONUS_CHEST);
    }
    if (mode>=0 && info->format>=64) {
        has_profile=!!(mode&HEADER_WORLD_PROFILE);
        mode&=~HEADER_WORLD_PROFILE;
    }
    if (mode>=0 && info->format>=63) {
        info->has_explicit_seed=!!(mode&HEADER_EXPLICIT_SEED);
        mode&=~HEADER_EXPLICIT_SEED;
    }
    if (!info->has_explicit_seed && low!=seed_for(id)) return 0;
    if (mode>=0 && info->format>=62) {
        info->has_maps=!!(mode&HEADER_MAPS);
        mode&=~HEADER_MAPS;
    }
    if (info->format>=58 && (mode==HEADER_HARDCORE || mode==(HEADER_HARDCORE|HEADER_HARDCORE_DEAD))) {
        info->hardcore=1;
        info->hardcore_dead=!!(mode&HEADER_HARDCORE_DEAD);
        mode=SURVIVAL;
    }
    if (mode != SURVIVAL && mode != CREATIVE) return 0;
    if (info->bonus_chest && info->hardcore) return 0;
    info->mode = (GameMode)mode;
    int generation = info->format < 5 ? GENERATOR_LEGACY : fgetc(fp);
    if (!generation_valid(generation) || (info->format < 6 && generation > GENERATOR_CLIMATE) ||
        (info->format<11 && generation>=GENERATOR_EQUINE) ||
        (info->format<15 && generation>=GENERATOR_FARM) || (info->format<19 && generation>=GENERATOR_FILTER_ARCHIVE) ||
        (info->format<23 && generation>=GENERATOR_BOOKS) || (info->format<39 && generation>=GENERATOR_VILLAGES) ||
        (info->format<41 && generation>=GENERATOR_DOORS) || (info->format<42 && generation>=GENERATOR_TREASURE) ||
        (info->format<43 && generation>=GENERATOR_HORSE_ARMOR) ||
        (info->format<44 && generation>=GENERATOR_FORESTRY) ||
        (info->format<46 && generation>=GENERATOR_GOLEMS) ||
        (info->format<51 && generation>=GENERATOR_DYES) ||
        (info->format<55 && generation>=GENERATOR_TEMPLES) ||
        (info->format<60 && generation>=GENERATOR_FUNGI) ||
        (info->format<61 && generation>=GENERATOR_MUSHROOM_ISLAND) ||
        (info->format<79 && generation>=GENERATOR_DUNGEONS) ||
        (info->format<91 && generation>=GENERATOR_PRESSURE_WORKS) ||
        (info->format<92 && generation>=GENERATOR_SHELL_HABITATS) ||
        (info->format<98 && generation>=GENERATOR_WINDCUT) ||
        (info->format<99 && generation>=GENERATOR_RAIN_GARDEN) ||
        (info->format<106 && generation>=GENERATOR_JOINT_COAST) ||
        (info->format<111 && generation>=GENERATOR_STRATA_GALLERIES) ||
        (info->format<112 && generation>=GENERATOR_SPILLWAYS)) return 0;
    info->generation = generation;
    int dimension = info->format < 6 ? OVERWORLD : fgetc(fp);
    if ((unsigned)dimension >= DIMENSION_COUNT) return 0;
    if (info->has_explicit_seed && !get_u32(fp,&high)) return 0;
    if (has_profile) {
        int profile=fgetc(fp);
        if (!world_profile_valid(profile) || profile==WORLD_STANDARD ||
            generation<GENERATOR_MUSHROOM_ISLAND) return 0;
        info->profile=(WorldProfile)profile;
    }
    if (info->format>=67 && info->profile==WORLD_SUPERFLAT) {
        FlatLayout *flat=&info->flat;
        if (!get_short(fp,&flat->height) || flat->height>FLAT_MAX_HEIGHT) return 0;
        if (flat->height) {
            int biome=fgetc(fp),features=fgetc(fp);
            if (biome<0 || features<0 || fread(flat->blocks,1,flat->height,fp)!=flat->height) return 0;
            for (int i=0;i<flat->height;++i) if (flat->blocks[i]==ARCHIVE_PALE_BLOCK) flat->blocks[i]=STONE;
            flat->biome=(uint8_t)biome; flat->features=(uint8_t)features;
            if (!flat_valid(flat)) return 0;
        }
    }
    info->dimension = (Dimension)dimension;
    info->id = id;
    info->seed = ((uint64_t)high<<32)|low;
    info->readable = 1;
    return 1;
}

static uint32_t file_id(const char *name) {
    if (strlen(name) != 12 || strcmp(name + 8, ".vxl")) return 0;
    uint32_t id = 0;
    for (int i = 0; i < 8; i++) {
        int c = name[i];
        if (c >= '0' && c <= '9') c -= '0';
        else if (c >= 'a' && c <= 'f') c -= 'a' - 10;
        else return 0;
        id = (id << 4) | (uint32_t)c;
    }
    return id;
}
static int newest_first(const void *a, const void *b) {
    uint32_t x = ((const SaveInfo *)a)->id, y = ((const SaveInfo *)b)->id;
    return x < y ? 1 : x > y ? -1 : 0;
}
int save_list(SaveInfo **out, size_t *count) {
    *out = NULL;
    *count = 0;
    if (!*folder) return 0;
    DIR *dir = opendir(folder);
    if (!dir) return fail("无法读取存档目录");
    size_t n = 0, capacity = 0;
    SaveInfo *items = NULL;
    int ok = 1;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) { ok = errno == 0; break; }
        uint32_t id = file_id(entry->d_name);
        if (!id) continue;
        if (n == capacity) {
            size_t next = capacity ? capacity * 2 : 16;
            if (next < capacity || next > SIZE_MAX / sizeof *items) { ok = 0; break; }
            SaveInfo *p = (SaveInfo *)realloc(items, next * sizeof *items);
            if (!p) { ok = 0; break; }
            items = p;
            capacity = next;
        }
        SaveInfo info = {0};
        char path[800];
        save_path(path, id);
        FILE *fp = fopen(path, "rb");
        if (fp) { read_header(fp, id, &info); fclose(fp); }
        if (!info.readable) {
            info.id = id; /* Keep even corrupt IDs reserved. */
            snprintf(info.name, sizeof info.name, "无法读取存档 #%lu", (unsigned long)id);
        }
        items[n++] = info;
    }
    closedir(dir);
    if (!ok) { free(items); return fail("无法列出存档"); }
    if (n > 1) qsort(items, n, sizeof *items, newest_first);
    *out = items;
    *count = n;
    return 1;
}

static int write_record(FILE *fp, int tag, int x, int y, int z) {
    return fputc(tag, fp) != EOF && put_u32(fp, (uint32_t)x) &&
        fputc(y, fp) != EOF && put_u32(fp, (uint32_t)z);
}
typedef struct {
    FILE *fp;
    size_t count;
    uint8_t bytes[4096];
} SaveEditBatch;

static int flush_edits(SaveEditBatch *batch) {
    if (batch->count && fwrite(batch->bytes,1,batch->count,batch->fp)!=batch->count) return 0;
    batch->count=0;
    return 1;
}
static size_t edit_record_size(int cell) { return cell>255 ? 12u : 11u; }
static void encode_edit(uint8_t *out,int x,int y,int z,int cell) {
    uint32_t ux=(uint32_t)x,uz=(uint32_t)z;
    /* Same tagged, little-endian record as the single-record writer. */
    out[0]=cell>255 ? WIDE_EDIT_TAG : 1;
    for (unsigned i=0;i<4;++i) {
        out[1+i]=(uint8_t)(ux>>(i*8u));
        out[6+i]=(uint8_t)(uz>>(i*8u));
    }
    out[5]=(uint8_t)y;
    out[10]=(uint8_t)cell;
    if (cell>255) out[11]=(uint8_t)((unsigned)cell>>8);
}
static int write_edit(void *ctx, int x, int y, int z, int cell) {
    SaveEditBatch *batch=(SaveEditBatch *)ctx;
    size_t length=edit_record_size(cell);
    if (length>sizeof batch->bytes-batch->count && !flush_edits(batch)) return 0;
    encode_edit(batch->bytes+batch->count,x,y,z,cell);
    batch->count+=length;
    return 1;
}

/* Memoized v108 records live only while the world is open. Keys follow
   actual saved cells, not transient lighting, mesh or residency changes.
   The budget only limits this optional cache, never editing or saving. */
enum { EDIT_MEMO_SLOTS=256, EDIT_MEMO_BUDGET=4*1024*1024 };
typedef struct SaveEditMemo {
    int cx,cz;
    Dimension dimension;
    unsigned long long revision;
    size_t capacity,length;
    uint8_t *bytes;
    struct SaveEditMemo *next;
} SaveEditMemo;
static SaveEditMemo *edit_memo[EDIT_MEMO_SLOTS];
static size_t edit_memo_used;

static void clear_edit_memo(void) {
    if (!edit_memo_used) return;
    for (int i=0;i<EDIT_MEMO_SLOTS;++i) {
        SaveEditMemo *cell=edit_memo[i];
        while (cell) {
            SaveEditMemo *next=cell->next;
            free(cell->bytes); free(cell);
            cell=next;
        }
        edit_memo[i]=NULL;
    }
    edit_memo_used=0;
}
typedef struct { uint8_t *next; size_t remaining; } EditEncoder;
static int encode_memo_edit(void *ctx,int x,int y,int z,int cell) {
    EditEncoder *encoder=(EditEncoder *)ctx;
    size_t len=edit_record_size(cell);
    if (len>encoder->remaining) return 0;
    encode_edit(encoder->next,x,y,z,cell);
    encoder->next+=len;
    encoder->remaining-=len;
    return 1;
}
typedef struct { FILE *fp; Dimension dimension; } EditMemoWriter;
static SaveEditMemo *cached_group(EditMemoWriter *writer,const WorldEditGroup *group,int cx,int cz,
        unsigned long long revision,unsigned count,unsigned wide) {
    if (!count || wide>count || count>CHUNK_SIZE*CHUNK_SIZE*WY) return NULL;
    const size_t len=(size_t)count*11u+wide;
    unsigned slot=(hash2(cx,cz)^(unsigned)writer->dimension*117u)%EDIT_MEMO_SLOTS;
    SaveEditMemo *cached=edit_memo[slot];
    while (cached && (cached->cx!=cx || cached->cz!=cz || cached->dimension!=writer->dimension))
        cached=cached->next;
    if (cached && cached->revision==revision && cached->length==len) return cached;
    size_t capacity=cached ? cached->capacity : 0;
    size_t need=(len>capacity ? len-capacity : 0)+(cached ? 0 : sizeof(SaveEditMemo));
    if (need>EDIT_MEMO_BUDGET-edit_memo_used) return NULL;
    if (!cached) {
        cached=(SaveEditMemo *)malloc(sizeof *cached);
        if (!cached) return NULL;
        *cached=(SaveEditMemo){.cx=cx,.cz=cz,.dimension=writer->dimension,.next=edit_memo[slot]};
        edit_memo[slot]=cached;
        edit_memo_used+=sizeof *cached;
    }
    if (len>cached->capacity) {
        uint8_t *bytes=(uint8_t *)realloc(cached->bytes,len);
        if (!bytes) return NULL;
        edit_memo_used+=len-cached->capacity;
        cached->bytes=bytes;
        cached->capacity=len;
    }
    cached->revision=0; cached->length=0;
    EditEncoder encoder={cached->bytes,len};
    if (!world_visit_edit_group(group,encode_memo_edit,&encoder) || encoder.remaining) return NULL;
    cached->revision=revision;
    cached->length=len;
    return cached;
}
static int write_edit_group(void *ctx,const WorldEditGroup *group,int cx,int cz,
        unsigned long long revision,unsigned count,unsigned wide) {
    EditMemoWriter *writer=(EditMemoWriter *)ctx;
    SaveEditMemo *cached=cached_group(writer,group,cx,cz,revision,count,wide);
    if (!writer->fp) return 1;
    if (cached) return fwrite(cached->bytes,1,cached->length,writer->fp)==cached->length;
    SaveEditBatch batch;
    batch.fp=writer->fp;
    batch.count=0;
    return world_visit_edit_group(group,write_edit,&batch) && flush_edits(&batch);
}
static int write_edits(FILE *fp,Dimension d) {
    EditMemoWriter writer={fp,d};
    return world_visit_edit_groups(d,write_edit_group,&writer);
}
static int write_pending(void *fp, int x, int y, int z, int cell) {
    (void)cell;
    return write_record((FILE *)fp, 2, x, y, z);
}
static int write_furnaces(FILE *fp, Dimension dimension) {
    for (size_t i = 0; i < furnaces_count_in(dimension); i++) {
        const Furnace *f = furnace_at_index_in(dimension,i);
        if (!f || !write_record(fp, 3, f->x, f->y, f->z)) return 0;
        for (int slot = 0; slot < FURNACE_SLOTS; slot++)
            if (!put_stack(fp, f->slots[slot])) return 0;
        if (!put_u32(fp, f->cook) || !put_u32(fp, f->burn) || !put_u32(fp, f->fuel_duration)) return 0;
    }
    return 1;
}

static int read_furnace(FILE *fp, unsigned version, int x, int y, int z) {
    Furnace f;
    memset(&f, 0, sizeof f);
    f.x = x; f.y = y; f.z = z;
    for (int slot = 0; slot < FURNACE_SLOTS; slot++)
        if (!get_stack(fp, version, 0, &f.slots[slot])) return 0;
    uint32_t cook, burn, duration;
    if (!get_u32(fp, &cook) || cook >= SMELT_TICKS || !get_u32(fp, &burn) ||
        !get_u32(fp, &duration) || burn > duration || duration > FURNACE_MAX_FUEL_TICKS) return 0;
    f.cook = (uint16_t)cook; f.burn = (uint16_t)burn; f.fuel_duration = (uint16_t)duration;
    return furnace_restore(&f);
}

static int write_chests(FILE *fp,Dimension d) {
    for (size_t i=0;i<chests_count_in(d);++i) {
        const ChestRecord *r=chest_record_in(d,i);
        int count=0;
        for (int s=0;s<CHEST_SLOTS;++s) count+=r->items[s].count!=0;
        if (!count || !write_record(fp,4,r->at.x,r->at.y,r->at.z) || fputc(count,fp)==EOF) return 0;
        for (int s=0;s<CHEST_SLOTS;++s) if (r->items[s].count &&
            (fputc(s,fp)==EOF || !put_stack(fp,r->items[s]))) return 0;
    }
    return 1;
}
static int read_chest(FILE *fp,unsigned version,int x,int y,int z) {
    ChestRecord r;
    memset(&r,0,sizeof r);
    r.at=(ChestPos){x,y,z};
    int count=fgetc(fp),previous=-1;
    if (count<1 || count>CHEST_SLOTS) return 0;
    for (int i=0;i<count;++i) {
        int s=fgetc(fp);
        if (s<=previous || s>=CHEST_SLOTS || !get_stack(fp,version,1,&r.items[s])) return 0;
        previous=s;
    }
    return chest_restore(&r);
}

static int write_brewing(FILE *fp,Dimension d) {
    for (size_t i=0;i<brewing_count_in(d);++i) {
        const BrewingStand *s=brewing_at_index(d,i);
        if (!s || !write_record(fp,5,s->x,s->y,s->z) || !put_short(fp,s->progress) || fputc(s->active,fp)==EOF) return 0;
        for (int j=0;j<BREW_SLOTS;++j) if (!put_stack(fp,s->slots[j])) return 0;
    }
    return 1;
}
static int read_brewing(FILE *fp,unsigned format,int x,int y,int z) {
    BrewingStand s={.x=x,.y=y,.z=z};
    if (!get_short(fp,&s.progress)) return 0;
    if (format<96) {
        if (s.progress>=400) return 0;
        /* Preserve ingredients and vessels. The old timer described a
           different operation and cannot give a new batch free progress. */
        s.progress=0;
    } else {
        int lane=fgetc(fp);
        if (lane<0 || lane>BREW_BOTTLES) return 0;
        s.active=(uint8_t)lane;
    }
    for (int j=0;j<BREW_SLOTS;++j) if (!get_stack(fp,format,0,&s.slots[j])) return 0;
    return brewing_restore(&s);
}
static int write_hopper(const Hopper *h,void *context) {
    FILE *fp=(FILE *)context;
    if (!write_record(fp,6,h->x,h->y,h->z) || fputc(h->cooldown,fp)==EOF) return 0;
    for (int i=0;i<HOPPER_SLOTS;++i) if (!put_stack(fp,h->slots[i])) return 0;
    return 1;
}
static int read_hopper(FILE *fp,unsigned version,int x,int y,int z) {
    int cooldown=fgetc(fp);
    if (cooldown<0 || cooldown>HOPPER_TICKS) return 0;
    Hopper h={0}; h.x=x; h.y=y; h.z=z; h.cooldown=(uint8_t)cooldown;
    for (int i=0;i<HOPPER_SLOTS;++i) if (!get_stack(fp,version,0,&h.slots[i])) return 0;
    return hopper_restore(&h);
}
static int write_dropper(const Dropper *d,void *context) {
    FILE *fp=(FILE *)context;
    if (!write_record(fp,7,d->x,d->y,d->z) || !put_u32(fp,d->rolls)) return 0;
    for (int i=0;i<DROPPER_SLOTS;++i) if (!put_stack(fp,d->slots[i])) return 0;
    return 1;
}
static int read_dropper(FILE *fp,unsigned version,int x,int y,int z) {
    Dropper d={0}; d.x=x; d.y=y; d.z=z;
    if (!get_u32(fp,&d.rolls)) return 0;
    for (int i=0;i<DROPPER_SLOTS;++i) if (!get_stack(fp,version,0,&d.slots[i])) return 0;
    return dropper_restore(&d);
}
static int write_fire_cell(const FireCell *s,void *context) {
    FILE *fp=(FILE *)context;
    return write_record(fp,8,s->x,s->y,s->z) && put_short(fp,s->attempts) &&
        fputc(s->age,fp)!=EOF && fputc(s->delay,fp)!=EOF;
}
static int read_fire_cell(FILE *fp,int x,int y,int z) {
    FireCell s={0}; s.x=x; s.y=y; s.z=z;
    if (!get_short(fp,&s.attempts)) return 0;
    int age=fgetc(fp),delay=fgetc(fp);
    if (age<0 || delay<0) return 0;
    s.age=(uint8_t)age;s.delay=(uint8_t)delay;
    return fire_restore(&s);
}
static int write_lava_tick(void *context,int x,int y,int z,int delay) {
    FILE *fp=(FILE *)context;
    return write_record(fp,9,x,y,z) && fputc(delay,fp)!=EOF;
}
static int write_entity_name(FILE *fp,uint16_t id) {
    const char *name=item_name_for_id(id);
    size_t n=strlen(name);
    return (!id || n) && fputc((int)n,fp)!=EOF && (!n || fwrite(name,1,n,fp)==n);
}
static int read_entity_name(FILE *fp,unsigned version,uint16_t *id) {
    *id=0;
    if (version<80) return 1;
    int n=fgetc(fp);
    if (n<0 || n>ITEM_NAME_BYTES) return 0;
    if (!n) return 1;
    char name[ITEM_NAME_BYTES+1],normalized[ITEM_NAME_BYTES+1];
    if (fread(name,1,(size_t)n,fp)!=(size_t)n || memchr(name,0,(size_t)n)) return 0;
    name[n]=0;
    return item_name_normalize(name,normalized) && !strcmp(name,normalized) && item_name_intern(name,id) && *id;
}
static int write_villager_market(FILE *fp,const VillagerSave *s) {
    if (!write_record(fp,10,s->x,s->y,s->z) || fputc(s->home,fp)==EOF || fputc(s->health,fp)==EOF ||
        fputc(s->unlocked,fp)==EOF || fputc(s->restock,fp)==EOF) return 0;
    return fwrite(s->uses,1,VILLAGER_OFFERS,fp)==VILLAGER_OFFERS;
}
static int write_villager(const VillagerSave *s,void *context) {
    FILE *fp=(FILE *)context;
    return write_villager_market(fp,s) && write_entity_name(fp,villagers_resident_name(*s));
}
static int read_villager_market(FILE *fp,int x,int y,int z,VillagerSave *out) {
    int home=fgetc(fp),health=fgetc(fp),unlocked=fgetc(fp),restock=fgetc(fp);
    if (home<0 || health<0 || unlocked<0 || restock<0) return 0;
    VillagerSave s={x,y,z,(uint8_t)home,(uint8_t)health,(uint8_t)unlocked,{0},(uint8_t)restock};
    if (fread(s.uses,1,VILLAGER_OFFERS,fp)!=VILLAGER_OFFERS || !villagers_market_valid(s)) return 0;
    *out=s;
    return 1;
}
static int read_villager(FILE *fp,unsigned version,int x,int y,int z) {
    VillagerSave market;
    uint16_t name;
    return read_villager_market(fp,x,y,z,&market) && read_entity_name(fp,version,&name) &&
        villagers_restore_named(market,name);
}
static int read_actor_market(FILE *fp,VillagerSave *market) {
    int x,y,z;
    uint32_t ux,uz;
    if (fgetc(fp)!=10 || !get_u32(fp,&ux) || (y=fgetc(fp))<0 || !get_u32(fp,&uz) ||
        !signed_coord(ux,&x) || !signed_coord(uz,&z)) return 0;
    return read_villager_market(fp,x,y,z,market);
}
static int write_dimension(FILE *fp,Dimension d) {
    const Drop *list = dimension_drops[d];
    int n = 0;
    for (int i = 0; i < MAXDROP; ++i) n += list[i].item.count != 0;
    if (!put_u32(fp,n)) return 0;
    for (int i = 0; i < MAXDROP; ++i) if (list[i].item.count) {
        Drop v = list[i];
        if (!drop_valid(v) || !put_float(fp,v.x) || !put_float(fp,v.y) || !put_float(fp,v.z) || !put_stack(fp,v.item)) return 0;
    }
    return write_edits(fp,d) &&
        water_visit_dimension_pending(d,&write_pending,fp) && write_furnaces(fp,d) &&
        write_chests(fp,d) && write_brewing(fp,d) && hoppers_visit(d,write_hopper,fp) &&
        droppers_visit(d,write_dropper,fp) && fire_visit(d,write_fire_cell,fp) &&
        lava_visit(d,write_lava_tick,fp) && villagers_visit(d,write_villager,fp) && fputc(0,fp) != EOF;
}
static int write_lead(FILE *fp,LeadLink lead) {
    if (!lead_valid(lead)) return 0;
    return lead.kind==LEAD_FENCE ? write_record(fp,lead.kind,lead.x,lead.y,lead.z) : fputc(lead.kind,fp)!=EOF;
}
static int read_lead(FILE *fp,LeadLink *lead) {
    lead->kind=fgetc(fp);
    if (lead->kind==LEAD_FENCE) {
        uint32_t x,z;
        if (!get_u32(fp,&x) || (lead->y=fgetc(fp))<0 || !get_u32(fp,&z) ||
            !signed_coord(x,&lead->x) || !signed_coord(z,&lead->z)) return 0;
    }
    return lead_valid(*lead);
}

static int saved_actor(ActorRecord a) {
    return a.type && (a.type!=ACTOR_VILLAGER || (a.traits&ROAMING_VILLAGER));
}
static int write_actors(FILE *fp, Dimension dim) {
    const ActorRecord *list = actors_in(dim);
    const ActorCare *care=actors_care_in(dim);
    int count = 0;
    for (int i = 0; i < ACTOR_CAPACITY; ++i) count += saved_actor(list[i]);
    if (fputc(count,fp) == EOF) return 0;
    for (int i = 0; i < ACTOR_CAPACITY; ++i) if (saved_actor(list[i])) {
        ActorRecord a = list[i];
        if (fputc(a.type,fp) == EOF || fputc(a.health,fp) == EOF ||
            !put_float(fp,a.feet.x) || !put_float(fp,a.feet.y) ||
            !put_float(fp,a.feet.z) || !put_float(fp,a.heading) ||
            ((a.type==ACTOR_SHEEP || a.type==ACTOR_SHELLBACK || a.type==ACTOR_REEDLIZARD || a.type==ACTOR_IRON_GOLEM ||
                a.type==ACTOR_WOLF || a.type==ACTOR_OCELOT) &&
                fputc(a.traits,fp)==EOF)) return 0;
        if (a.type==ACTOR_REEDLIZARD && !put_short(fp,(uint16_t)actors_reed_cooldown(dim,i))) return 0;
        if (actors_can_wear_armor(a.type)) {
            ActorArmorState state=actors_armor_state(dim,i);
            if (fputc(a.traits,fp)==EOF || !put_short(fp,state.helmet_wear) ||
                fputc(state.health_debt|(state.door_breaker ? ZOMBIE_DOOR_FLAG : 0),fp)==EOF) return 0;
        }
        if (a.type==ACTOR_KILN_MOLE && (fputc(0,fp)==EOF || !put_short(fp,0))) return 0;
        if (a.type==ACTOR_STRANDED_GUIDE) {
            StrandedGuideState s=actors_stranded_state(dim,i);
            if (!put_short(fp,s.rest_ticks) ||
                fputc(s.origin.linked,fp)==EOF || !write_villager_market(fp,&s.origin.market)) return 0;
        } else if (a.type==ACTOR_VILLAGER) {
            VillagerSave market;
            if (dim!=OVERWORLD || !villagers_roaming_market(i,&market) || a.health!=market.health ||
                !write_villager_market(fp,&market)) return 0;
        }
        if (actors_has_care(a.type)) {
            ActorCare c=care[i];
            if (fputc(c.managed,fp)==EOF || !put_short(fp,c.grow) || !put_short(fp,c.cooldown) ||
                !put_short(fp,c.love) || !put_short(fp,c.egg) || !write_lead(fp,*actors_lead_at(dim,i))) return 0;
        }
    }
    return 1;
}

static int write_boats(FILE *fp) {
    int saved_rider=0;
    for (int d=0;d<DIMENSION_COUNT;++d) {
        const BoatRecord *list=boats_in((Dimension)d);
        int count=0, ordinal=0;
        for (int i=0;i<BOAT_CAPACITY;++i) count+=list[i].occupied!=0;
        if (fputc(count,fp)==EOF) return 0;
        for (int i=0;i<BOAT_CAPACITY;++i) if (list[i].occupied) {
            BoatRecord p=list[i];
            ++ordinal;
            if (d==(int)world_dimension() && i==boat_rider()) saved_rider=ordinal;
            if (!put_float(fp,p.position.x) || !put_float(fp,p.position.y) ||
                !put_float(fp,p.position.z) || !put_float(fp,p.heading)) return 0;
        }
    }
    return fputc(saved_rider,fp)!=EOF;
}

static int write_carts(FILE *fp) {
    int saved_rider=0;
    for (int d=0;d<DIMENSION_COUNT;++d) {
        const CartRecord *list=carts_in((Dimension)d);
        int count=0, ordinal=0;
        for (int i=0;i<CART_CAPACITY;++i) count+=list[i].occupied!=0;
        if (fputc(count,fp)==EOF) return 0;
        for (int i=0;i<CART_CAPACITY;++i) if (list[i].occupied) {
            CartRecord p=list[i];
            ++ordinal;
            if (d==(int)world_dimension() && i==cart_rider()) saved_rider=ordinal;
            if (!put_float(fp,p.position.x) || !put_float(fp,p.position.y) ||
                !put_float(fp,p.position.z) || !put_float(fp,p.heading) ||
                !put_float(fp,p.along) || !put_float(fp,p.speed) ||
                !put_u32(fp,(uint32_t)p.track.x) || fputc(p.track.y,fp)==EOF ||
                !put_u32(fp,(uint32_t)p.track.z) || fputc(p.kind,fp)==EOF || fputc(p.fuse,fp)==EOF) return 0;
            if (fputc(p.off_track,fp)==EOF || (p.off_track &&
                (!put_float(fp,p.free_velocity.x) || !put_float(fp,p.free_velocity.y) ||
                 !put_float(fp,p.free_velocity.z)))) return 0;
            if (p.off_track && p.kind==CART_TNT && !put_float(fp,p.fall_distance)) return 0;
            const ItemStack *items=cart_cargo_in((Dimension)d,i);
            for (int j=0,n=cart_cargo_count((Dimension)d,i);j<n;++j)
                if (!items || !put_stack(fp,items[j])) return 0;
            if (p.kind==CART_HOPPER) {
                int ticks=cart_hopper_cooldown((Dimension)d,i);
                if (ticks<0 || fputc(ticks,fp)==EOF) return 0;
            }
            if (p.kind==CART_FURNACE) {
                CartFurnaceDrive drive;
                if (!cart_furnace_drive((Dimension)d,i,&drive) || !put_short(fp,drive.fuel) ||
                    !put_float(fp,drive.x) || !put_float(fp,drive.z)) return 0;
            }
        }
    }
    return fputc(saved_rider,fp)!=EOF;
}
static int write_horses(FILE *fp) {
    int saved_rider=0;
    for (int d=0;d<DIMENSION_COUNT;++d) {
        const HorseRecord *list=horses_in((Dimension)d);
        int count=0,ordinal=0;
        for (int i=0;i<HORSE_CAPACITY;++i) count+=list[i].type!=0;
        if (fputc(count,fp)==EOF) return 0;
        for (int i=0;i<HORSE_CAPACITY;++i) if (list[i].type) {
            const HorseRecord *h=&list[i];
            ++ordinal;
            if (d==(int)world_dimension() && i==horse_rider()) saved_rider=ordinal;
            uint8_t bytes[]={h->type,h->coat,h->max_health,h->health,h->gait,h->spring,h->temper,h->tamed,h->saddle};
            if (fwrite(bytes,1,sizeof bytes,fp)!=sizeof bytes ||
                !put_float(fp,h->feet.x) || !put_float(fp,h->feet.y) || !put_float(fp,h->feet.z) ||
                !put_float(fp,h->heading) || !put_short(fp,h->age) ||
                !put_short(fp,h->cooldown) || !put_short(fp,h->love) ||
                !write_lead(fp,*horses_lead_at((Dimension)d,i))) return 0;
        }
    }
    return fputc(saved_rider,fp)!=EOF;
}

/* Horses restore densely, so key the separate v43 trailer by serialized ordinal. */
static int write_horse_gear(FILE *fp) {
    for (int d=0;d<DIMENSION_COUNT;++d) {
        const HorseRecord *list=horses_in((Dimension)d);
        int count=0,ordinal=0;
        for (int i=0;i<HORSE_CAPACITY;++i)
            if (list[i].type && (list[i].armor.count || list[i].health_debt)) ++count;
        if (fputc(count,fp)==EOF) return 0;
        for (int i=0;i<HORSE_CAPACITY;++i) if (list[i].type) {
            const HorseRecord *h=&list[i];
            if ((h->armor.count || h->health_debt) &&
                (fputc(ordinal,fp)==EOF || fputc(h->health_debt,fp)==EOF || !put_stack(fp,h->armor))) return 0;
            ++ordinal;
        }
    }
    return 1;
}

/* Dynamic entities restore into dense slots, so names follow saved ordinal
   rather than the original slot. Settled villagers use their home record. */
static int write_mob_names(FILE *fp) {
    for (int horse=0;horse<2;++horse) for (int dim=0;dim<DIMENSION_COUNT;++dim) {
        Dimension d=(Dimension)dim;
        int cap=horse ? HORSE_CAPACITY : ACTOR_CAPACITY,count=0,ordinal=0;
        for (int i=0;i<cap;++i) if (horse ? horses_in(d)[i].type : saved_actor(actors_in(d)[i]))
            count+=(horse ? horse_name_id(d,i) : actors_name_id(d,i))!=0;
        if (fputc(count,fp)==EOF) return 0;
        for (int i=0;i<cap;++i) if (horse ? horses_in(d)[i].type : saved_actor(actors_in(d)[i])) {
            uint16_t id=horse ? horse_name_id(d,i) : actors_name_id(d,i);
            if (id && (fputc(ordinal,fp)==EOF || !write_entity_name(fp,id))) return 0;
            ++ordinal;
        }
    }
    return 1;
}

static int write_drop_timers(FILE *fp,const Drop *drop) {
    return put_short(fp,drop->age) && put_short(fp,drop->pickup_delay);
}
static int write_death_drop(const Drop *d,void *ctx) {
    FILE *fp=(FILE *)ctx;
    return drop_valid(*d) && put_float(fp,d->x) && put_float(fp,d->y) && put_float(fp,d->z) &&
        put_stack(fp,d->item) && write_drop_timers(fp,d);
}
static int write_drop_state(FILE *fp) {
    if (fputc(death_keep_inventory(),fp)==EOF) return 0;
    for (int d=0;d<DIMENSION_COUNT;++d) {
        for (int i=0;i<MAXDROP;++i) {
            const Drop *drop=&dimension_drops[d][i];
            if (drop->item.count && !write_drop_timers(fp,drop)) return 0;
        }
        if (!put_u32(fp,(uint32_t)death_drop_count((Dimension)d)) ||
            !death_drop_visit((Dimension)d,&write_death_drop,fp)) return 0;
    }
    return 1;
}
static int write_experience(FILE *fp) {
    if (!put_u32(fp,experience_points()) || !put_u32(fp,experience_rng())) return 0;
    for (int d=0;d<DIMENSION_COUNT;++d) {
        size_t n=0;
        const XPOrb *list=experience_orbs((Dimension)d,&n);
        if (!put_u32(fp,(uint32_t)n)) return 0;
        for (size_t i=0;i<n;++i) {
            XPOrb orb=list[i];
            if (!put_float(fp,orb.point.x) || !put_float(fp,orb.point.y) || !put_float(fp,orb.point.z) ||
                !put_float(fp,orb.velocity.x) || !put_float(fp,orb.velocity.y) || !put_float(fp,orb.velocity.z) ||
                !put_u32(fp,orb.value) || !put_short(fp,orb.age)) return 0;
        }
    }
    return 1;
}
static int write_fall_check(FallCheck p,void *context) {
    FILE *fp=(FILE *)context;
    return put_u32(fp,(uint32_t)p.x) && fputc(p.y,fp)!=EOF && put_u32(fp,(uint32_t)p.z);
}
static int write_gravity(FILE *fp) {
    for (int d=0;d<DIMENSION_COUNT;++d) {
        Dimension dimension=(Dimension)d;
        size_t count=0;
        const FallingBlock *list=gravity_falling(dimension,&count);
        if (!put_u32(fp,(uint32_t)gravity_pending_count(dimension)) ||
            !gravity_visit_pending(dimension,write_fall_check,fp) || !put_u32(fp,(uint32_t)count)) return 0;
        for (size_t i=0;i<count;++i) {
            FallingBlock e=list[i];
            if (!write_fall_check((FallCheck){e.x,e.origin,e.z},fp) || !put_short(fp,e.cell) ||
                !put_float(fp,e.bottom) || !put_float(fp,e.speed)) return 0;
        }
    }
    return 1;
}
static int write_timing_record(const RedstoneTiming *t,void *context) {
    FILE *fp=(FILE *)context;
    CircuitPos p=t->position;
    return write_fall_check((FallCheck){p.x,p.y,p.z},fp) && fputc(t->remaining,fp)!=EOF &&
        fputc(t->target,fp)!=EOF && fputc(t->cooldown,fp)!=EOF && fputc(t->count,fp)!=EOF && fputc(t->output,fp)!=EOF &&
        fwrite(t->ages,1,t->count,fp)==t->count;
}
static int write_redstone_timing(FILE *fp) {
    for (int d=0;d<DIMENSION_COUNT;++d) {
        Dimension dim=(Dimension)d;
        if (!put_u32(fp,(uint32_t)redstone_timing_count(dim)) ||
            !redstone_visit_timing(dim,write_timing_record,fp)) return 0;
    }
    return 1;
}
static int write_button_arrows(FILE *fp) {
    for (int d=0;d<DIMENSION_COUNT;++d) {
        size_t count=0;
        const ButtonArrow *arrows=button_arrows_in((Dimension)d,&count);
        if (fputc((int)count,fp)==EOF) return 0;
        for (size_t i=0;i<count;++i) {
            const ButtonArrow *a=&arrows[i];
            if (!write_fall_check((FallCheck){a->x,a->y,a->z},fp) || !put_short(fp,a->raw) ||
                !put_short(fp,a->ticks) || fputc(a->recoverable,fp)==EOF ||
                !put_float(fp,a->point.x) || !put_float(fp,a->point.y) || !put_float(fp,a->point.z) ||
                !put_float(fp,a->direction.x) || !put_float(fp,a->direction.y) || !put_float(fp,a->direction.z)) return 0;
        }
    }
    return 1;
}
static int write_blasts(FILE *fp) {
    for (int d=0;d<DIMENSION_COUNT;++d) {
        size_t n=0;
        const PrimedBlast *list=blasts_in((Dimension)d,&n);
        if (!put_short(fp,(uint16_t)n)) return 0;
        for (size_t i=0;i<n;++i) {
            PrimedBlast a=list[i];
            if (fputc(a.kind,fp)==EOF || !put_short(fp,a.ticks) ||
                !put_float(fp,a.point.x) || !put_float(fp,a.point.y) || !put_float(fp,a.point.z) ||
                !put_float(fp,a.speed.x) || !put_float(fp,a.speed.y) || !put_float(fp,a.speed.z)) return 0;
        }
    }
    return 1;
}
static int write_leaf_tile(const LeafWork *w,void *context) {
    FILE *fp=(FILE *)context;
    if (!write_fall_check((FallCheck){w->x,w->y,w->z},fp) || !put_short(fp,(uint16_t)w->cursor) ||
        fputc((int)w->delay,fp)==EOF) return 0;
    for (int i=0;i<LEAF_TILE;++i)
        if (!put_u32(fp,(uint32_t)w->layers[i]) || !put_u32(fp,(uint32_t)(w->layers[i]>>32))) return 0;
    return 1;
}
static int write_leaf_work(FILE *fp) {
    for (int d=0;d<DIMENSION_COUNT;++d) {
        Dimension dim=(Dimension)d;
        if (!put_u32(fp,(uint32_t)leaves_pending(dim)) || !leaves_visit(dim,write_leaf_tile,fp)) return 0;
    }
    return 1;
}
static int write_ender_chest(FILE *fp) {
    const ItemStack *items=ender_chest_items();
    int count=0;
    for (int i=0;i<CHEST_SLOTS;++i) count+=items[i].count!=0;
    if (fputc(count,fp)==EOF) return 0;
    for (int i=0;i<CHEST_SLOTS;++i) if (items[i].count &&
        (fputc(i,fp)==EOF || !put_stack(fp,items[i]))) return 0;
    return 1;
}
/* Empty legacy encounter and aura slots keep the trailer layout stable. */
static int write_retired_slots(FILE *fp) {
    for (int i=0;i<2*DIMENSION_COUNT+1;++i) if (fputc(0,fp)==EOF) return 0;
    return 1;
}
static int write_dungeon_clocks(FILE *fp) {
    const DungeonClock *list=dungeon_clocks();
    uint16_t count=0;
    for (int i=0;i<DUNGEON_CLOCKS;++i) if (list[i].random) ++count;
    if (!put_short(fp,count)) return 0;
    for (int i=0;i<DUNGEON_CLOCKS;++i) if (list[i].random &&
        (!write_record(fp,1,list[i].x,list[i].y,list[i].z) ||
         !put_short(fp,list[i].delay) || !put_u32(fp,list[i].random))) return 0;
    return 1;
}
/* Write only the painted span of each row, not the surrounding parchment. */
static int write_maps(FILE *fp) {
    unsigned count=map_count();
    if (!count) return 1;
    if (fputc((int)count,fp)==EOF) return 0;
    for (unsigned id=1;id<=count;++id) {
        MapView m;
        if (!map_view(id,&m) || !put_u32(fp,(uint32_t)m.x) || !put_u32(fp,(uint32_t)m.z) ||
            fputc(m.dimension,fp)==EOF || fputc((int)m.scale,fp)==EOF) return 0;
        for (int row=0;row<MAP_SIDE;++row) {
            const uint8_t *r=m.colors+row*MAP_SIDE;
            int start=0,end=MAP_SIDE;
            while (start<end && !r[start]) ++start;
            while (start<end && !r[end-1]) --end;
            unsigned span=(unsigned)(end-start);
            if (fputc(span ? start : 0,fp)==EOF || fputc((int)span,fp)==EOF ||
                (span && fwrite(r+start,1,span,fp)!=span)) return 0;
        }
    }
    return 1;
}
static int read_maps(FILE *fp) {
    int count=fgetc(fp);
    if (count<1 || count>MAP_LIMIT) return 0;
    for (int id=0;id<count;++id) {
        uint32_t ux,uz;
        int x,z,dim,scale;
        if (!get_u32(fp,&ux) || !get_u32(fp,&uz) || !signed_coord(ux,&x) || !signed_coord(uz,&z) ||
            (dim=fgetc(fp))<0 || dim>=DIMENSION_COUNT || (scale=fgetc(fp))<0 || scale>MAP_MAX_SCALE) return 0;
        uint8_t pixels[MAP_PIXELS]={0};
        for (int row=0;row<MAP_SIDE;++row) {
            int start=fgetc(fp),span=fgetc(fp);
            if (start<0 || span<0 || start+span>MAP_SIDE || (!span && start)) return 0;
            uint8_t *r=pixels+row*MAP_SIDE;
            if (span && (fread(r+start,1,(size_t)span,fp)!=(size_t)span || !r[start] || !r[start+span-1])) return 0;
        }
        if (!map_restore(x,z,(Dimension)dim,(unsigned)scale,pixels)) return 0;
    }
    return 1;
}
/* v86 appends one wall-frame bank per dimension. Coordinates and items
   are serialized explicitly, including map references and custom names. */
static int write_item_frames(FILE *fp) {
    for (int d=0;d<DIMENSION_COUNT;++d) {
        const ItemFrame *frames=item_frames_in((Dimension)d);
        uint16_t count=0;
        for (int i=0;i<ITEM_FRAME_LIMIT;++i) count+=frames[i].present;
        if (!put_short(fp,count)) return 0;
        for (int i=0;i<ITEM_FRAME_LIMIT;++i) if (frames[i].present) {
            const ItemFrame *f=&frames[i];
            int flags=f->face|(f->rotation<<2)|(f->detached<<4);
            if (!put_u32(fp,(uint32_t)f->x) || !put_short(fp,(uint16_t)f->y) ||
                !put_u32(fp,(uint32_t)f->z) || fputc(flags,fp)==EOF || !put_stack(fp,f->item)) return 0;
        }
    }
    return 1;
}
static int read_item_frames(FILE *fp,unsigned format) {
    for (int d=0;d<DIMENSION_COUNT;++d) {
        uint16_t count;
        if (!get_short(fp,&count) || count>ITEM_FRAME_LIMIT) return 0;
        for (unsigned i=0;i<count;++i) {
            uint32_t x,z; uint16_t y; int flags;
            ItemStack item;
            if (!get_u32(fp,&x) || !get_short(fp,&y) || !get_u32(fp,&z) ||
                (flags=fgetc(fp))<0 || flags>31 || !get_stack(fp,format,0,&item) ||
                !item_frame_restore((Dimension)d,(ItemFrame){(int32_t)x,y,(int32_t)z,item,
                    (uint8_t)(flags&3),(uint8_t)((flags>>2)&3),1,(uint8_t)(flags>>4)})) return 0;
        }
    }
    return 1;
}
enum { REGEN_II_FLAG=UINT16_C(0x8000) };
static int valid_regeneration(unsigned duration,int strong) {
    return strong ? (duration>0 && duration<=REGEN_II_TICKS) : duration<=REGEN_LONG_TICKS;
}
static int snapshot(FILE *fp, const SaveInfo *info) {
    size_t len = strlen(info->name);
    if (player_effects.poison>PUFFER_POISON_TICKS || player_effects.hunger>CHICKEN_HUNGER_TICKS ||
        (player_effects.poison_slow && (!player_effects.poison || player_effects.poison>CAVE_POISON_HARD)) ||
        player_effects.fire>FIRE_POTION_LONG_TICKS || player_effects.weakness>WEAKNESS_LONG_TICKS ||
        player_rules.health_debt>=HEALTH_UNITS ||
        !valid_regeneration(player_effects.regeneration,player_effects.regeneration_ii) ||
        (player_effects.regeneration && (!player_rules.health || info->mode!=SURVIVAL)) ||
        player_effects.water_breathing>WATER_BREATHING_LONG_TICKS ||
        (player_effects.water_breathing && !player_rules.health) ||
        player_effects.night_vision>NIGHT_VISION_LONG_TICKS ||
        (player_effects.night_vision && !player_rules.health) ||
        ((!player_rules.health || info->mode==CREATIVE) && player_rules.health_debt) ||
        (info->mode==CREATIVE && (player_effects.poison || player_effects.hunger || player_effects.fire ||
            player_effects.weakness)) || !actors_validate_stranded_origins()) return 0;
    if (info->seed != world_seed() || (!info->has_explicit_seed && info->seed!=seed_for(info->id)) ||
        info->mode != player_rules.mode || info->hardcore!=player_rules.hardcore ||
        (info->hardcore && (info->mode!=SURVIVAL || (info->hardcore_dead && player_rules.health))) ||
        info->generation != world_generation() || info->profile!=world_profile() ||
        info->structures_disabled!=world_structures_disabled() ||
        (info->profile==WORLD_SUPERFLAT &&
            (!flat_valid(&info->flat) || !flat_equal(&info->flat,world_flat_layout()))) ||
        fwrite(magic, 1, sizeof magic, fp) != sizeof magic || fputc(CURRENT_FORMAT, fp) == EOF ||
        !put_u32(fp, info->id) || !put_u32(fp, (uint32_t)info->seed) || fputc((int)len, fp) == EOF ||
        fwrite(info->name, 1, len, fp) != len ||
        fputc((info->hardcore ? HEADER_HARDCORE|(!player_rules.health ? HEADER_HARDCORE_DEAD : 0) : info->mode) |
            (map_count() ? HEADER_MAPS : 0) | (info->has_explicit_seed ? HEADER_EXPLICIT_SEED : 0) |
            (info->profile!=WORLD_STANDARD ? HEADER_WORLD_PROFILE : 0) |
            (info->structures_disabled ? HEADER_NO_STRUCTURES : 0) |
            (info->bonus_chest ? HEADER_BONUS_CHEST : 0),fp) == EOF ||
        fputc(info->generation, fp) == EOF || fputc(world_dimension(),fp) == EOF ||
        (info->has_explicit_seed && !put_u32(fp,(uint32_t)(info->seed>>32))) ||
        (info->profile!=WORLD_STANDARD && fputc(info->profile,fp)==EOF) ||
        (info->profile==WORLD_SUPERFLAT && (!put_short(fp,info->flat.height) ||
            (info->flat.height && (fputc(info->flat.biome,fp)==EOF || fputc(info->flat.features,fp)==EOF ||
                fwrite(info->flat.blocks,1,info->flat.height,fp)!=info->flat.height)))) ||
        !put_float(fp, px) || !put_float(fp, py) || !put_float(fp, pz) ||
        !put_float(fp, remainderf(yaw, 6.2831853f)) || !put_float(fp, pitch) || !put_float(fp, vy) ||
        fputc(selected_slot, fp) == EOF ||
        fputc(player_rules.health, fp) == EOF || fputc(player_rules.food, fp) == EOF ||
        fputc(player_rules.air, fp) == EOF || fputc(player_rules.flying, fp) == EOF ||
        !put_u32(fp, player_rules.day_tick) || !put_float(fp, player_rules.exhaustion) ||
        !put_float(fp, player_rules.submerged_time) || !put_float(fp, player_rules.hurt_time) ||
        !put_float(fp, player_rules.fall_origin)) return 0;
    if (!write_maps(fp)) return 0;
    for (int i = 0; i < SLOT_COUNT; i++)
        if (!put_stack(fp, inventory[i])) return 0;
    for (int d = 0; d < DIMENSION_COUNT; ++d) if (!write_dimension(fp,(Dimension)d)) return 0;
    EndReturn p = portals_end_return();
    if (fputc(p.valid != 0,fp) == EOF || (p.valid && (!put_float(fp,p.position.x) ||
        !put_float(fp,p.position.y) || !put_float(fp,p.position.z)))) return 0;
    for (int d = 0; d < DIMENSION_COUNT; ++d) if (!write_actors(fp,(Dimension)d)) return 0;
    BedAnchor anchor=bed_anchor();
    /* Retain the record shape. Only the survey generation uses these words. */
    if (!(put_u32(fp,0) &&
        put_u32(fp,uses_aquifer() ? portals_aquifer_samples() : 0u) &&
        write_boats(fp) &&
        write_carts(fp) && write_horses(fp) && put_short(fp,player_effects.poison|
            (player_effects.poison_slow ? POISON_SLOW_FLAG : 0)) &&
        put_short(fp,player_effects.hunger) &&
        (!anchor.valid ? fputc(0,fp)!=EOF : write_record(fp,1,anchor.head.x,anchor.head.y,anchor.head.z)) &&
        put_short(fp,player_effects.fire) &&
        fputc(player_rules.health_debt|(player_effects.weakness ? PLAYER_WEAKNESS_FLAG : 0),fp)!=EOF &&
        (!player_effects.weakness || put_short(fp,player_effects.weakness)))) return 0;
    for (int i=0;i<ARMOR_SLOTS;++i) if (!put_stack(fp,armor_worn(i))) return 0;
    PlayerBurn burn=player_burn_state();
    FieldSave field=field_record();
    WeatherSave w=weather_record();
    return write_drop_state(fp) && write_experience(fp) && write_gravity(fp) && write_redstone_timing(fp) &&
        write_button_arrows(fp) && write_blasts(fp) && put_short(fp,burn.remaining) && fputc(burn.due,fp)!=EOF &&
        put_u32(fp,w.random) && put_u32(fp,w.remaining) && put_short(fp,w.strike) && put_short(fp,w.cover) &&
        fputc(w.kind,fp)!=EOF && write_horse_gear(fp) && write_leaf_work(fp) && write_ender_chest(fp) &&
        write_retired_slots(fp) && write_dungeon_clocks(fp) && write_mob_names(fp) &&
        put_short(fp,(uint16_t)(player_effects.regeneration | (player_effects.regeneration_ii ? REGEN_II_FLAG : 0))) &&
        put_short(fp,player_effects.water_breathing) && put_short(fp,player_effects.night_vision) &&
        put_u32(fp,player_rules.world_day) && write_item_frames(fp) &&
        fputc(field.water,fp)!=EOF && fputc(field.heat,fp)!=EOF && put_short(fp,field.drain);
}

typedef struct {
    char path[800],temp[824];
    int fd,error;
    FILE *stream;
    char *buffer;
} StagedSave;

/* A single bounded staging buffer avoids 4 KiB write calls when encoding
   a large checkpoint. It does not bound what can be saved. stdio drains
   full buffers to this private file, never to the live save. */
enum { AUTOSAVE_BUFFER=256*1024 };
typedef struct {
    StagedSave file;
    pthread_t worker;
    uint32_t id;
    Dimension dimension;
    int hardcore_dead;
    int running,done,written;
} PendingCheckpoint;
static PendingCheckpoint pending;

static int stage_save(const SaveInfo *info, StagedSave *out, int background) {
    *out=(StagedSave){0};
    save_path(out->path,info->id);
    snprintf(out->temp,sizeof out->temp,"%s.tmp.XXXXXX",out->path);
    out->fd=mkstemp(out->temp);
    if (out->fd<0) {
        fprintf(stderr, "save temporary file: %s\n", strerror(errno));
        return fail("无法写入存档目录");
    }
    out->stream=fdopen(out->fd,"wb");
    if (out->stream && background) {
        out->buffer=(char *)malloc(AUTOSAVE_BUFFER);
        if (out->buffer && setvbuf(out->stream,out->buffer,_IOFBF,AUTOSAVE_BUFFER)!=0) {
            free(out->buffer); out->buffer=NULL;
        }
    }
    if (out->stream && snapshot(out->stream,info) && !ferror(out->stream)) return 1;
    if (out->stream) (void)fclose(out->stream); else (void)close(out->fd);
    (void)unlink(out->temp);
    free(out->buffer);
    return fail("未能准备存档；原存档未被覆盖");
}
static int publish_save(StagedSave *file,int creating) {
    /* No references back into the simulation. Only this owner touches
       the stream, including the caller-provided buffer, after staging. */
    int ok=fflush(file->stream)==0 && !ferror(file->stream) && fsync(file->fd)==0;
    if (!ok) file->error=errno ? errno : EIO;
    if (fclose(file->stream)!=0 && ok) { file->error=errno; ok=0; }
    free(file->buffer);
    if (ok && (creating ? link(file->temp,file->path) : rename(file->temp,file->path))!=0) {
        file->error=errno; ok=0;
    }
    /* A successful rename already removed the temporary directory entry. */
    if (!ok || creating) (void)unlink(file->temp);
    return ok;
}
static int publish_error(int error) {
    fprintf(stderr,"save commit: %s\n",strerror(error ? error : EIO));
    return fail("未能保存；原存档未被覆盖");
}
static int commit(const SaveInfo *info, int creating) {
    StagedSave file;
    if (!stage_save(info,&file,0)) return 0;
    if (!publish_save(&file,creating)) return publish_error(file.error);
    return 1;
}

static void *checkpoint_write(void *opaque) {
    PendingCheckpoint *task=(PendingCheckpoint *)opaque;
    task->written=publish_save(&task->file,0);
    __atomic_store_n(&task->done,1,__ATOMIC_RELEASE);
    return NULL;
}
static SaveCheckpoint checkpoint_result(void) {
    if (!pending.written) {
        publish_error(pending.file.error);
        return SAVE_CHECKPOINT_FAILED;
    }
    if (active.id==pending.id) {
        active.dimension=pending.dimension;
        active.hardcore_dead=pending.hardcore_dead;
    }
    return SAVE_CHECKPOINT_WRITTEN;
}
static SaveCheckpoint finish_autosave(int wait) {
    if (!pending.running) return SAVE_CHECKPOINT_IDLE;
    if (!wait && !__atomic_load_n(&pending.done,__ATOMIC_ACQUIRE)) return SAVE_CHECKPOINT_PENDING;
    /* Join even on successful poll: do not reuse the task while the
       original writer still owns its thread environment. */
    if (pthread_join(pending.worker,NULL)!=0) {
        fail("无法等待自动保存完成；仍保留当前世界");
        return SAVE_CHECKPOINT_FAILED;
    }
    pending.running=0;
    return checkpoint_result();
}
SaveCheckpoint save_autosave_poll(void) { return finish_autosave(0); }
static int drain_autosave(void) {
    (void)finish_autosave(1);
    return !pending.running;
}
SaveCheckpoint save_autosave_begin(void) {
    if (pending.running) return SAVE_CHECKPOINT_PENDING;
    if (!active.id) {
        fail("没有正在游玩的世界");
        return SAVE_CHECKPOINT_FAILED;
    }
    if (!stage_save(&active,&pending.file,1)) return SAVE_CHECKPOINT_FAILED;
    pending.id=active.id;
    pending.dimension=world_dimension();
    pending.hardcore_dead=active.hardcore && !player_rules.health;
    __atomic_store_n(&pending.done,0,__ATOMIC_RELAXED);
    if (pthread_create(&pending.worker,NULL,checkpoint_write,&pending)==0) {
        pending.running=1;
        return SAVE_CHECKPOINT_PENDING;
    }
    /* The same staged bytes are durable even if this engine cannot
       allocate another thread (do not serialize a second time). */
    pending.written=publish_save(&pending.file,0);
    return checkpoint_result();
}

int save_create(const char *name) {
    return save_create_in_mode(name, SURVIVAL);
}

int save_create_in_mode(const char *name, GameMode mode) {
    return save_create_with_generation(name, mode, GENERATOR_CURRENT);
}

static int create_world(const char *name,GameMode mode,int generation,int hardcore,
                        WorldProfile profile,const uint64_t *seed,int structures_disabled,int bonus_chest,
                        const FlatLayout *flat) {
    if (active.id) return fail("请先保存并退出当前世界");
    clear_edit_memo();
    if (!*folder) return 0;
    if (mode != CREATIVE && mode != SURVIVAL) return fail("未知的游戏模式");
    if (hardcore && mode!=SURVIVAL) return fail("极限模式必须使用生存规则");
    if (hardcore && bonus_chest) return fail("极限模式不能使用奖励箱，请先关闭奖励箱。");
    if (!generation_valid(generation)) return fail("未知的地形版本");
    if (!world_profile_valid(profile)) return fail("未知的世界类型");
    if ((flat && profile!=WORLD_SUPERFLAT) || !flat_valid(flat)) return fail("地层无效。");
    if (!name) name = "";
    while (*name == ' ') name++;
    size_t len = strlen(name);
    while (len && name[len - 1] == ' ') len--;
    if (len && !valid_name(name, len)) return fail("世界名太长或包含换行，请缩短名称");
    SaveInfo *items;
    size_t count;
    if (!save_list(&items, &count)) return 0;
    uint32_t latest = count ? items[0].id : 0;
    free(items);
    if (latest == UINT32_MAX) return fail("存档编号已用完");
    SaveInfo info = {0};
    info.id = latest + 1;
    info.seed = seed ? *seed : seed_for(info.id);
    info.has_explicit_seed = seed!=NULL;
    info.readable = 1;
    info.mode = mode;
    info.hardcore=hardcore;
    info.format = CURRENT_FORMAT;
    info.generation = generation;
    info.profile=profile;
    if (flat && !flat_equal(flat,NULL)) info.flat=*flat;
    info.structures_disabled=structures_disabled;
    info.bonus_chest=bonus_chest;
    if (len) memcpy(info.name, name, len);
    else snprintf(info.name, sizeof info.name, "世界 %lu", (unsigned long)info.id);
    if (!world_begin_layout(info.seed,info.generation,profile,structures_disabled,
        profile==WORLD_SUPERFLAT ? &info.flat : NULL)) return fail("无法生成世界");
    generate();
    player_reset_rules(mode);
    player_rules.hardcore=hardcore;
    if (bonus_chest && !chest_create_bonus()) return fail("出生点附近无法安全放置奖励箱，请更换种子或关闭奖励箱。");
    if (!commit(&info, 1)) return 0;
    active = info;
    printf("WORLD CREATED %lu seed=%" PRId64 "\n",(unsigned long)info.id,world_seed_signed(info.seed));
    fflush(stdout);
    return 1;
}

int save_create_with_generation(const char *name, GameMode mode, int generation) {
    return create_world(name,mode,generation,0,WORLD_STANDARD,NULL,0,0,NULL);
}
int save_create_seeded_generation(const char *name,GameMode mode,int generation,uint64_t seed) {
    return create_world(name,mode,generation,0,WORLD_STANDARD,&seed,0,0,NULL);
}

int save_create_hardcore(const char *name) {
    return create_world(name,SURVIVAL,GENERATOR_CURRENT,1,WORLD_STANDARD,NULL,0,0,NULL);
}

int save_create_seeded(const char *name,GameMode mode,int hardcore,uint64_t seed) {
    return save_create_profiled(name,mode,hardcore,WORLD_STANDARD,&seed);
}

int save_create_profiled(const char *name,GameMode mode,int hardcore,WorldProfile profile,const uint64_t *seed) {
    return save_create_options(name,mode,hardcore,profile,seed,0,0);
}

int save_create_options(const char *name,GameMode mode,int hardcore,WorldProfile profile,
    const uint64_t *seed,int structures_disabled,int bonus_chest) {
    return save_create_layout(name,mode,hardcore,profile,seed,structures_disabled,bonus_chest,NULL);
}

int save_create_layout(const char *name,GameMode mode,int hardcore,WorldProfile profile,
    const uint64_t *seed,int structures_disabled,int bonus_chest,const FlatLayout *flat) {
    return create_world(name,mode,GENERATOR_CURRENT,hardcore!=0,profile,seed,structures_disabled!=0,bonus_chest!=0,flat);
}

int save_write(void) {
    if (!active.id) return fail("没有正在游玩的世界");
    if (!drain_autosave()) return 0;
    if (!commit(&active, 0)) return 0;
    active.dimension = world_dimension();
    active.hardcore_dead=active.hardcore && !player_rules.health;
    return 1;
}

static int validate_piston(void *ctx,int x,int y,int z,int raw) {
    (void)ctx;
    CircuitPos peer;
    int expected;
    return !piston_peer((CircuitPos){x,y,z},raw,&peer,&expected) ||
        world_recorded_cell(world_dimension(),peer.x,peer.y,peer.z)==expected;
}
static int validate_door(void *ctx,int x,int y,int z,int raw) {
    (void)ctx;
    if (!door_cell(raw)) return 1;
    int lower=y-(door_upper(raw)!=0),bits=door_bits(raw);
    int support=world_recorded_cell(world_dimension(),x,lower-1,z);
    return lower>=1 && lower<WY-1 &&
        (!(bits&DOOR_IRON) || !!(bits&DOOR_POWERED)==!!(bits&DOOR_OPEN)) &&
        (support<0 || door_support(support)) &&
        world_recorded_cell(world_dimension(),x,lower+(y==lower),z)==door_peer(raw);
}
static int validate_wall_attachment(void *ctx,int x,int y,int z,int raw) {
    (void)ctx;
    int d=wall_attachment_dir(raw);
    if (d<0) return 1;
    int support=world_recorded_cell(world_dimension(),x+rail_dx(d),y,z+rail_dz(d));
    return support<0 || wall_attachment_support(support);
}
static int read_dimension(FILE *fp, unsigned version) {
    const float lim = WORLD_SAVE_LIMIT;
    uint32_t drop_count;
    if (!get_u32(fp, &drop_count) || drop_count > MAXDROP) return 0;
    for (uint32_t i = 0; i < drop_count; i++) {
        Drop *d = &drops[i];
        if (!get_float(fp, &d->x, -lim, lim) || !get_float(fp, &d->y, 0, WY) ||
            !get_float(fp, &d->z, -lim, lim) || !get_stack(fp, version, 1, &d->item)) return 0;
    }
    int container_phase=0,hopper_blocks=0,hopper_records=0,dropper_blocks=0,dropper_records=0,fires=0,fire_records=0;
    int has_piston=0,has_door=0,has_wall_attachment=0;
    for (int tag; (tag = fgetc(fp)) != 0;) {
        uint32_t u, v;
        int x, y, z;
        int wide=version>=31 && tag==WIDE_EDIT_TAG;
        if (wide) tag=1;
        if ((tag!=1 && tag!=2 && !(tag==3 && version>=4) && !(tag==4 && version>=13) &&
             !(tag==5 && version>=19) && !(tag==6 && version>=32) && !(tag==7 && version>=33) &&
             !(tag==8 && version>=36) && !(tag==9 && version>=39) && !(tag==10 && version>=40)) ||
            tag<container_phase || !get_u32(fp, &u) || (y = fgetc(fp)) < 0 || y >= WY ||
            !get_u32(fp, &v) || !signed_coord(u, &x) || !signed_coord(v, &z)) return 0;
        if (tag == 1) {
            int cell;
            if (wide) {
                uint16_t value;
                if (!get_short(fp,&value) || value<=255) return 0;
                cell=value;
            } else if ((cell=fgetc(fp))<0) return 0;
            if (version < 3 && cell >= LEGACY_BLOCKS) {
                if (cell < LEGACY_FLOW_BASE || cell >= LEGACY_FLOW_BASE + WATER_LEVELS) return 0;
                cell = FLOW_BASE + cell - LEGACY_FLOW_BASE;
            }
            if (cell==ARCHIVE_REWARD_CELL) {
                if (version<56) return 0;
                cell=GRAVEL;
            }
            if (cell>=ARCHIVE_PALE_BLOCK && cell<=ARCHIVE_EXIT) {
                if (version<6) return 0;
                cell=cell==ARCHIVE_PALE_BLOCK ? STONE :
                    cell==ARCHIVE_FRAME || cell==ARCHIVE_FRAME_SET ? STONE_BRICKS : AIR;
            }
            if (cell==NETHER_PORTAL_X || cell==NETHER_PORTAL_Z) {
                if (version<6) return 0;
                cell=AIR;
            }
            if (version < 10 && rail_type(cell_type(cell))) return 0;
            if (version<68 && detector_track(cell)) return 0;
            if (version<69 && activator_track(cell)) return 0;
            if ((version<75 && ender_chest_cell(cell)) || (version<76 && air_outlet_cell(cell)) ||
                (version<78 && ((cell>=AIR_STATION_BASE && cell<AIR_STATION_ARCHIVE_END) ||
                    cell==GOLD_BLOCK_CELL || cell==DIAMOND_BLOCK_CELL))) return 0;
            if (version<94 && cell>=AIR_STATION_BASE && cell<AIR_STATION_ARCHIVE_END) cell=AIR_STATION_BASE;
            if (version<94 && cell==AIR_PIPE) return 0;
            if (version<101 && cistern_cell(cell)) return 0;
            if (version<104 && cell==HYDRAULIC_SORTER) return 0;
            if (version<105 && heat_bench_cell(cell)) return 0;
            if (version<106 && coast_stone(cell)) return 0;
            if (version<109 && air_relay_cell(cell)) return 0;
            if (version<110 && cell==FIBROUS_CLAY) return 0;
            if (strata_sample_cell(cell) && (version<111 || world_generation()<GENERATOR_STRATA_GALLERIES)) return 0;
            if (version<112 && cell==FALL_SIEVE) return 0;
            if (version<114 && cistern_probe_cell(cell)) return 0;
            if (version<11 && (cell==CHEST || cell==SADDLE_CHEST)) return 0;
            if (version<42 && cell>=SMITH_CHEST_CELL && cell<=EMERALD_BLOCK_CELL) return 0;
            if (version<44 && (sapling_cell(cell) || placed_leaf_cell(cell) || (cell>=TREE_LEAF_BASE && cell<TREE_LEAF_END))) return 0;
            if (version<46 && (pumpkin_cell(cell) || pumpkin_stem_cell(cell) || cell==POPPY_CELL)) return 0;
            if (version<50 && stained_glass_cell(cell)) return 0;
            if (version<51 && (dye_resource_cell(cell) || (stained_glass_cell(cell) && cell>=STAINED_GLASS_LEGACY_END))) return 0;
            if (version<52 && pane_cell(cell)) return 0;
            if (version<53 && cell==TRAPPED_CHEST_CELL) return 0;
            if (version<54 && (cell==TRIPWIRE_CELL || tripwire_hook_cell(cell))) return 0;
            if (version<55 && cell==TEMPLE_CHEST_CELL) return 0;
            if (cell>=BUILD_STAIRS_END && cell<BUILD_STAIRS_END+4) {
                if (version<87 || version>=95) return 0;
                const int legacy_material[]={STONE,COBBLESTONE,STONE_BRICKS,AIR};
                cell=legacy_material[cell-BUILD_STAIRS_END];
            }
            if (version<95 && seep_cell(cell)) return 0;
            if (version<89 && geology_block(cell)) return 0;
            if (version<92 && cell==RESONANCE_MARKER) return 0;
            if (aquifer_cell(cell) && (!uses_aquifer() || aquifer_fixture(cell))) return 0;
            if (pressure_works_cell(cell) && (version<91 || world_generation()<GENERATOR_PRESSURE_WORKS ||
                (cell!=SINTERED_TILE && (cell!=PRESSURE_OPEN || !inside_pressure_works() ||
                    !pressure_valve_site(x,y,z))))) return 0;
            if (version<79 && (cell==DUNGEON_CHEST_CELL || dungeon_spawner_cell(cell))) return 0;
            if (version<79 && (cell==COBWEB || mine_cart_track(cell))) return 0;
            if (version<79 && (cell==DESERT_PYRAMID_CHEST_CELL || stained_clay_cell(cell))) return 0;
            if (version<60 && cell==BROWN_MUSHROOM) return 0;
            if (version<61 && (cell==RED_MUSHROOM || cell==BROWN_MUSHROOM_BLOCK)) return 0;
            if (version<14 && (is_bed_cell(cell) || cell==WOOL)) return 0;
            if (version<15 && (is_farm_cell(cell) || cell==TALL_GRASS || cell==TORCH)) return 0;
            if (version<17 && (fence_type(cell) || gate_cell(cell))) return 0;
            if (version<19 && (brewing_block(cell) || filter_mat_cell(cell))) return 0;
            if (version<23 && book_block(cell)) return 0;
            if (version<24 && (cell==IRON_BLOCK || anvil_cell(cell))) return 0;
            if (version<26 && circuit_cell(cell)) return 0;
            if (version<27 && timed_circuit_cell(cell)) return 0;
            if (version<28 && lamp_cell(cell)) return 0;
            if (version<29 && comparator_cell(cell)) return 0;
            if (version<30 && pressure_cell(cell)) return 0;
            if (hopper_cell(cell) && (version<32 || ++hopper_blocks>HOPPER_LIMIT)) return 0;
            if (ejector_cell(cell) && (version<(dispenser_cell(cell) ? 34u : 33u) ||
                ++dropper_blocks>DROPPER_LIMIT)) return 0;
            if (cell==TNT_BLOCK && version<35) return 0;
            if (cell==FIRE_CELL && (version<36 || ++fires>FIRE_FILE_LIMIT)) return 0;
            if (cell==SNOW_LAYER && version<37) return 0;
            if (lava_flow_cell(cell) && version<39) return 0;
            if (door_cell(cell)) {
                if (version<41) return 0;
                has_door=1;
            }
            if (piston_cell(cell)) {
                if (version<38) return 0;
                has_piston=1;
            }
            if (wall_attachment_dir(cell)>=0) has_wall_attachment=1;
            if (!world_restore_edit(x,y,z,cell) || (version>=26 && !redstone_restore(x,y,z,cell))) return 0;
        } else if (tag == 2) {
            if (!water_restore_pending(x, y, z)) return 0;
        } else if (tag==3) {
            container_phase=3;
            if (!read_furnace(fp, version, x, y, z)) return 0;
        } else if (tag==4) {
            container_phase=4;
            if (!read_chest(fp,version,x,y,z)) return 0;
        } else if (tag==5) {
            container_phase=5;
            if (!read_brewing(fp,version,x,y,z)) return 0;
        } else if (tag==6) {
            container_phase=6;
            if (++hopper_records>hopper_blocks || !read_hopper(fp,version,x,y,z)) return 0;
        } else if (tag==7) {
            container_phase=7;
            if (++dropper_records>dropper_blocks || !read_dropper(fp,version,x,y,z)) return 0;
        } else if (tag==8) {
            container_phase=8;
            if (++fire_records>fires || !read_fire_cell(fp,x,y,z)) return 0;
        } else if (tag==9) {
            container_phase=9;
            if (!lava_restore(x,y,z,fgetc(fp))) return 0;
        } else {
            container_phase=10;
            if (!read_villager(fp,version,x,y,z)) return 0;
        }
    }
    return hopper_records==hopper_blocks && dropper_records==dropper_blocks && fire_records==fires &&
        (!has_piston || world_visit_edits(validate_piston,NULL)) &&
        (!has_door || world_visit_edits(validate_door,NULL)) &&
        (!has_wall_attachment || world_visit_edits(validate_wall_attachment,NULL)) &&
        (version>=25 || world_visit_edits(gravity_seed_legacy_edit,NULL));
}

static int read_actors(FILE *fp, Dimension dim, unsigned version) {
    const float lim = WORLD_SAVE_LIMIT;
    int count = fgetc(fp);
    if (count < 0 || count > ACTOR_CAPACITY) return 0;
    for (int i = 0; i < count; ++i) {
        ActorRecord a;
        int type = fgetc(fp), health = fgetc(fp);
        if (type < 1 || type >= ACTOR_TYPES ||
            (version<87 && type==ACTOR_SEEP_BEETLE) ||
            (version<82 && type==ACTOR_KILN_MOLE) ||
            (version<81 && type==ACTOR_LANTERN_MOTH) ||
            (version<79 && type==ACTOR_CAVE_SPIDER) ||
            (version<76 && type==ACTOR_FAULTBACK) ||
            (version<60 && (type==ACTOR_VILLAGER || type==ACTOR_STRANDED_GUIDE)) ||
            (version<61 && type==ACTOR_TIDE_CRAB) ||
            (type==ACTOR_SKELETON && version<15) ||
            (type==ACTOR_COW && version<16) || (type==ACTOR_SILTBACK && version<18) ||
            (type==ACTOR_ARCHIVE_MAGMA && (version<19 || version>=110)) ||
            (type==ACTOR_SHELLBACK && version<35) ||
            (type==ACTOR_IRON_GOLEM && version<46) || (type==ACTOR_SNOW_GOLEM && version<47) ||
            (type==ACTOR_WOLF && version<48) || (type==ACTOR_OCELOT && version<49) ||
            (type==ACTOR_SQUID && version<50) || health<1 ||
            !get_float(fp,&a.feet.x,-lim,lim) || !get_float(fp,&a.feet.y,0,WY) ||
            !get_float(fp,&a.feet.z,-lim,lim) || !get_float(fp,&a.heading,-3.142f,3.142f)) return 0;
        /* Slot 20 was a bovine variant before v107. Keep the record in place
           and parse its original livestock care/lead data before restoring. */
        if (version<107 && type==ACTOR_TIDE_CRAB) type=ACTOR_COW;
        int traits=0;
        if (type==ACTOR_FAULTBACK && version<97) {
            /* Keep damaged health proportional and retain the ordinal so
               the saved creature's name still maps to the same record. */
            if (health>20) return 0;
            health=(health*FAULTBACK_HEALTH+19)/20;
        }
        if (type==ACTOR_SEEP_BEETLE && version<95) {
            if (health>8) return 0;
            health=(health*SEEP_BEETLE_HEALTH+7)/8;
        }
        if (type==ACTOR_TIDERAY && version<93) {
            /* The retired creature's ordinal and saved names still decode. */
            if (health>40) return 0;
            health=(health*TIDERAY_HEALTH+39)/40;
        }
        if (type==ACTOR_KILN_MOLE) {
            /* Keep the saved ordinal so names follow the recovered actor.
               The pre-v108 creature's age and combat timer are retired. */
            int former_traits=fgetc(fp);
            uint16_t former_timer=0;
            if (former_traits<0 || !get_short(fp,&former_timer)) return 0;
            if (version<108) {
                if (former_traits>1 || former_timer>799 || health>20) return 0;
                health=(health*KILN_MOLE_HEALTH+19)/20;
            } else if (former_traits || former_timer) return 0;
        }
        if (type==ACTOR_SHEEP && (version<14 || (traits=fgetc(fp))<0 || traits>SHEEP_SHORN)) return 0;
        uint16_t reed_cooldown=0;
        if (type==ACTOR_REEDLIZARD && version>=102 &&
            ((traits=fgetc(fp))<0 || traits>REED_HARVESTED || !get_short(fp,&reed_cooldown) ||
                reed_cooldown>REED_COOL_TICKS)) return 0;
        if (version<110 && (type==ACTOR_SILTBACK || type==ACTOR_ARCHIVE_MAGMA)) {
            const int size=fgetc(fp);
            if (size<1 || size>3 ||
                (type==ACTOR_SILTBACK ? dim!=OVERWORLD : dim!=NETHER)) return 0;
            const int old_life[]={1,4,16};
            const int capacity=old_life[size-1];
            if (health>capacity) return 0;
            if (type==ACTOR_ARCHIVE_MAGMA) type=ACTOR_REEDLIZARD;
            const int life=type==ACTOR_SILTBACK ? SILTBACK_HEALTH : REED_HEALTH;
            health=(health*life+capacity-1)/capacity;
            traits=0;
        }
        if (type==ACTOR_SHELLBACK) {
            if (version>=37 && ((traits=fgetc(fp))<0 || traits>SHELLBACK_RIPE)) return 0;
            if (version<92) {
                /* Same ordinal preserves the relation to saved names and
                   slots. The old electrical charge is never carried over. */
                if (health>20) return 0;
                health=(health*SHELLBACK_HEALTH+19)/20;
                traits=0;
            }
        }
        if (type==ACTOR_IRON_GOLEM && ((traits=fgetc(fp))<0 || traits>GOLEM_PLAYER_BUILT)) return 0;
        if (type==ACTOR_WOLF && ((traits=fgetc(fp))<0 || traits>(WOLF_TAMED|WOLF_SITTING))) return 0;
        if (type==ACTOR_OCELOT && ((traits=fgetc(fp))<0 || traits>(CAT_COAT_MASK|PET_TAMED|PET_SITTING))) return 0;
        ActorArmorState armor={0};
        const int retired_guide_record=type==ACTOR_STRANDED_GUIDE && version<113;
        if (version>=20 && (actors_can_wear_armor(type) || retired_guide_record)) {
            int debt;
            if ((traits=fgetc(fp))<0 || !armor_kit_valid((unsigned)traits) ||
                !get_short(fp,&armor.helmet_wear) || (debt=fgetc(fp))<0) return 0;
            if (version>=59 && (actors_is_zombie(type) || retired_guide_record)) {
                armor.door_breaker=!!(debt&ZOMBIE_DOOR_FLAG);
                debt&=~ZOMBIE_DOOR_FLAG;
            }
            if (debt>=HEALTH_UNITS) return 0;
            armor.health_debt=(uint8_t)debt;
        }
        a.type=(uint8_t)type; a.health=(uint8_t)health; a.traits=(uint8_t)traits;
        if (type==ACTOR_VILLAGER) {
            VillagerSave market;
            if (!read_actor_market(fp,&market)) return 0;
            a.traits=market.home|ROAMING_VILLAGER;
            if (!actors_restore_roaming(dim,a,market)) return 0;
            continue;
        }
        if (type==ACTOR_STRANDED_GUIDE) {
            StrandedGuideState state={0};
            int linked;
            if (retired_guide_record) {
                uint16_t discarded_effect,discarded_timer;
                if (health>20 || !get_short(fp,&discarded_effect) ||
                    !get_short(fp,&discarded_timer) || discarded_effect>WEAKNESS_LONG_TICKS ||
                    discarded_timer>6000 || (discarded_effect && discarded_timer)) return 0;
                /* Preserve identity and market in place; old armor and
                   effects are not part of the new recovery route. */
                a.traits=0;
                a.health=STRANDED_HEALTH;
            } else if (!get_short(fp,&state.rest_ticks)) return 0;
            if ((linked=fgetc(fp))<0 || linked>1 || !read_actor_market(fp,&state.origin.market)) return 0;
            state.origin.linked=(uint8_t)linked;
            if (!actors_restore_stranded(dim,a,state)) return 0;
            continue;
        }
        ActorCare care={0,0,0,0,0};
        LeadLink lead={LEAD_FREE,0,0,0};
        if (version>=16 && actors_has_care(type)) {
            int managed=fgetc(fp);
            if (managed<0 || managed>1 || !get_short(fp,&care.grow) || !get_short(fp,&care.cooldown) ||
                !get_short(fp,&care.love) || !get_short(fp,&care.egg)) return 0;
            care.managed=(uint8_t)managed;
            if (version>=18 && !read_lead(fp,&lead)) return 0;
        }
        if (!actors_restore_snapshot(dim,a,care,lead,armor)) return 0;
        if (type==ACTOR_REEDLIZARD && !actors_restore_reed_cooldown(dim,i,reed_cooldown)) return 0;
    }
    return 1;
}

static int read_boats(FILE *fp) {
    for (int d=0;d<DIMENSION_COUNT;++d) {
        int count=fgetc(fp);
        if (count<0 || count>BOAT_CAPACITY) return 0;
        for (int i=0;i<count;++i) {
            BoatRecord a={{0,0,0},0,1};
            if (!get_float(fp,&a.position.x,-WORLD_SAVE_LIMIT,WORLD_SAVE_LIMIT) ||
                !get_float(fp,&a.position.y,0,WY) ||
                !get_float(fp,&a.position.z,-WORLD_SAVE_LIMIT,WORLD_SAVE_LIMIT) ||
                !get_float(fp,&a.heading,-3.142f,3.142f) || !boat_restore((Dimension)d,a)) return 0;
        }
    }
    int rider=fgetc(fp);
    return rider>=0 && boat_restore_rider(rider-1);
}

static int read_carts(FILE *fp,unsigned version) {
    for (int d=0;d<DIMENSION_COUNT;++d) {
        int count=fgetc(fp);
        if (count<0 || count>CART_CAPACITY) return 0;
        for (int i=0;i<count;++i) {
            CartRecord a;
            uint32_t x,z;
            memset(&a,0,sizeof a);
            a.occupied=1;
            float speed_limit=version>=74 ? BLAST_CART_SPEED_LIMIT : 8.1f;
            if (!get_float(fp,&a.position.x,-WORLD_SAVE_LIMIT,WORLD_SAVE_LIMIT) ||
                !get_float(fp,&a.position.y,0,WY) ||
                !get_float(fp,&a.position.z,-WORLD_SAVE_LIMIT,WORLD_SAVE_LIMIT) ||
                !get_float(fp,&a.heading,-3.142f,3.142f) ||
                !get_float(fp,&a.along,0,1) || !get_float(fp,&a.speed,-speed_limit,speed_limit) ||
                !get_u32(fp,&x) || (a.track.y=fgetc(fp))<0 || !get_u32(fp,&z) ||
                !signed_coord(x,&a.track.x) || !signed_coord(z,&a.track.z)) return 0;
            if (version>=69) {
                int kind=fgetc(fp),fuse=fgetc(fp);
                if (kind<0 || fuse<0 || (version<74 && fuse>CART_TNT_IMPACT_PENDING) ||
                    (version<70 && kind==CART_CHEST) ||
                    (version<71 && kind==CART_HOPPER) || (version<72 && kind==CART_FURNACE)) return 0;
                a.kind=(uint8_t)kind; a.fuse=(uint8_t)fuse;
            }
            if (version>=73) {
                int mode=fgetc(fp);
                if ((unsigned)mode>1) return 0;
                a.off_track=(uint8_t)mode;
                if (mode && (!get_float(fp,&a.free_velocity.x,-CART_FREE_HORIZONTAL_LIMIT,CART_FREE_HORIZONTAL_LIMIT) ||
                    !get_float(fp,&a.free_velocity.y,-CART_FREE_VERTICAL_LIMIT,CART_FREE_VERTICAL_LIMIT) ||
                    !get_float(fp,&a.free_velocity.z,-CART_FREE_HORIZONTAL_LIMIT,CART_FREE_HORIZONTAL_LIMIT))) return 0;
                if (version>=74 && mode && a.kind==CART_TNT && !get_float(fp,&a.fall_distance,0,WY)) return 0;
            }
            if (!cart_restore((Dimension)d,a)) return 0;
            int slots=cart_cargo_count((Dimension)d,i);
            if (slots) {
                ItemStack items[CART_STORAGE_SLOTS];
                for (int j=0;j<slots;++j) if (!get_stack(fp,version,0,&items[j])) return 0;
                if (!cart_restore_cargo((Dimension)d,i,items,slots)) return 0;
            }
            if (a.kind==CART_HOPPER && !cart_restore_hopper_cooldown((Dimension)d,i,fgetc(fp))) return 0;
            if (a.kind==CART_FURNACE) {
                CartFurnaceDrive drive;
                if (!get_short(fp,&drive.fuel) || !get_float(fp,&drive.x,-1.f,1.f) ||
                    !get_float(fp,&drive.z,-1.f,1.f) || !cart_restore_furnace_drive((Dimension)d,i,drive)) return 0;
            }
        }
    }
    int rider=fgetc(fp);
    return rider>=0 && cart_restore_rider(rider-1);
}
static int read_horses(FILE *fp,unsigned version) {
    for (int d=0;d<DIMENSION_COUNT;++d) {
        int count=fgetc(fp);
        if (count<0 || count>HORSE_CAPACITY) return 0;
        for (int i=0;i<count;++i) {
            HorseRecord h={0};
            uint8_t bytes[9];
            if (fread(bytes,1,sizeof bytes,fp)!=sizeof bytes ||
                !get_float(fp,&h.feet.x,-WORLD_SAVE_LIMIT,WORLD_SAVE_LIMIT) ||
                !get_float(fp,&h.feet.y,0,WY+6) ||
                !get_float(fp,&h.feet.z,-WORLD_SAVE_LIMIT,WORLD_SAVE_LIMIT) ||
                !get_float(fp,&h.heading,-3.142f,3.142f) ||
                !get_short(fp,&h.age) || !get_short(fp,&h.cooldown) || !get_short(fp,&h.love)) return 0;
            h.type=bytes[0]; h.coat=bytes[1]; h.max_health=bytes[2]; h.health=bytes[3];
            h.gait=bytes[4]; h.spring=bytes[5]; h.temper=bytes[6]; h.tamed=bytes[7]; h.saddle=bytes[8];
            LeadLink lead={LEAD_FREE,0,0,0};
            if ((version>=18 && !read_lead(fp,&lead)) || !horse_restore_with_lead((Dimension)d,h,lead)) return 0;
        }
    }
    int rider=fgetc(fp);
    return rider>=0 && horse_restore_rider(rider-1);
}

static int read_horse_gear(FILE *fp,unsigned version) {
    for (int d=0;d<DIMENSION_COUNT;++d) {
        int count=fgetc(fp),last=-1;
        if (count<0 || count>HORSE_CAPACITY) return 0;
        for (int i=0;i<count;++i) {
            int ordinal=fgetc(fp),debt=fgetc(fp);
            ItemStack armor;
            if (ordinal<=last || ordinal>=HORSE_CAPACITY || debt<0 || debt>=HEALTH_UNITS ||
                !get_stack(fp,version,0,&armor) || (!armor.count && !debt) ||
                !horse_restore_equipment((Dimension)d,ordinal,armor,(unsigned)debt)) return 0;
            last=ordinal;
        }
    }
    return 1;
}

static int read_drop_timers(FILE *fp,Drop *drop) {
    return get_short(fp,&drop->age) && get_short(fp,&drop->pickup_delay) && drop_valid(*drop);
}
static int read_drop_state(FILE *fp,unsigned version) {
    if (!death_set_keep_inventory(fgetc(fp))) return 0;
    for (int d=0;d<DIMENSION_COUNT;++d) {
        for (int i=0;i<MAXDROP;++i) {
            Drop *drop=&dimension_drops[d][i];
            if (drop->item.count && !read_drop_timers(fp,drop)) return 0;
        }
        uint32_t n;
        if (!get_u32(fp,&n) || n>DEATH_DROP_FILE_LIMIT) return 0;
        for (uint32_t i=0;i<n;++i) {
            Drop drop={0,0,0,{0,0,0,0,0},0,0};
            if (!get_float(fp,&drop.x,-WORLD_SAVE_LIMIT,WORLD_SAVE_LIMIT) ||
                !get_float(fp,&drop.y,0,WY) || !get_float(fp,&drop.z,-WORLD_SAVE_LIMIT,WORLD_SAVE_LIMIT) ||
                !get_stack(fp,version,1,&drop.item) || !read_drop_timers(fp,&drop) ||
                !death_drop_restore((Dimension)d,drop)) return 0;
        }
    }
    return 1;
}
static int read_experience(FILE *fp,GameMode mode) {
    uint32_t points,rng;
    if (!get_u32(fp,&points) || !get_u32(fp,&rng) || (mode==CREATIVE && points) ||
        !experience_restore_player(points,rng)) return 0;
    for (int d=0;d<DIMENSION_COUNT;++d) {
        uint32_t n;
        if (!get_u32(fp,&n) || n>XP_ORB_LIMIT || (mode==CREATIVE && n)) return 0;
        for (uint32_t i=0;i<n;++i) {
            XPOrb orb;
            if (!get_float(fp,&orb.point.x,-WORLD_SAVE_LIMIT,WORLD_SAVE_LIMIT) ||
                !get_float(fp,&orb.point.y,0,WY+40) ||
                !get_float(fp,&orb.point.z,-WORLD_SAVE_LIMIT,WORLD_SAVE_LIMIT) ||
                !get_float(fp,&orb.velocity.x,-24,24) || !get_float(fp,&orb.velocity.y,-24,24) ||
                !get_float(fp,&orb.velocity.z,-24,24) || !get_u32(fp,&orb.value) ||
                !get_short(fp,&orb.age) || !experience_restore_orb((Dimension)d,orb)) return 0;
        }
    }
    return 1;
}
static int read_fall_check(FILE *fp,FallCheck *p) {
    uint32_t x,z;
    return get_u32(fp,&x) && (p->y=fgetc(fp))>=0 && p->y<WY && get_u32(fp,&z) &&
        signed_coord(x,&p->x) && signed_coord(z,&p->z);
}
static int read_gravity(FILE *fp,unsigned format) {
    for (int d=0;d<DIMENSION_COUNT;++d) {
        uint32_t count;
        if (!get_u32(fp,&count) || count>GRAVITY_FILE_LIMIT) return 0;
        for (uint32_t i=0;i<count;++i) {
            FallCheck p;
            if (!read_fall_check(fp,&p) || !gravity_restore_pending((Dimension)d,p)) return 0;
        }
        if (!get_u32(fp,&count) || count>GRAVITY_FILE_LIMIT) return 0;
        for (uint32_t i=0;i<count;++i) {
            FallCheck p;
            float y,speed;
            uint16_t raw;
            if (!read_fall_check(fp,&p)) return 0;
            if (format>=56) {
                if (!get_short(fp,&raw)) return 0;
            } else {
                int low=fgetc(fp);
                if (low<0) return 0;
                raw=(uint16_t)low;
            }
            if (raw==ARCHIVE_REWARD_CELL) raw=GRAVEL;
            if (!get_float(fp,&y,0.f,(float)p.y) || !get_float(fp,&speed,0.f,2.f) ||
                !gravity_restore_falling((Dimension)d,(FallingBlock){p.x,p.z,p.y,y,speed,raw})) return 0;
        }
    }
    return 1;
}
static int read_redstone_timing(FILE *fp,unsigned version) {
    for (int d=0;d<DIMENSION_COUNT;++d) {
        uint32_t count;
        if (!get_u32(fp,&count) || count>REDSTONE_TIMING_FILE_LIMIT) return 0;
        for (uint32_t i=0;i<count;++i) {
            FallCheck p;
            unsigned char fields[4];
            if (!read_fall_check(fp,&p) || fread(fields,1,sizeof fields,fp)!=sizeof fields || fields[3]>8) return 0;
            int output=version>=29 ? fgetc(fp) : 0;
            if (output<0 || output>15) return 0;
            RedstoneTiming t={{p.x,p.y,p.z},fields[0],fields[1],fields[2],fields[3],{0},(uint8_t)output};
            if (fread(t.ages,1,t.count,fp)!=t.count || !redstone_restore_timing((Dimension)d,t)) return 0;
        }
    }
    return 1;
}
static int read_button_arrows(FILE *fp) {
    const float lim=WORLD_SAVE_LIMIT+1.f;
    for (int d=0;d<DIMENSION_COUNT;++d) {
        int count=fgetc(fp);
        if (count<0 || count>BUTTON_ARROW_LIMIT) return 0;
        for (int i=0;i<count;++i) {
            FallCheck p;
            ButtonArrow a;
            int recoverable;
            if (!read_fall_check(fp,&p) || !get_short(fp,&a.raw) || !get_short(fp,&a.ticks) ||
                (recoverable=fgetc(fp))<0 || recoverable>1 ||
                !get_float(fp,&a.point.x,-lim,lim) || !get_float(fp,&a.point.y,0,WY) ||
                !get_float(fp,&a.point.z,-lim,lim) || !get_float(fp,&a.direction.x,-1,1) ||
                !get_float(fp,&a.direction.y,-1,1) || !get_float(fp,&a.direction.z,-1,1)) return 0;
            a.x=p.x; a.y=p.y; a.z=p.z; a.recoverable=(uint8_t)recoverable;
            if (!button_arrow_restore((Dimension)d,a)) return 0;
        }
    }
    return 1;
}
static int read_blasts(FILE *fp,unsigned version) {
    const float lim=WORLD_SAVE_LIMIT;
    for (int d=0;d<DIMENSION_COUNT;++d) {
        uint16_t n;
        if (!get_short(fp,&n) || n>BLAST_CAP) return 0;
        for (unsigned i=0;i<n;++i) {
            PrimedBlast a={0};
            int kind=fgetc(fp);
            float cap=(kind==BLAST_CART_TRACK || kind==BLAST_CART_IMPACT) ?
                (version>=74 ? BLAST_CART_SPEED_LIMIT : 8.1f) : 16.f;
            if (kind<0 || (version<69 && kind>BLAST_ARCHIVE_LARGE) ||
                (version<81 && kind==BLAST_ARCHIVE_VENT) ||
                (version<77 && kind>BLAST_CART_IMPACT) ||
                (version<37 && kind==BLAST_ARCHIVE_LARGE) || !get_short(fp,&a.ticks) ||
                !get_float(fp,&a.point.x,-lim,lim) || !get_float(fp,&a.point.y,0,BLAST_TOP) ||
                !get_float(fp,&a.point.z,-lim,lim) || !get_float(fp,&a.speed.x,-16,cap) ||
                !get_float(fp,&a.speed.y,-20,20) || !get_float(fp,&a.speed.z,-16,16)) return 0;
            if (kind==BLAST_ARCHIVE_SMALL || kind==BLAST_ARCHIVE_LARGE) {
                if (version>=92 || a.ticks>1) return 0;
                continue;
            }
            if (kind>=BLAST_ARCHIVE_BOSS_FIRST && kind<=BLAST_ARCHIVE_BOSS_LAST) {
                if (version>=94 || a.ticks || a.speed.x || a.speed.y || a.speed.z) return 0;
                continue;
            }
            if (kind==BLAST_ARCHIVE_VENT) {
                if (version>=103 || d!=NETHER || a.ticks || a.speed.x || a.speed.y || a.speed.z) return 0;
                continue;
            }
            a.kind=(uint8_t)kind;
            if (!blast_restore((Dimension)d,a)) return 0;
        }
    }
    return 1;
}
static int read_ender_chest(FILE *fp,unsigned version) {
    ItemStack saved[CHEST_SLOTS]={0};
    int count=fgetc(fp),previous=-1;
    if ((unsigned)count>CHEST_SLOTS) return 0;
    for (int i=0;i<count;++i) {
        int slot=fgetc(fp);
        if (slot<=previous || slot>=CHEST_SLOTS || !get_stack(fp,version,1,&saved[slot])) return 0;
        previous=slot;
    }
    return ender_chest_restore(saved);
}
static int get_vec(FILE *fp,Vec3 *p,float xz,float y0,float y1) {
    return get_float(fp,&p->x,-xz,xz) && get_float(fp,&p->y,y0,y1) && get_float(fp,&p->z,-xz,xz);
}
static int read_retired_encounters(FILE *fp,unsigned format) {
    for (int d=0;d<DIMENSION_COUNT;++d) {
        int count=fgetc(fp);
        if ((unsigned)count>(format<94 ? 4u : 0u)) return 0;
        for (int i=0;i<count;++i) {
            Vec3 point;
            float direction;
            uint16_t health,charge,attack,retaliation;
            uint32_t ticks;
            if (!get_vec(fp,&point,WORLD_SAVE_LIMIT,0,WY) || !get_float(fp,&direction,-3.142f,3.142f) ||
                !get_short(fp,&health) || !get_short(fp,&charge) || !get_short(fp,&attack) ||
                !get_short(fp,&retaliation) || !get_u32(fp,&ticks) || health>300 || charge>220 ||
                attack>40 || retaliation>20 || (!health && (charge || attack || retaliation)) ||
                (charge && (health<100 || attack || retaliation))) return 0;
        }
        count=fgetc(fp);
        if ((unsigned)count>(format<94 ? 24u : 0u)) return 0;
        for (int i=0;i<count;++i) {
            Vec3 point,velocity;
            uint16_t ticks;
            int flags;
            if (!get_vec(fp,&point,WORLD_SAVE_LIMIT,0,WY) || !get_vec(fp,&velocity,22.f,-22.f,22.f) ||
                !get_short(fp,&ticks) || !ticks || ticks>100 || (flags=fgetc(fp))<0 || flags>3 ||
                velocity.x*velocity.x+velocity.y*velocity.y+velocity.z*velocity.z>484.f) return 0;
        }
    }
    if (format<78) return 1;
    int count=fgetc(fp),last=-1;
    if ((unsigned)count>(format<94 ? 6u : 0u) || (count && !player_rules.health)) return 0;
    for (int i=0;i<count;++i) {
        int code=fgetc(fp),ticks=fgetc(fp),power=code<0 ? -1 : code>>2;
        if (power<=last || power>=6 || ticks<1 || ticks>180 || !(code&3) ||
            (code&3)>(power==5 ? 1 : 2)) return 0;
        last=power;
    }
    return 1;
}
static int read_dungeon_clocks(FILE *fp) {
    uint16_t count;
    if (!get_short(fp,&count) || count>DUNGEON_CLOCKS || (count && world_generation()<GENERATOR_DUNGEONS)) return 0;
    for (int i=0;i<count;++i) {
        DungeonClock clock={0};
        uint32_t x,z;
        if (fgetc(fp)!=1 || !get_u32(fp,&x) || (clock.y=fgetc(fp))<0 || !get_u32(fp,&z) ||
            !signed_coord(x,&clock.x) || !signed_coord(z,&clock.z) ||
            !get_short(fp,&clock.delay) || !get_u32(fp,&clock.random) || !dungeon_restore_clock(clock)) return 0;
    }
    return 1;
}
static int read_player_burn(FILE *fp) {
    PlayerBurn b={0};
    if (!get_short(fp,&b.remaining)) return 0;
    int due=fgetc(fp);
    if (due<0) return 0;
    b.due=(uint8_t)due;
    return player_restore_burn(b);
}
static int read_weather(FILE *fp) {
    WeatherSave w={0};
    if (!get_u32(fp,&w.random) || !get_u32(fp,&w.remaining) || !get_short(fp,&w.strike) || !get_short(fp,&w.cover)) return 0;
    int kind=fgetc(fp);
    if (kind<0) return 0;
    w.kind=(uint8_t)kind;
    return weather_restore(w);
}
static int read_leaf_work(FILE *fp) {
    for (int d=0;d<DIMENSION_COUNT;++d) {
        uint32_t count;
        if (!get_u32(fp,&count) || count>LEAF_WORK_LIMIT) return 0;
        for (uint32_t i=0;i<count;++i) {
            FallCheck pos;
            LeafWork w={0};
            uint16_t cursor;
            if (!read_fall_check(fp,&pos) || !get_short(fp,&cursor)) return 0;
            int delay=fgetc(fp);
            if (delay<0) return 0;
            w.x=pos.x; w.y=pos.y; w.z=pos.z; w.cursor=cursor; w.delay=(unsigned)delay;
            for (int l=0;l<LEAF_TILE;++l) {
                uint32_t lo,hi;
                if (!get_u32(fp,&lo) || !get_u32(fp,&hi)) return 0;
                w.layers[l]=((uint64_t)hi<<32)|lo;
            }
            if (!leaves_restore((Dimension)d,&w)) return 0;
        }
    }
    return 1;
}
static int read_mob_names(FILE *fp,unsigned version) {
    for (int horse=0;horse<2;++horse) for (int dim=0;dim<DIMENSION_COUNT;++dim) {
        Dimension d=(Dimension)dim;
        int cap=horse ? HORSE_CAPACITY : ACTOR_CAPACITY,count=fgetc(fp),previous=-1;
        if (count<0 || count>cap) return 0;
        for (int i=0;i<count;++i) {
            int slot=fgetc(fp);
            uint16_t id;
            if (slot<=previous || slot>=cap || !read_entity_name(fp,version,&id) || !id ||
                !(horse ? horse_set_name(d,slot,id) : actors_set_name(d,slot,id))) return 0;
            previous=slot;
        }
    }
    return 1;
}
static int read_regeneration(FILE *fp) {
    uint16_t flag;
    if (!get_short(fp,&flag)) return 0;
    unsigned duration=flag&~REGEN_II_FLAG;
    int strong=!!(flag&REGEN_II_FLAG);
    if (!valid_regeneration(duration,strong) || (duration && (!player_rules.health || player_rules.mode!=SURVIVAL)))
        return 0;
    player_effects.regeneration=(uint16_t)duration; player_effects.regeneration_ii=(uint8_t)strong;
    return 1;
}
static int read_water_breathing(FILE *fp) {
    uint16_t duration;
    if (!get_short(fp,&duration) || duration>WATER_BREATHING_LONG_TICKS ||
        (duration && !player_rules.health)) return 0;
    player_effects.water_breathing=duration;
    return 1;
}
static int read_field_supplies(FILE *fp) {
    int water=fgetc(fp),heat=fgetc(fp);
    uint16_t drain;
    return water>=0 && heat>=0 && get_short(fp,&drain) &&
        field_restore((FieldSave){(uint8_t)water,(uint8_t)heat,drain});
}

static int read_night_vision(FILE *fp) {
    uint16_t duration;
    if (!get_short(fp,&duration) || duration>NIGHT_VISION_LONG_TICKS ||
        (duration && !player_rules.health)) return 0;
    player_effects.night_vision=duration;
    return 1;
}
static int restore(FILE *fp, const SaveInfo *info) {
    if (!world_begin_layout(info->seed,info->generation,info->profile,info->structures_disabled,
        info->profile==WORLD_SUPERFLAT ? &info->flat : NULL)) return 0;
    generate();
    const float lim = WORLD_SAVE_LIMIT;
    if (!get_float(fp, &px, -lim, lim) || !get_float(fp, &py, 0, WY + 12) ||
        !get_float(fp, &pz, -lim, lim) || !get_float(fp, &yaw, -4, 4) ||
        !get_float(fp, &pitch, -1.46f, 1.46f) || !get_float(fp, &vy, -21, 8)) return 0;
    selected_slot = fgetc(fp);
    if (selected_slot < 0 || selected_slot >= HOTBAR_SIZE) return 0;
    player_reset_rules(info->mode);
    player_rules.hardcore=info->hardcore;
    if (info->format >= 2) {
        uint32_t tick;
        int health = fgetc(fp), food = fgetc(fp), air = fgetc(fp), flying = fgetc(fp);
        if (health < 0 || health > MAX_HEALTH || food < 0 || food > MAX_FOOD ||
            air < 0 || air > MAX_AIR || flying < 0 || flying > (info->mode == CREATIVE) ||
            (info->mode == CREATIVE && health != MAX_HEALTH) ||
            (info->hardcore && info->hardcore_dead!=(health==0)) ||
            !get_u32(fp, &tick) || tick >= DAY_TICKS ||
            !get_float(fp, &player_rules.exhaustion, 0, 10000) ||
            !get_float(fp, &player_rules.submerged_time, 0, .75f) ||
            !get_float(fp, &player_rules.hurt_time, 0, 4.f) ||
            !get_float(fp, &player_rules.fall_origin, 0, WY + 12)) return 0;
        player_rules.health = health;
        player_rules.food = food;
        player_rules.air = air;
        player_rules.flying = flying;
        player_rules.day_tick = tick;
    }
    if (info->has_maps && !read_maps(fp)) return 0;
    for (int i = 0; i < SLOT_COUNT; i++)
        if (!get_stack(fp, info->format, 0, &inventory[i])) return 0;
    if (!read_dimension(fp,info->format)) return 0;
    if (info->format >= 6) {
        for (int d = 1; d < DIMENSION_COUNT; ++d)
            if (!world_select_dimension((Dimension)d) || !read_dimension(fp,info->format)) return 0;
        EndReturn p = {0,{0,0,0}};
        p.valid = fgetc(fp);
        if (p.valid != 0 && p.valid != 1) return 0;
        if (p.valid && (!get_float(fp,&p.position.x,-lim,lim) || !get_float(fp,&p.position.y,0,WY) ||
            !get_float(fp,&p.position.z,-lim,lim))) return 0;
        portals_set_end_return(p);
        if (!world_select_dimension(info->dimension)) return 0;
    }
    if (info->format >= 7)
        for (int d=0;d<DIMENSION_COUNT;++d) if (!read_actors(fp,(Dimension)d,info->format)) return 0;
    if (info->format >= 8) {
        uint32_t health, mask;
        if (!get_u32(fp,&health) || !get_u32(fp,&mask)) return 0;
        if (uses_aquifer()) {
            if (health || !portals_restore_aquifer(mask)) return 0;
        } else {
            /* Validate the historical layout, not the retired gameplay. */
            enum { OLD_ENCOUNTER_MAX=200, OLD_STATE_MASK=1023, OLD_REWARD=1024,
                OLD_RETURN_PENDING=2048, OLD_RETURN_SEEN=4096 };
            unsigned allowed=OLD_STATE_MASK | (info->format>=56 ? OLD_REWARD : 0) |
                (info->format>=57 ? OLD_RETURN_PENDING|OLD_RETURN_SEEN : 0);
            unsigned returned=mask&(OLD_RETURN_PENDING|OLD_RETURN_SEEN);
            if (health>OLD_ENCOUNTER_MAX || (mask&~allowed) || ((mask&OLD_REWARD) && health) ||
                returned==(OLD_RETURN_PENDING|OLD_RETURN_SEEN) ||
                (returned && (health || !portals_end_return().valid)) ||
                ((mask&OLD_RETURN_PENDING) && info->dimension!=OVERWORLD)) return 0;
        }
    }
    if (info->format >= 9 && !read_boats(fp)) return 0;
    if (info->format >= 10 && !read_carts(fp,info->format)) return 0;
    if (info->format >= 11 && !read_horses(fp,info->format)) return 0;
    if (info->format >= 12) {
        uint16_t poison;
        if (!get_short(fp,&poison) || !get_short(fp,&player_effects.hunger) ||
            player_effects.hunger>(info->format<16 ? PUFFER_HUNGER_TICKS : CHICKEN_HUNGER_TICKS) ||
            (info->mode==CREATIVE && (poison || player_effects.hunger))) return 0;
        if (info->format>=79 && (poison&POISON_SLOW_FLAG)) {
            poison&=~POISON_SLOW_FLAG;
            if (!poison || poison>CAVE_POISON_HARD) return 0;
            player_effects.poison_slow=1;
        } else if (poison>PUFFER_POISON_TICKS) return 0;
        player_effects.poison=poison;
    }
    if (info->format>=14) {
        BedAnchor a={0,{0,0,0}};
        a.valid=fgetc(fp);
        if (a.valid==1) {
            uint32_t ux,uz;
            if (!get_u32(fp,&ux) || (a.head.y=fgetc(fp))<0 || !get_u32(fp,&uz) ||
                !signed_coord(ux,&a.head.x) || !signed_coord(uz,&a.head.z)) return 0;
        }
        if (!bed_restore_anchor(a)) return 0;
    }
    if (info->format>=19 && (!get_short(fp,&player_effects.fire) || player_effects.fire>FIRE_POTION_LONG_TICKS ||
        (info->mode==CREATIVE && player_effects.fire))) return 0;
    if (info->format>=20) {
        int debt=fgetc(fp);
        if (info->format>=60 && debt>=0 && (debt&PLAYER_WEAKNESS_FLAG)) {
            debt&=~PLAYER_WEAKNESS_FLAG;
            if (info->mode==CREATIVE || !get_short(fp,&player_effects.weakness) ||
                !player_effects.weakness || player_effects.weakness>WEAKNESS_LONG_TICKS) return 0;
        }
        if (info->format>=76 && info->format<97 && debt>=0 && (debt&PLAYER_RETIRED_EFFECT_FLAG)) {
            debt&=~PLAYER_RETIRED_EFFECT_FLAG;
            uint16_t value;
            if (info->mode==CREATIVE || !player_rules.health || !get_short(fp,&value)) return 0;
            int extended=info->format>=77 && (value&RETIRED_EFFECT_TIER_FLAG);
            if (extended) value&=~RETIRED_EFFECT_TIER_FLAG;
            if (!value || value>(extended ? 800u : 200u)) return 0;
        }
        if (debt<0 || debt>=HEALTH_UNITS || ((!player_rules.health || info->mode==CREATIVE) && debt)) return 0;
        player_rules.health_debt=(uint8_t)debt;
        for (int i=0;i<ARMOR_SLOTS;++i) {
            ItemStack piece;
            if (!get_stack(fp,info->format,0,&piece) || !armor_restore(i,piece)) return 0;
        }
    }
    return (info->format<21 || read_drop_state(fp,info->format)) &&
        (info->format<22 || read_experience(fp,info->mode)) &&
        (info->format<25 || read_gravity(fp,info->format)) &&
        (info->format<27 || read_redstone_timing(fp,info->format)) &&
        (info->format<31 || read_button_arrows(fp)) &&
        (info->format<35 || read_blasts(fp,info->format)) &&
        (info->format<36 || read_player_burn(fp)) &&
        (info->format<37 || read_weather(fp)) &&
        (info->format<43 || read_horse_gear(fp,info->format)) &&
        (info->format<45 || read_leaf_work(fp)) &&
        (info->format<75 || read_ender_chest(fp,info->format)) &&
        (info->format<77 || read_retired_encounters(fp,info->format)) &&
        (info->format<79 || read_dungeon_clocks(fp)) &&
        (info->format<80 || read_mob_names(fp,info->format)) &&
        (info->format<81 || read_regeneration(fp)) &&
        (info->format<83 || read_water_breathing(fp)) &&
        (info->format<84 || read_night_vision(fp)) &&
        (info->format<85 || get_u32(fp,&player_rules.world_day)) &&
        (info->format<86 || read_item_frames(fp,info->format)) &&
        (info->format<100 || read_field_supplies(fp)) &&
        fgetc(fp) == EOF && !ferror(fp);
}

int save_load(uint32_t id) {
    if (active.id) return fail("请先保存并退出当前世界");
    clear_edit_memo();
    if (!*folder || !id) return fail("未找到该存档");
    char path[800];
    save_path(path, id);
    FILE *fp = fopen(path, "rb");
    if (!fp) return fail("无法打开该世界");
    SaveInfo info = {0};
    int ok = read_header(fp, id, &info) && restore(fp, &info) && actors_validate_stranded_origins();
    fclose(fp);
    if (!ok) { generate(); return fail("存档损坏或版本不兼容，原文件未更改"); }
    if (inside_pressure_works()) portals_reload_pressure_relief();
    if ((!uses_aquifer() && info.dimension==END) ||
        (info.dimension==NETHER && !inside_pressure_works())) {
        if (!portals_return_from_retired_site()) {
            generate();
            return fail("找不到安全的营地落点，原存档未更改");
        }
        info.dimension=OVERWORLD;
        info.returned_from_retired_site=1;
    }
    actors_apply_difficulty();
    active = info;
    active.format = CURRENT_FORMAT;
    /* Prepare untouched groups before gameplay resumes, so the first
       timed checkpoint does not rebuild a distant construction site. */
    for (int d=0;d<DIMENSION_COUNT;++d) (void)write_edits(NULL,(Dimension)d);
    printf("WORLD LOADED %lu seed=%" PRId64 " at %.2f %.2f %.2f water=%d\n",
           (unsigned long)info.id,world_seed_signed(info.seed),px,py,pz,inventory[1].count);
    fflush(stdout);
    return 1;
}
