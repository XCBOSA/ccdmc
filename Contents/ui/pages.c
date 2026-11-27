#include "pages.h"
#include "../render/renderer.h"
#include "inventory_ui.h"
#include "../game/game.h"
#include "../game/interaction.h"
#include "../game/furnace.h"
#include "../game/brewing.h"
#include "../game/armor.h"
#include "../game/horses.h"
#include "../game/drops.h"
#include "../game/fireworks.h"
#include "../game/experience.h"
#include "../game/enchanting.h"
#include "../game/anvil.h"
#include "../game/chests.h"
#include "../game/ender_chest.h"
#include "../game/minecarts.h"
#include "../game/hoppers.h"
#include "../game/droppers.h"
#include "../game/villagers.h"
#include "../game/portals.h"
#include "../save/save.h"
#include "../save/seed.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static enum { PAGE_WORLDS, PAGE_CREATE, PAGE_PLAY, PAGE_PAUSE, PAGE_BAG, PAGE_DEATH,
    PAGE_FURNACE, PAGE_CHEST, PAGE_BREWING, PAGE_ENCHANTING, PAGE_ANVIL, PAGE_VILLAGER } current_page;
enum { PANEL_PADDING=12, GRID_SLOT_MIN=34, STORAGE_BAG_TITLE_HEIGHT=24 };
static int page_w = 320, page_h = 400;
static int held_index = -1, empty_bag_focus = -1;
static int craft_size = 2, craft_sources[9], sorting_bag, palette_page, palette_eggs;
static int inventory_edge;
static void layout_inventory_grid(AutoTree root,int width);
static int fx, fy, fz, furnace_burn_shown, furnace_cook_shown;
static ItemStack furnace_slots_shown[FURNACE_SLOTS];
static int brew_x,brew_y,brew_z;
static char brew_status_shown[160];
static ItemStack brew_slots_shown[BREW_SLOTS];
static ChestView storage;
static EnderChestView ender_storage;
static CartStorageView cart_storage;
enum { STORAGE_CART=-1 };
static int storage_type;
static ItemStack storage_shown[DOUBLE_CHEST_SLOTS+SLOT_COUNT];
static int storage_selected=-1,storage_has_source,storage_single,storage_edge;
static void layout_storage_grid(AutoTree root,int width);
static int enchant_x,enchant_y,enchant_z;
static void refresh_enchanting(AutoTree root);
static void anvil_pick(AutoTree root,int slot);
static int anvil_x,anvil_y,anvil_z,anvil_source[2],anvil_input;
static char anvil_shown_name[ITEM_NAME_BYTES+1];
static int anvil_name_valid;
static VillagerSession trade_session;
static WorldProfile create_profile;
static int create_no_structures,create_bonus_chest;
static char create_flat_code[FLAT_CODE_MAX+2];

int playing(void) { return current_page == PAGE_PLAY; }
int furnace_panel_visible(void) { return current_page == PAGE_FURNACE; }
int brewing_panel_visible(void) { return current_page==PAGE_BREWING; }
int chest_panel_visible(void) { return current_page==PAGE_CHEST; }
int villager_panel_visible(void) { return current_page==PAGE_VILLAGER; }

static AutoTree titled(const char *id, const char *text, int size, const char *color) {
    AutoTree node = label((char *)text);
    if (id) node.id = id;
    node.fontSize = size;
    node.color = color;
    return node;
}
static AutoTree menu_button(const char *id, const char *text, CCDMCAction action) {
    AutoTree node = button((char *)text, action);
    node.id = id;
    node.height = 44;
    node.fontSize = 17;
    return node;
}

static void show_panel(AutoTree root, AutoTree rows, int inventory_page) {
    AutoTree body = vstack(rows);
    body.spacing = inventory_page ? 5 : 10;
    body.paddingTop = 12; body.paddingBottom = 12;
    body.paddingLeft = PANEL_PADDING; body.paddingRight = PANEL_PADDING;
    AutoTree body_array = tree_newarr();
    tree_array_append(body_array, body);
    AutoTree scroll = vscroll(body_array);
    scroll.id = "panelScroll";
    scroll.width = page_w;
    scroll.height = page_h;
    AutoTree background = mc_color(inventory_page ? "#C6C6C6" : "#222820");
    background.width = page_w;
    background.height = page_h;
    background.id = "panelBackground";
    AutoTree layers = tree_newarr();
    tree_array_append(layers, background);
    tree_array_append(layers, scroll);
    AutoTree layout = tree_newarr();
    tree_array_append(layout, zstack(layers));
    ccd_ui_id(root, "pageHost").sub = layout;
}

static int panel_grid_available(int width) {
    return width>2*PANEL_PADDING ? width-2*PANEL_PADDING : 1;
}
static int panel_grid_edge(int width) {
    int edge=panel_grid_available(width)/HOTBAR_SIZE;
    /* If the window is narrower still, preserve tap targets and scroll. */
    return edge<SLOT_EDGE ? (edge<GRID_SLOT_MIN ? GRID_SLOT_MIN : edge) : SLOT_EDGE;
}
static int panel_grid_width(int width,int edge) {
    int available=panel_grid_available(width),preferred=HOTBAR_SIZE*edge;
    return available<preferred ? available : preferred;
}

void layout_page(AutoTree root, int w, int h) {
    page_w = w; page_h = h;
    if (playing()) return;
    AutoTree background = ccd_ui_id(root, "panelBackground");
    background.width = w; background.height = h;
    AutoTree scroll = ccd_ui_id(root, "panelScroll");
    scroll.width = w; scroll.height = h;
    if (chest_panel_visible()) layout_storage_grid(root,w);
    else if (current_page==PAGE_BAG || current_page==PAGE_ENCHANTING || current_page==PAGE_ANVIL ||
        furnace_panel_visible() || brewing_panel_visible()) layout_inventory_grid(root,w);
}

static void leave_panel(AutoTree root) {
    chest_access_end();
    if (trade_session) { villagers_close(trade_session); trade_session=0; }
    game_clear_jump_taps();
    current_page = PAGE_PLAY;
    held_index = empty_bag_focus = -1;
    ccd_ui_id(root, "pageHost").sub = tree_newarr();
}

static void start_play(AutoTree root) {
    world_set_interactive(1);
    palette_page=palette_eggs=0;
    leave_panel(root);
    restart_game_ui(root);
    if (!player_rules.health) show_death(root);
    else if (save_current()->returned_from_retired_site)
        status(root,"旧远征区已退役，已安全返回营地");
}

static int prepare_game(AutoTree root,char *message_id) {
    static int workers_started;
    if (!renderer_init()) {
        ccd_ui_id(root,message_id).text=renderer_startup_error();
        return 0;
    }
    if (!workers_started) {
        if (!world_stream_start() || !renderer_start_meshing()) {
            world_stream_stop();
            const char *message="无法启动地形后台线程，请再试一次。";
            ccd_ui_id(root,message_id).text=message;
            fprintf(stderr,"CCDMC: %s\n",message);
            fflush(stderr);
            return 0;
        }
        workers_started=1;
    }
    return 1;
}

static const char *difficulty_names[]={"和平","简单","普通","困难"};
static const char *difficulty_labels[]={"难度：和平","难度：简单","难度：普通","难度：困难"};
static const char *difficulty_ids[]={"difficultyPeaceful","difficultyEasy","difficultyNormal","difficultyHard"};

static void select_difficulty(AutoTree root,AutoTree sender) {
    if (current_page!=PAGE_CREATE && current_page!=PAGE_PAUSE) return;
    const char *id=(const char *)sender.id;
    if (!id) return;
    for (int i=0;i<=HARD;++i) if (!strcmp(id,difficulty_ids[i])) {
        int ok=save_set_difficulty((GameDifficulty)i);
        if (ok) ccd_ui_id(root,"difficultyMenu").text=difficulty_labels[i];
        const char *message=ok ? "普通世界共享这个难度；极限世界始终是困难。" : save_error();
        if (current_page==PAGE_CREATE) ccd_ui_id(root,"createMessage").text=message;
        else ccd_ui_id(root,"pauseMessage").text=message;
        return;
    }
}

static void append_difficulty(AutoTree rows,int locked) {
    if (locked) {
        tree_array_append(rows,titled(NULL,"难度：困难 · 极限模式锁定",15,"#F5B7A7"));
        return;
    }
    AutoTree choices=tree_newarr();
    for (int i=0;i<=HARD;++i)
        tree_array_append(choices,menu_button(difficulty_ids[i],difficulty_names[i],&select_difficulty));
    AutoTree menu=@{type:"menu",id:"difficultyMenu",sub:choices,height:44,fontSize:17};
    menu.text=difficulty_labels[game_selected_difficulty()];
    tree_array_append(rows,menu);
}

static const char *const profile_ids[]={"worldStandard","worldSuperflat","worldLargeBiomes","worldAmplified"};
static const char *const profile_labels[]={"世界类型：默认","世界类型：超平坦",
    "世界类型：巨型生物群系","世界类型：放大化"};
static const char *const profile_descriptions[]={
    "标准地形：海洋、山地、洞穴与不同群系。", "选择模板或修改地层代码；结构受地形与开关限制。",
    "扩大群系、海岸与河流区域，保留局部细节。", "提高陆地起伏，可产生更高的山脊与悬崖。"
};

static void select_flat_preset(AutoTree root,AutoTree sender) {
    if (current_page!=PAGE_CREATE || create_profile!=WORLD_SUPERFLAT) return;
    const char *id=(const char *)sender.id;
    if (!id || strlen(id)!=5 || strncmp(id,"flat",4) || id[4]<'0' || id[4]>='0'+FLAT_PRESETS) return;
    const FlatPreset *preset=&flat_presets[id[4]-'0'];
    ccd_ui_id(root,"flatCode").text=preset->code;
    ccd_ui_id(root,"createMessage").text=profile_descriptions[WORLD_SUPERFLAT];
}

static AutoTree flat_options(void) {
    AutoTree rows=tree_newarr(),choices=tree_newarr();
    for (int i=0;i<FLAT_PRESETS;++i) {
        char id[]={'f','l','a','t',(char)('0'+i),0};
        tree_array_append(choices,menu_button(id,flat_presets[i].name,&select_flat_preset));
    }
    AutoTree menu=@{type:"menu",id:"flatPresetMenu",text:"地层预设…",sub:choices,height:44,fontSize:17};
    tree_array_append(rows,menu);
    AutoTree code=input("地层配方（留空使用基础平地）");
    code.id="flatCode"; code.accessibilityLabel="地层代码";
    code.height=44; code.colorfg="#FFFFFF"; code.colorbg="#363D32";
    code.text=create_flat_code;
    tree_array_append(rows,code);
    tree_array_append(rows,titled(NULL,"从下到上：bedrock,4*loam,turf | meadow | settlements",14,"#CDD8CA"));
    return rows;
}

static void select_world_profile(AutoTree root,AutoTree sender) {
    if (current_page!=PAGE_CREATE) return;
    const char *id=(const char *)sender.id;
    if (!id) return;
    for (int i=0;i<WORLD_PROFILE_COUNT;++i) if (!strcmp(id,profile_ids[i])) {
        WorldProfile previous=create_profile;
        if (previous==(WorldProfile)i) return;
        if (previous==WORLD_SUPERFLAT) {
            const char *code=(const char *)ccd_ui_id(root,"flatCode").text;
            snprintf(create_flat_code,sizeof create_flat_code,"%s",code ? code : "");
            ccd_ui_id(root,"flatOptions").sub=tree_newarr();
        }
        create_profile=(WorldProfile)i;
        if (create_profile==WORLD_SUPERFLAT) ccd_ui_id(root,"flatOptions").sub=flat_options();
        ccd_ui_id(root,"worldProfileMenu").text=profile_labels[i];
        ccd_ui_id(root,"createMessage").text=profile_descriptions[i];
        return;
    }
}

static AutoTree world_profile_menu(void) {
    AutoTree choices=tree_newarr();
    for (int i=0;i<WORLD_PROFILE_COUNT;++i)
        tree_array_append(choices,menu_button(profile_ids[i],world_profile_name((WorldProfile)i),&select_world_profile));
    AutoTree menu=@{type:"menu",id:"worldProfileMenu",sub:choices,height:44,fontSize:17};
    menu.text=profile_labels[create_profile];
    return menu;
}

static const char *structures_label(int disabled) { return disabled ? "生成结构：关" : "生成结构：开"; }
static const char *bonus_chest_label(int enabled) { return enabled ? "奖励箱：开" : "奖励箱：关"; }

static void toggle_create_structures(AutoTree root,AutoTree sender) {
    (void)sender;
    if (current_page!=PAGE_CREATE) return;
    create_no_structures=!create_no_structures;
    ccd_ui_id(root,"createStructures").text=structures_label(create_no_structures);
    ccd_ui_id(root,"createMessage").text=create_no_structures ?
        "不会生成聚落、采样廊或其他天然结构。" : "生成适用于所选世界类型的天然结构。";
}

static void toggle_create_bonus(AutoTree root,AutoTree sender) {
    (void)sender;
    if (current_page!=PAGE_CREATE) return;
    create_bonus_chest=!create_bonus_chest;
    ccd_ui_id(root,"createBonusChest").text=bonus_chest_label(create_bonus_chest);
    ccd_ui_id(root,"createMessage").text=create_bonus_chest ?
        "出生点附近放置一个补给箱；极限世界不适用。" : profile_descriptions[create_profile];
}

static void create_submit(AutoTree root, AutoTree sender) {
    if (current_page != PAGE_CREATE) return;
    uint64_t seed=0;
    WorldSeedInput seed_input=world_seed_parse((const char *)ccd_ui_id(root,"worldSeed").text,&seed);
    if (seed_input==SEED_INVALID) {
        ccd_ui_id(root,"createMessage").text="种子须是有效文字（UTF-8 不超过 256 字节）。";
        return;
    }
    const char *id = (const char *)sender.id;
    int hardcore=id && !strcmp(id,"createHardcore");
    if (hardcore && create_bonus_chest) {
        ccd_ui_id(root,"createMessage").text="极限模式不能使用奖励箱，请先关闭奖励箱。";
        return;
    }
    FlatLayout flat;
    if (create_profile==WORLD_SUPERFLAT) {
        const char *error=flat_parse((const char *)ccd_ui_id(root,"flatCode").text,&flat);
        if (error) { ccd_ui_id(root,"createMessage").text=error; return; }
    }
    if (!prepare_game(root,"createMessage")) return;
    GameMode mode=id && !strcmp(id,"createCreative") ? CREATIVE : SURVIVAL;
    const char *name=(const char *)ccd_ui_id(root,"worldName").text;
    int ok=save_create_layout(name,mode,hardcore,create_profile,seed_input==SEED_EXPLICIT ? &seed : NULL,
        create_no_structures,create_bonus_chest,create_profile==WORLD_SUPERFLAT ? &flat : NULL);
    if (ok) start_play(root);
    else ccd_ui_id(root, "createMessage").text = save_error();
}

static void cancel_create(AutoTree root, AutoTree sender) {
    (void)sender;
    show_world_list(root, NULL);
}

static void new_world_form(AutoTree root, AutoTree sender) {
    (void)sender;
    current_page = PAGE_CREATE;
    create_profile=WORLD_STANDARD;
    create_no_structures=create_bonus_chest=0;
    create_flat_code[0]=0;
    AutoTree rows = tree_newarr();
    tree_array_append(rows, titled(NULL, "创建新世界", 26, "#FFFFFF"));
    AutoTree name = input("世界名称（可留空）");
    name.id = "worldName"; name.height = 44;
    name.colorfg = "#FFFFFF"; name.colorbg = "#363D32";
    tree_array_append(rows, name);
    tree_array_append(rows,titled(NULL,"世界种子",14,"#CDD8CA"));
    AutoTree seed = input("数字或文字，可留空");
    seed.id="worldSeed"; seed.height=44; seed.colorfg="#FFFFFF"; seed.colorbg="#363D32";
    tree_array_append(rows,seed);
    tree_array_append(rows,world_profile_menu());
    AutoTree flat=vstack(tree_newarr());
    flat.id="flatOptions"; flat.spacing=5;
    tree_array_append(rows,flat);
    AutoTree options=tree_newarr();
    tree_array_append(options,menu_button("createStructures",structures_label(0),&toggle_create_structures));
    tree_array_append(options,menu_button("createBonusChest",bonus_chest_label(0),&toggle_create_bonus));
    AutoTree options_row=hstack(options);
    options_row.spacing=20;
    tree_array_append(rows,options_row);
    tree_array_append(rows,titled("createMessage",profile_descriptions[WORLD_STANDARD],14,"#CDD8CA"));
    append_difficulty(rows,0);
    tree_array_append(rows, menu_button("createWorld", "创建生存世界", &create_submit));
    tree_array_append(rows, menu_button("createCreative", "创建创造世界", &create_submit));
    tree_array_append(rows, menu_button("createHardcore", "创建极限世界（仅一命）", &create_submit));
    tree_array_append(rows, menu_button("cancelCreate", "返回世界列表", &cancel_create));
    show_panel(root, rows, 0);
}

static void load_world(AutoTree root, AutoTree sender) {
    if (current_page != PAGE_WORLDS) return;
    const char *id = (const char *)sender.id;
    if (!id || strncmp(id, "world_", 6)) return;
    char *end;
    unsigned long n = strtoul(id + 6, &end, 10);
    if (!n || n > UINT32_MAX || *end) return;
    if (!prepare_game(root,"worldListMessage")) return;
    if (save_load((uint32_t)n)) start_play(root);
    else ccd_ui_id(root, "worldListMessage").text = save_error();
}

void show_world_list(AutoTree root, const char *message) {
    chest_access_end();
    world_set_interactive(0);
    current_page = PAGE_WORLDS;
    AutoTree rows = tree_newarr();
    SaveInfo *worlds;
    size_t count;
    int available = save_list(&worlds, &count);
    tree_array_append(rows, titled(NULL, "方块沙盒 3D", 28, "#FFFFFF"));
    tree_array_append(rows, titled(NULL, "选择世界", 20, "#CBDEB5"));
    if (!available) message = save_error();
    if (!message) message = count ? "选择一个世界继续游戏" : "还没有世界。创建一个开始冒险吧！";
    tree_array_append(rows, titled("worldListMessage", (char *)message, 14, "#CDD8CA"));
    if (available) tree_array_append(rows, menu_button("newWorld", "创建新世界", &new_world_form));
    for (size_t i = 0; i < count; i++) {
        char id[24], detail[148];
        snprintf(id, sizeof id, "world_%lu", (unsigned long)worlds[i].id);
        if (!worlds[i].readable) {
            tree_array_append(rows, titled(NULL, worlds[i].name, 15, "#F5B7A7"));
            continue;
        }
        const char *mode=worlds[i].hardcore_dead ? "极限 · 已结束" : worlds[i].hardcore ? "极限" : game_mode_name(worlds[i].mode);
        snprintf(detail, sizeof detail, "%s · %s%s%s · 种子 %" PRId64, mode,
            worlds[i].generation == GENERATOR_LEGACY ? "旧版地形" : world_profile_name(worlds[i].profile),
            worlds[i].flat.height ? " / " : "",worlds[i].flat.height ? flat_name(&worlds[i].flat) : "",
            world_seed_signed(worlds[i].seed));
        AutoTree card_rows = tree_newarr();
        AutoTree enter = menu_button(id, worlds[i].name, &load_world);
        enter.accessibilityLabel = worlds[i].name;
        tree_array_append(card_rows, enter);
        tree_array_append(card_rows, titled(NULL, detail, 13, "#CDD8CA"));
        AutoTree card = vstack(card_rows);
        card.spacing = 0;
        tree_array_append(rows, card);
    }
    free(worlds);
    show_panel(root, rows, 0);
}

static void resume_game(AutoTree root, AutoTree sender) {
    (void)sender;
    leave_panel(root);
    status(root, "");
}
static void save_now(AutoTree root, AutoTree sender) {
    (void)sender;
    int ok = save_write();
    ccd_ui_id(root, "pauseMessage").text = ok ? "游戏已保存" : save_error();
    if (ok) { puts("WORLD SAVED"); fflush(stdout); }
}
static void profile_frames(AutoTree root, AutoTree sender) {
    (void)sender;
    if (current_page != PAGE_PAUSE) return;
    start_frame_sample();
    ccd_ui_id(root, "pauseMessage").text = "返回游戏后记录 120 帧；结果输出到终端。";
}
static void save_and_quit(AutoTree root, AutoTree sender) {
    (void)sender;
    if (save_write()) {
        save_leave();
        show_world_list(root, "世界已保存");
    } else ccd_ui_id(root, "pauseMessage").text = save_error();
}

static void respawn(AutoTree root, AutoTree sender) {
    (void)sender;
    if (current_page != PAGE_DEATH || player_rules.hardcore) return;
    world_set_interactive(0);
    RespawnResult result=player_respawn();
    start_play(root);
    status(root,result==RESPAWN_RETAINED ? "已重生；暂时无法放置掉落物，背包未扣除。" : "已重生");
    if (!save_write()) note_autosave(root,0);
}

void show_death(AutoTree root) {
    if (current_page == PAGE_DEATH) return;
    chest_access_end();
    if (trade_session) { villagers_close(trade_session); trade_session=0; }
    current_page = PAGE_DEATH;
    mx = my = touch_altitude = 0;
    AutoTree rows = tree_newarr();
    tree_array_append(rows, titled(NULL, player_rules.hardcore ? "极限世界已结束" : "你死了", 26, "#F5B7A7"));
    tree_array_append(rows,titled(NULL,player_rules.hardcore ? "只有一次生命。世界存档保留，但不能重生。" :
        death_keep_inventory() ? "本世界开启了死亡保留物品和经验。" :
        "物品和部分经验留在死亡地点；熔岩和虚空可能让它们消失。",15,"#CDD8CA"));
    if (!player_rules.hardcore) tree_array_append(rows,menu_button("respawn","重生",&respawn));
    tree_array_append(rows, menu_button("deadSaveQuit", "保存并退出到世界列表", &save_and_quit));
    tree_array_append(rows, titled("pauseMessage", "", 14, "#CDD8CA"));
    show_panel(root, rows, 0);
}

static const char *keep_label(void) {
    return death_keep_inventory() ? "死亡保留物品：开" : "死亡保留物品：关";
}
static void toggle_keep_inventory(AutoTree root,AutoTree sender) {
    if (current_page!=PAGE_PAUSE || player_rules.mode!=SURVIVAL || player_rules.hardcore) return;
    int previous=death_keep_inventory();
    death_set_keep_inventory(!previous);
    if (!save_write()) {
        death_set_keep_inventory(previous);
        ccd_ui_id(root,"pauseMessage").text=save_error();
        return;
    }
    sender.text=keep_label();
    ccd_ui_id(root,"pauseMessage").text=death_keep_inventory() ? "已开启；仅影响当前世界。" :
        "已关闭；死亡后可回原地找回掉落物。";
}

static const char *advanced_lighting_label(void) {
    return save_advanced_lighting() ? "高级光影：开" : "高级光影：关";
}

static void toggle_advanced_lighting(AutoTree root,AutoTree sender) {
    if (current_page!=PAGE_PAUSE) return;
    if (!save_set_advanced_lighting(!save_advanced_lighting())) {
        ccd_ui_id(root,"pauseMessage").text=save_error();
        return;
    }
    sender.text=advanced_lighting_label();
    ccd_ui_id(root,"pauseMessage").text=save_advanced_lighting() ?
        "已启用太阳阴影、水面反射和体积云。" : "已关闭额外光影，保留方块光照和夜视。";
}

void pause_game(AutoTree root, AutoTree sender) {
    (void)sender;
    if (!playing() || !save_current()) return;
    current_page = PAGE_PAUSE;
    mx = my = touch_altitude = 0;
    char detail[SAVE_NAME_MAX + 76];
    const SaveInfo *info = save_current();
    snprintf(detail,sizeof detail,"%s  ·  %s  ·  种子 %" PRId64,info->name,
        world_profile_name(info->profile),world_seed_signed(info->seed));
    AutoTree rows = tree_newarr();
    tree_array_append(rows, titled(NULL, "游戏菜单", 26, "#FFFFFF"));
    tree_array_append(rows, titled(NULL, detail, 15, "#CBDEB5"));
    if (info->structures_disabled || info->bonus_chest) {
        char settings[64];
        snprintf(settings,sizeof settings,"%s  ·  %s",structures_label(info->structures_disabled),
            bonus_chest_label(info->bonus_chest));
        tree_array_append(rows,titled(NULL,settings,13,"#CDD8CA"));
    }
    tree_array_append(rows, menu_button("resumeWorld", "返回游戏", &resume_game));
    tree_array_append(rows, menu_button("saveNow", "保存游戏", &save_now));
    tree_array_append(rows, menu_button("saveQuit", "保存并退出到世界列表", &save_and_quit));
    append_difficulty(rows,info->hardcore);
    if (player_rules.mode==SURVIVAL && !info->hardcore)
        tree_array_append(rows,menu_button("deathKeepInventory",keep_label(),&toggle_keep_inventory));
    tree_array_append(rows,menu_button("advancedLighting",advanced_lighting_label(),&toggle_advanced_lighting));
    tree_array_append(rows, menu_button("recordFrames", "记录帧时", &profile_frames));
    tree_array_append(rows, titled("pauseMessage", "游玩期间会自动保存。", 14, "#CDD8CA"));
    show_panel(root, rows, 0);
}

void note_autosave(AutoTree root, int success) {
    if (success) return;
    if (playing()) status(root, "自动保存失败，请打开菜单重试");
    else if (current_page==PAGE_PAUSE || current_page==PAGE_DEATH) ccd_ui_id(root,"pauseMessage").text=save_error();
    else if (chest_panel_visible()) ccd_ui_id(root,"storageMessage").text="自动保存失败；返回游戏后请手动保存";
    else if (villager_panel_visible()) ccd_ui_id(root,"tradeMessage").text="自动保存失败；返回游戏后请手动保存";
}

static const char *const trade_ids[VILLAGER_OFFERS]={"tradeBuy0","tradeBuy1","tradeBuy2"};
static void trade_button_text(char out[72],const VillagerOffer *offer) {
    if (offer->remaining>0) snprintf(out,72,"交付 · %s（余 %d 单）",offer->name,offer->remaining);
    else snprintf(out,72,"%s · 等待补货",offer->name);
}
static void trade_progress_text(char out[108]) {
    int left=villagers_handoffs_needed(trade_session);
    if (left>0) snprintf(out,108,"向最新订单再供货 %d 次，解锁下一项。",left);
    else snprintf(out,108,"全部订单已开放。缺货时返回游戏让工坊补货。");
}
void close_villager(AutoTree root) {
    if (!villager_panel_visible()) return;
    leave_panel(root);
    status(root,"");
}
static void close_trade_action(AutoTree root,AutoTree sender) { (void)sender; close_villager(root); }
void update_villager_panel(AutoTree root) {
    if (villager_panel_visible() && !villagers_count(trade_session)) {
        close_villager(root);
        status(root,"居民已离开交易范围");
    }
}
static void show_trade(AutoTree root,int role,int count,const char *message);
static void trade_exchange(AutoTree root,AutoTree sender) {
    if (!villager_panel_visible()) return;
    const char *id=(const char *)sender.id;
    if (!id) return;
    for (int row=0;row<VILLAGER_OFFERS;++row) if (!strcmp(id,trade_ids[row])) {
        int ok=0;
        int before=villagers_count(trade_session);
        const char *message=villagers_exchange(trade_session,row,&ok);
        int after=villagers_count(trade_session);
        if (ok && after>before) {
            show_trade(root,villagers_role(trade_session),after,message);
            return;
        }
        ccd_ui_id(root,"tradeMessage").text=message;
        VillagerOffer offer;
        if (ok && villagers_offer(trade_session,row,&offer)) {
            char state[72],progress[108];
            trade_button_text(state,&offer);
            trade_progress_text(progress);
            ccd_ui_id(root,(char *)trade_ids[row]).text=state;
            ccd_ui_id(root,"tradeProgress").text=progress;
        }
        break;
    }
}
static void show_trade(AutoTree root,int role,int count,const char *message) {
    AutoTree rows=tree_newarr();
    char title[96],progress[108],region[80];
    snprintf(title,sizeof title,"工坊订单 · %s",villagers_role_name((unsigned)role));
    snprintf(region,sizeof region,"工坊地带：%s",villagers_workshop_region(trade_session));
    trade_progress_text(progress);
    tree_array_append(rows,titled(NULL,title,24,"#F4EBD8"));
    tree_array_append(rows,menu_button("tradeBack","返回游戏",&close_trade_action));
    tree_array_append(rows,titled(NULL,region,13,"#A9CEC3"));
    tree_array_append(rows,titled("tradeMessage",message,14,"#F4EBD8"));
    tree_array_append(rows,titled("tradeProgress",progress,14,"#CDD8CA"));
    for (int row=0;row<count;++row) {
        VillagerOffer offer;
        if (!villagers_offer(trade_session,row,&offer)) continue;
        char state[72],detail[208];
        if (offer.cost[1].count) snprintf(detail,sizeof detail,"%s × %u + %s × %u → %s × %u",
            item_name(offer.cost[0].type),(unsigned)offer.cost[0].count,item_name(offer.cost[1].type),
            (unsigned)offer.cost[1].count,item_name(offer.result.type),(unsigned)offer.result.count);
        else snprintf(detail,sizeof detail,"%s × %u → %s × %u",item_name(offer.cost[0].type),
            (unsigned)offer.cost[0].count,item_name(offer.result.type),(unsigned)offer.result.count);
        tree_array_append(rows,titled(NULL,detail,15,"#F4EBD8"));
        AutoTree icons=tree_newarr();
        tree_array_append(icons,make_item_slot("tradePay",row,NULL));
        if (offer.cost[1].count) tree_array_append(icons,make_item_slot("tradePayExtra",row,NULL));
        tree_array_append(icons,titled(NULL,"→",17,"#DCD1B4"));
        tree_array_append(icons,make_item_slot("tradeGet",row,NULL));
        AutoTree icon_row=hstack(icons);
        icon_row.height=SLOT_EDGE; icon_row.spacing=7;
        tree_array_append(rows,icon_row);
        trade_button_text(state,&offer);
        tree_array_append(rows,menu_button(trade_ids[row],state,&trade_exchange));
    }
    show_panel(root,rows,0);
    for (int row=0;row<count;++row) {
        VillagerOffer offer;
        if (!villagers_offer(trade_session,row,&offer)) continue;
        paint_item_stack(root,"tradePay",row,offer.cost[0]);
        if (offer.cost[1].count) paint_item_stack(root,"tradePayExtra",row,offer.cost[1]);
        paint_item_stack(root,"tradeGet",row,offer.result);
    }
}
void open_villager(AutoTree root,int actor) {
    if (!playing()) return;
    trade_session=villagers_open(actor);
    int role=villagers_role(trade_session),count=villagers_count(trade_session);
    if (role<0 || !count) { villagers_close(trade_session); trade_session=0; return; }
    current_page=PAGE_VILLAGER;
    show_trade(root,role,count,"按订单提供背包材料，不需要兑换货币。");
}

static void clear_empty_bag_focus(AutoTree root) {
    if (empty_bag_focus>=0) paint_item_slot(root,"bagSlot",empty_bag_focus,0);
    empty_bag_focus=-1;
}

static void creative_hotbar_tap(AutoTree root,int i) {
    int previous=selected_slot, from=held_index;
    if (!game_select_hotbar(i)) return;
    char message[ITEM_NAME_BYTES+100];
    if (from>=0) {
        inventory_move(from,i);
        held_index=-1;
        snprintf(message,sizeof message,"%s；当前选中快捷栏第 %d 格",i==from ? "已放回原格" : "已移动",i+1);
    } else if (i==previous && inventory[i].count) {
        held_index=i;
        snprintf(message,sizeof message,"拿起：%s · 点目标格放下",item_display_name(inventory[i]));
    } else {
        snprintf(message,sizeof message,"快捷栏第 %d 格 · %s · 点下方物品取到此格",i+1,
            inventory[i].count ? item_display_name(inventory[i]) : "空格");
    }
    if (previous!=i) paint_item_slot(root,"bagSlot",previous,0);
    if (from>=0 && from!=previous && from!=i) paint_item_slot(root,"bagSlot",from,0);
    paint_item_slot(root,"bagSlot",i,1);
    status(root,"");
    ccd_ui_id(root,"bagMessage").text=message;
}

static void bag_tap(AutoTree root, AutoTree sender) {
    if (current_page!=PAGE_BAG && current_page!=PAGE_ENCHANTING && current_page!=PAGE_ANVIL &&
        !furnace_panel_visible() && !brewing_panel_visible()) return;
    int i = item_slot_index(sender, "bagSlot");
    if (i < 0) return;
    if (current_page==PAGE_BAG && player_rules.mode==CREATIVE) {
        creative_hotbar_tap(root,i);
        return;
    }
    if (current_page==PAGE_ANVIL) { anvil_pick(root,i); return; }
    char text[220];
    if (current_page==PAGE_ENCHANTING) {
        int previous=held_index;
        held_index=i==previous || !inventory[i].count ? -1 : i;
        if (previous>=0) paint_item_slot(root,"bagSlot",previous,0);
        paint_item_slot(root,"bagSlot",i,i==held_index);
        refresh_enchanting(root);
        return;
    }
    if (current_page==PAGE_BAG) {
        if (!inventory[i].count && (!sorting_bag || held_index<0)) {
            /* Empty focus is visual only and never acts as an item in hand. */
            clear_empty_bag_focus(root);
            if (held_index>=0) paint_item_slot(root,"bagSlot",held_index,0);
            held_index=-1;
            empty_bag_focus=i;
            paint_item_slot(root,"bagSlot",i,1);
            snprintf(text,sizeof text,"已选中%s第 %d 格（空）",i<HOTBAR_SIZE ? "快捷栏" : "背包",
                (i<HOTBAR_SIZE ? i : i-HOTBAR_SIZE)+1);
            ccd_ui_id(root,"bagMessage").text=text;
            return;
        }
        clear_empty_bag_focus(root);
    }
    if (player_rules.mode == SURVIVAL && !sorting_bag) {
        int previous = held_index;
        held_index = i == held_index || !inventory[i].count ? -1 : i;
        if (previous >= 0) paint_item_slot(root, "bagSlot", previous, 0);
        if (held_index >= 0) paint_item_slot(root, "bagSlot", held_index, 1);
        if (held_index>=0) snprintf(text,sizeof text,"已选：%s · %s",item_display_name(inventory[i]),
            brewing_panel_visible() ? "点瓶位或材料位" : furnace_panel_visible() ? "点炉槽放进去" :
            current_page==PAGE_BAG && craft_size==2 && horse_can_equip_mounted() &&
                horse_armor_points(inventory[i].type) ? "点坐骑马铠格装备" :
            craft_size==2 && armor_slot(inventory[i].type)>=0 ? "点对应部位装备，或放入配方格修复" : "点配方格放入一份");
        else snprintf(text,sizeof text,"%s",brewing_panel_visible() ? "未选物品时，点调剂槽取回瓶子或原料。" :
            furnace_panel_visible() ? "未选材料时点炉槽取回物品。" : "点选材料，再点上方合成格。");
        if (held_index>=0 && inventory[i].enchants) {
            size_t used=strlen(text);
            if (used+4<sizeof text) {
                char effects[120];
                enchant_describe(effects,sizeof effects,inventory[i].enchants);
                snprintf(text+used,sizeof text-used,"\n%s",effects);
            }
        }
        if (held_index>=0 && firework_item(inventory[i].type)) {
            size_t n=strlen(text);
            char effects[80];
            firework_describe(firework_spec(inventory[i]),effects,sizeof effects);
            if (n<sizeof text) snprintf(text+n,sizeof text-n,"\n%s",effects);
        }
    } else if (held_index < 0) {
        if (!inventory[i].count) return;
        held_index = i;
        paint_item_slot(root, "bagSlot", i, 1);
        snprintf(text, sizeof text, "拿起：%s × %u · %s", item_display_name(inventory[i]), (unsigned)inventory[i].count,
            current_page==PAGE_BAG && craft_size==2 && horse_can_equip_mounted() &&
                horse_armor_points(inventory[i].type) ? "点坐骑马铠格装备，或点目标格放下" : "点目标格放下");
    } else {
        int from = held_index;
        held_index = -1;
        inventory_move(from, i);
        paint_item_slot(root, "bagSlot", from, 0);
        paint_item_slot(root, "bagSlot", i, 0);
        status(root, "");
        if (i == from) snprintf(text, sizeof text, "已放回原格");
        else snprintf(text, sizeof text, "目标格：%s × %u", item_display_name(inventory[i]), (unsigned)inventory[i].count);
    }
    ccd_ui_id(root, "bagMessage").text = text;
}

static void reset_recipe(void) {
    for (int i = 0; i < 9; i++) craft_sources[i] = -1;
}

static void update_recipe(AutoTree root) {
    for (int i = 0; i < craft_size * craft_size; i++) {
        int src = craft_sources[i];
        ItemStack item = {0, 0, 0,0,0};
        if (src >= 0 && src < SLOT_COUNT && inventory[src].count) {
            item = inventory[src];
            item.count = 1;
        }
        paint_item_stack(root, "recipeSlot", i, item);
    }
    char desc[144],details[80];
    ItemStack result=craft_preview_details(craft_sources,craft_size,details,sizeof details);
    paint_item_stack(root, "recipeOutput", 0, result);
    if (result.count) snprintf(desc, sizeof desc, "取出：%s × %u%s%s", item_name(result.type), (unsigned)result.count,
        *details ? " · " : "",details);
    else snprintf(desc, sizeof desc, "合成结果");
    ccd_ui_id(root, "recipeResult").text = desc;
}

static void toggle_bag_mode(AutoTree root, AutoTree sender) {
    if (current_page != PAGE_BAG || player_rules.mode != SURVIVAL) return;
    sorting_bag = !sorting_bag;
    clear_empty_bag_focus(root);
    if (held_index >= 0) paint_item_slot(root, "bagSlot", held_index, 0);
    held_index = -1;
    reset_recipe();
    update_recipe(root);
    sender.text = sorting_bag ? "返回合成" : "整理背包";
    ccd_ui_id(root, "bagMessage").text = sorting_bag ? "点物品拿起，再点目标格整理" : "点选材料，再点上方合成格。";
}

static void recipe_tap(AutoTree root, AutoTree sender) {
    if (current_page != PAGE_BAG || sorting_bag || player_rules.mode != SURVIVAL) return;
    int pos = item_slot_index(sender, "recipeSlot");
    if (pos < 0 || pos >= craft_size * craft_size) return;
    int source = held_index;
    if (craft_sources[pos] >= 0 && (source < 0 || source == craft_sources[pos])) {
        craft_sources[pos] = -1;
    } else {
        if (source < 0 || !inventory[source].count) {
            ccd_ui_id(root, "bagMessage").text = "先点选背包或快捷栏里的材料。";
            return;
        }
        int used = 0;
        for (int i = 0; i < craft_size * craft_size; i++) used += craft_sources[i] == source;
        if (used >= inventory[source].count) {
            ccd_ui_id(root, "bagMessage").text = "这一格的材料不够。";
            return;
        }
        craft_sources[pos] = source;
    }
    ccd_ui_id(root, "bagMessage").text = "改变配方格；点结果取出成品。";
    update_recipe(root);
}

static void take_recipe(AutoTree root, AutoTree sender) {
    (void)sender;
    if (current_page != PAGE_BAG || sorting_bag || player_rules.mode != SURVIVAL) return;
    ItemStack result = craft_preview(craft_sources, craft_size);
    int selected_type = held_index >= 0 ? inventory[held_index].type : AIR;
    if (!result.count || !craft_take(craft_sources, craft_size)) {
        ccd_ui_id(root, "bagMessage").text = result.count ? "背包空间不足，材料未消耗。" : "配方不匹配，检查格子和材料。";
        return;
    }
    clear_empty_bag_focus(root);
    reset_recipe();
    update_recipe(root);
    if (held_index >= 0 && (!inventory[held_index].count || inventory[held_index].type != selected_type))
        held_index = -1;
    for (int i = 0; i < SLOT_COUNT; i++) paint_item_slot(root, "bagSlot", i, i == held_index);
    status(root, "");
    char text[80];
    snprintf(text, sizeof text, "已合成：%s × %u", item_name(result.type), (unsigned)result.count);
    ccd_ui_id(root, "bagMessage").text = text;
    printf("CRAFT item=%u count=%u\n", (unsigned)result.type, (unsigned)result.count);
    fflush(stdout);
}

static AutoTree recipe_grid(void) {
    AutoTree rows = tree_newarr();
    for (int y = 0; y < craft_size; y++) {
        AutoTree cells = tree_newarr();
        for (int x = 0; x < craft_size; x++)
            tree_array_append(cells, make_item_slot("recipeSlot", y * craft_size + x, &recipe_tap));
        AutoTree row = hstack(cells);
        row.spacing = 0;
        tree_array_append(rows, row);
    }
    AutoTree in = vstack(rows);
    in.spacing = 0;
    AutoTree out_items = tree_newarr();
    tree_array_append(out_items, make_item_slot("recipeOutput", 0, &take_recipe));
    tree_array_append(out_items, titled("recipeResult", "合成结果", 14, "#252525"));
    AutoTree out = vstack(out_items);
    /* The preview text is variable, but the grid and output must not move
       under the player's finger when the last ingredient matches. */
    out.width = 116;
    AutoTree columns = tree_newarr();
    tree_array_append(columns, in);
    tree_array_append(columns, titled(NULL, "→", 20, "#303030"));
    tree_array_append(columns, out);
    AutoTree group = hstack(columns);
    group.spacing = 10;
    return group;
}

static int bag_slot_highlighted(int slot,int creative) {
    if (creative) return slot==selected_slot;
    if (current_page==PAGE_ANVIL) return slot==anvil_source[0] || slot==anvil_source[1];
    return slot==held_index || (current_page==PAGE_BAG && slot==empty_bag_focus);
}
static AutoTree bag_row(int first,int edge,int creative) {
    AutoTree cells = tree_newarr();
    for (int i = first; i < first + HOTBAR_SIZE; i++)
        tree_array_append(cells,make_inventory_slot(i,&bag_tap,edge,bag_slot_highlighted(i,creative)));
    AutoTree row = hstack(cells);
    row.spacing = 0; row.width = HOTBAR_SIZE * edge; row.height = edge;
    return row;
}

static void refresh_armor_grid(AutoTree root) {
    char label_text[48];
    snprintf(label_text,sizeof label_text,"护甲 · %d / %d",armor_points(),ARMOR_MAX_POINTS);
    ccd_ui_id(root,"armorTitle").text=label_text;
    for (int i=0;i<ARMOR_SLOTS;++i) paint_item_stack(root,"armorSlot",i,armor_worn(i));
}
static void tap_armor_slot(AutoTree root,AutoTree sender) {
    if (current_page!=PAGE_BAG || craft_size!=2 || player_rules.mode!=SURVIVAL) return;
    int slot=item_slot_index(sender,"armorSlot");
    if ((unsigned)slot>=ARMOR_SLOTS) return;
    const char *result;
    if (held_index>=0) {
        int source=held_index;
        if (!armor_equip(source,slot)) {
            ccd_ui_id(root,"bagMessage").text="所选物品不能装备到这个部位。";
            return;
        }
        held_index=-1;
        paint_item_slot(root,"bagSlot",source,0);
        result="已装备；替换的护甲放回原格。";
    } else {
        if (!armor_worn(slot).count) return;
        if (!armor_unequip(slot)) {
            ccd_ui_id(root,"bagMessage").text="背包已满，护甲未卸下。";
            return;
        }
        clear_empty_bag_focus(root);
        for (int i=0;i<SLOT_COUNT;++i) paint_item_slot(root,"bagSlot",i,0);
        result="已卸下护甲。";
    }
    reset_recipe();
    update_recipe(root);
    refresh_armor_grid(root);
    status(root,"");
    ccd_ui_id(root,"bagMessage").text=result;
}
static AutoTree armor_grid(void) {
    AutoTree contents=tree_newarr();
    tree_array_append(contents,titled("armorTitle","护甲",15,"#252525"));
    AutoTree pieces=tree_newarr();
    for (int i=0;i<ARMOR_SLOTS;++i) {
        AutoTree column=tree_newarr();
        tree_array_append(column,make_item_slot("armorSlot",i,&tap_armor_slot));
        tree_array_append(column,titled(NULL,armor_slot_name(i),12,"#252525"));
        AutoTree stack=vstack(column); stack.width=SLOT_EDGE; stack.spacing=3;
        tree_array_append(pieces,stack);
    }
    AutoTree row=hstack(pieces); row.spacing=6;
    tree_array_append(contents,row);
    return vstack(contents);
}

static void refresh_mount_armor(AutoTree root) {
    ItemStack item=horse_mounted_armor();
    char title[ITEM_NAME_BYTES+68];
    if (item.count) snprintf(title,sizeof title,"马铠 · %s · 防御 %d",item_display_name(item),horse_armor_points(item.type));
    else snprintf(title,sizeof title,"马铠 · 未装备");
    ccd_ui_id(root,"mountArmorTitle").text=title;
    paint_item_stack(root,"mountArmor",0,item);
    ccd_ui_id(root,"mountArmor0").accessibilityLabel=title;
}
static void tap_mount_armor(AutoTree root,AutoTree sender) {
    (void)sender;
    if (current_page!=PAGE_BAG || craft_size!=2 || !horse_can_equip_mounted()) return;
    int creative=player_rules.mode==CREATIVE;
    const char *result;
    int source=held_index;
    if (creative && (unsigned)selected_slot<HOTBAR_SIZE && inventory[selected_slot].count &&
        horse_armor_points(inventory[selected_slot].type)) source=selected_slot;
    if (source>=0) {
        if (!horse_equip_mounted(source)) {
            ccd_ui_id(root,"bagMessage").text="请先选中铁、金或钻石马铠。";
            return;
        }
        held_index=-1;
        paint_item_slot(root,"bagSlot",source,creative && source==selected_slot);
        result="已装备马铠；替换下来的马铠放回原格。";
    } else {
        if (!horse_mounted_armor().count) return;
        if (!horse_unequip_mounted()) {
            ccd_ui_id(root,"bagMessage").text=creative ? "快捷栏已满，马铠未卸下。" : "背包已满，马铠未卸下。";
            return;
        }
        clear_empty_bag_focus(root);
        for (int i=0;i<(creative ? HOTBAR_SIZE : SLOT_COUNT);++i)
            paint_item_slot(root,"bagSlot",i,creative && i==selected_slot);
        result="已卸下马铠。";
    }
    if (!creative) { reset_recipe(); update_recipe(root); }
    refresh_mount_armor(root);
    status(root,"");
    ccd_ui_id(root,"bagMessage").text=result;
}
static AutoTree mount_armor_row(void) {
    AutoTree contents=tree_newarr();
    tree_array_append(contents,make_item_slot("mountArmor",0,&tap_mount_armor));
    tree_array_append(contents,titled("mountArmorTitle","马铠",14,"#252525"));
    AutoTree row=hstack(contents); row.spacing=8;
    return row;
}

static void palette_tap(AutoTree root, AutoTree sender) {
    if (current_page != PAGE_BAG || player_rules.mode != CREATIVE) return;
    int type = item_slot_index(sender, "palette");
    if (!inventory_creative_pick(type)) return;
    held_index=-1;
    char message[120];
    snprintf(message, sizeof message, "%s已放入快捷栏第 %d 格", item_name(type), selected_slot + 1);
    paint_item_slot(root, "bagSlot", selected_slot, 1);
    status(root, "");
    ccd_ui_id(root, "bagMessage").text = message;
}

enum { PALETTE_ROWS=3, PALETTE_ITEMS=PALETTE_ROWS*HOTBAR_SIZE };
static int in_catalog(int type) {
    return type!=ENCHANTED_BOOK && type!=FILLED_MAP && type!=REDSTONE_BLOCK &&
        type!=EGG_ARCHIVE_MAGMA && type!=MAGMA_CREAM && type!=COMPARATOR_ITEM && item_def(type) &&
        (uses_aquifer() || !aquifer_item(type)) &&
        (world_generation()>=GENERATOR_PRESSURE_WORKS || type!=SINTERED_TILE_ITEM) &&
        (!palette_eggs || spawn_egg_item(type));
}
/* Never materialize hundreds of Canvas/gesture/path trees in one CCDUI
   callback. Page by catalog index, retaining the original IDs for taps. */
static int catalog_size(void) {
    int count=0;
    for (int type=1;type<ITEM_LIMIT;++type) count+=in_catalog(type);
    return count;
}
static AutoTree palette_rows(int edge) {
    AutoTree rows=tree_newarr(),cells=tree_newarr();
    int ordinal=0,displayed=0;
    for (int type=1;type<ITEM_LIMIT && displayed<PALETTE_ITEMS;++type) {
        if (!in_catalog(type) || ordinal++<palette_page*PALETTE_ITEMS) continue;
        tree_array_append(cells,make_item_preview_slot("palette",type,&palette_tap,edge));
        if (++displayed%HOTBAR_SIZE!=0) continue;
        AutoTree row=hstack(cells);
        row.spacing=0; row.height=edge;
        tree_array_append(rows,row);
        if (displayed<PALETTE_ITEMS) cells=tree_newarr();
    }
    if (displayed%HOTBAR_SIZE) {
        AutoTree row=hstack(cells);
        row.spacing=0; row.height=edge;
        tree_array_append(rows,row);
    }
    return rows;
}
static int palette_page_count(void) { return (catalog_size()+PALETTE_ITEMS-1)/PALETTE_ITEMS; }
static void palette_caption(char out[32],int pages) { snprintf(out,32,"%d / %d",palette_page+1,pages); }
static void palette_move(AutoTree root,int step) {
    if (current_page!=PAGE_BAG || player_rules.mode!=CREATIVE) return;
    int pages=palette_page_count(),next=palette_page+step;
    if (next<0 || next>=pages) return;
    palette_page=next;
    char caption[32]; palette_caption(caption,pages);
    ccd_ui_id(root,"paletteRows").sub=palette_rows(inventory_edge);
    ccd_ui_id(root,"palettePage").text=caption;
    ccd_ui_id(root,"paletteBack").enabled=palette_page>0;
    ccd_ui_id(root,"paletteNext").enabled=palette_page+1<pages;
}
static void palette_back(AutoTree root,AutoTree sender) { (void)sender; palette_move(root,-1); }
static void palette_next(AutoTree root,AutoTree sender) { (void)sender; palette_move(root,1); }
static void palette_toggle_eggs(AutoTree root,AutoTree sender) {
    if (current_page!=PAGE_BAG || player_rules.mode!=CREATIVE) return;
    palette_eggs=!palette_eggs;
    palette_page=0;
    sender.text=palette_eggs ? "全部物品" : "生物蛋";
    palette_move(root,0);
}
static AutoTree palette_navigation(void) {
    int pages=palette_page_count();
    char caption[32]; palette_caption(caption,pages);
    AutoTree back=menu_button("paletteBack","上一页",&palette_back);
    AutoTree next=menu_button("paletteNext","下一页",&palette_next);
    back.enabled=palette_page>0; next.enabled=palette_page+1<pages;
    AutoTree elements=tree_newarr();
    tree_array_append(elements,back);
    tree_array_append(elements,titled("palettePage",caption,15,"#252525"));
    tree_array_append(elements,next);
    tree_array_append(elements,menu_button("paletteEggs",palette_eggs ? "全部物品" : "生物蛋",&palette_toggle_eggs));
    AutoTree bar=hstack(elements);
    bar.spacing=12;
    return bar;
}

static AutoTree inventory_grid_content(int creative,int edge) {
    AutoTree grid_rows = tree_newarr();
    if (creative) {
        tree_array_append(grid_rows,titled(NULL,"快捷栏",15,"#252525"));
        tree_array_append(grid_rows,bag_row(0,edge,creative));
        tree_array_append(grid_rows,titled(NULL,"可取用的方块与物品",15,"#252525"));
        AutoTree palette=vstack(palette_rows(edge));
        palette.id="paletteRows";
        palette.height=PALETTE_ROWS*edge;
        palette.spacing=0;
        tree_array_append(grid_rows,palette);
    } else {
        tree_array_append(grid_rows, titled(NULL, "背包", 15, "#252525"));
        for (int i = HOTBAR_SIZE; i < SLOT_COUNT; i += HOTBAR_SIZE)
            tree_array_append(grid_rows,bag_row(i,edge,creative));
        tree_array_append(grid_rows, titled(NULL, "快捷栏", 15, "#252525"));
        tree_array_append(grid_rows,bag_row(0,edge,creative));
    }
    AutoTree grid_body = vstack(grid_rows);
    grid_body.spacing = 0;
    grid_body.width = HOTBAR_SIZE * edge;
    AutoTree grid_children = tree_newarr();
    tree_array_append(grid_children, grid_body);
    return grid_children;
}
static void measure_inventory_grid(AutoTree scroll,int width,int edge,int creative) {
    scroll.width=panel_grid_width(width,edge);
    scroll.height=(creative ? PALETTE_ROWS+1 : SLOT_COUNT/HOTBAR_SIZE)*edge+44;
}
static AutoTree inventory_grid(int creative) {
    inventory_edge=panel_grid_edge(page_w);
    AutoTree scroll=hscroll(inventory_grid_content(creative,inventory_edge));
    scroll.id = "bagGrid";
    measure_inventory_grid(scroll,page_w,inventory_edge,creative);
    return scroll;
}
static void layout_inventory_grid(AutoTree root,int width) {
    int creative=current_page==PAGE_BAG && player_rules.mode==CREATIVE;
    int edge=panel_grid_edge(width);
    AutoTree scroll=ccd_ui_id(root,"bagGrid");
    if (edge!=inventory_edge) {
        scroll.sub=inventory_grid_content(creative,edge);
        inventory_edge=edge;
    }
    measure_inventory_grid(scroll,width,edge,creative);
}

static void open_bag(AutoTree root, int workbench) {
    if (!playing() || !save_current()) return;
    current_page = PAGE_BAG;
    held_index = empty_bag_focus = -1;
    sorting_bag = 0;
    craft_size = workbench ? 3 : 2;
    reset_recipe();
    mx = my = touch_altitude = 0;
    AutoTree rows = tree_newarr();
    int creative = player_rules.mode == CREATIVE;
    int mount_gear=!workbench && horse_can_equip_mounted();
    tree_array_append(rows, titled(NULL, creative ? "创造物品栏" : workbench ? "工作台 · 3 × 3" : "生存物品栏", 24, "#242424"));
    tree_array_append(rows, menu_button("closeBag", "返回游戏", &resume_game));
    tree_array_append(rows, titled("bagMessage",
        mount_gear ? "选马铠后点坐骑装备格；未选物品时点装备格卸下。" :
        creative ? "点快捷格选择取物位置；再点已选物品可移动。" :
        workbench ? "点选材料，再点上方合成格。" : "选护甲后点部位穿戴；未选物品时点部位卸下。",
        14, "#252525"));
    if (mount_gear) tree_array_append(rows,mount_armor_row());
    if (creative) tree_array_append(rows,palette_navigation());
    else {
        if (!workbench) tree_array_append(rows,armor_grid());
        tree_array_append(rows, recipe_grid());
        tree_array_append(rows, menu_button("switchBagMode", "整理背包", &toggle_bag_mode));
    }
    tree_array_append(rows, inventory_grid(creative));
    show_panel(root, rows, 1);
    if (mount_gear) refresh_mount_armor(root);
    if (!creative) {
        update_recipe(root);
        if (!workbench) refresh_armor_grid(root);
    }
}
static void refresh_enchanting(AutoTree root) {
    ItemStack held=(unsigned)held_index<SLOT_COUNT ? inventory[held_index] : (ItemStack){0};
    char text[240];
    paint_item_stack(root,"enchantItem",0,held);
    if (held.count) {
        char desc[140];
        const ItemDef *def=item_def(held.type);
        enchant_describe(desc,sizeof desc,held.enchants);
        if (def && def->durability)
            snprintf(text,sizeof text,"%s · 耐久 %u/%u%s%s",item_display_name(held),
                (unsigned)(def->durability-held.damage),(unsigned)def->durability,*desc ? " · " : "",desc);
        else snprintf(text,sizeof text,"%s",item_display_name(held));
    } else snprintf(text,sizeof text,"从背包选择一件装备");
    ccd_ui_id(root,"enchantResult").text=text;
    ccd_ui_id(root,"enchantLevels").text="耗材装配，不消耗经验；每把镐只可使用一种配件。";
    for (int j=0;j<CALIBRATION_COUNT;++j) {
        const CalibrationOption *option=calibration_option(j);
        char id[24]; snprintf(id,sizeof id,"enchantOffer%d",j);
        AutoTree row=ccd_ui_id(root,id);
        CalibrationStatus state=calibration_preview(held_index,j);
        const char *note=state==CALIBRATION_SELECT ? " · 未选择" :
            state==CALIBRATION_INCOMPATIBLE ? " · 不适用" : state==CALIBRATION_MISSING ? " · 缺材料" :
            state==CALIBRATION_UNNEEDED ? (j==CALIBRATE_LINER ? " · 无需保养" : " · 已安装") : "";
        snprintf(text,sizeof text,"%s · %s ×1%s",option->name,item_display_name((ItemStack){option->material,1,0,0,0}),note);
        row.text=text;
        /* CCDUI only honors "enabled" on timers. Show unavailable choices
           as plain text so they are neither tappable nor announced as buttons. */
        row.type=state==CALIBRATION_READY ? "button" : "label";
        row.color=state==CALIBRATION_READY ? "#0052A5" : "#5D594F";
    }
}
static void apply_enchantment(AutoTree root,AutoTree sender) {
    if (current_page!=PAGE_ENCHANTING || held_index<0) return;
    int choice=item_slot_index(sender,"enchantOffer");
    if (!calibration_apply(enchant_x,enchant_y,enchant_z,held_index,choice)) {
        refresh_enchanting(root);
        ccd_ui_id(root,"enchantLevels").text="没有装配：校准台、装备或材料已发生变化。";
        return;
    }
    for (int i=0;i<SLOT_COUNT;++i) paint_item_slot(root,"bagSlot",i,i==held_index);
    refresh_enchanting(root);
    const char *message=choice==CALIBRATE_LAYER ? "已安装分层刃" :
        choice==CALIBRATE_CORE ? "已安装取芯夹" : "已完成砌板保养";
    ccd_ui_id(root,"enchantLevels").text=save_write() ? message : "已装配但尚未保存，请回到游戏后手动保存。";
}
void open_enchanting(AutoTree root,int x,int y,int z) {
    if (!playing() || !save_current() || player_rules.mode!=SURVIVAL) return;
    current_page=PAGE_ENCHANTING;
    held_index=-1; sorting_bag=0;
    enchant_x=x; enchant_y=y; enchant_z=z;
    mx=my=touch_altitude=0;
    AutoTree rows=tree_newarr();
    tree_array_append(rows,titled(NULL,"工况校准台",24,"#4D3B26"));
    tree_array_append(rows,menu_button("closeEnchanting","返回游戏",&resume_game));
    tree_array_append(rows,titled("enchantLevels","",15,"#4D3B26"));
    tree_array_append(rows,make_item_slot("enchantItem",0,NULL));
    tree_array_append(rows,titled("enchantResult","",15,"#4D3B26"));
    for (int i=0;i<CALIBRATION_COUNT;++i) {
        char id[24]; snprintf(id,sizeof id,"enchantOffer%d",i);
        tree_array_append(rows,menu_button(id,"",&apply_enchantment));
        tree_array_append(rows,titled(NULL,(char *)calibration_option(i)->detail,14,"#4D3B26"));
    }
    tree_array_append(rows,inventory_grid(0));
    show_panel(root,rows,1);
    refresh_enchanting(root);
}

static void refresh_anvil(AutoTree root,AutoTree sender) {
    (void)sender;
    if (current_page!=PAGE_ANVIL) return;
    char line[ITEM_NAME_BYTES+128];
    const char *field=(const char *)ccd_ui_id(root,"anvilName").text;
    anvil_name_valid=item_name_normalize(field ? field : "",anvil_shown_name);
    for (int n=0;n<2;++n) {
        ItemStack input={0,0,0,0,0};
        if (anvil_source[n]>=0) input=inventory[anvil_source[n]];
        paint_selected_stack(root,"anvilSlot",n,anvil_input==n,input);
        snprintf(line,sizeof line,"%s%s：%s",anvil_input==n ? "▸ " : "",
            n==0 ? "待处理物品" : "材料 / 附魔书",input.count ? item_display_name(input) : "从背包选择");
        ccd_ui_id(root,(char *)(n==0 ? "anvilFirst" : "anvilSecond")).text=line;
    }
    unsigned level=experience_progress().level;
    int wear=anvil_stage(world_cell(anvil_x,anvil_y,anvil_z)),creative=player_rules.mode==CREATIVE;
    if (wear<0) snprintf(line,sizeof line,"铁砧已损坏；处理好的物品在背包中。");
    else if (creative) snprintf(line,sizeof line,"创造模式 · 不消耗等级 · 磨损 %d/2",wear);
    else snprintf(line,sizeof line,"当前等级 %u · 铁砧磨损 %d/2",level,wear);
    ccd_ui_id(root,"anvilInfo").text=line;
    AnvilPlan plan;
    AnvilResult result=anvil_name_valid ? anvil_preview(anvil_source[0],anvil_source[1],anvil_shown_name,&plan) : ANVIL_BAD_NAME;
    AutoTree commit=ccd_ui_id(root,"anvilCommit");
    commit.enabled=wear>=0 && result==ANVIL_READY && (creative || level>=plan.cost);
    commit.text="确认处理";
    ccd_ui_id(root,"anvilEffects").text="";
    const ItemStack empty={0,0,0,0,0};
    paint_item_stack(root,"anvilSlot",2,result==ANVIL_READY ? plan.item : empty);
    if (result==ANVIL_READY) {
        const char *name=plan.rename ? (*plan.name ? plan.name : item_name(plan.item.type)) : item_display_name(plan.item);
        snprintf(line,sizeof line,"结果：%s × %u",name,(unsigned)plan.item.count);
        ccd_ui_id(root,"anvilResult").text=line;
        const ItemDef *def=item_def(plan.item.type);
        char effects[140];
        enchant_describe(effects,sizeof effects,plan.item.enchants);
        if (def->durability && plan.item.type!=ANVIL)
            snprintf(line,sizeof line,"耐久 %u/%u%s%s",(unsigned)(def->durability-plan.item.damage),
                (unsigned)def->durability,*effects ? " · " : "",effects);
        else snprintf(line,sizeof line,"%s",effects);
        ccd_ui_id(root,"anvilEffects").text=line;
        snprintf(line,sizeof line,"%s · %u 级 · 材料 × %u",!creative && level<plan.cost ? "等级不足" : "确认处理",
            creative ? 0u : plan.cost,plan.materials);
        commit.text=line;
    } else {
        const char *message=result==ANVIL_BAD_NAME ? "名称过长或含有无效字符（最多 30 个 UTF-16 单位）。" :
            result==ANVIL_TOO_EXPENSIVE ? "过于昂贵（生存模式最多 39 级）。" :
            result==ANVIL_INCOMPATIBLE ? "材料不适用，或没有可增加的耐久或附魔。再点材料格可清除。" :
            result==ANVIL_NOTHING ? "选第二份材料，或填写名称后点更新预览。" : "先点背包里的待处理物品。";
        ccd_ui_id(root,"anvilResult").text=message;
    }
}
static void select_anvil_input(AutoTree root,AutoTree sender) {
    if (current_page!=PAGE_ANVIL) return;
    const char *id=(const char *)sender.id;
    anvil_input=id && !strcmp(id,"anvilSecond") ? 1 : 0;
    refresh_anvil(root,sender);
}
static void anvil_pick(AutoTree root,int slot) {
    if (!inventory[slot].count) return;
    int chosen=anvil_input,old=anvil_source[chosen],other=anvil_source[1-chosen];
    if (slot==other) return;
    anvil_source[chosen]=slot==old ? -1 : slot;
    if (chosen==0) {
        ccd_ui_id(root,"anvilName").text=slot==old ? "" : item_override(inventory[slot]);
        if (slot!=old) anvil_input=1;
    }
    for (int i=0;i<SLOT_COUNT;++i) paint_item_slot(root,"bagSlot",i,i==anvil_source[0] || i==anvil_source[1]);
    refresh_anvil(root,root);
}
static void commit_anvil(AutoTree root,AutoTree sender) {
    if (current_page!=PAGE_ANVIL) return;
    char name[ITEM_NAME_BYTES+1];
    const char *field=(const char *)ccd_ui_id(root,"anvilName").text;
    if (!anvil_name_valid || !item_name_normalize(field ? field : "",name) || strcmp(name,anvil_shown_name)) {
        refresh_anvil(root,sender);
        return; /* An edited field gets a fresh preview before it can spend resources. */
    }
    AnvilResult result=anvil_apply(anvil_x,anvil_y,anvil_z,anvil_source[0],anvil_source[1],name);
    if (result!=ANVIL_READY) {
        refresh_anvil(root,sender);
        if (result==ANVIL_NO_MEMORY) ccd_ui_id(root,"anvilResult").text="无法处理；物品、材料和经验未消耗。";
        return;
    }
    anvil_source[1]=-1;
    for (int i=0;i<SLOT_COUNT;++i) paint_item_slot(root,"bagSlot",i,i==anvil_source[0]);
    refresh_anvil(root,sender);
    ccd_ui_id(root,"anvilResult").text=save_write() ? "已处理，结果留在原背包格。" :
        "已处理，尚未保存；请回到游戏手动保存。";
}
void open_anvil(AutoTree root,int x,int y,int z) {
    if (!playing() || !save_current()) return;
    current_page=PAGE_ANVIL;
    anvil_source[0]=anvil_source[1]=-1;
    anvil_input=0; anvil_x=x; anvil_y=y; anvil_z=z;
    mx=my=touch_altitude=0;
    AutoTree rows=tree_newarr();
    tree_array_append(rows,titled(NULL,"铁砧",24,"#292C35"));
    tree_array_append(rows,menu_button("closeAnvil","返回游戏",&resume_game));
    tree_array_append(rows,titled("anvilInfo","",15,"#292C35"));
    for (int i=0;i<2;++i) {
        AutoTree row=tree_newarr();
        tree_array_append(row,make_item_slot("anvilSlot",i,NULL));
        tree_array_append(row,menu_button(i==0 ? "anvilFirst" : "anvilSecond","",&select_anvil_input));
        tree_array_append(rows,hstack(row));
    }
    AutoTree name=inputa("物品名称（留空恢复默认名）",&refresh_anvil);
    name.id="anvilName"; name.height=34; name.fontSize=16; name.paddingLeft=8;
    name.color="#20212A"; name.colorbg="#E4E4E4"; name.cornerRadius=5;
    tree_array_append(rows,name);
    tree_array_append(rows,menu_button("anvilPreview","更新预览",&refresh_anvil));
    AutoTree result_row=tree_newarr();
    tree_array_append(result_row,make_item_slot("anvilSlot",2,NULL));
    tree_array_append(result_row,titled("anvilResult","",15,"#292C35"));
    tree_array_append(rows,hstack(result_row));
    tree_array_append(rows,titled("anvilEffects","",14,"#423051"));
    tree_array_append(rows,menu_button("anvilCommit","",&commit_anvil));
    tree_array_append(rows,inventory_grid(0));
    show_panel(root,rows,1);
    refresh_anvil(root,root);
}

void open_inventory(AutoTree root, AutoTree sender) {
    (void)sender;
    open_bag(root, 0);
}
void open_workbench(AutoTree root) {
    if (player_rules.mode == SURVIVAL) open_bag(root, 1);
}

void update_furnace_panel(AutoTree root) {
    if (!furnace_panel_visible()) return;
    const Furnace *f = furnace_at(fx, fy, fz);
    for (int i = 0; i < FURNACE_SLOTS; i++) {
        ItemStack item = {0, 0, 0,0,0};
        if (f) item = f->slots[i];
        const ItemStack previously = furnace_slots_shown[i];
        if (previously.type != item.type || previously.count != item.count || previously.damage != item.damage) {
            furnace_slots_shown[i] = item;
            paint_item_stack(root, "furnaceSlot", i, item);
        }
    }
    int burn = f ? (f->burn + 19) / 20 : 0, cook = f ? 100 * f->cook / SMELT_TICKS : 0;
    /* Render/update text only for changes a player can use, not every tick. */
    cook -= cook % 5;
    if (burn != furnace_burn_shown || cook != furnace_cook_shown) {
        char text[104];
        if (burn) snprintf(text, sizeof text, "烧制进度 %d%%　·　剩余燃料 %d 秒", cook, burn);
        else snprintf(text, sizeof text, "烧制进度 %d%%　·　未点燃", cook);
        ccd_ui_id(root, "furnaceTiming").text = text;
        furnace_burn_shown = burn;
        furnace_cook_shown = cook;
    }
}

static void click_furnace_slot(AutoTree root, AutoTree sender) {
    if (!furnace_panel_visible()) return;
    int slot = item_slot_index(sender, "furnaceSlot");
    if (slot < 0 || slot >= FURNACE_SLOTS) return;
    int take = held_index < 0 || slot == FURNACE_OUTPUT;
    int ok = take ? furnace_withdraw(fx, fy, fz, (FurnaceSlot)slot) :
        furnace_deposit(fx, fy, fz, (FurnaceSlot)slot, held_index);
    ccd_ui_id(root, "bagMessage").text = ok ? (take ? "已取回背包" : "已放入熔炉") :
        (take ? "炉槽为空，或背包放不下。" : "该材料不适用，或炉槽放不下。");
    if (!ok) return;
    if (held_index >= 0 && !inventory[held_index].count) held_index = -1;
    for (int i = 0; i < SLOT_COUNT; i++) paint_item_slot(root, "bagSlot", i, i == held_index);
    status(root, "");
    update_furnace_panel(root);
    if (take && slot == FURNACE_OUTPUT) { puts("SMELT output collected"); fflush(stdout); }
}

void open_furnace(AutoTree root, int x, int y, int z) {
    if (!playing() || !save_current() || player_rules.mode != SURVIVAL) return;
    current_page = PAGE_FURNACE;
    held_index = -1;
    sorting_bag = 0;
    mx = my = touch_altitude = 0;
    fx = x; fy = y; fz = z;
    furnace_burn_shown = furnace_cook_shown = -1;
    memset(furnace_slots_shown, 0, sizeof furnace_slots_shown);
    AutoTree rows = tree_newarr();
    tree_array_append(rows, titled(NULL, "熔炉", 24, "#242424"));
    tree_array_append(rows, menu_button("closeFurnace", "返回游戏", &resume_game));
    tree_array_append(rows, titled("bagMessage", "从背包选矿石或燃料，点对应炉槽放入。", 14, "#252525"));
    AutoTree columns = tree_newarr();
    const char *labels[] = {"原料", "燃料", "产物"};
    for (int i = 0; i < FURNACE_SLOTS; i++) {
        AutoTree column = tree_newarr();
        tree_array_append(column, titled(NULL, labels[i], 15, "#252525"));
        tree_array_append(column, make_item_slot("furnaceSlot", i, &click_furnace_slot));
        AutoTree stack = vstack(column);
        stack.width = 70;
        tree_array_append(columns, stack);
    }
    AutoTree work = hstack(columns);
    work.spacing = 8;
    tree_array_append(rows, work);
    AutoTree timing = titled("furnaceTiming", "烧制进度 0%　·　未点燃", 14, "#252525");
    timing.height = 20;
    tree_array_append(rows, timing);
    tree_array_append(rows, inventory_grid(0));
    show_panel(root, rows, 1);
    update_furnace_panel(root);
}

void update_brewing_panel(AutoTree root) {
    if (!brewing_panel_visible()) return;
    const BrewingStand *b=brew_at(brew_x,brew_y,brew_z);
    for (int i=0;i<BREW_SLOTS;++i) {
        ItemStack item=b ? b->slots[i] : (ItemStack){0,0,0,0,0};
        if (item_same_kind(item,brew_slots_shown[i]) && item.count==brew_slots_shown[i].count) continue;
        brew_slots_shown[i]=item;
        paint_item_stack(root,"brewSlot",i,item);
    }
    BrewDose dose;
    int lane=brew_pending(b,&dose);
    char text[sizeof brew_status_shown];
    if (lane>=0) {
        const ItemDef *output=item_def(dose.output);
        snprintf(text,sizeof text,"%s中 · %s · 剩余 %u 秒",brew_slot_name(lane),output ? output->name : "",
            (dose.ticks-b->progress+19u)/20u);
    } else if (b && b->slots[BREW_INGREDIENT].count) {
        BrewDose quick=brew_dose(BREW_QUICK,b->slots[BREW_INGREDIENT].type);
        if (!quick.output) snprintf(text,sizeof text,"旧原料可取回，不参与调剂。");
        else {
            int waiting=0;
            for (int i=0;i<BREW_BOTTLES;++i)
                if (b->slots[i].type==WATER_BOTTLE) waiting=1;
            snprintf(text,sizeof text,"%s",waiting ? "该档不支持这种原料，或原料数量不足。" :
                "等待水瓶；已完成的滤剂可用空手取回。");
        }
    } else snprintf(text,sizeof text,"等待原料；合格水瓶从左到右逐瓶调剂。");
    if (!strcmp(text,brew_status_shown)) return;
    snprintf(brew_status_shown,sizeof brew_status_shown,"%s",text);
    ccd_ui_id(root,"brewTiming").text=text;
}
static void click_brew_slot(AutoTree root,AutoTree sender) {
    if (!brewing_panel_visible()) return;
    int slot=item_slot_index(sender,"brewSlot");
    if ((unsigned)slot>=BREW_SLOTS) return;
    int taking=held_index<0;
    int ok=taking ? brew_withdraw(brew_x,brew_y,brew_z,slot) : brew_deposit(brew_x,brew_y,brew_z,slot,held_index);
    ccd_ui_id(root,"bagMessage").text=ok ? (taking ? "已取回背包" : "已放入调剂台") :
        (taking ? "槽位为空或背包放不下。" : "前三槽只收水瓶；原料槽需使用对应原料。");
    if (!ok) return;
    if (held_index>=0 && !inventory[held_index].count) held_index=-1;
    for (int i=0;i<SLOT_COUNT;++i) paint_item_slot(root,"bagSlot",i,i==held_index);
    update_brewing_panel(root);
}
void open_brewing(AutoTree root,int x,int y,int z) {
    if (!playing() || !save_current() || player_rules.mode!=SURVIVAL) return;
    current_page=PAGE_BREWING;
    held_index=-1; sorting_bag=0;
    brew_x=x; brew_y=y; brew_z=z; brew_status_shown[0]=0;
    memset(brew_slots_shown,0,sizeof brew_slots_shown);
    mx=my=touch_altitude=0;
    AutoTree rows=tree_newarr();
    tree_array_append(rows,titled(NULL,"野外调剂台",24,"#243630"));
    tree_array_append(rows,menu_button("closeBrewing","返回游戏",&resume_game));
    tree_array_append(rows,titled("bagMessage","选背包物品后放入；取消选择再取回。",14,"#252525"));
    AutoTree columns=tree_newarr();
    for (int i=0;i<BREW_SLOTS;++i) {
        AutoTree stack=tree_newarr();
        tree_array_append(stack,titled(NULL,brew_slot_name(i),15,"#252525"));
        tree_array_append(stack,make_item_slot("brewSlot",i,&click_brew_slot));
        const char *details[]={"1 份 · 6 秒","3 份 · 15 秒","2 份 · 10 秒","按瓶扣除"};
        tree_array_append(stack,titled(NULL,details[i],10,"#35433D"));
        AutoTree column=vstack(stack); column.width=62;
        tree_array_append(columns,column);
    }
    AutoTree work=hstack(columns); work.spacing=5;
    tree_array_append(rows,work);
    tree_array_append(rows,titled("brewTiming","等待原料",14,"#243630"));
    tree_array_append(rows,titled(NULL,"石灰岩隔热 · 南瓜恢复 · 柔光陶砖助潜\n鸣壳片辨暗 · 棕菇缓力",12,"#35433D"));
    tree_array_append(rows,inventory_grid(0));
    show_panel(root,rows,1);
    update_brewing_panel(root);
}

static void storage_id(char out[24],int index) {
    snprintf(out,24,"storage_%d",index);
}
static int storage_index(AutoTree sender) {
    const char *id=(const char *)sender.id;
    if (!id || strncmp(id,"storage_",8) || id[8]<'0' || id[8]>'9') return -1;
    char *end=NULL;
    long n=strtol(id+8,&end,10);
    if ((*end && strcmp(end,"Tap")) || n<0 || n>=DOUBLE_CHEST_SLOTS+SLOT_COUNT) return -1;
    return (int)n;
}
static int storage_count(void) {
    return storage_type==STORAGE_CART ? cart_cargo_count(cart_storage.dimension,cart_storage.slot) :
        storage_type==ENDER_CHEST ? CHEST_SLOTS :
        storage_type==HOPPER ? HOPPER_SLOTS : ejector_type(storage_type) ? DROPPER_SLOTS : storage.count*CHEST_SLOTS;
}
static const char *storage_name(void) {
    return storage_type==STORAGE_CART ? (storage_count()==HOPPER_CART_SLOTS ? "漏斗矿车" : "箱子矿车") :
        storage_type==HOPPER ? "漏斗" : storage_type==DISPENSER ? "发射器" :
        storage_type==DROPPER ? "投掷器" : storage_type==TRAPPED_CHEST ? "陷阱箱" :
        storage_type==ENDER_CHEST ? "联储箱" : "箱子";
}
static ItemStack storage_item(int index) {
    if (index>=DOUBLE_CHEST_SLOTS && index<DOUBLE_CHEST_SLOTS+SLOT_COUNT)
        return inventory[index-DOUBLE_CHEST_SLOTS];
    if (storage_type==STORAGE_CART) return cart_storage_item(&cart_storage,index);
    if (storage_type==ENDER_CHEST) return ender_chest_item(&ender_storage,index);
    if (chest_inventory_type(storage_type)) return chest_slot(&storage,index);
    ChestPos at=storage.halves[0];
    if ((unsigned)index<(unsigned)storage_count()) {
        if (storage_type==HOPPER) {
            const Hopper *h=hopper_at(at.x,at.y,at.z);
            if (h) return h->slots[index];
        } else if (ejector_type(storage_type)) {
            const Dropper *d=dropper_at(at.x,at.y,at.z);
            if (d) return d->slots[index];
        }
    }
    return (ItemStack){0,0,0,0,0};
}
static ChestResult move_storage_item(int from,int to) {
    if (storage_type==ENDER_CHEST) return ender_chest_move(&ender_storage,from,to,storage_single);
    if (chest_inventory_type(storage_type)) return chest_move(&storage,from,to,storage_single);
    if ((unsigned)from>=DOUBLE_CHEST_SLOTS+SLOT_COUNT || (unsigned)to>=DOUBLE_CHEST_SLOTS+SLOT_COUNT)
        return CHEST_NO_MOVE;
    ChestPos at=storage.halves[0];
    if (storage_type==STORAGE_CART ? !cart_storage_valid(&cart_storage) :
        storage.dimension!=world_dimension() ||
        (storage_type==HOPPER ? !hopper_at(at.x,at.y,at.z) : !dropper_at(at.x,at.y,at.z))) return CHEST_MISSING;
    int count=storage_count();
    if ((from>=count && from<DOUBLE_CHEST_SLOTS) || (to>=count && to<DOUBLE_CHEST_SLOTS))
        return CHEST_NO_MOVE;
    int a=from>=DOUBLE_CHEST_SLOTS ? from-DOUBLE_CHEST_SLOTS+count : from;
    int b=to>=DOUBLE_CHEST_SLOTS ? to-DOUBLE_CHEST_SLOTS+count : to;
    int moved=storage_type==STORAGE_CART ? cart_storage_move(&cart_storage,a,b,storage_single) :
        storage_type==HOPPER ? hopper_move(at.x,at.y,at.z,a,b,storage_single) :
        dropper_move(at.x,at.y,at.z,a,b,storage_single);
    return moved ? CHEST_OK : CHEST_NO_MOVE;
}
static const char *storage_slot_group(int index) {
    if (index<DOUBLE_CHEST_SLOTS) return storage_name();
    return index<DOUBLE_CHEST_SLOTS+HOTBAR_SIZE ? "快捷栏" : "背包";
}
static int storage_slot_number(int index) {
    if (index<DOUBLE_CHEST_SLOTS) return index+1;
    int bag=index-DOUBLE_CHEST_SLOTS;
    return (bag<HOTBAR_SIZE ? bag : bag-HOTBAR_SIZE)+1;
}
static int storage_stack_unchanged(ItemStack a,ItemStack b) {
    return a.count==b.count && (!a.count || item_same_kind(a,b));
}
static void storage_accessibility(char *text,size_t capacity,int index,ItemStack item) {
    const char *selected=index==storage_selected ? "，已选中" : "";
    if (item.count) snprintf(text,capacity,"%s第 %d 格，%s，%u 件%s",
        storage_slot_group(index),storage_slot_number(index),item_display_name(item),(unsigned)item.count,selected);
    else snprintf(text,capacity,"%s第 %d 格，空%s",
        storage_slot_group(index),storage_slot_number(index),selected);
}
static void repaint_storage_slot(AutoTree root,int index) {
    if (index<0 || index>=DOUBLE_CHEST_SLOTS+SLOT_COUNT) return;
    ItemStack item=storage_item(index);
    storage_shown[index]=item;
    paint_selected_stack(root,"storage_",index,index==storage_selected,item);
    char id[24],accessibility[ITEM_NAME_BYTES+96];
    storage_accessibility(accessibility,sizeof accessibility,index,item);
    storage_id(id,index);
    ccd_ui_id(root,id).accessibilityLabel=accessibility;
}
static void select_storage_slot(AutoTree root,int index,int has_source) {
    const int previous=storage_selected;
    storage_selected=index;
    storage_has_source=has_source;
    if (previous>=0 && previous!=index) repaint_storage_slot(root,previous);
    if (index>=0) repaint_storage_slot(root,index);
}
void update_storage_panel(AutoTree root) {
    if (!chest_panel_visible()) return;
    ChestPos p=storage.halves[0];
    if (storage_type==ENDER_CHEST ? !ender_chest_valid(&ender_storage) :
        storage_type==STORAGE_CART ? !cart_storage_valid(&cart_storage) :
        storage.dimension!=world_dimension() || (chest_inventory_type(storage_type) ?
            !chest_view_valid(&storage) : block(p.x,p.y,p.z)!=storage_type)) {
        close_chest(root);
        status(root,"容器状态已变化，请重新打开");
        return;
    }
    int count=storage_count();
    for (int i=0;i<DOUBLE_CHEST_SLOTS+SLOT_COUNT;++i) {
        if (i==count) i=DOUBLE_CHEST_SLOTS;
        ItemStack item=storage_item(i),old=storage_shown[i];
        if (storage_stack_unchanged(item,old)) continue;
        if (storage_selected==i) {
            select_storage_slot(root,-1,0);
            ccd_ui_id(root,"storageMessage").text="物品已变化，请重新选择。";
        } else {
            repaint_storage_slot(root,i);
        }
    }
}
static void storage_tap(AutoTree root,AutoTree sender) {
    if (!chest_panel_visible()) return;
    int index=storage_index(sender);
    if (index<0 || (index>=storage_count() && index<DOUBLE_CHEST_SLOTS)) return;
    if (!storage_has_source) {
        ItemStack item=storage_item(index);
        select_storage_slot(root,index,item.count!=0);
        char tip[ITEM_NAME_BYTES+80];
        if (item.count) snprintf(tip,sizeof tip,"已选 %s × %u；点目标格转移",item_display_name(item),(unsigned)item.count);
        else snprintf(tip,sizeof tip,"已选中%s第 %d 格（空）；请选择要移动的物品",
            storage_slot_group(index),storage_slot_number(index));
        ccd_ui_id(root,"storageMessage").text=tip;
        return;
    }
    int from=storage_selected;
    if (from==index) {
        select_storage_slot(root,-1,0);
        ccd_ui_id(root,"storageMessage").text="选择物品，再点目标格。";
        return;
    }
    if (!storage_stack_unchanged(storage_item(from),storage_shown[from])) {
        select_storage_slot(root,-1,0);
        ccd_ui_id(root,"storageMessage").text="物品已变化，请重新选择。";
        return;
    }
    ChestResult result=move_storage_item(from,index);
    if (result==CHEST_MISSING) {
        close_chest(root);
        status(root,"容器状态已变化，请重新打开");
    } else if (result==CHEST_OK) {
        select_storage_slot(root,-1,0);
        repaint_storage_slot(root,index);
        ccd_ui_id(root,"storageMessage").text="已移动；可选择下一组物品。";
    } else {
        ccd_ui_id(root,"storageMessage").text=result==CHEST_NO_MEMORY ? "不能分配容器格，未移动任何物品" :
            "目标格放不下；请选择其他目标，或点原格取消。";
    }
}
static AutoTree storage_row(int start,int count,int edge) {
    AutoTree cells=tree_newarr();
    for (int i=start;i<start+count;++i) {
        ItemStack item=storage_item(i);
        storage_shown[i]=item;
        char accessibility[ITEM_NAME_BYTES+96];
        storage_accessibility(accessibility,sizeof accessibility,i,item);
        tree_array_append(cells,make_storage_slot(i,&storage_tap,item,accessibility,edge,i==storage_selected));
    }
    AutoTree row=hstack(cells);
    row.width=edge*count; row.height=edge; row.spacing=0;
    return row;
}
static void toggle_storage_mode(AutoTree root,AutoTree sender) {
    if (!chest_panel_visible()) return;
    storage_single=!storage_single;
    sender.text=storage_single ? "转移单件 · 切换" : "转移整组 · 切换";
    ccd_ui_id(root,"storageMessage").text=storage_single ? "单件模式只往空格或同类物品格放一件。" :
        "整组模式会合并同类物品，或互换不同物品。";
}
void close_chest(AutoTree root) {
    if (!chest_panel_visible()) return;
    storage_selected=-1; storage_has_source=0;
    leave_panel(root);
    status(root,"");
}
static void close_chest_action(AutoTree root,AutoTree sender) {
    (void)sender;
    close_chest(root);
}
static int storage_row_count(int count) { return (count+HOTBAR_SIZE-1)/HOTBAR_SIZE; }
static AutoTree storage_grid_content(int count,int edge) {
    AutoTree grid_rows=tree_newarr();
    for (int i=0;i<count;i+=HOTBAR_SIZE) {
        int remaining=count-i;
        tree_array_append(grid_rows,storage_row(i,remaining<HOTBAR_SIZE ? remaining : HOTBAR_SIZE,edge));
    }
    AutoTree bag_title=titled(NULL,"背包和快捷栏",15,"#272321");
    bag_title.height=STORAGE_BAG_TITLE_HEIGHT;
    tree_array_append(grid_rows,bag_title);
    for (int i=HOTBAR_SIZE;i<SLOT_COUNT;i+=HOTBAR_SIZE)
        tree_array_append(grid_rows,storage_row(DOUBLE_CHEST_SLOTS+i,HOTBAR_SIZE,edge));
    tree_array_append(grid_rows,storage_row(DOUBLE_CHEST_SLOTS,HOTBAR_SIZE,edge));
    AutoTree grid=vstack(grid_rows);
    grid.spacing=0; grid.width=HOTBAR_SIZE*edge;
    AutoTree layers=tree_newarr();
    tree_array_append(layers,grid);
    return layers;
}
static void measure_storage_grid(AutoTree scroll,int width,int count,int edge) {
    scroll.width=panel_grid_width(width,edge);
    scroll.height=(storage_row_count(count)+SLOT_COUNT/HOTBAR_SIZE)*edge+STORAGE_BAG_TITLE_HEIGHT;
}
static void layout_storage_grid(AutoTree root,int width) {
    AutoTree grid=ccd_ui_id(root,"bagGrid");
    int next_edge=panel_grid_edge(width),count=storage_count();
    if (next_edge!=storage_edge) {
        /* Preserve selection and its original snapshot: repaint only the layout. */
        ItemStack source={0};
        int source_pending=storage_has_source && storage_selected>=0 &&
            storage_selected<DOUBLE_CHEST_SLOTS+SLOT_COUNT;
        if (source_pending) source=storage_shown[storage_selected];
        grid.sub=storage_grid_content(count,next_edge);
        if (source_pending) storage_shown[storage_selected]=source;
        storage_edge=next_edge;
    }
    measure_storage_grid(grid,width,count,storage_edge);
}
static void open_storage_panel(AutoTree root) {
    current_page=PAGE_CHEST;
    storage_selected=-1; storage_has_source=0; storage_single=0;
    int count=storage_count();
    storage_edge=panel_grid_edge(page_w);
    AutoTree scroll=hscroll(storage_grid_content(count,storage_edge));
    scroll.id="bagGrid";
    measure_storage_grid(scroll,page_w,count,storage_edge);
    AutoTree rows=tree_newarr();
    tree_array_append(rows,titled(NULL,storage_type==STORAGE_CART ?
        (count==HOPPER_CART_SLOTS ? "漏斗矿车 · 5 格" : "箱子矿车 · 27 格") :
        storage_type==HOPPER ? "漏斗 · 5 格" :
        storage_type==DISPENSER ? "发射器 · 9 格" : storage_type==DROPPER ? "投掷器 · 9 格" :
        storage_type==ENDER_CHEST ? "联储箱 · 27 格" :
        storage_type==TRAPPED_CHEST ? (storage.count==2 ? "双陷阱箱 · 54 格" : "陷阱箱 · 27 格") :
        storage.count==2 ? "双箱 · 54 格" : "箱子 · 27 格",23,"#272321"));
    AutoTree actions=tree_newarr();
    tree_array_append(actions,menu_button("storageDone","返回游戏",&close_chest_action));
    tree_array_append(actions,menu_button("storageMode","转移整组 · 切换",&toggle_storage_mode));
    tree_array_append(rows,hstack(actions));
    tree_array_append(rows,titled("storageMessage","选择物品，再点目标格。",14,"#252525"));
    tree_array_append(rows,scroll);
    /* Mount a complete snapshot rather than rewalk the mounted grid. */
    show_panel(root,rows,1);
}
void open_chest(AutoTree root,int x,int y,int z) {
    if (!playing() || !save_current()) return;
    ChestResult ready=chest_open((ChestPos){x,y,z},&storage);
    if (ready!=CHEST_OK) {
        status(root,ready==CHEST_BLOCKED ? "箱盖上方有障碍，无法打开" :
            ready==CHEST_CAT ? "猫坐在箱盖上，空手轻点让它站起" :
            ready==CHEST_NO_MEMORY ? "暂时无法打开箱子，物品未改变" : "此处没有箱子");
        return;
    }
    storage_type=storage.kind;
    chest_access_begin(&storage);
    open_storage_panel(root);
}
void open_cart_storage(AutoTree root,int slot) {
    if (!playing() || !save_current() || !cart_open_storage(slot,&cart_storage)) return;
    storage_type=STORAGE_CART;
    open_storage_panel(root);
}
void open_ender_chest(AutoTree root,int x,int y,int z) {
    if (!playing() || !save_current()) return;
    ChestResult result=ender_chest_open((ChestPos){x,y,z},&ender_storage);
    if (result!=CHEST_OK) {
        status(root,result==CHEST_BLOCKED ? "联储箱上方有整块障碍，无法打开" : "此处没有联储箱");
        return;
    }
    storage_type=ENDER_CHEST;
    open_storage_panel(root);
}
void open_hopper(AutoTree root,int x,int y,int z) {
    if (!playing() || !save_current() || !hopper_at(x,y,z)) return;
    storage=(ChestView){world_dimension(),{{x,y,z},{0,0,0}},0,0};
    storage_type=HOPPER;
    open_storage_panel(root);
}
void open_ejector(AutoTree root,int x,int y,int z) {
    if (!playing() || !save_current() || !dropper_at(x,y,z)) return;
    int type=block(x,y,z);
    if (!ejector_type(type)) return;
    storage=(ChestView){world_dimension(),{{x,y,z},{0,0,0}},0,0};
    storage_type=type;
    open_storage_panel(root);
}
