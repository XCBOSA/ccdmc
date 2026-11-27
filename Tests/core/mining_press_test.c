#include "world_test.h"
#include "../game/mining_press.h"
#include <stdio.h>

#define CHECK(x) do { if (!(x)) { printf("MINING PRESS TEST line %d: %s\n",__LINE__,#x); ++failures; } } while (0)

int mining_press_selftest(void) {
    int failures=0;
    const MiningAim first={1,10,70,5,1,450},second={1,10,70,4,1,450},empty={0,0,0,0,0,450};
    MiningPress p={0};
    mining_press_begin(&p,first,110,110);
    CHECK(p.held && !mining_press_ready(&p,449));
    mining_press_aim(&p,first,270); // Camera moved within the same block.
    CHECK(mining_press_ready(&p,450));
    mining_press_finish_attempt(&p,second,450,1);
    mining_press_aim(&p,second,500);
    CHECK(p.held && !mining_press_ready(&p,899) && mining_press_ready(&p,900));
    mining_press_aim(&p,empty,899); // Sweeping across a gap isn't a release.
    CHECK(p.held && !mining_press_ready(&p,1200));
    mining_press_aim(&p,first,1300);
    CHECK(!mining_press_ready(&p,1749) && mining_press_ready(&p,1750));
    mining_press_finish_attempt(&p,first,1750,0); // Entity, protected block or failure.
    mining_press_aim(&p,first,2000);
    CHECK(p.held && !mining_press_ready(&p,9999));
    mining_press_aim(&p,second,2200);
    CHECK(mining_press_ready(&p,2650));
    mining_press_release(&p);
    mining_press_aim(&p,first,2700);
    CHECK(!p.held && !mining_press_ready(&p,UINT64_MAX));
    printf("MINING PRESS TEST jitter/retarget/gap/repeat/release failures=%d\n",failures);
    return failures;
}
