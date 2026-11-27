#include "game.h"
#include "fireworks.h"
#include "spawn_eggs.h"
#include "armor.h"
#include "enchanting.h"
#include "item_custom.h"
#include "../world/build_blocks.h"

#define ITEM(id, label, rgb, stack, block_id, tier, food, life) \
    case id: { \
        _Static_assert((stack)>0 && STACK_MAX%(stack)==0, "stack must divide comparator slot weight"); \
        static const ItemDef def = {label, rgb, stack, block_id, tier, food, life, 0}; return &def; \
    }
#define BLOCK(id, label, rgb) ITEM(id, label, rgb, STACK_MAX, id, 0, 0, 0)
#define MATERIAL(id, label, rgb) ITEM(id, label, rgb, STACK_MAX, AIR, 0, 0, 0)
#define BUILD_SLAB_ITEM(mat,label,rgb) \
    ITEM(BUILD_SLAB_ITEMS+BUILD_##mat,label "台阶",rgb,STACK_MAX,BUILD_SLAB_BASE+3*BUILD_##mat,0,0,0)
#define BUILD_SHAPE_ITEMS(mat,label,rgb) \
    BUILD_SLAB_ITEM(mat,label,rgb) \
    ITEM(BUILD_STAIRS_ITEMS+BUILD_##mat-(BUILD_##mat>BUILD_STONE),label "楼梯",rgb,STACK_MAX, \
        BUILD_STAIRS_BASE+8*(BUILD_##mat-(BUILD_##mat>BUILD_STONE)),0,0,0)
#define STAINED(color, label, rgb) \
    ITEM(color##_GLASS_ITEM, label "染色玻璃", rgb, STACK_MAX, STAINED_GLASS_BASE+color##_GLASS_ITEM-BLACK_GLASS_ITEM, 0, 0, 0) \
    ITEM(color##_PANE_ITEM, label "染色玻璃板", rgb, STACK_MAX, STAINED_PANE_BASE+color##_PANE_ITEM-BLACK_PANE_ITEM, 0, 0, 0)
#define COLORED_CLAY(color,label,rgb) \
    ITEM(color##_CLAY_ITEM,label "硬化黏土",rgb,STACK_MAX,STAINED_CLAY_BASE+color##_CLAY_ITEM-BLACK_CLAY_ITEM,0,0,0)
#define PICK(id, label, rgb, tier, life) ITEM(id, label, rgb, 1, AIR, tier, 0, life)
#define SWORD(id, label, rgb, life, force) \
    case id: { static const ItemDef def = {label, rgb, 1, AIR, 0, 0, life, force}; return &def; }
const ItemDef *item_def(int id) {
    switch (id) {
        BLOCK(GRASS, "草方块", 0x65a43b)
        BLOCK(DIRT, "泥土", 0x986945)
        BLOCK(STONE, "石头", 0x899091)
        ITEM(SLATE_ITEM,"层理页岩",0x526376,16,SLATE_ROCK,0,0,0)
        ITEM(LIMESTONE_ITEM,"孔隙石灰岩",0xcab392,32,LIMESTONE_ROCK,0,0,0)
        ITEM(LAMINATE_ITEM,"层压砌板",0x91a194,32,LAMINATED_MASONRY,0,0,0)
        ITEM(SURVEY_TAPPER,"岩层探锤",0xb48357,1,AIR,0,0,0)
        MATERIAL(SHALE_SECTION,"页岩交界样片",0x7793b4)
        MATERIAL(LIME_SECTION,"石灰岩交界样片",0xd39e66)
        ITEM(STRATA_GAUGE,"地层测线尺",0x568a83,1,AIR,0,0,0)
        ITEM(FALL_SIEVE_ITEM,"落水筛",0x5d8e89,16,FALL_SIEVE,0,0,0)
        ITEM(WINCH_ITEM,"勘测绞盘",0x679785,4,AQUIFER_WINCH,0,0,0)
        MATERIAL(BRINE_CORE,"盐析岩芯",0x57aabc)
        MATERIAL(THERMAL_CORE,"换热岩芯",0xcb863e)
        ITEM(AQUIFER_LIGHT_ITEM,"导光嵌板",0x6cbea2,16,AQUIFER_LIGHT,0,0,0)
        ITEM(SINTERED_TILE_ITEM,"烧结岩砖",0x888c80,16,SINTERED_TILE,0,0,0)
        MATERIAL(SHELL_FLAKE,"鸣壳片",0xf1c779)
        ITEM(RESONANCE_MARKER_ITEM,"回声路标",0xa78b57,16,RESONANCE_MARKER,0,0,0)
        ITEM(TIDE_GUIDE,"潮路导盘",0x74bdc8,1,AIR,0,0,0)
        ITEM(AIR_PIPE_ITEM,"导气管",0x668f8a,STACK_MAX,AIR_PIPE,0,0,0)
        ITEM(SEEP_BED_ITEM,"渗光床",0x488786,16,SEEP_BED,0,0,0)
        ITEM(SEEP_TILE_ITEM,"柔光陶砖",0x69c2b9,32,SEEP_TILE,0,0,0)
        ITEM(SEEP_BRIGHT_ITEM,"高亮陶砖",0xbfebac,32,SEEP_TILE_BRIGHT,0,0,0)
        ITEM(SETTLING_MOLD_ITEM,"石浆模床",0xaf7350,16,SETTLING_MOLD,0,0,0)
        ITEM(CRAWLER_KIT,"岩甲巡护机",0xa87148,1,AIR,0,0,0)
        ITEM(PULSE_KIT,"水冷脉冲机",0x419e9c,1,AIR,0,0,0)
        ITEM(FIELD_FLASK_EMPTY,"行旅水囊 · 空",0x9b754d,1,AIR,0,0,0)
        ITEM(FIELD_FLASK_ONE,"行旅水囊 · 1 口",0x9b754d,1,AIR,0,0,0)
        ITEM(FIELD_FLASK_TWO,"行旅水囊 · 2 口",0x9b754d,1,AIR,0,0,0)
        ITEM(FIELD_FLASK_FULL,"行旅水囊 · 3 口",0x9b754d,1,AIR,0,0,0)
        ITEM(CISTERN_ITEM,"营地集雨罐",0x8c8075,16,CISTERN_EMPTY,0,0,0)
        ITEM(CISTERN_PROBE_ITEM,"水位探头",0x487e83,16,CISTERN_PROBE_BASE,0,0,0)
        ITEM(HYDRAULIC_SORTER_ITEM,"水力洗选槽",0x658374,16,HYDRAULIC_SORTER,0,0,0)
        ITEM(HEAT_BENCH_ITEM,"余热回温台",0xa87855,16,HEAT_BENCH_EMPTY,0,0,0)
        ITEM(JOINT_BASALT_ITEM,"节理玄武岩",0x3f555a,16,JOINT_BASALT,0,0,0)
        ITEM(BASALT_PAVING_ITEM,"玄武路板",0x687779,32,BASALT_PAVING,0,0,0)
        ITEM(TIDE_GRIT,"潮磨砂",0x7aabb2,32,AIR,0,0,0)
        ITEM(CERAMIC_CORE,"空陶芯",0x9a7465,16,AIR,0,0,0)
        ITEM(WARM_CORE,"暖陶芯",0xda8860,1,AIR,0,0,0)
        ITEM(AIR_RELAY_ITEM,"储压信号箱",0x68897e,16,AIR_RELAY_BASE,0,0,0)
        BLOCK(WOOD, "原木", 0x9e7445)
        BLOCK(LEAF, "树叶", 0x4a922c)
        ITEM(BIRCH_LEAF_ITEM,"白桦树叶",0x87a842,STACK_MAX,LEAF,0,0,0)
        ITEM(SPRUCE_LEAF_ITEM,"云杉树叶",0x3c7350,STACK_MAX,LEAF,0,0,0)
        ITEM(JUNGLE_LEAF_ITEM,"丛林树叶",0x549b2d,STACK_MAX,LEAF,0,0,0)
        ITEM(ACACIA_LEAF_ITEM,"金合欢树叶",0x8d9c3b,STACK_MAX,LEAF,0,0,0)
        ITEM(DARK_OAK_LEAF_ITEM,"深色橡木树叶",0x4d6f35,STACK_MAX,LEAF,0,0,0)
        ITEM(OAK_SAPLING,"橡树树苗",0x689f39,STACK_MAX,SAPLING,0,0,0)
        ITEM(BIRCH_SAPLING,"白桦树苗",0x8bab50,STACK_MAX,SAPLING,0,0,0)
        ITEM(SPRUCE_SAPLING,"云杉树苗",0x428766,STACK_MAX,SAPLING,0,0,0)
        ITEM(JUNGLE_SAPLING,"丛林树苗",0x5c9b35,STACK_MAX,SAPLING,0,0,0)
        ITEM(ACACIA_SAPLING,"金合欢树苗",0xa3aa48,STACK_MAX,SAPLING,0,0,0)
        ITEM(DARK_OAK_SAPLING,"深色橡木树苗",0x566e36,STACK_MAX,SAPLING,0,0,0)
        ITEM(PUMPKIN_ITEM,"南瓜",0xd37821,STACK_MAX,PUMPKIN,0,0,0)
        MATERIAL(PUMPKIN_SEEDS,"南瓜种子",0xd2c693)
        ITEM(POPPY_ITEM,"虞美人",0xd74d3d,STACK_MAX,POPPY,0,0,0)
        ITEM(SNOWBALL,"雪球",0xe9f6fa,16,AIR,0,0,0)
        ITEM(WOOD_SHOVEL,"木锹",0xbf8851,1,AIR,0,0,59)
        ITEM(STONE_SHOVEL,"石锹",0xa3a9aa,1,AIR,0,0,131)
        ITEM(IRON_SHOVEL,"铁锹",0xd4d7db,1,AIR,0,0,250)
        ITEM(GOLD_SHOVEL,"金锹",0xe2bc54,1,AIR,0,0,32)
        ITEM(DIAMOND_SHOVEL,"钻石锹",0x51d0c6,1,AIR,0,0,1561)
        MATERIAL(INK_SAC,"墨囊",0x363643)
        MATERIAL(GRAY_DYE,"灰色染料",0x565a60)
        MATERIAL(LIGHT_GRAY_DYE,"淡灰色染料",0xa4a5ae)
        ITEM(GLASS_PANE_ITEM,"玻璃板",0xbce4e8,STACK_MAX,GLASS_PANE_CELL,0,0,0)
        ITEM(IRON_BARS_ITEM,"铁栏杆",0xa1a8ad,STACK_MAX,IRON_BARS_CELL,0,0,0)
        ITEM(TRIPWIRE_HOOK_ITEM,"绊线钩",0xaa8c60,STACK_MAX,TRIPWIRE_HOOK,0,0,0)
        ITEM(TRAPPED_CHEST_ITEM,"陷阱箱",0xaf6b35,STACK_MAX,TRAPPED_CHEST,0,0,0)
        ITEM(ENDER_CHEST_ITEM,"联储箱",0x9a744c,STACK_MAX,ENDER_CHEST,0,0,0)
        ITEM(AIR_OUTLET_ITEM,"水下气嘴",0xb88c52,STACK_MAX,AIR_OUTLET,0,0,0)
        MATERIAL(PUMP_GASKET,"复合密封垫",0x8a6754)
        ITEM(AIR_STATION_ITEM,"手摇供气站",0x479a93,STACK_MAX,AIR_STATION,0,0,0)
        ITEM(GOLD_BLOCK_ITEM,"金块",0xe3b736,STACK_MAX,GOLD_BLOCK,0,0,0)
        ITEM(DIAMOND_BLOCK_ITEM,"钻石块",0x48d9d0,STACK_MAX,DIAMOND_BLOCK,0,0,0)
        STAINED(BLACK,"黑色",0x333641)
        STAINED(GRAY,"灰色",0x666b71)
        STAINED(LIGHT_GRAY,"淡灰色",0xa3abb3)
        STAINED(WHITE,"白色",0xe4edf0)
        STAINED(RED,"红色",0xb64136)
        STAINED(GREEN,"绿色",0x507d3a)
        STAINED(BROWN,"棕色",0x81583e)
        STAINED(BLUE,"蓝色",0x425bb1)
        STAINED(PURPLE,"紫色",0x804ba9)
        STAINED(CYAN,"青色",0x428f97)
        STAINED(PINK,"粉色",0xe984ad)
        STAINED(LIME,"黄绿色",0x89bd3f)
        STAINED(YELLOW,"黄色",0xe3be38)
        STAINED(LIGHT_BLUE,"淡蓝色",0x75b7e3)
        STAINED(MAGENTA,"品红色",0xc65db3)
        STAINED(ORANGE,"橙色",0xe08236)
        COLORED_CLAY(BLACK,"黑色",0x37241f)
        COLORED_CLAY(GRAY,"灰色",0x544641)
        COLORED_CLAY(LIGHT_GRAY,"淡灰色",0x8b7066)
        COLORED_CLAY(WHITE,"白色",0xd6b6a5)
        COLORED_CLAY(RED,"红色",0x953e31)
        COLORED_CLAY(GREEN,"绿色",0x5c6338)
        COLORED_CLAY(BROWN,"棕色",0x644a37)
        COLORED_CLAY(BLUE,"蓝色",0x51466e)
        COLORED_CLAY(PURPLE,"紫色",0x775067)
        COLORED_CLAY(CYAN,"青色",0x5a6866)
        COLORED_CLAY(PINK,"粉色",0xa65362)
        COLORED_CLAY(LIME,"黄绿色",0x757f3e)
        COLORED_CLAY(YELLOW,"黄色",0xc49436)
        COLORED_CLAY(LIGHT_BLUE,"淡蓝色",0x716f8a)
        COLORED_CLAY(MAGENTA,"品红色",0x995b78)
        COLORED_CLAY(ORANGE,"橙色",0xbb692f)
        MATERIAL(LAPIS_LAZULI,"青金石",0x325cbd)
        ITEM(LAPIS_ORE_ITEM,"青金石矿石",0x3c61af,STACK_MAX,LAPIS_ORE,0,0,0)
        ITEM(LAPIS_BLOCK_ITEM,"青金石块",0x2a53a4,STACK_MAX,LAPIS_BLOCK,0,0,0)
        ITEM(DANDELION_ITEM,"蒲公英",0xefc830,STACK_MAX,DANDELION,0,0,0)
        ITEM(CACTUS_ITEM,"仙人掌",0x60923e,STACK_MAX,CACTUS,0,0,0)
        MATERIAL(COCOA_BEANS,"可可豆",0x8b5331)
        ITEM(COOKIE,"曲奇",0xbe854c,STACK_MAX,AIR,0,2,0)
        MATERIAL(ROSE_RED,"玫瑰红",0xbc373a)
        MATERIAL(CACTUS_GREEN,"仙人掌绿",0x507837)
        MATERIAL(DANDELION_YELLOW,"蒲公英黄",0xe6bb31)
        MATERIAL(PURPLE_DYE,"紫色染料",0x824ba7)
        MATERIAL(CYAN_DYE,"青色染料",0x3a9096)
        MATERIAL(PINK_DYE,"粉色染料",0xe581a8)
        MATERIAL(LIME_DYE,"黄绿色染料",0x89b73c)
        MATERIAL(LIGHT_BLUE_DYE,"淡蓝色染料",0x72b8de)
        MATERIAL(MAGENTA_DYE,"品红色染料",0xbe55ad)
        MATERIAL(ORANGE_DYE,"橙色染料",0xde7633)
        BLOCK(BEDROCK, "基岩", 0x414141)
        BLOCK(SAND, "沙子", 0xe1ca8b)
        BLOCK(GRAVEL, "砂砾", 0x9b8c85)
        BLOCK(COAL_ORE, "煤矿石", 0x454649)
        BLOCK(IRON_ORE, "铁矿石", 0xb0826e)
        BLOCK(SNOW, "雪", 0xdfeff3)
        BLOCK(WATER, "水", 0x387de1)
        BLOCK(CLAY, "黏土", 0x9f9ba9)
        BLOCK(SANDSTONE, "砂岩", 0xc9a96b)
        BLOCK(GOLD_ORE, "金矿石", 0xd6b03c)
        BLOCK(DIAMOND_ORE, "钻石矿石", 0x4ec4bb)
        ITEM(EMERALD_ORE_ITEM,"绿宝石矿石",0x34bf70,STACK_MAX,EMERALD_ORE,0,0,0)
        ITEM(EMERALD_BLOCK_ITEM,"绿宝石块",0x28ad5d,STACK_MAX,EMERALD_BLOCK,0,0,0)
        BLOCK(MOSS_STONE, "苔石", 0x648356)
        BLOCK(PLANKS, "木板", 0xc39354)
        BLOCK(COBBLESTONE, "圆石", 0x858581)
        BLOCK(CRAFTING_TABLE, "工作台", 0x9e613a)
        BLOCK(FURNACE, "熔炉", 0x5e6462)
        BLOCK(PODZOL, "灰化土", 0x785b3e)
        BLOCK(RED_SAND, "红沙", 0xcb6935)
        BLOCK(TERRACOTTA, "硬化黏土", 0xb47452)
        BLOCK(MYCELIUM, "菌丝", 0x847286)
        BLOCK(ICE, "冰", 0x9ebee8)
        BLOCK(PACKED_ICE, "浮冰", 0x7aa6dc)
        BLOCK(BIRCH_LOG, "白桦原木", 0xd6d6be)
        BLOCK(SPRUCE_LOG, "云杉原木", 0x6b4633)
        BLOCK(JUNGLE_LOG, "丛林原木", 0xa99762)
        BLOCK(ACACIA_LOG, "金合欢原木", 0xbf6b49)
        BLOCK(DARK_OAK_LOG, "深色橡木原木", 0x4f392c)
        BLOCK(MUSHROOM_STEM, "蘑菇柄", 0xd1c6a3)
        BLOCK(RED_MUSHROOM_BLOCK, "红色蘑菇方块", 0xb03730)
        BLOCK(OBSIDIAN, "黑曜石", 0x413154)
        BLOCK(NETHERRACK, "热蚀岩", 0x854136)
        BLOCK(ASH_SAND, "灰砂床基", 0x7d867f)
        BLOCK(GLOWSTONE, "萤石", 0xeac08a)
        BLOCK(NETHER_QUARTZ_ORE, "热脉石英", 0xc18678)
        BLOCK(STONE_BRICKS, "石砖", 0x8d9091)
        BLOCK(NETHER_BRICKS, "赤烧砖块", 0x612f3a)
        BLOCK(REDSTONE_ORE, "导脉矿", 0xb49467)
        BLOCK(REDSTONE_BLOCK, "旧式常通源", 0xa57245)
        BLOCK(RAIL, "铁轨", 0xa4a29d)
        BLOCK(POWERED_RAIL, "动力铁轨", 0xd8a747)
        ITEM(DETECTOR_RAIL_ITEM,"探测铁轨",0xca4e3b,STACK_MAX,DETECTOR_RAIL,0,0,0)
        ITEM(ACTIVATOR_RAIL_ITEM,"激活铁轨",0xaa443b,STACK_MAX,ACTIVATOR_RAIL,0,0,0)
        BLOCK(CHEST, "箱子", 0xa9753f)
        BLOCK(WOOL, "白色羊毛", 0xe3ded4)
        ITEM(BED, "红床", 0xb7242f, 1, BED, 0, 0, 0)
        BLOCK(TORCH, "火把", 0xe4a544)
        BLOCK(WOOD_FENCE,"木栅栏",0xa97544)
        BLOCK(NETHER_FENCE,"赤烧砖栅栏",0x623328)
        BLOCK(FENCE_GATE,"栅栏门",0xbd8450)
        BLOCK(GLASS,"玻璃",0xbce4e8)
        BLOCK(BREWING_STAND,"野外调剂台",0x658a78)
        BLOCK(IRON_BLOCK,"铁块",0xd3d2cc)
        ITEM(ANVIL,"铁砧",0x51545b,STACK_MAX,ANVIL,0,0,3)
        ITEM(LEVER_ITEM,"拉杆",0x907058,STACK_MAX,LEVER,0,0,0)
        ITEM(REDSTONE_TORCH_ITEM,"信号柱",0xc8914b,STACK_MAX,REDSTONE_TORCH,0,0,0)
        ITEM(REPEATER_ITEM,"延时中继器",0xb98b57,STACK_MAX,REPEATER,0,0,0)
        ITEM(REDSTONE_LAMP_ITEM,"信号灯",0xb78356,STACK_MAX,REDSTONE_LAMP,0,0,0)
        ITEM(COMPARATOR_ITEM,"旧式比较器",0xb77872,STACK_MAX,COMPARATOR,0,0,0)
        ITEM(STONE_PLATE_ITEM,"石质压力板",0x888c94,STACK_MAX,PRESSURE_PLATE,0,0,0)
        ITEM(WOOD_PLATE_ITEM,"木质压力板",0xb58452,STACK_MAX,PRESSURE_PLATE,0,0,0)
        ITEM(GOLD_PLATE_ITEM,"测重压力板（轻质）",0xe6be48,STACK_MAX,PRESSURE_PLATE,0,0,0)
        ITEM(IRON_PLATE_ITEM,"测重压力板（重质）",0xc9cfd8,STACK_MAX,PRESSURE_PLATE,0,0,0)
        ITEM(STONE_BUTTON_ITEM,"石质按钮",0x9498a3,STACK_MAX,BUTTON,0,0,0)
        ITEM(WOOD_BUTTON_ITEM,"木质按钮",0xb88450,STACK_MAX,BUTTON,0,0,0)
        ITEM(HOPPER_ITEM,"漏斗",0x767b86,STACK_MAX,HOPPER,0,0,0)
        ITEM(DROPPER_ITEM,"投掷器",0x878984,STACK_MAX,DROPPER,0,0,0)
        ITEM(DISPENSER_ITEM,"发射器",0x757c80,STACK_MAX,DISPENSER,0,0,0)
        ITEM(TNT_ITEM,"TNT",0xb6342e,STACK_MAX,TNT_BLOCK,0,0,0)
        ITEM(GUNPOWDER,"火药",0x56545c,STACK_MAX,AIR,0,0,0)
        ITEM(PISTON_ITEM,"活塞",0x81817b,STACK_MAX,PISTON,0,0,0)
        ITEM(STICKY_PISTON_ITEM,"黏性活塞",0x81817b,STACK_MAX,STICKY_PISTON,0,0,0)
        ITEM(WOOD_DOOR_ITEM,"木门",0xa36b38,1,WOOD_DOOR,0,0,0)
        ITEM(IRON_DOOR_ITEM,"铁门",0xb6bdc5,1,IRON_DOOR,0,0,0)
        BLOCK(BOOKSHELF,"书架",0xaa763c)
        BLOCK(ENCHANTING_TABLE,"工况校准台",0x887263)
        BLOCK(SUGAR_CANE,"甘蔗",0xa6bb55)
        MATERIAL(PAPER,"纸",0xe3dfce)
        MATERIAL(BOOK,"书",0xb4784b)
        ITEM(ENCHANTED_BOOK,"附魔书",0xce7bf7,1,AIR,0,0,0)
        MATERIAL(STICK, "木棍", 0xaf794a)
        MATERIAL(COAL, "煤炭", 0x343438)
        MATERIAL(CHARCOAL, "木炭", 0x473833)
        MATERIAL(IRON_INGOT, "铁锭", 0xc8cbcc)
        MATERIAL(GOLD_INGOT, "金锭", 0xe6bb3c)
        MATERIAL(DIAMOND, "钻石", 0x63e5d6)
        MATERIAL(EMERALD, "绿宝石", 0x28b764)
        PICK(WOOD_PICKAXE, "木镐", 0xbf8851, 1, 59)
        PICK(STONE_PICKAXE, "石镐", 0xa5a4a1, 2, 131)
        PICK(IRON_PICKAXE, "铁镐", 0xe3e3df, 3, 250)
        PICK(GOLD_PICKAXE, "金镐", 0xebc445, 1, 32)
        PICK(DIAMOND_PICKAXE, "钻石镐", 0x5addc9, 4, 1561)
        SWORD(WOOD_AXE, "木斧", 0xbf8851, 59, 4)
        SWORD(STONE_AXE, "石斧", 0xa5a4a1, 131, 5)
        SWORD(IRON_AXE, "铁斧", 0xe3e3df, 250, 6)
        SWORD(GOLD_AXE, "金斧", 0xebc445, 32, 4)
        SWORD(DIAMOND_AXE, "钻石斧", 0x5addc9, 1561, 7)
        ITEM(APPLE, "苹果", 0xc8363d, STACK_MAX, AIR, 0, 4, 0)
        MATERIAL(FLINT, "燧石", 0x9d988e)
        ITEM(FLINT_AND_STEEL, "打火石", 0xd5b988, 1, AIR, 0, 0, 64)
        MATERIAL(QUARTZ, "石英", 0xefddd6)
        MATERIAL(GLOWSTONE_DUST, "萤石粉", 0xdfb859)
        MATERIAL(STRIATED_GLASS,"层纹玻璃",0x73bcca)
        MATERIAL(REED_GRIT, "多孔陶砂", 0xc8a47a)
        ITEM(BUCKET, "铁桶", 0xd2d3d2, 16, AIR, 0, 0, 0)
        ITEM(LAVA_BUCKET, "熔岩桶", 0xff8731, 1, AIR, 0, 0, 0)
        ITEM(WATER_BUCKET, "水桶", 0x6097ec, 1, AIR, 0, 0, 0)
        ITEM(RAW_PORK, "生猪排", 0xe98891, STACK_MAX, AIR, 0, 3, 0)
        ITEM(COOKED_PORK, "熟猪排", 0xc17a48, STACK_MAX, AIR, 0, 8, 0)
        MATERIAL(ROTTEN_FLESH, "苔壳碎片", 0x847d52)
        MATERIAL(REED_FIN, "陶鳍片", 0xd99564)
        SWORD(WOOD_SWORD, "木剑", 0xc79a63, 59, 4)
        SWORD(STONE_SWORD, "石剑", 0xa6a8aa, 131, 5)
        SWORD(IRON_SWORD, "铁剑", 0xe2e3e6, 250, 6)
        SWORD(GOLD_SWORD, "金剑", 0xebc64f, 32, 4)
        SWORD(DIAMOND_SWORD, "钻石剑", 0x52d9d0, 1561, 7)
        ITEM(STRING, "线", 0xd0d1db, STACK_MAX, TRIPWIRE, 0, 0, 0)
        MATERIAL(FEATHER, "羽毛", 0xe5e6ea)
        ITEM(BOW, "弓", 0xb17c55, 1, AIR, 0, 0, 384)
        MATERIAL(ARROW, "箭", 0xc6cbd1)
        ITEM(BOAT, "船", 0xaa7549, 1, AIR, 0, 0, 0)
        ITEM(REDSTONE_DUST,"导脉砂",0xd18c43,STACK_MAX,REDSTONE_WIRE,0,0,0)
        ITEM(MINECART, "矿车", 0xadb5bb, 1, AIR, 0, 0, 0)
        ITEM(TNT_MINECART,"TNT 矿车",0xc43631,1,AIR,0,0,0)
        ITEM(CHEST_MINECART,"箱子矿车",0xae7540,1,AIR,0,0,0)
        ITEM(HOPPER_MINECART,"漏斗矿车",0x687283,1,AIR,0,0,0)
        ITEM(FURNACE_MINECART,"熔炉矿车",0x77797d,1,AIR,0,0,0)
        ITEM(SADDLE, "鞍", 0x9b4a2e, 1, AIR, 0, 0, 0)
        MATERIAL(NAME_TAG, "命名牌", 0xddbc83)
        MATERIAL(LANTERN_SCALES, "磨砂晶屑", 0xb4d1c5)
        MATERIAL(GOLD_NUGGET,"金粒",0xf2c849)
        ITEM(IRON_HORSE_ARMOR,"铁马铠",0xa3afb9,1,AIR,0,0,0)
        ITEM(GOLD_HORSE_ARMOR,"金马铠",0xdbaa44,1,AIR,0,0,0)
        ITEM(DIAMOND_HORSE_ARMOR,"钻石马铠",0x4ebeb8,1,AIR,0,0,0)
        ITEM(FRUIT_RATION, "果麦干粮", 0xcb9c5e, STACK_MAX, AIR, 0, 5, 0)
        ITEM(GOLDEN_CARROT,"金胡萝卜",0xf0b337,STACK_MAX,AIR,0,6,0)
        MATERIAL(LEATHER, "皮革", 0x9d6444)
        ITEM(FISHING_ROD, "钓鱼竿", 0x99623c, 1, AIR, 0, 0, 64)
        ITEM(RAW_FISH, "生鱼", 0x8a9cad, STACK_MAX, AIR, 0, 2, 0)
        ITEM(COOKED_FISH, "熟鱼", 0xcead82, STACK_MAX, AIR, 0, 5, 0)
        ITEM(RAW_SALMON, "生鲑鱼", 0xbd575a, STACK_MAX, AIR, 0, 2, 0)
        ITEM(COOKED_SALMON, "熟鲑鱼", 0xe68460, STACK_MAX, AIR, 0, 6, 0)
        ITEM(CLOWNFISH, "小丑鱼", 0xee9a52, STACK_MAX, AIR, 0, 1, 0)
        ITEM(PUFFERFISH, "河豚", 0xcda65c, STACK_MAX, AIR, 0, 1, 0)
        ITEM(SHEARS, "剪刀", 0xbac4c6, 1, AIR, 0, 0, 238)
        ITEM(WOOD_HOE, "木锄", 0xb78957, 1, AIR, 0, 0, 59)
        ITEM(STONE_HOE, "石锄", 0x9a9b97, 1, AIR, 0, 0, 131)
        ITEM(IRON_HOE, "铁锄", 0xdaddd9, 1, AIR, 0, 0, 250)
        ITEM(GOLD_HOE, "金锄", 0xe9c849, 1, AIR, 0, 0, 32)
        ITEM(DIAMOND_HOE, "钻石锄", 0x64d6bd, 1, AIR, 0, 0, 1561)
        MATERIAL(WHEAT_SEEDS,"小麦种子",0x789a3d)
        MATERIAL(WHEAT,"小麦",0xdcb04e)
        ITEM(BREAD,"面包",0xc98d49,STACK_MAX,AIR,0,5,0)
        ITEM(CARROT,"胡萝卜",0xe47836,STACK_MAX,AIR,0,3,0)
        ITEM(POTATO,"马铃薯",0xc4a660,STACK_MAX,AIR,0,1,0)
        ITEM(BAKED_POTATO,"烤马铃薯",0xcb9255,STACK_MAX,AIR,0,5,0)
        MATERIAL(BONE,"骨头",0xe9e6d4)
        MATERIAL(BONE_MEAL,"骨粉",0xf5edd9)
        ITEM(RAW_BEEF,"生牛肉",0xb15551,STACK_MAX,AIR,0,3,0)
        ITEM(COOKED_BEEF,"牛排",0x9b5438,STACK_MAX,AIR,0,8,0)
        ITEM(RAW_CHICKEN,"生鸡肉",0xe5ac91,STACK_MAX,AIR,0,2,0)
        ITEM(COOKED_CHICKEN,"熟鸡肉",0xca8d51,STACK_MAX,AIR,0,6,0)
        ITEM(MILK_BUCKET,"牛奶桶",0xf1efe4,1,AIR,0,0,0)
        ITEM(EGG,"鸡蛋",0xf0debb,16,AIR,0,0,0)
        MATERIAL(SILT_GEL,"泥纤胶",0x9b7356)
        MATERIAL(LEAD,"拴绳",0x9f6942)
        MATERIAL(MAGMA_CREAM,"旧耐热剂",0xa69176)
        MATERIAL(FILTER_MOSS,"滤苔簇",0x82ad8e)
        MATERIAL(GLASS_BOTTLE,"玻璃瓶",0x9cc8cd)
        ITEM(WATER_BOTTLE,"水瓶",0x5b91ec,1,AIR,0,0,0)
        ITEM(AWKWARD_POTION,"旧滤液",0x93adf2,1,AIR,0,0,0)
        ITEM(MUNDANE_POTION,"浑浊滤液",0xa19abc,1,AIR,0,0,0)
        ITEM(POTION_FIRE,"隔热滤剂",0xd77a2e,1,AIR,0,0,0)
        ITEM(POTION_FIRE_LONG,"浓缩隔热滤剂",0xde6936,1,AIR,0,0,0)
        ITEM(SUGAR,"糖",0xe6e0d0,STACK_MAX,AIR,0,0,0)
        ITEM(SPIDER_EYE,"蜘蛛眼",0x932735,STACK_MAX,AIR,0,0,0)
        ITEM(BROWN_MUSHROOM_ITEM,"棕色蘑菇",0x946e4e,STACK_MAX,BROWN_MUSHROOM,0,0,0)
        ITEM(LILY_PAD_ITEM,"睡莲",0x4f8742,STACK_MAX,LILY_PAD,0,0,0)
        ITEM(WOOD_TRAPDOOR_ITEM,"木活板门",0xa5733d,STACK_MAX,WOOD_TRAPDOOR,0,0,0)
        ITEM(LADDER_ITEM,"梯子",0xc69653,STACK_MAX,LADDER,0,0,0)
        ITEM(BIRCH_PLANK_ITEM,"白桦木板",0xd9bd81,STACK_MAX,BIRCH_PLANK_CELL,0,0,0)
        ITEM(SPRUCE_PLANK_ITEM,"云杉木板",0x825d38,STACK_MAX,SPRUCE_PLANK_CELL,0,0,0)
        ITEM(JUNGLE_PLANK_ITEM,"丛林木板",0xaa7653,STACK_MAX,JUNGLE_PLANK_CELL,0,0,0)
        ITEM(ACACIA_PLANK_ITEM,"金合欢木板",0xba6843,STACK_MAX,ACACIA_PLANK_CELL,0,0,0)
        ITEM(DARK_OAK_PLANK_ITEM,"深色橡木板",0x61452d,STACK_MAX,DARK_OAK_PLANK_CELL,0,0,0)
        ITEM(BRICK_BLOCK_ITEM,"红砖块",0xaa5d50,STACK_MAX,BRICK_CELL,0,0,0)
        ITEM(QUARTZ_BLOCK_ITEM,"石英块",0xe5ddd3,STACK_MAX,QUARTZ_BLOCK_CELL,0,0,0)
        MATERIAL(CLAY_BALL,"黏土球",0x9f9ba9)
        MATERIAL(BRICK_ITEM,"红砖",0xab6357)
        MATERIAL(NETHER_BRICK_ITEM,"赤烧砖",0x612f3a)
        MATERIAL(ITEM_FRAME_ITEM,"物品展示框",0xb7844d)
        MATERIAL(FIRE_CHARGE,"火焰弹",0xf58d36)
        MATERIAL(FIREWORK_STAR,"烟火之星",0xffbd58)
        MATERIAL(FIREWORK_ROCKET,"烟花火箭",0xdf4b42)
        BUILD_SHAPE_ITEMS(OAK,"橡木",0xc39354)
        BUILD_SHAPE_ITEMS(BIRCH,"白桦木",0xd9bd81)
        BUILD_SHAPE_ITEMS(SPRUCE,"云杉木",0x825d38)
        BUILD_SHAPE_ITEMS(JUNGLE,"丛林木",0xaa7653)
        BUILD_SHAPE_ITEMS(ACACIA,"金合欢木",0xba6843)
        BUILD_SHAPE_ITEMS(DARK_OAK,"深色橡木",0x61452d)
        BUILD_SLAB_ITEM(STONE,"石",0x969795)
        BUILD_SHAPE_ITEMS(SANDSTONE,"砂岩",0xd6c38d)
        BUILD_SHAPE_ITEMS(COBBLE,"圆石",0x858581)
        BUILD_SHAPE_ITEMS(BRICK,"红砖",0xaa5d50)
        BUILD_SHAPE_ITEMS(STONE_BRICK,"石砖",0x8d9091)
        BUILD_SHAPE_ITEMS(NETHER_BRICK,"赤烧砖",0x612f3a)
        BUILD_SHAPE_ITEMS(QUARTZ,"石英",0xe5ddd3)
        ITEM(FERMENTED_SPIDER_EYE,"发酵蜘蛛眼",0xa76b56,STACK_MAX,AIR,0,0,0)
        ITEM(POTION_WEAKNESS,"缓力滤剂",0x8f99a8,1,AIR,0,0,0)
        ITEM(POTION_REGEN,"回元滤剂",0xca6290,1,AIR,0,0,0)
        ITEM(POTION_REGEN_LONG,"浓缩回元滤剂",0xe3729c,1,AIR,0,0,0)
        ITEM(POTION_REGEN_II,"强效回元滤剂",0xd83775,1,AIR,0,0,0)
        ITEM(POTION_WATER_BREATHING,"助潜滤剂",0x367eaa,1,AIR,0,0,0)
        ITEM(POTION_WATER_BREATHING_LONG,"浓缩助潜滤剂",0x40a3c7,1,AIR,0,0,0)
        ITEM(SPLASH_WATER_BREATHING,"助潜雾剂",0x367eaa,1,AIR,0,0,0)
        ITEM(SPLASH_WATER_BREATHING_LONG,"浓缩助潜雾剂",0x40a3c7,1,AIR,0,0,0)
        ITEM(POTION_NIGHT_VISION,"辨暗滤剂",0x4e50b2,1,AIR,0,0,0)
        ITEM(POTION_NIGHT_VISION_LONG,"浓缩辨暗滤剂",0x746bdf,1,AIR,0,0,0)
        ITEM(SPLASH_NIGHT_VISION,"辨暗雾剂",0x4e50b2,1,AIR,0,0,0)
        ITEM(SPLASH_NIGHT_VISION_LONG,"浓缩辨暗雾剂",0x746bdf,1,AIR,0,0,0)
        ITEM(POTION_WEAKNESS_LONG,"浓缩缓力滤剂",0xaebbd0,1,AIR,0,0,0)
        ITEM(SPLASH_WEAKNESS,"缓力雾剂",0x8196b2,1,AIR,0,0,0)
        ITEM(SPLASH_WEAKNESS_LONG,"浓缩缓力雾剂",0xb3c4db,1,AIR,0,0,0)
        ITEM(RED_MUSHROOM_ITEM,"红色蘑菇",0xd64e47,STACK_MAX,RED_MUSHROOM,0,0,0)
        MATERIAL(BOWL,"碗",0x9b6235)
        ITEM(MUSHROOM_STEW,"蘑菇煲",0xa96141,1,AIR,0,6,0)
        ITEM(BROWN_MUSHROOM_BLOCK_ITEM,"棕色蘑菇方块",0xb18a67,STACK_MAX,BROWN_MUSHROOM_BLOCK,0,0,0)
        ITEM(COBWEB_ITEM,"蜘蛛网",0xdce3dc,STACK_MAX,COBWEB,0,0,0)
        MATERIAL(COMPASS_ITEM,"指南针",0xabb2b6)
        MATERIAL(CLOCK_ITEM,"时钟",0xe6c365)
        MATERIAL(EMPTY_MAP,"空地图",0xe1c594)
        MATERIAL(FILLED_MAP,"地图",0xb4c68b)
        default: return spawn_egg_item(id) ? spawn_egg_def(id) : armor_item_def(id);
    }
}
#undef PICK
#undef SWORD
#undef MATERIAL
#undef BLOCK
#undef COLORED_CLAY
#undef ITEM
const char *item_name(int id) {
    const ItemDef *def = item_def(id);
    return def ? def->name : "空";
}
int item_stack_valid(ItemStack item) {
    if (!item.type) return !item.count && !item.damage && !item.enchants && !item.custom;
    const ItemDef *def = item_def(item.type);
    return def && item.count && item.count <= def->max_stack &&
        (firework_item(item.type) ? firework_spec(item)!=NULL :
            def->durability ? item.damage < def->durability : !item.damage) &&
        enchant_valid(item) && item_custom_valid(item);
}
int item_same_kind(ItemStack a,ItemStack b) {
    return a.type==b.type && a.damage==b.damage && a.enchants==b.enchants && a.custom==b.custom;
}

int is_log(int id) {
    return id == WOOD || id == BIRCH_LOG || id == SPRUCE_LOG || id == JUNGLE_LOG ||
        id == ACACIA_LOG || id == DARK_OAK_LOG;
}
unsigned axe_chop_milliseconds(int item) {
    static const unsigned hold[]={365,305,265,185,225};
    return is_axe(item) ? hold[item-WOOD_AXE] : 0;
}
int mining_drop(int block_id, int pick_tier) {
    if (strata_sample_cell(block_id)) block_id=strata_sample_host(block_id);
    if (air_relay_cell(block_id)) return AIR_RELAY_ITEM;
    if (cistern_probe_cell(block_id)) return CISTERN_PROBE_ITEM;
    if (heat_bench_cell(block_id)) return HEAT_BENCH_ITEM;
    if (coast_stone(block_id))
        return pick_tier>=geology_pick_tier(block_id) ?
            (block_id==JOINT_BASALT ? JOINT_BASALT_ITEM : BASALT_PAVING_ITEM) : AIR;
    if (block_id==RESONANCE_MARKER) return pick_tier ? RESONANCE_MARKER_ITEM : AIR;
    if (block_id==SINTERED_TILE) return pick_tier ? SINTERED_TILE_ITEM : AIR;
    if (block_id==AQUIFER_WINCH || block_id==AQUIFER_LIGHT)
        return pick_tier ? (block_id==AQUIFER_WINCH ? WINCH_ITEM : AQUIFER_LIGHT_ITEM) : AIR;
    if (geology_block(block_id))
        return pick_tier>=geology_pick_tier(block_id) ? SLATE_ITEM+block_id-SLATE_ROCK : AIR;
    int mat=build_material(block_id);
    if (mat>=0) return mat>=BUILD_STONE && !pick_tier ? AIR :
        build_slab(block_id) ? BUILD_SLAB_ITEMS+mat : building_stair_item(mat);
    if (block_id>=BIRCH_PLANK_CELL && block_id<=DARK_OAK_PLANK_CELL)
        return BIRCH_PLANK_ITEM+block_id-BIRCH_PLANK_CELL;
    if (block_id==BRICK_CELL || block_id==QUARTZ_BLOCK_CELL)
        return pick_tier ? (block_id==BRICK_CELL ? BRICK_BLOCK_ITEM : QUARTZ_BLOCK_ITEM) : AIR;
    if (stained_clay_cell(block_id)) return pick_tier>=1 ? BLACK_CLAY_ITEM+block_id-STAINED_CLAY_BASE : AIR;
    if (block_id==BROWN_MUSHROOM) return BROWN_MUSHROOM_ITEM;
    if (block_id==LILY_PAD) return LILY_PAD_ITEM;
    if (block_id==RED_MUSHROOM) return RED_MUSHROOM_ITEM;
    if (block_id==BROWN_MUSHROOM_BLOCK || block_id==RED_MUSHROOM_BLOCK || block_id==MUSHROOM_STEM) return AIR;
    switch (block_id) {
    case CLAY: case FIBROUS_CLAY: return CLAY_BALL;
    case GLASS: case GLASS_PANE: case COBWEB: return AIR;
    case IRON_BARS: return pick_tier>=1 ? IRON_BARS_ITEM : AIR;
    case LAPIS_ORE: return pick_tier>=2 ? LAPIS_LAZULI : AIR;
    case LAPIS_BLOCK: return pick_tier>=2 ? LAPIS_BLOCK_ITEM : AIR;
    case DANDELION: return DANDELION_ITEM;
    case CACTUS: return CACTUS_ITEM;
    case COCOA: return COCOA_BEANS;
    case BREWING_STAND: return pick_tier>=1 ? BREWING_STAND : AIR;
    case ENCHANTING_TABLE: return pick_tier>=1 ? ENCHANTING_TABLE : AIR;
    case ANVIL: return pick_tier>=1 ? ANVIL : AIR;
    case IRON_BLOCK: return pick_tier>=2 ? IRON_BLOCK : AIR;
    case REDSTONE_WIRE: return REDSTONE_DUST;
    case LEVER: return LEVER_ITEM;
    case REDSTONE_TORCH: return REDSTONE_TORCH_ITEM;
    case REPEATER: return REPEATER_ITEM;
    case REDSTONE_LAMP: return REDSTONE_LAMP_ITEM;
    case COMPARATOR: return COMPARATOR_ITEM;
    case DROPPER: return pick_tier>=1 ? DROPPER_ITEM : AIR;
    case DISPENSER: return pick_tier>=1 ? DISPENSER_ITEM : AIR;
    case TNT_BLOCK: return TNT_ITEM;
    case PISTON: return PISTON_ITEM;
    case STICKY_PISTON: return STICKY_PISTON_ITEM;
    case WOOD_DOOR: return WOOD_DOOR_ITEM;
    case WOOD_TRAPDOOR: return WOOD_TRAPDOOR_ITEM;
    case LADDER: return LADDER_ITEM;
    case IRON_DOOR: return pick_tier>=1 ? IRON_DOOR_ITEM : AIR;
    case BOOKSHELF: return BOOK;
    case REDSTONE_ORE: return pick_tier >= 3 ? REDSTONE_DUST : AIR;
    case REDSTONE_BLOCK: return pick_tier >= 1 ? REDSTONE_BLOCK : AIR;
    case GRASS: case PODZOL: case MYCELIUM: return DIRT;
    case SADDLE_CHEST: case SMITH_CHEST: case TEMPLE_CHEST: case DUNGEON_CHEST: case DESERT_PYRAMID_CHEST:
        return CHEST;
    case MONSTER_SPAWNER: return AIR;
    case TRAPPED_CHEST: return TRAPPED_CHEST_ITEM;
    case ENDER_CHEST: return pick_tier ? ENDER_CHEST_ITEM : AIR;
    case AIR_OUTLET: return AIR_OUTLET_ITEM;
    case AIR_STATION: return AIR_STATION_ITEM;
    case AIR_PIPE: return pick_tier ? AIR_PIPE_ITEM : AIR;
    case SEEP_BED: return pick_tier ? SEEP_BED_ITEM : AIR;
    case SEEP_TILE: return pick_tier ? SEEP_TILE_ITEM : AIR;
    case SEEP_TILE_BRIGHT: return pick_tier ? SEEP_BRIGHT_ITEM : AIR;
    case SETTLING_MOLD: return SETTLING_MOLD_ITEM;
    case CISTERN_EMPTY: return CISTERN_ITEM;
    case HYDRAULIC_SORTER: return HYDRAULIC_SORTER_ITEM;
    case FALL_SIEVE: return FALL_SIEVE_ITEM;
    case GOLD_BLOCK: return pick_tier>=3 ? GOLD_BLOCK_ITEM : AIR;
    case DIAMOND_BLOCK: return pick_tier>=3 ? DIAMOND_BLOCK_ITEM : AIR;
    case TRIPWIRE: return STRING;
    case TRIPWIRE_HOOK: return TRIPWIRE_HOOK_ITEM;
    case DETECTOR_RAIL: return DETECTOR_RAIL_ITEM;
    case ACTIVATOR_RAIL: return ACTIVATOR_RAIL_ITEM;
    case STONE: return pick_tier >= 1 ? COBBLESTONE : AIR;
    case COAL_ORE: return pick_tier >= 1 ? COAL : AIR;
    case IRON_ORE: return pick_tier >= 2 ? IRON_ORE : AIR;
    case GOLD_ORE: return pick_tier >= 3 ? GOLD_ORE : AIR;
    case DIAMOND_ORE: return pick_tier >= 3 ? DIAMOND : AIR;
    case EMERALD_ORE: return pick_tier>=3 ? EMERALD : AIR;
    case EMERALD_BLOCK: return pick_tier>=3 ? EMERALD_BLOCK_ITEM : AIR;
    case PUMPKIN: return PUMPKIN_ITEM;
    case PUMPKIN_STEM: return PUMPKIN_SEEDS;
    case POPPY: return POPPY_ITEM;
    case OBSIDIAN: return pick_tier >= 4 ? OBSIDIAN : AIR;
    case NETHER_QUARTZ_ORE: return pick_tier >= 1 ? QUARTZ : AIR;
    case GLOWSTONE: return GLOWSTONE_DUST;
    case NETHERRACK: case STONE_BRICKS: case NETHER_BRICKS: case NETHER_FENCE:
        return pick_tier >= 1 ? block_id : AIR;
    case COBBLESTONE: case SANDSTONE: case MOSS_STONE: case FURNACE:
        return pick_tier >= 1 ? block_id : AIR;
    case AIR: case BEDROCK: case WATER: case LEAF: case SNOW: case SNOW_LAYER: case ICE: case PACKED_ICE:
    case LAVA: return AIR;
    default: return item_def(block_id) ? block_id : AIR;
    }
}
