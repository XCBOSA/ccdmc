#include "interaction.h"
#include "air_station.h"
#include "silt_bed.h"
#include "cistern.h"
#include "hydraulic_sorter.h"
#include "fall_sieve.h"
#include "heat_bench.h"
#include "seep.h"
#include "golems.h"
#include "combat.h"
#include "../world/barriers.h"
#include "actors.h"
#include "armor.h"
#include "beds.h"
#include "brewing.h"
#include "boats.h"
#include "chests.h"
#include "farming.h"
#include "fishing.h"
#include "horses.h"
#include "item_frames.h"
#include "fireworks.h"
#include "spawn_eggs.h"
#include "sound.h"
#include "survey.h"
#include "minecarts.h"
#include "navigation.h"
#include "portals.h"
#include "../world/redstone.h"
#include <stdio.h>

static uint64_t last_jump;
static int awaiting_second_jump;

int game_select_hotbar(int index) {
    if ((unsigned)index>=HOTBAR_SIZE) return 0;
    if (index!=selected_slot) { combat_cancel_draw(); fishing_cancel(); }
    selected_slot=index;
    return 1;
}

const char *game_dismount(void) {
    if (horse_rider()>=0) return horse_use();
    if (cart_rider()>=0) return cart_dismount();
    if (boat_rider()>=0) return boat_dismount();
    return NULL;
}
void game_clear_jump_taps(void) { awaiting_second_jump=0; }
void game_jump_tap(uint64_t now_ms) {
    if (!player_rules.health || bed_sleeping()) { game_clear_jump_taps(); return; }
    if (horse_rider()>=0 || boat_rider()>=0 || cart_rider()>=0) {
        game_clear_jump_taps();
        if (horse_rider()>=0) horse_jump(.55f);
        return;
    }
    if (player_rules.mode==CREATIVE && awaiting_second_jump && now_ms>=last_jump && now_ms-last_jump<=325) {
        game_clear_jump_taps();
        player_toggle_flight();
        return;
    }
    last_jump=now_ms;
    awaiting_second_jump=player_rules.mode==CREATIVE;
    player_jump();
}

static Interaction message(const char *text) { return (Interaction){INTERACTION_NONE,{0},text,-1,0}; }
static Interaction panel(InteractionPanel kind,Hit hit) { return (Interaction){kind,hit,NULL,-1,0}; }
static Interaction placement(void) {
    Hit h=target();
    const unsigned long long before=world_edit_version();
    Interaction request=message(place());
    request.placed=world_edit_version()!=before;
    if (request.placed && h.hit)
        sound_material(SOUND_PLACE,world_cell(h.ax,h.ay,h.az),(Vec3){h.ax+.5f,h.ay+.5f,h.az+.5f});
    return request;
}
Interaction game_tap(void) {
    if (!player_rules.health || bed_sleeping() || (unsigned)selected_slot>=HOTBAR_SIZE ||
        horse_rider()>=0 || cart_rider()>=0 || boat_rider()>=0) return message(NULL);
    ItemStack held=inventory[selected_slot];
    Hit h=target();
    const char *resupply=actors_resupply_guide(h);
    if (resupply) return message(resupply);
    const char *machine=camp_machine_deploy(h);
    if (machine) return message(machine);
    machine=actors_camp_machine_use(h);
    if (machine) return message(machine);
    const char *mold=settling_mold_use(h);
    if (mold) return message(mold);
    const char *seep=seep_use(h);
    if (seep) return message(seep);
    const char *cistern=cistern_use(h);
    if (cistern) return message(cistern);
    const char *sorter=hydraulic_sorter_use(h);
    if (sorter) return message(sorter);
    const char *sieve=fall_sieve_use(h);
    if (sieve) return message(sieve);
    const char *bench=heat_bench_use(h);
    if (bench) return message(bench);
    const char *sediment=silt_bed_tap(h);
    if (sediment) return message(sediment);
    /* Probe and lift interactions take precedence over using a held object
       or opening unrelated storage. The fixed dock always offers return. */
    if (uses_aquifer() || (held.count && held.type==SURVEY_TAPPER)) {
        const char *expedition=portals_use_aquifer(h);
        if (expedition) return message(expedition);
        if (held.count && held.type==SURVEY_TAPPER) {
            const char *shell=actors_collect_shell(h);
            if (shell) return message(shell);
            const char *reed=actors_collect_reed(h);
            if (reed) return message(reed);
            const char *ram=actors_probe_faultback(h);
            if (ram) return message(ram);
            const char *ray=actors_sound_ray(h);
            if (ray) return message(ray);
            const char *sample=strata_sample_tap(h);
            return message(sample ? sample : survey_tap(h));
        }
    }
    if (held.count && held.type==STRATA_GAUGE) return message(strata_gauge_tap(h));
    int installing=held.count && (held.type==AIR_STATION_ITEM || held.type==AIR_OUTLET_ITEM ||
        held.type==AIR_PIPE_ITEM || held.type==AIR_RELAY_ITEM);
    if (!installing) {
        const char *air=air_station_tap(h);
        if (air) return message(air);
    }
    if (held.count && held.type==TIDE_GUIDE)
        return message(tide_report((Vec3){px,py+1.55f,pz},0));
    const char *framed=item_frame_use();
    if (framed) return message(framed);
    const char *draw=map_activate();
    if (draw) return message(draw);
    if (!held.count) {
        const char *moth=actors_moth_station(h);
        if (moth) return message(moth);
    }
    int type=h.hit ? block(h.x,h.y,h.z) : AIR;
    if (held.count) {
        if (held.type==NAME_TAG) {
            const char *named=actors_use_name_tag(h);
            if (named) return message(named);
        }
        /* These items act in the direction of the view, even when the ray
           passes over a chest, wire switch, animal or vehicle. */
        if (held.type==BOW) return message(bow_use());
        if (held.type==FIREWORK_ROCKET || held.type==FIRE_CHARGE) return message(firework_use(h));
        if (spawn_egg_item(held.type)) return message(spawn_egg_use(h));
        if (held.type==FISHING_ROD) return message(fishing_use());
        if (held.type==LILY_PAD_ITEM && type==AIR) return placement();
        if (held.type==SNOWBALL) return message(actors_throw_snowball());
        if (brew_splash(held.type).effect!=BREW_SPLASH_NONE) return message(actors_throw_splash());
        if (held.type==EGG || held.type==MILK_BUCKET) return message(actors_use_livestock(h));
    }
    int transfer=held.count && (held.type==HOPPER_ITEM || held.type==DROPPER_ITEM || held.type==DISPENSER_ITEM);
    /* Brewing ingredients can also be food or potions. A tap aimed at the
       stand must open it, not eat or drink what the player meant to deposit. */
    if (type==BREWING_STAND && player_rules.mode==SURVIVAL && !transfer) return panel(INTERACTION_BREW,h);
    if (world_dimension()==OVERWORLD) {
        int villager=actors_aimed_villager(h);
        if (villager>=0) return (Interaction){INTERACTION_VILLAGER,{0},NULL,villager,0};
    }
    const char *used=lead_use_fence(h);
    if (!used) used=actors_use_tide_crab(h);
    if (!used) used=actors_use_kiln_mole(h);
    if (!used) used=actors_use_lead(h);
    if (!used) used=actors_use_pet(h);
    if (!used) used=actors_shear(h);
    if (!used) used=actors_use_livestock(h);
    if (!used) used=horse_use();
    if (!used) used=farm_use(h);
    if (!used) {
        int opened=-1;
        used=cart_use_with_storage(&opened);
        if (opened>=0) return (Interaction){INTERACTION_CART_STORAGE,{0},NULL,opened,0};
    }
    if (!used) used=boat_use();
    if (!used) used=portal_item_use(h);
    /* A probe is often tapped immediately after filling the held flask.
       The targeted device action takes priority over drinking. */
    if (!used && type==CISTERN_PROBE_BASE) {
        int raw;
        if (!redstone_toggle_cistern_probe(h.x,h.y,h.z,&raw)) return message("水位探头模式未更改");
        const int level=redstone_cistern_probe_level(h.x,h.y,h.z,raw),empty=cistern_probe_empty(raw);
        if (level<0) return message(empty ? "空罐告警 · 未连接集雨罐" : "备水信号 · 未连接集雨罐");
        return message(empty ? (!level ? "空罐告警 · 正在输出" : "空罐告警 · 有水，未输出") :
            (level>=2 ? "备水信号 · 正在输出" : "备水信号 · 不足两口，未输出"));
    }
    if (!used) used=player_use_bucket();
    if (!used) used=player_use_bottle();
    if (used) return message(used);
    const ItemDef *def=held.count ? item_def(held.type) : NULL;
    if (def && player_rules.mode==SURVIVAL) {
        int gear=armor_slot(held.type);
        if (gear>=0) {
            if (armor_worn(gear).count) return message("该部位已有护甲，可在背包中替换");
            if (armor_equip(selected_slot,gear)) return message("已装备护甲");
        }
        if (def->food) return message(player_eat() ? "已进食" : "现在不需要进食");
    }
    /* An explicitly held transfer block must be able to attach to containers
       without introducing a separate sneak/placement button. */
    if (transfer && h.hit &&
        (is_chest(type) || type==HOPPER || ejector_type(type) || type==FURNACE || type==BREWING_STAND))
        return placement();
    if (type==FURNACE && held.count && held.type==HEAT_BENCH_ITEM) return placement();
    switch (type) {
    case BUTTON: {
        int r=redstone_press_button(h.x,h.y,h.z);
        return message(r==1 ? "已按下按钮" : r==2 ? "按钮已按下" : "无法按下按钮");
    }
    case LEVER: {
        int r=redstone_toggle_lever(h.x,h.y,h.z);
        return message(r==2 ? "拉杆：通电" : r==1 ? "拉杆：断电" : "拉杆没有可用支撑");
    }
    case REPEATER: {
        static char label[100];
        int delay=redstone_next_delay(h.x,h.y,h.z);
        if (!delay) return message("中继器设置未更改");
        snprintf(label,sizeof label,"中继器：延迟 %d 个信号步%s",delay,
            redstone_repeater_locked(h.x,h.y,h.z) ? " · 已锁定" : "");
        return message(label);
    }
    case COMPARATOR: {
        int mode=redstone_toggle_comparator(h.x,h.y,h.z);
        return message(mode==2 ? "比较器：减法模式" : mode==1 ? "比较器：比较模式" : "比较器设置未更改");
    }
    case FENCE_GATE: return message(player_use_gate(h));
    case WOOD_TRAPDOOR: {
        int changed=trapdoor_toggle(h.x,h.y,h.z);
        return message(changed==2 ? "已打开木活板门" : changed==1 ? "已关闭木活板门" : "无法开关木活板门");
    }
    case WOOD_DOOR: return message(door_toggle(h.x,h.y,h.z) ? NULL : "无法开关木门");
    case IRON_DOOR: return message("铁门由线路控制");
    case BED: return message(bed_use(h));
    case CHEST: case SADDLE_CHEST: case SMITH_CHEST: case TRAPPED_CHEST: case TEMPLE_CHEST:
    case DUNGEON_CHEST: case DESERT_PYRAMID_CHEST: return panel(INTERACTION_CHEST,h);
    case ENDER_CHEST: return panel(INTERACTION_ENDER_CHEST,h);
    case HOPPER: return panel(INTERACTION_HOPPER,h);
    case DROPPER: case DISPENSER: return panel(INTERACTION_EJECTOR,h);
    case ANVIL: return panel(INTERACTION_ANVIL,h);
    case ENCHANTING_TABLE: if (player_rules.mode==SURVIVAL) return panel(INTERACTION_ENCHANT,h); break;
    case CRAFTING_TABLE: if (player_rules.mode==SURVIVAL) return panel(INTERACTION_WORKBENCH,h); break;
    case FURNACE: if (player_rules.mode==SURVIVAL) return panel(INTERACTION_FURNACE,h); break;
    default: break;
    }
    return def && def->placed_block ? placement() : message(NULL);
}
