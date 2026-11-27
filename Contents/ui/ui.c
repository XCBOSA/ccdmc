#include "ui.h"
#include "inventory_ui.h"
#include "pages.h"
#include "sound_host.h"
#include "map_ui.h"
#include "../game/game.h"
#include "../game/interaction.h"
#include "../world/redstone.h"
#include "../world/windcut.h"
#include "../world/rain_garden.h"
#include "../game/furnace.h"
#include "../game/brewing.h"
#include "../game/item_custom.h"
#include "../game/item_frames.h"
#include "../game/armor.h"
#include "../game/experience.h"
#include "../game/portals.h"
#include "../game/combat.h"
#include "../game/boats.h"
#include "../game/minecarts.h"
#include "../game/mining_press.h"
#include "../game/horses.h"
#include "../game/fishing.h"
#include "../game/chests.h"
#include "../game/beds.h"
#include "../game/farming.h"
#include "../game/field_supplies.h"
#include "../game/actors.h"
#include "../game/navigation.h"
#include "../render/renderer.h"
#include "../save/save.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ccd_haptics.h>

static int looking;
static UIContainer gameWindow=-1;
static int stick_contact, suppress_stick, suppress_keys;
static float baseYaw, basePitch;
static Uint64 lastTick, lastFurnaceTick, lastMineTick, lastLookDrag, resizeSince, lastAutosave, statusAge;
static int viewW, viewH, resizePending;
static char mob_label[ITEM_NAME_BYTES+1];
static int mob_label_x,mob_label_y;
static ItemStack hud_slots[HOTBAR_SIZE];
static int hud_slots_valid, hud_selected=-1, hud_gear_edge;
static unsigned look_chop_ms;
static MiningPress miningPress;
static char hud_message[256];
static char nav_message[128];
static int nav_message_valid;
static Uint64 last_nav_refresh;
enum { HUD_MARGIN = 8, STICK_MAX = 108, STICK_MIN = 64, JUMP_EDGE = 68,
       HUD_BUTTON_WIDTH = 86, HUD_BUTTON_HEIGHT = 44, HUD_GAP = 4,
       HUD_GEAR_SPACING = 2, HUD_GEAR_MIN = 34, XP_BAR_WIDTH = 120, XP_HUD_HEIGHT = 25 };
static int stickSize = STICK_MAX;
static int lastHealth = -1, lastFood = -1, lastAir = -1, lastFlight = -1;
static int lastWater=-2,lastExposure=-1;
static int lastEffect=-1,lastFire=-1,lastArmor=-1,lastBreath=-1;
static int lastVision=-1;
static uint32_t lastXP=UINT32_MAX;
static int lastSleeping=-1;
enum { HUD_AQUIFER_INACTIVE=-1, HUD_AQUIFER_UNSET=-2, HUD_VENT_DANGER=AQUIFER_SAMPLES+1, HUD_PRESSURE_WORKS=8 };
static int lastAquiferProgress=HUD_AQUIFER_UNSET;
static int lastRider=-1,lastHorse=-2,lastHorseFlags=-1,lastHorseHealth=-1;
static Uint64 jumpHeldAt;
static int biome_x, biome_z, last_biome = -1;
enum { PROFILE_FRAMES = 120 };
enum { FRAME_SPACING, FRAME_OUTSIDE, FRAME_CALLBACK, FRAME_STREAM,
    FRAME_SIM, FRAME_MAP, FRAME_MESH, FRAME_DRAW, FRAME_AUTOSAVE, FRAME_PHASES };
enum { MOTION_IDLE, MOTION_TRANSITION, MOTION_WITHIN_CHUNK, MOTION_ACROSS_CHUNK, MOTION_CLASSES };
static uint16_t frame_profile[FRAME_PHASES][PROFILE_FRAMES];
static unsigned char frame_motion_class[PROFILE_FRAMES];
static int profile_active, profile_count, profile_moved;
static int profile_previous_moved;
static Uint64 profile_last_begin,profile_last_end;
static AutoTree hud_button(const char *id, const char *title, CCDMCAction action);
static void dismount_action(AutoTree root,AutoTree sender);
static void show_sleep(AutoTree root);
static AutoTree look_bindings(unsigned chop_ms);

void start_frame_sample(void) {
    profile_active = 1;
    profile_count = profile_moved = 0;
    profile_previous_moved=0;
    profile_last_begin=profile_last_end=0;
    renderer_begin_mesh_sample();
    world_begin_cache_sample();
}
static int compare_ms(const void *a, const void *b) {
    return (int)*(const uint16_t *)a - (int)*(const uint16_t *)b;
}
static uint16_t sample_ms(Uint64 ns) {
    Uint64 ms=ns/UINT64_C(1000000);
    return ms>UINT16_MAX ? UINT16_MAX : (uint16_t)ms;
}
static void summarize_frame_motion(int kind,const char *name) {
    uint16_t intervals[PROFILE_FRAMES],outside[PROFILE_FRAMES],callback[PROFILE_FRAMES];
    unsigned count=0,late=0;
    for (int i=0;i<PROFILE_FRAMES;++i) {
        int cls=frame_motion_class[i];
        if (kind<0 ? cls<MOTION_WITHIN_CHUNK : cls!=kind) continue;
        intervals[count]=frame_profile[FRAME_SPACING][i];
        outside[count]=frame_profile[FRAME_OUTSIDE][i];
        callback[count]=frame_profile[FRAME_CALLBACK][i];
        late+=intervals[count]>=40;
        ++count;
    }
    if (!count) return;
    qsort(intervals,count,sizeof intervals[0],compare_ms);
    qsort(outside,count,sizeof outside[0],compare_ms);
    qsort(callback,count,sizeof callback[0],compare_ms);
    unsigned tail=(count*95u-1u)/100u;
    printf("%s count=%u interval p95=%u ms outside p95=%u ms callback p95=%u ms interval>=40ms:%u\n",
        name,count,(unsigned)intervals[tail],(unsigned)outside[tail],(unsigned)callback[tail],late);
}
static void sample_frame(Uint64 begin,const Uint64 stages[FRAME_PHASES],int moved,int crossed) {
    Uint64 end=SDL_GetTicksNS();
    if (profile_last_begin) {
        for (int i=FRAME_STREAM;i<FRAME_PHASES;++i) frame_profile[i][profile_count]=sample_ms(stages[i]);
        frame_profile[FRAME_SPACING][profile_count]=sample_ms(begin-profile_last_begin);
        frame_profile[FRAME_OUTSIDE][profile_count]=sample_ms(begin-profile_last_end);
        frame_profile[FRAME_CALLBACK][profile_count]=sample_ms(end-begin);
        int cls=moved && profile_previous_moved ? (crossed ? MOTION_ACROSS_CHUNK : MOTION_WITHIN_CHUNK) :
            moved || profile_previous_moved ? MOTION_TRANSITION : MOTION_IDLE;
        frame_motion_class[profile_count]=(unsigned char)cls;
        if (cls>=MOTION_WITHIN_CHUNK && (profile_count&3)==0) renderer_sample_forward_chunks();
        profile_count++;
        profile_moved += moved != 0;
    }
    profile_last_begin=begin;
    profile_last_end=end;
    profile_previous_moved=moved;
    if (profile_count<PROFILE_FRAMES) return;
    static const char *const names[]={"interval","outside","callback","stream","sim","map","mesh","draw","autosave"};
    static const char *const motion_names[]={"idle","transition","within-chunk","across-chunk"};
    printf("FRAME SAMPLE count=%d moving=%d\n",PROFILE_FRAMES,profile_moved);
    summarize_frame_motion(-1,"motion");
    for (int kind=0;kind<MOTION_CLASSES;++kind) summarize_frame_motion(kind,motion_names[kind]);
    for (int i=0;i<FRAME_PHASES;++i) {
        uint16_t *samples=frame_profile[i];
        qsort(samples,PROFILE_FRAMES,sizeof *samples,&compare_ms);
        printf("%s ms p50=%u p95=%u max=%u\n",names[i],(unsigned)samples[59],
            (unsigned)samples[113],(unsigned)samples[119]);
    }
    ChunkCacheSample cache=world_finish_cache_sample();
    printf("CACHE SAMPLE installs=%u evictions=%u near=%u ahead=%u reused=%u\n",
        cache.installs,cache.evictions,cache.near_evictions,cache.ahead_evictions,cache.generated_reused);
    renderer_finish_mesh_sample(1);
    fflush(stdout);
    profile_active = 0;
}

static void show_field_vitals(AutoTree root) {
    const FieldSave field=field_record();
    const int water=field_enabled() ? field.water : -1;
    const int exposure=water<0 ? 0 : field_in_sun() ? 2 : field.heat>0 ? 1 : 0;
    if (water==lastWater && exposure==lastExposure) return;
    lastWater=water; lastExposure=exposure;
    AutoTree meter=ccd_ui_id(root,"fieldVitals");
    char text[90]="";
    const char *heat=exposure==2 ? " · 暴晒" : exposure==1 ? " · 降温中" : "";
    if (water>=0) snprintf(text,sizeof text,"水分 %d%%%s%s",water,water<FIELD_WATER_LOW ? " · 口渴" : "",heat);
    meter.text=text;
    meter.height=water>=0 ? 18 : 0;
    meter.color=water>=0 && water<FIELD_WATER_LOW ? "#FFD9A0" : "#C6E9F1";
}

static void show_vitals(AutoTree root) {
    show_field_vitals(root);
    const PlayerRules *p = &player_rules;
    int effect=(player_effects.poison!=0)+2*(player_effects.hunger!=0)+4*(player_burn_state().remaining!=0)+
        8*(player_effects.weakness!=0)+
        32*(player_effects.regeneration!=0)+64*(player_effects.regeneration_ii!=0);
    int fire=(player_effects.fire+19)/20;
    int breath=(player_effects.water_breathing+19)/20;
    int vision=(player_effects.night_vision+19)/20;
    int armor=p->mode==CREATIVE ? 0 : armor_points();
    if (p->health == lastHealth && p->food == lastFood && p->air == lastAir &&
        p->flying == lastFlight && effect==lastEffect && fire==lastFire &&
        armor==lastArmor && breath==lastBreath && vision==lastVision) return;
    lastHealth = p->health; lastFood = p->food; lastAir = p->air; lastFlight = p->flying;
    lastEffect=effect; lastFire=fire; lastBreath=breath; lastVision=vision;
    char text[240];
    if (p->mode == CREATIVE) {
        snprintf(text, sizeof text, "%s", p->flying ? "创造 · 拖动右侧升降，双点落地" : "创造 · 双点跳跃起飞");
        if (horse_rider()<0 && boat_rider()<0 && cart_rider()<0)
            ccd_ui_id(root, "jumpPad").accessibilityLabel = p->flying ? "上下拖动升降，双点退出飞行" : "跳跃，双点起飞";
    } else if (p->air < MAX_AIR) {
        snprintf(text, sizeof text, "生命 %d · 饥饿 %d · 氧气 %d", p->health, p->food, p->air);
    } else {
        snprintf(text, sizeof text, "生命 %d / %d　饥饿 %d / %d", p->health, MAX_HEALTH, p->food, MAX_FOOD);
    }
    int symptoms=effect&3;
    const char *suffix=symptoms==3 ? " · 中毒、饥饿加剧" : symptoms==2 ? " · 饥饿加剧" : symptoms==1 ? " · 中毒" : "";
    size_t len=strlen(text);
    snprintf(text+len,sizeof text-len,"%s",suffix);
    len=strlen(text);
    if (effect&4) { snprintf(text+len,sizeof text-len," · 着火"); len=strlen(text); }
    if (effect&8) { snprintf(text+len,sizeof text-len," · 虚弱"); len=strlen(text); }
    if (fire) { snprintf(text+len,sizeof text-len," · 抗火 %d:%02d",fire/60,fire%60); len=strlen(text); }
    if (effect&32) { snprintf(text+len,sizeof text-len," · 再生%s",effect&64 ? " II" : ""); len=strlen(text); }
    if (breath) { snprintf(text+len,sizeof text-len," · 水肺 %d:%02d",breath/60,breath%60); len=strlen(text); }
    if (vision) snprintf(text+len,sizeof text-len," · 夜视 %d:%02d",vision/60,vision%60);
    ccd_ui_id(root, "vitalStats").text = text;
    if (armor!=lastArmor) {
        AutoTree meter=ccd_ui_id(root,"armorVitals");
        if (armor) snprintf(text,sizeof text,"护甲 %d / %d",armor,ARMOR_MAX_POINTS);
        meter.text=armor ? text : "";
        meter.height=armor ? 18 : 0;
        lastArmor=armor;
    }
}
static void show_experience(AutoTree root) {
    uint32_t total=experience_points();
    if (total==lastXP) return;
    lastXP=total;
    AutoTree hud=ccd_ui_id(root,"xpHud");
    int survival=player_rules.mode==SURVIVAL;
    hud.height=survival ? XP_HUD_HEIGHT : 0;
    hud.opacity=survival ? 1 : 0;
    if (!survival) return;
    XPProgress xp=experience_progress();
    char text[56];
    snprintf(text,sizeof text,"经验 %u  ·  %u/%u",(unsigned)xp.level,(unsigned)xp.within,(unsigned)xp.next);
    ccd_ui_id(root,"xpTitle").text=text;
    AutoTree fill=ccd_ui_id(root,"xpFill");
    fill.width=(double)XP_BAR_WIDTH*xp.within/xp.next;
    fill.opacity=xp.within ? 1 : 0;
}

static void show_biome(AutoTree root) {
    enum { WINDCUT_LABEL=BIOME_COUNT, RAIN_GARDEN_LABEL };
    int x = (int)floorf(px), z = (int)floorf(pz);
    if (last_biome >= 0 && x == biome_x && z == biome_z) return;
    int b = terrain_biome(x, z);
    WindcutPlan site;
    RainGardenPlan garden;
    if (b==BIOME_DESERT && windcut_plan_for(x,z,&site) && windcut_contains(site,x,z)) b=WINDCUT_LABEL;
    if (b==BIOME_JUNGLE && rain_garden_plan_for(x,z,&garden) && rain_garden_contains(garden,x,z)) b=RAIN_GARDEN_LABEL;
    biome_x = x;
    biome_z = z;
    if (b == last_biome) return;
    last_biome = b;
    AutoTree item = ccd_ui_id(root, "biomeName");
    item.text = b==WINDCUT_LABEL ? "风蚀矿阶" : b==RAIN_GARDEN_LABEL ? "林冠集雨园" : biome_name((Biome)b);
}

static void show_mount(AutoTree root) {
    int i=horse_rider();
    int rider=i>=0 ? 1 : boat_rider()>=0 ? 2 : cart_rider()>=0 ? 3 : 0;
    const HorseRecord *h=i>=0 ? &horses_in(world_dimension())[i] : NULL;
    int flags=h ? h->tamed+2*h->saddle : -1, health=h ? h->health : -1;
    if (i==lastHorse && flags==lastHorseFlags && health==lastHorseHealth && rider==lastRider) return;
    lastHorse=i; lastHorseFlags=flags; lastHorseHealth=health;
    AutoTree label=ccd_ui_id(root,"mountVitals");
    AutoTree pad=ccd_ui_id(root,"jumpPad");
    char text[100];
    if (h) snprintf(text,sizeof text,"%s · 生命 %d / %d · %s",horse_name(h->type),h->health,
        h->max_health,h->saddle ? "已上鞍" : h->tamed ? "已驯服，未上鞍" : "驯服中");
    label.height=h ? 20 : 0;
    label.text=h ? text : "";
    if (rider!=lastRider) {
        lastRider=rider;
        AutoTree actions=tree_newarr();
        if (rider) {
            AutoTree exit=hud_button("dismountButton",rider==1 ? "下马" : rider==2 ? "下船" : "下车",&dismount_action);
            exit.width=60;
            tree_array_append(actions,exit);
        }
        ccd_ui_id(root,"mountActions").sub=actions;
        game_clear_jump_taps();
    }
    pad.accessibilityLabel=h ? "坐骑跳跃，向上拖动蓄力" : rider ? "乘坐载具" :
        player_rules.flying ? "上下拖动升降，双点退出飞行" : player_rules.mode==CREATIVE ? "跳跃，双点起飞" : "跳跃";
    if (!h) jumpHeldAt=0;
}

static void flight(AutoTree root, AutoTree sender) {
    (void)sender;
    if (!playing() || player_rules.mode != CREATIVE) return;
    game_clear_jump_taps();
    player_toggle_flight();
    show_vitals(root);
}

static int same_stack(ItemStack a,ItemStack b) {
    return a.type==b.type && a.count==b.count && a.damage==b.damage && a.enchants==b.enchants && a.custom==b.custom;
}
static void show_navigation(AutoTree root) {
    char next[128];
    navigation_status(next,sizeof next);
    if (nav_message_valid && !strcmp(next,nav_message)) return;
    AutoTree node=ccd_ui_id(root,"navigationStatus");
    node.text=next;
    node.height=*next ? 21 : 0;
    node.opacity=*next ? 1 : 0;
    snprintf(nav_message,sizeof nav_message,"%s",next);
    nav_message_valid=1;
}
static void refresh_mining_hold(AutoTree root) {
    if (looking || miningPress.held) return;
    unsigned chop=0;
    if (player_rules.mode==SURVIVAL && (unsigned)selected_slot<HOTBAR_SIZE) {
        unsigned axe=inventory[selected_slot].count ? axe_chop_milliseconds(inventory[selected_slot].type) : 0;
        if (axe && axe<450) chop=axe;
    }
    if (chop!=look_chop_ms) {
        look_chop_ms=chop;
        ccd_ui_id(root,"lookSurface").gestures=look_bindings(chop);
    }
}
void status(AutoTree root, const char *message) {
    for (int i=0;i<HOTBAR_SIZE;++i) {
        const ItemStack item=inventory[i];
        if (!hud_slots_valid || (i==hud_selected)!=(i==selected_slot) || !same_stack(hud_slots[i],item)) {
            paint_item_slot(root,"hotSlot",i,i==selected_slot);
            hud_slots[i]=item;
        }
    }
    hud_slots_valid=1;
    hud_selected=selected_slot;
    refresh_mining_hold(root);
    show_navigation(root);
    if (strcmp(hud_message,message)) {
        ccd_ui_id(root,"hudMessage").text=message;
        snprintf(hud_message,sizeof hud_message,"%s",message);
    }
    statusAge = *message ? SDL_GetTicks() : 0;
}

static void show_mob_name(AutoTree root,int enabled) {
    Vec3 head;
    const char *name=enabled ? actors_aimed_name(&head) : "";
    if (enabled && !*name) name=item_frame_aimed_name(&head);
    int w=viewW ? viewW : START_W,h=viewH ? viewH : START_H;
    float x,y;
    int left=0,top=0;
    if (*name && renderer_project(head,w,h,&x,&y) && x>=0 && x<w && y>=0 && y<h) {
        float width=0.f; /* Only used to center the proportional-font canvas label. */
        for (const unsigned char *p=(const unsigned char *)name;*p;++p)
            if ((*p&0xc0)!=0x80) width+=*p>=0xe0 ? 14.f : 8.f;
        left=(int)roundf(fmaxf(2.f,fminf(w-width-2.f,x-width*.5f)));
        top=(int)roundf(fmaxf(2.f,y-18.f));
    } else name="";
    if (!strcmp(mob_label,name) && (!*name || (left==mob_label_x && top==mob_label_y))) return;
    int changed=strcmp(mob_label,name)!=0;
    if (changed) memcpy(mob_label,name,strlen(name)+1);
    mob_label_x=left; mob_label_y=top;
    AutoTree shade=ccd_ui_id(root,"mobNameShadow"),label=ccd_ui_id(root,"mobName");
    if (*name) { shade.x=left+1; shade.y=top+1; label.x=left; label.y=top; }
    if (changed) { shade.text=mob_label; label.text=mob_label; }
}
void restart_game_ui(AutoTree root) {
    show_mob_name(root,0);
    renderer_reset_world();
    looking = 0;
    look_chop_ms=UINT32_MAX;
    mining_press_release(&miningPress);
    stick_contact = suppress_stick = suppress_keys = 0;
    lastTick = lastFurnaceTick = lastMineTick = lastLookDrag = 0;
    hud_slots_valid=0;
    map_ui_reset(root);
    nav_message_valid=0;
    last_nav_refresh=0;
    lastAutosave = SDL_GetTicks();
    lastHealth = lastFood = lastAir = lastFlight = -1;
    lastWater=-2; lastExposure=-1;
    lastEffect=lastFire=lastArmor=lastBreath=lastVision=-1;
    lastXP=UINT32_MAX;
    lastSleeping=-1;
    lastAquiferProgress=HUD_AQUIFER_UNSET;
    lastHorse=-2; lastRider=-1;
    jumpHeldAt=0;
    game_clear_jump_taps();
    if (profile_active) {
        renderer_finish_mesh_sample(0);
        (void)world_finish_cache_sample();
    }
    profile_active = 0;
    last_biome = -1;
    ccd_ui_id(root, "stickKnob").x = 37.0 * stickSize / STICK_MAX;
    ccd_ui_id(root, "stickKnob").y = 37.0 * stickSize / STICK_MAX;
    show_vitals(root);
    refresh_mining_hold(root);
    show_experience(root);
    show_biome(root);
    show_mount(root);
    show_sleep(root);
    status(root, "轻点使用或放置 · 长按破坏");
}

/* Drag and long press must never be followed by an incidental placement. */
static void stop_mining_hold(void) { mining_press_release(&miningPress); }

static const char *mine_scene_hold(int *removed) {
    unsigned long long before=world_edit_version();
    const char *message=mine_hold_action(removed);
    renderer_prioritize_edits_since(before);
    return message;
}

static void look_press(AutoTree root, AutoTree sender) {
    const char *phase=(const char *)sender.phase;
    if ((double)sender.releaseTracking!=0) {
        if (!phase || strcmp(phase,"began")) { stop_mining_hold(); return; }
        if (!playing() || bed_sleeping() || looking || !player_rules.health) return;
        Uint64 now=SDL_GetTicks();
        double duration=sender.duration;
        unsigned waited=duration>0. && duration<=.45 ? (unsigned)lround(duration*1000.) : 0;
        mining_press_begin(&miningPress,player_mining_aim(),now,waited);
        return;
    }
    if (!playing() || bed_sleeping() || looking || miningPress.held) return;
    Uint64 now = SDL_GetTicks();
    if (lastMineTick && now - lastMineTick < 500) return;
    lastMineTick = now;
    int removed=0;
    status(root,mine_scene_hold(&removed));
    if (removed) ccd_ui_impact_feedback(gameWindow,CCDUIImpactMedium);
}

static void look_chop(AutoTree root,AutoTree sender) {
    if (!playing() || looking || bed_sleeping() || miningPress.held) return;
    Uint64 now=SDL_GetTicks();
    if (lastMineTick && now-lastMineTick<500) return;
    double duration=sender.duration;
    if (!(duration>=.1 && duration<=.45)) return;
    int removed=0;
    unsigned long long before=world_edit_version();
    const char *message=mine_quick_chop_tracked((unsigned)lround(duration*1000.0),&removed);
    renderer_prioritize_edits_since(before);
    if (!message) return; /* The normal hold handles other materials. */
    lastMineTick=now;
    status(root,message);
    if (removed) ccd_ui_impact_feedback(gameWindow,CCDUIImpactMedium);
}
static void apply_scene_tap(AutoTree root) {
    if (!playing()) return;
    unsigned long long before=world_edit_version();
    Interaction action=game_tap();
    renderer_prioritize_edits_since(before);
    if (action.placed) ccd_ui_impact_feedback(gameWindow,CCDUIImpactLight);
    Hit h=action.hit;
    switch (action.panel) {
    case INTERACTION_CHEST: open_chest(root,h.x,h.y,h.z); break;
    case INTERACTION_ENDER_CHEST: open_ender_chest(root,h.x,h.y,h.z); break;
    case INTERACTION_CART_STORAGE: open_cart_storage(root,action.actor); break;
    case INTERACTION_HOPPER: open_hopper(root,h.x,h.y,h.z); break;
    case INTERACTION_EJECTOR: open_ejector(root,h.x,h.y,h.z); break;
    case INTERACTION_BREW: open_brewing(root,h.x,h.y,h.z); break;
    case INTERACTION_ENCHANT: open_enchanting(root,h.x,h.y,h.z); break;
    case INTERACTION_ANVIL: open_anvil(root,h.x,h.y,h.z); break;
    case INTERACTION_WORKBENCH: open_workbench(root); break;
    case INTERACTION_FURNACE: open_furnace(root,h.x,h.y,h.z); break;
    case INTERACTION_VILLAGER: open_villager(root,action.actor); break;
    case INTERACTION_NONE:
        if (action.message) status(root,action.message);
        show_sleep(root);
        show_vitals(root);
        show_mount(root);
        break;
    }
}
static void look_tap(AutoTree root, AutoTree sender) {
    (void)sender;
    Uint64 now=SDL_GetTicks();
    if (!playing() || looking || (lastMineTick && now-lastMineTick<500) ||
        (lastLookDrag && now-lastLookDrag<140)) return;
    apply_scene_tap(root);
}
static void jump(AutoTree root, AutoTree sender) {
    (void)sender;
    if (!playing()) return;
    game_jump_tap(SDL_GetTicks());
    show_vitals(root);
}
static void dismount_action(AutoTree root,AutoTree sender) {
    (void)sender;
    if (!playing()) return;
    const char *text=game_dismount();
    if (text) status(root,text);
    show_mount(root);
}
static void select_hotbar(AutoTree root, int index) {
    if (!playing() || bed_sleeping() || !game_select_hotbar(index)) return;
    status(root, inventory[index].count ? item_display_name(inventory[index]) : "");
}
static void hotbar_tap(AutoTree root, AutoTree sender) {
    select_hotbar(root, item_slot_index(sender, "hotSlot"));
}

static int finished(AutoTree sender) {
    const char *phase = (const char *)sender.phase;
    return phase && (!strcmp(phase, "ended") || !strcmp(phase, "cancelled") || !strcmp(phase, "canceled"));
}
static void altitude_drag(AutoTree root, AutoTree sender) {
    (void)root;
    if (playing() && horse_rider()>=0) {
        const char *phase=(const char *)sender.phase;
        if (phase && !strcmp(phase,"ended") && (double)sender.dy < -6.)
            horse_jump(fminf(1.f,.35f-(float)(double)sender.dy/60.f));
        return;
    }
    if (!playing() || (!player_rules.flying && !player_on_ladder()) || finished(sender)) { touch_altitude = 0; return; }
    touch_altitude = fmaxf(-1.f, fminf(1.f, -(float)(double)sender.dy / 28.f));
}
static void move_drag(AutoTree root, AutoTree sender) {
    int done = finished(sender);
    if (suppress_stick) {
        if (done) suppress_stick = stick_contact = 0;
        return;
    }
    if (!playing() || bed_sleeping()) return;
    stick_contact = !done;
    float radius = 34.f * stickSize / STICK_MAX;
    float x = (float)(double)sender.dx / radius, y = (float)(double)sender.dy / radius;
    float n = sqrtf(x * x + y * y);
    if (n > 1) { x /= n; y /= n; }
    if (done) { x = 0; y = 0; }
    mx = x; my = y;
    AutoTree knob = ccd_ui_id(root, "stickKnob");
    double scale = (double)stickSize / STICK_MAX;
    knob.x = (37 + x * 28) * scale;
    knob.y = (37 + y * 28) * scale;
}
static void look_drag(AutoTree root, AutoTree sender) {
    (void)root;
    if (!playing() || bed_sleeping()) return;
    int done = finished(sender);
    if (!looking && done) return;
    const char *phase=(const char *)sender.phase;
    if (done && (!phase || strcmp(phase,"ended"))) {
        looking=0;
        stop_mining_hold();
        lastLookDrag=SDL_GetTicks();
        return;
    }
    if (!looking) { baseYaw = yaw; basePitch = pitch; looking = 1; }
    yaw = baseYaw + (float)(double)sender.dx * .009f;
    pitch = fmaxf(-1.45f, fminf(1.45f, basePitch - (float)(double)sender.dy * .009f));
    if (miningPress.held) mining_press_aim(&miningPress,player_mining_aim(),SDL_GetTicks());
    if (done) { looking=0; lastLookDrag=SDL_GetTicks(); }
}

static AutoTree look_bindings(unsigned chop_ms) {
    AutoTree gestures=tree_newarr();
    const double travel=9;
    tree_array_append(gestures,mc_drag(travel,&look_drag));
    tree_array_append(gestures,mc_tap(1,&look_tap));
    AutoTree press=mc_press(.45,&look_press);
    press.reportsRelease=1;
    press.recognitionDelay=.11;
    press.maximumDistance=travel;
    tree_array_append(gestures,press);
    if (chop_ms&65535u) tree_array_append(gestures,mc_press((chop_ms&65535u)*.001,&look_chop));
    if (chop_ms>>16) tree_array_append(gestures,mc_press((chop_ms>>16)*.001,&look_chop));
    return gestures;
}

/* CCDUI owns the event loop; advance and render once per mc_timer callback. */
static void tick(AutoTree root, AutoTree sender) {
    const Uint64 profile_begin=profile_active ? SDL_GetTicksNS() : 0;
    Uint64 phases[FRAME_PHASES],phase=0;
    phases[FRAME_AUTOSAVE]=0;
    SDL_Event ev;
    while (renderer_ready() && SDL_PollEvent(&ev)) {
        if (bed_sleeping() && ev.type==SDL_EVENT_KEY_DOWN) {
            if (ev.key.scancode==SDL_SCANCODE_ESCAPE || ev.key.scancode==SDL_SCANCODE_R) {
                bed_cancel_sleep(); show_sleep(root);
                status(root,"已起床，重生点保留");
            }
            continue;
        }
        if (ev.type==SDL_EVENT_KEY_DOWN && ev.key.scancode==SDL_SCANCODE_ESCAPE && chest_panel_visible()) {
            close_chest(root);
            continue;
        }
        if (ev.type==SDL_EVENT_KEY_DOWN && ev.key.scancode==SDL_SCANCODE_ESCAPE && villager_panel_visible()) {
            close_villager(root);
            continue;
        }
        if (ev.type==SDL_EVENT_KEY_UP && ev.key.scancode==SDL_SCANCODE_SPACE && jumpHeldAt) {
            if (playing() && horse_rider()>=0)
                horse_jump(fminf(1.f,.25f+(SDL_GetTicks()-jumpHeldAt)*.0015f));
            jumpHeldAt=0;
        }
        if (playing() && ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
            SDL_Scancode code = ev.key.scancode;
            if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_9)
                select_hotbar(root, code - SDL_SCANCODE_1);
            else if (code == SDL_SCANCODE_E) open_inventory(root, sender);
            else if (code == SDL_SCANCODE_ESCAPE) pause_game(root, sender);
            else if (code == SDL_SCANCODE_SPACE) {
                if (horse_rider()>=0) jumpHeldAt=SDL_GetTicks();
                else jump(root,sender);
            }
            else if (code == SDL_SCANCODE_F) flight(root, sender);
            else if (code == SDL_SCANCODE_R) {
                if (horse_rider()>=0 || boat_rider()>=0 || cart_rider()>=0) dismount_action(root,sender);
                else apply_scene_tap(root);
            }
        }
    }
    Uint64 now = SDL_GetTicks();
    if (playing() || furnace_panel_visible() || chest_panel_visible() || brewing_panel_visible() || villager_panel_visible()) {
        if (lastFurnaceTick) {
            float elapsed=fminf(1,(now-lastFurnaceTick)*.001f);
            furnaces_advance(elapsed);
            brewing_advance(elapsed);
        }
        lastFurnaceTick = now;
        update_furnace_panel(root);
        update_brewing_panel(root);
    } else lastFurnaceTick = 0;
    if (save_autosave_poll()==SAVE_CHECKPOINT_FAILED) note_autosave(root,0);
    if (save_current() && now - lastAutosave >= 15000) {
        Uint64 started=profile_begin ? SDL_GetTicksNS() : 0;
        lastAutosave = now;
        if (save_autosave_begin()==SAVE_CHECKPOINT_FAILED) note_autosave(root,0);
        if (profile_begin) phases[FRAME_AUTOSAVE]=SDL_GetTicksNS()-started;
    }
    if (!playing()) {
        sound_host_tick(0);
        // Keep recorded samples, but do not count time in pause or inventory
        // as one extremely slow frame on the first callback after resuming.
        profile_last_begin=profile_last_end=0;
        if (chest_panel_visible() || villager_panel_visible()) {
            float elapsed=lastTick ? fminf(.08f,(now-lastTick)*.001f) : 0.f;
            lastTick=now;
            int x=(int)floorf(px),z=(int)floorf(pz);
            world_stream_pump(x,z);
            if (elapsed>0.f && world_chunk_ready(x,z)) game_step(elapsed,0,0,0,0);
            if (!player_rules.health) show_death(root);
            else if (chest_panel_visible()) update_storage_panel(root);
            else update_villager_panel(root);
        } else lastTick = 0;
        looking = 0; stop_mining_hold(); combat_cancel_draw(); return;
    }
    if (statusAge && now - statusAge >= 2200) {
        ccd_ui_id(root, "hudMessage").text = "";
        hud_message[0]='\0';
        statusAge = 0;
    }
    /* Let continuous layout changes settle before replacing the drawable. */
    if (resizePending && (!resizeSince || now - resizeSince >= 80)) {
        resizePending = 0;
        renderer_resize(viewW, viewH);
    }
    Uint64 previous = lastTick;
    float dt = previous ? (now - previous) * .001f : .033f;
    lastTick = now;
    /* Background suspension is a lost-touch fallback, not a per-frame
       gesture cutoff. A slow terrain update must not cancel the press. */
    if (dt>1.5f || bed_sleeping() || !player_rules.health) stop_mining_hold();
    if (mining_press_ready(&miningPress,now)) {
        /* Walking or a world edit can change the target without a drag. */
        mining_press_aim(&miningPress,player_mining_aim(),now);
    }
    if (mining_press_ready(&miningPress,now)) {
        int repeat_block=0;
        const char *result=mine_scene_hold(&repeat_block);
        lastMineTick=now;
        status(root,result);
        if (repeat_block) ccd_ui_impact_feedback(gameWindow,CCDUIImpactMedium);
        mining_press_finish_attempt(&miningPress,player_mining_aim(),now,repeat_block);
    }
    if (dt > .08f) dt = .08f;
    const bool *keys = SDL_GetKeyboardState(NULL);
    int side = keys ? keys[SDL_SCANCODE_D] - keys[SDL_SCANCODE_A] : 0;
    int front = keys ? keys[SDL_SCANCODE_W] - keys[SDL_SCANCODE_S] : 0;
    int ascend = keys && keys[SDL_SCANCODE_SPACE];
    int descend = keys && (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]);
    if (suppress_keys) {
        suppress_keys = side || front || ascend || descend;
        side = front = ascend = descend = 0;
    }
    float x_before = px, y_before = py, z_before = pz;
    int at_x=(int)floorf(px),at_z=(int)floorf(pz);
    if (profile_begin) phase=SDL_GetTicksNS();
    renderer_stream_world(at_x,at_z);
    if (profile_begin) { Uint64 t=SDL_GetTicksNS(); phases[FRAME_STREAM]=t-phase; phase=t; }
    if (world_chunk_ready(at_x,at_z) && game_step(dt,side,front,ascend,descend))
        status(root,"已拾取掉落物");
    sound_host_tick(1);
    if (profile_begin) { Uint64 t=SDL_GetTicksNS(); phases[FRAME_SIM]=t-phase; phase=t; }
    navigation_step(dt);
    map_ui_update(root,viewW ? viewW : START_W,viewH ? viewH : START_H,stickSize,px!=x_before || pz!=z_before,now);
    if (profile_begin) phases[FRAME_MAP]=SDL_GetTicksNS()-phase;
    if (now-last_nav_refresh>=100) { show_navigation(root); last_nav_refresh=now; }
    show_sleep(root);
    const char *rest=bed_take_message();
    if (rest) status(root,rest);
    show_mount(root);
    const char *combat = combat_take_message();
    if (combat) status(root,combat);
    const char *fish=fishing_take_message();
    if (fish) status(root,fish);
    const char *affliction=actors_take_affliction_note();
    if (affliction) status(root,affliction);
    Dimension before = world_dimension();
    const char *portal = portals_step(dt);
    if (world_dimension() != before) {
        int was_dragging = stick_contact;
        restart_game_ui(root);
        suppress_stick = was_dragging;
        suppress_keys = side || front || ascend || descend;
        if (!save_write()) note_autosave(root,0);
        else if (portal) status(root,portal);
    } else if (portal) status(root,portal);
    show_vitals(root);
    refresh_mining_hold(root);
    show_experience(root);
    show_biome(root);
    show_mob_name(root,!bed_sleeping());
    int works=inside_pressure_works(),expedition=inside_aquifer();
    int progress=works ? HUD_PRESSURE_WORKS | (int)portals_pressure_relief() |
        (portals_aquifer_vent_danger() ? HUD_VENT_DANGER : 0) :
        expedition ? (int)portals_aquifer_samples() |
        (portals_aquifer_vent_danger() ? HUD_VENT_DANGER : 0) : HUD_AQUIFER_INACTIVE;
    if (lastAquiferProgress!=progress) {
        lastAquiferProgress=progress;
        char label[80]="";
        if (works) snprintf(label,sizeof label,"西阀%s · 东阀%s · %s",
            progress&1 ? "开" : "关",progress&2 ? "开" : "关",
            progress&HUD_VENT_DANGER ? "热气危险" : (progress&3)==3 ? "可烧结圆石" : "升降机可返程");
        else if (expedition) snprintf(label,sizeof label,"潮沟%s · 热裂隙%s · %s",
            progress&AQUIFER_SAMPLE_BRINE ? "已取样" : "待取样",progress&AQUIFER_SAMPLE_VENT ? "已取样" : "待取样",
            progress&HUD_VENT_DANGER ? "热气危险" :
                (progress&AQUIFER_SAMPLES)==AQUIFER_SAMPLES && world_generation()>=GENERATOR_PRESSURE_WORKS ?
                "北侧升降机已可用" : "缆索可随时返程");
        AutoTree bossHud = ccd_ui_id(root,"bossStatus");
        bossHud.text = label;
        bossHud.height = expedition || works ? 22 : 0;
    }
    if (!player_rules.health) {
        show_death(root);
        if (!save_write()) note_autosave(root,0);
        return;
    }
    if (profile_begin) phase=SDL_GetTicksNS();
    if (!upload_mesh()) { fprintf(stderr, "mesh: %s\n", SDL_GetError()); sender.enabled = 0; return; }
    if (profile_begin) { Uint64 t=SDL_GetTicksNS(); phases[FRAME_MESH]=t-phase; phase=t; }
    if (!draw_scene()) { fprintf(stderr, "render: %s\n", SDL_GetError()); sender.enabled = 0; return; }
    if (profile_begin && profile_active) {
        phases[FRAME_DRAW]=SDL_GetTicksNS()-phase;
        int crossed=floorf(x_before/CHUNK_SIZE)!=floorf(px/CHUNK_SIZE) ||
            floorf(z_before/CHUNK_SIZE)!=floorf(pz/CHUNK_SIZE);
        sample_frame(profile_begin,phases,px!=x_before || py!=y_before || pz!=z_before,crossed);
    }
}

static AutoTree joystick(void) {
    AutoTree shapes = json_tree("[]");
    AutoTree ring = mc_ellipse(3, 3, 102, 102);
    ring.id = "stickRing"; ring.fill = "#344B56";
    ring.opacity = .75; ring.stroke = "#FFFFFF"; ring.lineWidth = 2;
    tree_array_append(shapes, ring);
    AutoTree knob = mc_ellipse(37, 37, 34, 34);
    knob.id = "stickKnob"; knob.fill = "#E9F4E9"; knob.opacity = .9;
    tree_array_append(shapes, knob);
    AutoTree node = mc_canvas(shapes, 108, 108);
    node.id = "movePad"; node.accessibilityLabel = "移动摇杆";
    AutoTree gestures = json_tree("[]");
    tree_array_append(gestures, mc_drag(1, &move_drag));
    node.gestures = gestures;
    return node;
}

static AutoTree jump_pad(void) {
    AutoTree shapes = json_tree("[]");
    AutoTree disk = mc_ellipse(2, 2, 64, 64);
    disk.fill = "#454545"; disk.opacity = .65; disk.stroke = "#DDDDDD"; disk.lineWidth = 2;
    tree_array_append(shapes, disk);
    AutoTree path = json_tree("[]");
    tree_array_append(path, mc_move(18, 37));
    tree_array_append(path, mc_line_to(34, 20));
    tree_array_append(path, mc_line_to(50, 37));
    tree_array_append(path, mc_line_to(43, 37));
    tree_array_append(path, mc_line_to(43, 48));
    tree_array_append(path, mc_line_to(25, 48));
    tree_array_append(path, mc_line_to(25, 37));
    tree_array_append(path, mc_close());
    AutoTree arrow = mc_path(path);
    arrow.fill = "#F5F5F5"; arrow.opacity = .9;
    tree_array_append(shapes, arrow);
    AutoTree node = mc_canvas(shapes, JUMP_EDGE, JUMP_EDGE);
    node.id = "jumpPad"; node.accessibilityLabel = "跳跃";
    AutoTree gestures = json_tree("[]");
    tree_array_append(gestures, mc_tap(1, &jump));
    tree_array_append(gestures, mc_drag(2, &altitude_drag));
    node.gestures = gestures;
    return node;
}

static AutoTree hud_button(const char *id, const char *title, CCDMCAction action) {
    AutoTree b = button((char *)title, action);
    b.id = id; b.width = HUD_BUTTON_WIDTH; b.height = HUD_BUTTON_HEIGHT; b.fontSize = 15;
    b.colorfg = "#FFFFFF"; b.colorbg = "#454545";
    return b;
}
static AutoTree backpack_icon(int edge) {
    double scale=(double)edge/SLOT_EDGE;
    AutoTree shapes=tree_newarr();
    AutoTree tile=mc_rect(scale,scale,edge-2*scale,edge-2*scale);
    tile.fill="#414842"; tile.stroke="#28332B"; tile.lineWidth=2*scale;
    tree_array_append(shapes,tile);
    AutoTree loop=mc_rect(17*scale,7*scale,10*scale,9*scale);
    loop.radius=3*scale; loop.stroke="#D8EDC1"; loop.lineWidth=2*scale;
    tree_array_append(shapes,loop);
    AutoTree bag=mc_rect(11*scale,12*scale,22*scale,25*scale);
    bag.radius=4*scale; bag.fill="#CAE7B1";
    tree_array_append(shapes,bag);
    AutoTree flap=mc_rect(11*scale,12*scale,22*scale,10*scale);
    flap.radius=4*scale; flap.fill="#E7F6D7";
    tree_array_append(shapes,flap);
    AutoTree pocket=mc_rect(15*scale,25*scale,14*scale,7*scale);
    pocket.radius=2*scale; pocket.fill="#6C9259";
    tree_array_append(shapes,pocket);
    AutoTree icon=mc_canvas(shapes,edge,edge);
    icon.id="bagButton"; icon.accessibilityLabel="打开背包";
    AutoTree gestures=tree_newarr();
    tree_array_append(gestures,mc_tap(1,&open_inventory));
    icon.gestures=gestures;
    return icon;
}
static int gear_edge(int width) {
    int edge=(width-HUD_GEAR_SPACING)/(HOTBAR_SIZE+1);
    return edge> SLOT_EDGE ? SLOT_EDGE : edge<HUD_GEAR_MIN ? HUD_GEAR_MIN : edge;
}
static int hotbar_width(int width,int edge) {
    int available=width-edge-HUD_GEAR_SPACING,preferred=HOTBAR_SIZE*edge;
    return available<1 ? 1 : available<preferred ? available : preferred;
}
static AutoTree gear_contents(int width,int edge) {
    AutoTree cells=tree_newarr();
    for (int i=0;i<HOTBAR_SIZE;++i)
        tree_array_append(cells,make_hotbar_slot(i,&hotbar_tap,edge,i==selected_slot));
    AutoTree row=hstack(cells);
    row.spacing=0; row.width=HOTBAR_SIZE*edge; row.height=edge;
    AutoTree rows=tree_newarr();
    tree_array_append(rows,row);
    AutoTree scroll=hscroll(rows);
    scroll.id="hotbarScroll"; scroll.width=hotbar_width(width,edge); scroll.height=edge;
    AutoTree gear=tree_newarr();
    tree_array_append(gear,scroll);
    tree_array_append(gear,backpack_icon(edge));
    return gear;
}
static void wake(AutoTree root,AutoTree sender) {
    (void)sender;
    if (!bed_sleeping()) return;
    bed_cancel_sleep(); show_sleep(root);
    status(root,"已起床，重生点保留");
}
static void show_sleep(AutoTree root) {
    int sleeping=bed_sleeping();
    if (sleeping==lastSleeping) return;
    lastSleeping=sleeping;
    AutoTree layer=ccd_ui_id(root,"sleepLayer"), content=tree_newarr();
    if (sleeping) {
        looking=0;
        suppress_stick=stick_contact;
        suppress_keys=1;
        AutoTree veil=mc_color("#12182B");
        veil.id="sleepCover"; veil.width=viewW ? viewW : START_W; veil.height=viewH ? viewH : START_H;
        veil.opacity=.84;
        tree_array_append(content,veil);
        AutoTree body=tree_newarr();
        AutoTree title=label("正在睡觉…");
        title.fontSize=26; title.color="#EEF2FF";
        tree_array_append(body,title);
        AutoTree hint=label("重生点已设定");
        hint.fontSize=16; hint.color="#D3DBEC";
        tree_array_append(body,hint);
        tree_array_append(body,hud_button("wakeButton","起床",&wake));
        AutoTree controls=vstack(body);
        controls.spacing=12;
        tree_array_append(content,controls);
    }
    layer.sub=content;
}

static AutoTree hud_anchor(const char *id, const char *align, AutoTree child) {
    AutoTree children = tree_newarr();
    tree_array_append(children, child);
    return @{type:"modifiedContent",id:id,align:align,sub:children};
}

/* Keep the drawable, touch surface and crosshair on the same live rectangle. */
static void layout(AutoTree root, AutoTree sender) {
    double measuredW = (double)sender.measuredWidth, measuredH = (double)sender.measuredHeight;
    if (measuredW < 1 || measuredH < 1) return;
    int w = (int)measuredW, h = (int)measuredH;
    if (w == viewW && h == viewH) return;
    resizeSince = viewW ? SDL_GetTicks() : 0;
    viewW = w; viewH = h;
    resizePending = 1;

    AutoTree scene = ccd_ui_id(root, "scene"); scene.width = w; scene.height = h;
    /* Even empty canvases participate in a CCDUI ZStack's layout. Size all
       world layers here, not only while they happen to contain something. */
    AutoTree viewport = ccd_ui_id(root, "gameViewport"); viewport.width = w; viewport.height = h;
    AutoTree paper = ccd_ui_id(root, "navigationPaper"); paper.width = w; paper.height = h;
    AutoTree marker = ccd_ui_id(root, "navigationMarker"); marker.width = w; marker.height = h;
    AutoTree names=ccd_ui_id(root,"mobNameCanvas"); names.width=w; names.height=h;
    if (bed_sleeping()) {
        AutoTree veil=ccd_ui_id(root,"sleepCover");
        veil.width=w; veil.height=h;
    }
    AutoTree look = ccd_ui_id(root, "lookSurface"); look.width = w; look.height = h;
    int hudW = w > 2 * HUD_MARGIN ? w - 2 * HUD_MARGIN : 1;
    int hudH = h > 2 * HUD_MARGIN ? h - 2 * HUD_MARGIN : 1;
    AutoTree hud = ccd_ui_id(root, "hud"); hud.width = hudW; hud.height = hudH;
    AutoTree buttons = ccd_ui_id(root, "hudButtons"); buttons.width = hudW; buttons.height = hudH;
    AutoTree top = ccd_ui_id(root, "hudTop"); top.width = hudW;
    AutoTree status = ccd_ui_id(root, "hudStatus"); status.width = hudW; status.height = hudH;
    AutoTree labels = ccd_ui_id(root, "hudLabels");
    /* In a short, wide viewport, leave the pause and dismount buttons their
       own columns and start the status text alongside them. */
    int beside = hudH < 320 && hudW >= 2 * (HUD_BUTTON_WIDTH + HUD_MARGIN) + 270;
    labels.width = beside ? hudW - 2 * (HUD_BUTTON_WIDTH + HUD_MARGIN) : hudW;
    status.y = beside ? HUD_GAP : HUD_BUTTON_HEIGHT + HUD_GAP;
    AutoTree bottom = ccd_ui_id(root, "hudBottom"); bottom.width = hudW; bottom.height = hudH;
    int gear=gear_edge(hudW);
    AutoTree gearBar = ccd_ui_id(root,"gearBar"); gearBar.height = gear;
    if (gear!=hud_gear_edge) {
        gearBar.sub=gear_contents(hudW,gear);
        hud_gear_edge=gear;
    } else ccd_ui_id(root,"hotbarScroll").width=hotbar_width(hudW,gear);
    layout_page(root, w, h);

    int nextStick = hudH - HUD_BUTTON_HEIGHT - gear - XP_HUD_HEIGHT - 2 * HUD_GAP;
    if (nextStick > hudW - JUMP_EDGE - HUD_MARGIN) nextStick = hudW - JUMP_EDGE - HUD_MARGIN;
    if (nextStick > STICK_MAX) nextStick = STICK_MAX;
    if (nextStick < STICK_MIN) nextStick = STICK_MIN;
    AutoTree controls = ccd_ui_id(root, "hudControls"); controls.width = hudW;
    controls.height = nextStick > JUMP_EDGE ? nextStick : JUMP_EDGE;
    if (nextStick != stickSize) {
        stickSize = nextStick;
        double scale = (double)stickSize / STICK_MAX;
        AutoTree pad = ccd_ui_id(root, "movePad"); pad.width = stickSize; pad.height = stickSize;
        AutoTree ring = ccd_ui_id(root, "stickRing");
        ring.x = 3 * scale; ring.y = 3 * scale; ring.width = 102 * scale; ring.height = 102 * scale;
        AutoTree knob = ccd_ui_id(root, "stickKnob");
        knob.x = (37 + mx * 28) * scale; knob.y = (37 + my * 28) * scale;
        knob.width = 34 * scale; knob.height = 34 * scale;
    }

    double cx = w * .5, cy = h * .5;
    AutoTree line1 = ccd_ui_id(root, "crossH");
    line1.x = cx - 7; line1.y = cy; line1.x2 = cx + 7; line1.y2 = cy;
    AutoTree line2 = ccd_ui_id(root, "crossV");
    line2.x = cx; line2.y = cy - 7; line2.x2 = cx; line2.y2 = cy + 7;
}

AutoTree build_ui(UIContainer window) {
    gameWindow=window;
    AutoTree scene = tree_newmap();
    scene.type = "sdl"; scene.id = "scene"; scene.width = START_W; scene.height = START_H;
    AutoTree shapes = json_tree("[]");
    double cx = START_W * .5, cy = START_H * .5;
    AutoTree h = mc_line(cx - 7, cy, cx + 7, cy);
    h.id = "crossH"; h.stroke = "#FFFFFF"; h.lineWidth = 2;
    tree_array_append(shapes, h);
    AutoTree v = mc_line(cx, cy - 7, cx, cy + 7);
    v.id = "crossV"; v.stroke = "#FFFFFF"; v.lineWidth = 2;
    tree_array_append(shapes, v);
    AutoTree look = mc_canvas(shapes, START_W, START_H);
    look.id = "lookSurface"; look.accessibilityLabel = "拖动视角，轻点使用或放置，长按破坏";
    look.gestures=look_bindings(0);

    AutoTree content = tree_newarr();
    AutoTree topButtons = tree_newarr();
    tree_array_append(topButtons, hud_button("pauseButton", "存档菜单", &pause_game));
    tree_array_append(topButtons, spacer());
    AutoTree mountActions=hstack(tree_newarr());
    mountActions.id="mountActions";
    tree_array_append(topButtons,mountActions);
    AutoTree buttons = hstack(topButtons);
    buttons.id = "hudTop"; buttons.width = START_W - 2 * HUD_MARGIN; buttons.height = HUD_BUTTON_HEIGHT;
    AutoTree statusItems = tree_newarr();
    AutoTree vitals = label("");
    vitals.id = "vitalStats"; vitals.fontSize = 14; vitals.color = "#FFFFFF"; vitals.colorbg = "#253442";
    tree_array_append(statusItems, vitals);
    AutoTree field=label("");
    field.id="fieldVitals"; field.height=0; field.fontSize=13; field.color="#C6E9F1";
    field.colorbg="#253442"; tree_array_append(statusItems,field);
    AutoTree armor=label("");
    armor.id="armorVitals"; armor.height=0; armor.fontSize=13; armor.color="#C4DAEF";
    armor.colorbg="#253442"; tree_array_append(statusItems,armor);
    AutoTree mount=label("");
    mount.id="mountVitals"; mount.height=0; mount.fontSize=13; mount.color="#F5F1D9";
    mount.colorbg="#253442"; tree_array_append(statusItems,mount);
    AutoTree biome = label("");
    biome.id = "biomeName"; biome.fontSize = 13; biome.color = "#F5F1D9"; biome.colorbg = "#253442";
    tree_array_append(statusItems, biome);
    AutoTree nav=label("");
    nav.id="navigationStatus"; nav.fontSize=14; nav.height=0; nav.opacity=0;
    nav.color="#F9E5B8"; nav.colorbg="#333127";
    tree_array_append(statusItems,nav);
    AutoTree boss = label("");
    boss.id = "bossStatus"; boss.height = 0; boss.fontSize = 14; boss.color = "#F1D1FB"; boss.colorbg = "#281E38";
    tree_array_append(statusItems, boss);
    AutoTree hint = label("");
    hint.id = "hudMessage"; hint.fontSize = 15; hint.color = "#FFFFFF";
    tree_array_append(statusItems, hint);
    AutoTree labels = vstack(statusItems); labels.id = "hudLabels"; labels.spacing = HUD_GAP;
    AutoTree status = hud_anchor("hudStatus", "top", labels); status.y = HUD_BUTTON_HEIGHT + HUD_GAP;
    tree_array_append(content, status);
    tree_array_append(content, hud_anchor("hudButtons", "top", buttons));

    AutoTree bottomItems = tree_newarr();
    AutoTree controls = json_tree("[]");
    tree_array_append(controls, joystick());
    tree_array_append(controls, spacer());
    tree_array_append(controls, jump_pad());
    AutoTree controlBar = hstack(controls); controlBar.id = "hudControls"; controlBar.height = stickSize;
    tree_array_append(bottomItems, controlBar);
    AutoTree xp_label=label("");
    xp_label.id="xpTitle"; xp_label.fontSize=13; xp_label.color="#CEF77C"; xp_label.colorbg="#263831";
    AutoTree xp_shapes=tree_newarr();
    AutoTree xp_track=mc_rect(0,0,XP_BAR_WIDTH,4);
    xp_track.fill="#343F39";
    AutoTree xp_fill=mc_rect(0,0,0,4);
    xp_fill.id="xpFill"; xp_fill.fill="#8BEB51"; xp_fill.opacity=0;
    tree_array_append(xp_shapes,xp_track);
    tree_array_append(xp_shapes,xp_fill);
    AutoTree xp_parts=tree_newarr();
    tree_array_append(xp_parts,xp_label);
    tree_array_append(xp_parts,mc_canvas(xp_shapes,XP_BAR_WIDTH,4));
    AutoTree xp=vstack(xp_parts);
    xp.id="xpHud"; xp.height=0; xp.spacing=2; xp.opacity=0;
    tree_array_append(bottomItems,xp);
    int initial_hud_width=START_W-2*HUD_MARGIN;
    hud_gear_edge=gear_edge(initial_hud_width);
    AutoTree gearBar=hstack(gear_contents(initial_hud_width,hud_gear_edge));
    gearBar.id="gearBar"; gearBar.spacing=HUD_GEAR_SPACING; gearBar.height=hud_gear_edge;
    tree_array_append(bottomItems,gearBar);
    AutoTree bottom = vstack(bottomItems); bottom.spacing = HUD_GAP;
    tree_array_append(content, hud_anchor("hudBottom", "bottom", bottom));
    AutoTree hud = zstack(content);
    hud.id = "hud"; hud.width = START_W - 2 * HUD_MARGIN; hud.height = START_H - 2 * HUD_MARGIN;
    AutoTree layers = json_tree("[]");
    tree_array_append(layers, scene);
    AutoTree paper=mc_canvas(tree_newarr(),START_W,START_H);
    paper.id="navigationPaper";
    tree_array_append(layers,paper);
    AutoTree mapArrow=mc_canvas(tree_newarr(),START_W,START_H);
    mapArrow.id="navigationMarker";
    tree_array_append(layers,mapArrow);
    AutoTree name_shapes=tree_newarr();
    AutoTree name_shadow=mc_text(0,0,""),name=mc_text(0,0,"");
    name_shadow.id="mobNameShadow"; name_shadow.color="#191919"; name_shadow.fontSize=14;
    name.id="mobName"; name.color="#FFFFFF"; name.fontSize=14;
    tree_array_append(name_shapes,name_shadow);
    tree_array_append(name_shapes,name);
    AutoTree name_canvas=mc_canvas(name_shapes,START_W,START_H);
    name_canvas.id="mobNameCanvas";
    tree_array_append(layers,name_canvas);
    tree_array_append(layers, look);
    tree_array_append(layers, hud);
    AutoTree sleeping=zstack(tree_newarr());
    sleeping.id="sleepLayer";
    tree_array_append(layers,sleeping);
    AutoTree pageHost = zstack(tree_newarr());
    pageHost.id = "pageHost";
    tree_array_append(layers, pageHost);
    AutoTree geochildren = json_tree("[]");
    AutoTree viewport = zstack(layers);
    viewport.id = "gameViewport"; viewport.width = START_W; viewport.height = START_H;
    tree_array_append(geochildren, viewport);
    AutoTree geo = mc_geometry(geochildren, &layout);
    AutoTree safe = json_tree("[]");
    tree_array_append(safe, geo);
    AutoTree top = json_tree("[]");
    tree_array_append(top, mc_safe(safe));
    /* The enclosing SDL viewport presents at 60 Hz. Keep frame production
       on that cadence; the gameplay simulation still advances by elapsed time. */
    tree_array_append(top, mc_timer(1.0 / 60.0, 1, &tick));
    return component(top);
}
