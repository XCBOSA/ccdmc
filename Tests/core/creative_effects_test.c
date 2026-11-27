#include "../game/actors.h"
#include "../game/fireworks.h"
#include "../game/horses.h"
#include "../game/interaction.h"
#include "../game/item_icons.h"
#include "../game/sound.h"
#include "../game/spawn_eggs.h"
#include "../game/villagers.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"CREATIVE EFFECTS %d: %s\n",__LINE__,#c); ++failures; } } while (0)
static ItemStack find_stack(int type) {
    for (int i=0;i<SLOT_COUNT;++i) if (inventory[i].count && inventory[i].type==type) return inventory[i];
    return (ItemStack){0};
}
int creative_effects_selftest(void) {
    int failures=0;
    generate();
    CHECK(world_begin_options(1712,GENERATOR_CURRENT,WORLD_SUPERFLAT,1));
    player_reset_rules(SURVIVAL);
    CHECK(game_set_difficulty(NORMAL));
    px=.5f; py=4.01f; pz=4.7f; yaw=0; pitch=-.36f;
    memset(inventory,0,sizeof inventory);
    inventory[0]=(ItemStack){GUNPOWDER,4,0,0,0};
    inventory[1]=(ItemStack){ROSE_RED,3,0,0,0};
    inventory[2]=(ItemStack){GOLD_NUGGET,2,0,0,0};
    inventory[3]=(ItemStack){PAPER,3,0,0,0};
    const int first[]={0,1,2,-1};
    char detail[80];
    CHECK(craft_preview_details(first,2,detail,sizeof detail).type==FIREWORK_STAR && strstr(detail,"星形"));
    CHECK(firework_variant_count()==0 && inventory[0].count==4);
    CHECK(craft_take(first,2));
    ItemStack star=find_stack(FIREWORK_STAR);
    const FireworkSpec *spec=firework_spec(star);
    CHECK(spec && spec->star[0].shape==FW_STAR && spec->star[0].colors==(1u<<4));
    CHECK(inventory[4].type==FIREWORK_STAR);
    const int rocket_recipe[]={0,0,3,4};
    CHECK(craft_preview_details(rocket_recipe,2,detail,sizeof detail).count==3 && strstr(detail,"飞行时间 2"));
    CHECK(craft_take(rocket_recipe,2));
    ItemStack rocket=find_stack(FIREWORK_ROCKET);
    spec=firework_spec(rocket);
    CHECK(rocket.count==3 && spec && spec->flight==2 && spec->count==1 && spec->star[0].shape==FW_STAR);
    FireworkSpark sparks[FW_SPARKS];
    CHECK(firework_launch(rocket,(Vec3){.5f,4.05f,.5f},0.f));
    const FireworkFlight *f=fireworks_in(OVERWORLD);
    CHECK(f && firework_sparks(f,sparks)==6);
    for (int i=0;i<27;++i) fireworks_step(.05f);
    CHECK(f->burst>0.f && f->point.y>9.f && firework_sparks(f,sparks)>=42);
    for (int i=0;i<100;++i) fireworks_step(.05f);
    CHECK(!f->fuse && firework_sparks(f,sparks)==0);
    CHECK(!firework_intern(&rocket,(FireworkSpec){1,1,{{0,0,FW_LARGE,0}}},NULL));
    for (int type=EGG_PIG;type<=EGG_SEEP_BEETLE;++type) {
        ItemIcon icon;
        CHECK(item_stack_valid((ItemStack){(uint16_t)type,1,0,0,0}) && item_icon(type,&icon) && icon.path[2] &&
            icon.rgb[2]==spawn_egg_spot(type));
    }
    CHECK(game_set_difficulty(PEACEFUL));
    selected_slot=0;
    inventory[0]=(ItemStack){EGG_OCELOT,2,0,0,0};
    const Hit grass={1,0,3,0,0,4,0};
    CHECK(spawn_egg_use(grass)!=NULL && inventory[0].count==1 && actors_in(OVERWORLD)[0].type==ACTOR_OCELOT);
    CHECK(spawn_egg_use(grass)!=NULL && inventory[0].count==1 && !actors_in(OVERWORLD)[1].type);
    inventory[0]=(ItemStack){EGG_ZOMBIE,2,0,0,0};
    CHECK(spawn_egg_use((Hit){1,3,3,0,3,4,0})!=NULL && inventory[0].count==2 && !actors_in(OVERWORLD)[1].type);
    CHECK(game_set_difficulty(NORMAL));
    CHECK(spawn_egg_use((Hit){1,3,3,0,3,4,0})!=NULL && inventory[0].count==1 &&
        actors_in(OVERWORLD)[1].type==ACTOR_ZOMBIE);
    CHECK(!spawn_egg_at(EGG_HORSE,(Vec3){3.5f,4.025f,.5f}) &&
        spawn_egg_at(EGG_HORSE,(Vec3){6.5f,4.025f,.5f}) && horses_in(OVERWORLD)[0].type);
    CHECK(!spawn_egg_at(EGG_SQUID,(Vec3){8.5f,4.025f,.5f}) && set_block(8,4,0,WATER) &&
        spawn_egg_at(EGG_SQUID,(Vec3){8.5f,4.025f,.5f}));
    CHECK(spawn_egg_at(EGG_PIG,(Vec3){10.5f,4.025f,.5f}) &&
        actors_in(OVERWORLD)[3].type==ACTOR_PIG && actors_care_in(OVERWORLD)[3].managed);
    CHECK(spawn_egg_at(EGG_VILLAGER,(Vec3){12.5f,4.025f,.5f}) &&
        actors_in(OVERWORLD)[4].type==ACTOR_VILLAGER);
    px=12.5f; pz=3.5f;
    VillagerSession shopping=villagers_open(4);
    CHECK(shopping && villagers_count(shopping)==1);
    villagers_close(shopping);
    float buffer[960*2];
    for (int k=SOUND_CAT_MEOW;k<=SOUND_CAT_HURT;++k) {
        sound_clear();
        sound_play((SoundKind)k,1.f,0,(Vec3){px+2.f,py+1.25f,pz});
        CHECK(sound_active());
        float left=0,right=0;
        for (int n=0;n<100;++n) {
            sound_mix(buffer,960);
            for (int i=0;i<960;++i) {
                left+=fabsf(buffer[2*i]); right+=fabsf(buffer[2*i+1]);
                CHECK(fabsf(buffer[2*i])<=1.f && fabsf(buffer[2*i+1])<=1.f);
            }
        }
        CHECK(!sound_active() && left>1.f && right>left*1.3f);
    }
    CHECK(mining_drop(SLATE_ROCK,1)==AIR && mining_drop(SLATE_ROCK,2)==SLATE_ITEM &&
        mining_drop(LIMESTONE_ROCK,0)==AIR && mining_drop(LIMESTONE_ROCK,1)==LIMESTONE_ITEM &&
        mining_drop(LAMINATED_MASONRY,1)==LAMINATE_ITEM);
    CHECK(set_block(2,4,-4,SLATE_ROCK) && block(2,4,-4)==SLATE_ROCK);
    memset(inventory,0,sizeof inventory);
    inventory[6]=(ItemStack){SLATE_ITEM,3,0,0,0};
    inventory[7]=(ItemStack){LIMESTONE_ITEM,2,0,0,0};
    const int masonry[]={6,6,7,-1};
    CHECK(craft_preview_details(masonry,2,NULL,0).type==LAMINATE_ITEM);
    CHECK(inventory[6].count==3 && inventory[7].count==2);
    CHECK(craft_take(masonry,2) && find_stack(LAMINATE_ITEM).count==3 &&
        inventory[6].count==1 && inventory[7].count==1);
    generate();
    printf("CREATIVE EFFECTS geology, staged recipes, rockets, eggs and cat PCM failures=%d\n",failures);
    return failures!=0;
}
