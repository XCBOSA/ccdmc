#include "world_test.h"
#include "../game/actors.h"
#include "../game/interaction.h"
#include "../game/leads.h"
#include <math.h>
#include <stdio.h>

enum { Y=154 };
#define CHECK(c) do { if (!(c)) { printf("WOLF TEST failed line %d: %s\n",__LINE__,#c); ++bad; } } while (0)
static ItemStack stack(int type,int count) { return (ItemStack){(uint16_t)type,(uint8_t)count,0,0,0}; }
static Vec3 at(float x,float z) { return (Vec3){x,Y+.01f,z}; }
static void player_at(float x,float z) { px=x; py=Y+.01f; pz=z; }
static void aim(Vec3 target,float above_feet) {
    float dx=target.x-px,dz=target.z-pz;
    yaw=atan2f(dx,-dz); pitch=atan2f(target.y+above_feet-py-1.55f,hypotf(dx,dz));
}
static void ticks(int n) { for (int i=0;i<n;++i) actors_step(.05f); }
static int room(void) {
    for (int x=4;x<=36;++x) for (int z=3;z<=14;++z)
        if (!set_block(x,Y-1,z,STONE)) return 0;
    return 1;
}
static int habitat(Biome b) {
    return b==BIOME_FOREST || b==BIOME_TAIGA || b==BIOME_MEGA_TAIGA || b==BIOME_COLD_TAIGA || b==BIOME_ROOFED;
}
static int natural_population(void) {
    int bad=0,found=0,x=0,z=0;
    CHECK(world_begin_generated(992382978,GENERATOR_GOLEMS));
    generate();
    for (int a=-1700;a<1700 && !found;a+=23) for (int b=-1550;b<1550 && !found;b+=23) {
        if (!habitat(terrain_biome(a,b)) || !habitat(terrain_biome(a+12,b)) || !habitat(terrain_biome(a-12,b)) ||
            !habitat(terrain_biome(a,b+12)) || !habitat(terrain_biome(a,b-12))) continue;
        x=a; z=b; found=1;
    }
    CHECK(found);
    if (!found) return bad;
    for (int a=-2;a<=2;++a) for (int b=-2;b<=2;++b) (void)block(x+a*CHUNK_SIZE,0,z+b*CHUNK_SIZE);
    px=x+.5f; pz=z+.5f; py=terrain_height(x,z)+1.01f;
    player_reset_rules(CREATIVE); player_rules.day_tick=0;
    const ActorRecord *a=actors_in(OVERWORLD);
    int wolves=0;
    for (int i=0;i<140 && !wolves;++i) {
        ticks(40);
        for (int j=0;j<ACTOR_CAPACITY;++j) if (a[j].type==ACTOR_WOLF) {
            ++wolves;
            CHECK(!a[j].traits && a[j].health==WOLF_WILD_HEALTH);
        }
    }
    CHECK(wolves>=1 && wolves<=4);
    printf("WOLF TEST forest generation origin=(%d,%d), pack=%d\n",x,z,wolves);
    return bad;
}
int wolf_selftest(void) {
    int bad=0;
    world_begin(1318); generate(); inventory_reset(); player_reset_rules(SURVIVAL);
    CHECK(room());
    const ActorRecord *a=actors_in(OVERWORLD);
    const ActorCare *care=actors_care_in(OVERWORLD);
    CHECK(!actors_restore(OVERWORLD,(ActorRecord){at(8.5f,8.5f),0,ACTOR_WOLF,20,0}));
    CHECK(!actors_restore(OVERWORLD,(ActorRecord){at(8.5f,8.5f),0,ACTOR_WOLF,8,WOLF_SITTING}));
    CHECK(!actors_restore(END,(ActorRecord){at(8.5f,8.5f),0,ACTOR_WOLF,8,0}));
    CHECK(actors_restore(OVERWORLD,(ActorRecord){at(8.5f,8.5f),0,ACTOR_WOLF,WOLF_WILD_HEALTH,0}));
    player_at(8.5f,10.8f); selected_slot=0; inventory[0]=stack(BONE,64);
    aim(a[0].feet,.44f);
    CHECK(set_block(8,Y,9,STONE) && set_block(8,Y+1,9,STONE));
    (void)game_tap();
    CHECK(inventory[0].count==64 && a[0].traits==0);
    CHECK(set_block(8,Y,9,AIR) && set_block(8,Y+1,9,AIR));
    for (int i=0;i<30 && !a[0].traits;++i) CHECK(game_tap().message);
    CHECK(a[0].traits==(WOLF_TAMED|WOLF_SITTING) && a[0].health==20 && inventory[0].count<64 && care[0].managed);
    player_at(15.5f,9.5f);
    ticks(22);
    CHECK(a[0].feet.x==8.5f && a[0].feet.z==8.5f);
    player_at(8.5f,10.8f); inventory[0]=(ItemStack){0};
    aim(a[0].feet,.4f);
    CHECK(game_tap().message && a[0].traits==WOLF_TAMED);
    player_at(17.5f,8.5f); ticks(20);
    CHECK(a[0].feet.x>9.f && a[0].feet.x<15.f);
    player_at(30.5f,8.5f); ticks(1);
    CHECK(hypotf(a[0].feet.x-px,a[0].feet.z-pz)<4.4f);
    float sitting_x=a[0].feet.x,sitting_z=a[0].feet.z;
    aim(a[0].feet,.4f);
    CHECK(game_tap().message && (a[0].traits&WOLF_SITTING));
    player_at(178.5f,8.5f); ticks(2);
    CHECK(a[0].type==ACTOR_WOLF && a[0].feet.x==sitting_x && a[0].feet.z==sitting_z);
    player_at(sitting_x,sitting_z+2.f); aim(a[0].feet,.37f);
    const int damage[ACTOR_CAPACITY]={5};
    actors_explosion_hits(damage);
    inventory[0]=stack(COOKED_BEEF,1);
    CHECK(game_tap().message && a[0].health==20 && !inventory[0].count && (a[0].traits&WOLF_SITTING));
    inventory[0]=stack(LEAD,1);
    CHECK(game_tap().message && actors_lead_at(OVERWORLD,0)->kind==LEAD_PLAYER);
    CHECK(set_block((int)sitting_x+1,Y,(int)sitting_z,WOOD_FENCE));
    CHECK(lead_use_fence((Hit){1,(int)sitting_x+1,Y,(int)sitting_z,0,0,0}) &&
        actors_lead_at(OVERWORLD,0)->kind==LEAD_FENCE);

    actors_reset(); player_reset_rules(SURVIVAL); player_rules.day_tick=18000;
    player_at(8.5f,11.1f);
    CHECK(actors_restore(OVERWORLD,(ActorRecord){at(6.3f,8.5f),0,ACTOR_WOLF,20,WOLF_TAMED}) &&
        actors_restore(OVERWORLD,(ActorRecord){at(9.0f,8.7f),0,ACTOR_ZOMBIE,20,0}));
    aim(a[1].feet,1.1f);
    CHECK(mine() && a[1].health==19);
    for (int i=0;i<32 && a[1].health==19;++i) ticks(1);
    CHECK(a[0].type==ACTOR_WOLF && a[1].health<=15);
    actors_reset();
    CHECK(actors_restore(OVERWORLD,(ActorRecord){at(7.2f,8.5f),0,ACTOR_WOLF,8,0}) &&
        actors_restore(OVERWORLD,(ActorRecord){at(10.5f,8.5f),0,ACTOR_WOLF,8,0}));
    player_at(7.2f,10.7f); aim(a[0].feet,.38f);
    CHECK(mine() && a[0].health==7 && actors_wolf_angry(OVERWORLD,0) &&
        actors_wolf_angry(OVERWORLD,1) && actors_hostile_near(a[0].feet,2,2));
    inventory[0]=stack(BONE,3);
    CHECK(game_tap().message && inventory[0].count==3 && !a[0].traits);
    ticks(23);
    CHECK(player_rules.health<20);

    actors_reset(); player_reset_rules(SURVIVAL);
    CHECK(actors_restore(OVERWORLD,(ActorRecord){at(8.3f,8.5f),0,ACTOR_WOLF,20,WOLF_TAMED}) &&
        actors_restore(OVERWORLD,(ActorRecord){at(10.1f,8.5f),0,ACTOR_WOLF,20,WOLF_TAMED}));
    inventory[0]=stack(RAW_BEEF,2);
    player_at(8.3f,10.6f); aim(a[0].feet,.35f);
    CHECK(game_tap().message && care[0].love && inventory[0].count==1);
    player_at(10.1f,10.6f); aim(a[1].feet,.35f);
    CHECK(game_tap().message && care[1].love && !inventory[0].count);
    for (int i=0;i<115 && a[2].type!=ACTOR_WOLF;++i) ticks(1);
    CHECK(a[2].type==ACTOR_WOLF && a[2].traits==WOLF_TAMED && a[2].health==20 && care[2].grow && care[2].managed &&
        care[0].cooldown && !care[0].love && care[1].cooldown && !care[1].love);
    bad+=natural_population();
    printf("WOLF TEST wild/tame/seat/follow/safe-landing/meat/lead/assist/anger/breed failures=%d\n",bad);
    return bad;
}
