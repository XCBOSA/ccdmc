#include "render/renderer.h"
#include "tests/save_test.h"
#include "tests/world_test.h"
#include <stdio.h>
#include <string.h>

struct test_entry {
    const char *argument;
    int (*run)(void);
};

static const struct test_entry tests[] = {
    {"--world-test", world_selftest},
    {"--dungeon-test", dungeon_selftest},
    {"--mineshaft-test", mineshaft_selftest},
    {"--pyramid-test", pyramid_selftest},
    {"--windcut-test", windcut_selftest},
    {"--rain-garden-test", rain_garden_selftest},
    {"--field-supplies-test", field_supplies_selftest},
    {"--villager-test", villager_selftest},
    {"--village-ai-test", village_ai_selftest},
    {"--treasure-test", treasure_selftest},
    {"--horse-armor-test", horse_armor_selftest},
    {"--forestry-test", forestry_selftest},
    {"--leaf-decay-test", leaf_decay_selftest},
    {"--golem-test", golem_selftest},
    {"--snow-test", snow_selftest},
    {"--wolf-test", wolf_selftest},
    {"--cat-test", cat_selftest},
    {"--squid-test", squid_selftest},
    {"--dye-test", dye_selftest},
    {"--trapped-chest-test", trapped_chest_selftest},
    {"--tripwire-test", tripwire_selftest},
    {"--door-test", door_selftest},
    {"--save-test", save_selftest},
    {"--mesh-test", renderer_mesh_selftest},
    {"--water-breathing-test", water_breathing_selftest},
};

int main(int argc, char **argv) {
    /* CCD can pass the configured arguments without an executable name. */
    for (int arg = 0; argv && arg < argc && argv[arg]; ++arg) {
        for (size_t i = 0; i < sizeof tests / sizeof tests[0]; ++i) {
            if (strcmp(argv[arg], tests[i].argument) == 0) return tests[i].run() ? 1 : 0;
        }
    }
    if (argc <= 1) return world_selftest() ? 1 : 0;
    fputs("CCDMC: choose a supported test argument (e.g. --world-test or --save-test).\n", stderr);
    return 2;
}
