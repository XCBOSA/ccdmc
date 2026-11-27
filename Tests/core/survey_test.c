#include "../game/survey.h"
#include "../game/interaction.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { printf("SURVEY TEST failed line %d: %s\n",__LINE__,#x); ++failures; } } while (0)

int survey_selftest(void) {
    int failures=0;
    world_begin(80214);
    player_reset_rules(SURVIVAL);
    inventory_reset();
    memset(inventory,0,sizeof inventory);
    inventory[0]=(ItemStack){BIRCH_PLANK_ITEM,1,0,0,0};
    inventory[1]=(ItemStack){COBBLESTONE,2,0,0,0};
    inventory[2]=(ItemStack){STICK,1,0,0,0};
    const int mix[]={2,0,-1,1},extra[]={2,0,1,1};
    char desc[112];
    CHECK(!craft_preview(extra,2).count);
    CHECK(craft_preview_details(mix,2,desc,sizeof desc).type==SURVEY_TAPPER && desc[0]);
    CHECK(craft_take(mix,2) && inventory[0].type==SURVEY_TAPPER && inventory[1].count==1 && !inventory[2].count);

    const Hit wall={1,0,164,0,0,164,1};
    for (int d=0;d<=8;++d) {
        CHECK(set_block(0,164,-d,STONE));
        CHECK(set_block(-1,164,-d,STONE));
    }
    CHECK(set_block(-1,164,-2,SLATE_ROCK));
    SurveyReading v=survey_read(wall);
    CHECK(v.state==SURVEY_SEAM && v.rock==SLATE_ROCK && v.depth==2 && v.lane==SURVEY_LEFT && !v.vertical);
    CHECK(set_block(0,164,-1,LIMESTONE_ROCK));
    v=survey_read(wall);
    CHECK(v.state==SURVEY_SEAM && v.rock==LIMESTONE_ROCK && v.depth==1 && v.lane==SURVEY_CENTER);
    selected_slot=0; px=.5f; py=162.6f; pz=3.3f; yaw=pitch=0;
    const unsigned long long edit=world_edit_version();
    Interaction tap=game_tap();
    CHECK(tap.message && strstr(tap.message,"孔隙石灰岩") && !tap.placed &&
        tap.panel==INTERACTION_NONE && world_edit_version()==edit && inventory[0].type==SURVEY_TAPPER);
    CHECK(set_block(0,164,-1,WATER) && set_block(0,164,-3,SLATE_ROCK) && set_block(-1,164,-1,AIR));
    CHECK(survey_read(wall).state==SURVEY_CLEAR);
    CHECK(set_block(0,164,0,LAMINATED_MASONRY) && survey_read(wall).state==SURVEY_UNSUITABLE);

    const Hit floor={1,-23,164,44,-23,165,44};
    for (int d=0;d<=SURVEY_VERTICAL_DEPTH;++d) CHECK(set_block(floor.x,floor.y-d,floor.z,STONE));
    CHECK(set_block(floor.x,floor.y-7,floor.z,LIMESTONE_ROCK));
    v=survey_read(floor);
    CHECK(v.state==SURVEY_SEAM && v.depth==7 && v.vertical==-1);
    CHECK(set_block(floor.x,floor.y,floor.z,LIMESTONE_ROCK) && survey_read(floor).state==SURVEY_EXPOSED);
    CHECK(survey_read((Hit){1,INT_MIN,0,INT_MAX,INT_MAX,0,INT_MIN}).state==SURVEY_NO_FACE);
    CHECK(survey_read((Hit){1,2305,160,9250,2305,161,9250}).state==SURVEY_INCOMPLETE);
    int cell;
    CHECK(!world_resident_cell(2305,160,9250,&cell));
    world_reset();
    printf("SURVEY TEST recipe/face/priority/water/air/floor/stream isolation failures=%d\n",failures);
    return failures;
}
