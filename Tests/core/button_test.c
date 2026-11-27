#define _POSIX_C_SOURCE 200809L
#include "../game/button_arrows.h"
#include "../game/drops.h"
#include "../game/combat.h"
#include "../world/internal.h"
#include "../world/light.h"
#include "../world/redstone.h"
#include <stdio.h>
#include <time.h>

#define CHECK_BUTTON(c) do { if (!(c)) { printf("BUTTON TEST failed line %d: %s\n",__LINE__,#c); ++failures; } } while (0)
enum { Y=150 };
static void ticks(int n) { while (n--) redstone_step(-16,0); }
static int power(int x,int z) { return redstone_button_power(x,Y,z); }
static size_t lodged(Dimension d) {
    size_t n=0;
    button_arrows_in(d,&n);
    return n;
}
static void aim(int z) { px=-15.5f; py=Y-1.02f; pz=z+2.55f; yaw=pitch=0; }
static void shoot(void) {
    (void)bow_use();
    for (int i=0;i<19;++i) combat_step(.05f);
    (void)bow_use();
    for (int i=0;i<8;++i) combat_step(.025f);
}
int button_selftest(void) {
    int failures=0;
    world_begin(86152);
    generate();
    for (int i=0;i<2;++i) {
        inventory_reset();
        inventory[0]=(ItemStack){(uint16_t)(i ? PLANKS : STONE),1,0,0,0};
        CHECK_BUTTON(craft_take((int[]){0,-1,-1,-1},2) && inventory[0].type==STONE_BUTTON_ITEM+i);
    }
    CHECK_BUTTON(set_block(-16,Y,-1,STONE) && set_block(-16,Y,5,STONE) && set_block(-16,Y,10,STONE));
    CHECK_BUTTON(!redstone_place_button((CircuitPos){-16,Y+1,-1},(CircuitPos){-16,Y,-1},0));
    CHECK_BUTTON(!redstone_place_button((CircuitPos){-16,Y,1},(CircuitPos){-16,Y,2},0));
    CHECK_BUTTON(redstone_place_button((CircuitPos){-16,Y,0},(CircuitPos){-16,Y,-1},0));
    CHECK_BUTTON(redstone_place_button((CircuitPos){-16,Y,6},(CircuitPos){-16,Y,5},1));
    CHECK_BUTTON(redstone_place_button((CircuitPos){-17,Y,10},(CircuitPos){-16,Y,10},0));
    CHECK_BUTTON(world_cell(-16,Y,0)==BUTTON_BASE && world_cell(-17,Y,10)==BUTTON_BASE+EAST &&
        block(-16,Y,0)==BUTTON && block(-17,Y,10)==BUTTON && world_column_top(-16,0)==Y);
    ChunkView v=chunk_view(-1,0);
    CHECK_BUTTON(v.cells && v.high && v.high->planes[Y/CELL_SECTION_HEIGHT] && !v.high->planes[1] &&
        (v.cells[Y]==AIR) && chunk_snapshot_cell(v.cells,v.high,Y)==BUTTON_BASE &&
        !chunk_view(2,0).high);
    CHECK_BUTTON(set_block(-17,Y,9,GLOWSTONE));
    CHECK_BUTTON(block_light_at(-17,Y,10)==14 && light_has_open_sky(-17,Y,10));
    const BlockEdit invalid[]={{-16,Y,0,AIR},{-16,Y+1,0,-1}};
    CHECK_BUTTON(!world_set_blocks(invalid,2) && world_cell(-16,Y,0)==BUTTON_BASE);

    CHECK_BUTTON(set_block(-17,Y,-1,REDSTONE_LAMP));
    aim(0);
    Hit hit=target();
    CHECK_BUTTON(hit.hit && hit.x==-16 && hit.y==Y && hit.z==0);
    BlockBox up,down;
    CHECK_BUTTON(redstone_button_box(BUTTON_BASE,0,&up) &&
        redstone_button_box(BUTTON_BASE,1,&down) && up.z1==.125f && down.z1==.0625f);
    CHECK_BUTTON(redstone_press_button(-16,Y,0)==1);
    ticks(1);
    CHECK_BUTTON(power(-16,0)==15 && world_cell(-17,Y,-1)==LAMP_ON);
    ticks(18);
    CHECK_BUTTON(redstone_press_button(-16,Y,0)==2 && power(-16,0)==15);
    ticks(1);
    CHECK_BUTTON(power(-16,0)==0 && world_cell(-17,Y,-1)==LAMP_ON);
    ticks(4);
    CHECK_BUTTON(world_cell(-17,Y,-1)==LAMP_OFF);
    CHECK_BUTTON(redstone_press_button(-16,Y,6)==1);
    ticks(29);
    CHECK_BUTTON(power(-16,6)==15);
    ticks(1);
    CHECK_BUTTON(!power(-16,6));

    player_reset_rules(SURVIVAL);
    inventory_reset();
    inventory[0]=(ItemStack){BOW,1,0,0,0};
    inventory[1]=(ItemStack){ARROW,3,0,0,0};
    selected_slot=0;
    aim(0);
    shoot();
    CHECK_BUTTON(inventory[1].count==2 && !power(-16,0) && !lodged(OVERWORLD));
    aim(6);
    shoot();
    CHECK_BUTTON(inventory[1].count==1 && power(-16,6)==15 && lodged(OVERWORLD)==1);
    ticks(34);
    CHECK_BUTTON(power(-16,6)==15 && button_arrow_present(-16,Y,6));
    CHECK_BUTTON(world_select_dimension(NETHER) && !lodged(NETHER) && !power(-16,6) &&
        world_select_dimension(OVERWORLD) && button_arrow_present(-16,Y,6));
    drops_reset();
    CHECK_BUTTON(set_block(-16,Y,5,AIR));
    ticks(3);
    CHECK_BUTTON(block(-16,Y,6)==AIR && !lodged(OVERWORLD));
    int wood=0,arrow=0;
    for (int i=0;i<MAXDROP;++i) if (drops[i].item.count) {
        wood|=drops[i].item.type==WOOD_BUTTON_ITEM; arrow|=drops[i].item.type==ARROW;
    }
    CHECK_BUTTON(wood && arrow);
    for (int i=0;i<CHUNK_CACHE+2;++i) (void)chunk_view(i+201,-203);
    CHECK_BUTTON(world_cell(-16,Y,0)==BUTTON_BASE && world_cell(-17,Y,10)==BUTTON_BASE+EAST);
    CHECK_BUTTON(world_stream_start());
    world_set_interactive(1);
    CHECK_BUTTON(world_select_dimension(NETHER) && world_select_dimension(OVERWORLD));
    int light=-1;
    const struct timespec delay={0,2000000};
    for (int i=0;i<4000 && light<0;++i) {
        world_stream_pump(-17,10);
        light=block_light_at(-17,Y,10);
        if (light<0) nanosleep(&delay,NULL);
    }
    CHECK_BUTTON(light==14 && world_cell(-17,Y,10)==BUTTON_BASE+EAST);
    world_stream_stop();
    world_reset();
    printf("BUTTON TEST craft/walls/aim/pulse/cross-chunk/real-arrows/drop/streaming/light failures=%d\n",failures);
    return failures;
}
