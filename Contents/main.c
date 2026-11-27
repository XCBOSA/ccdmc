#include "game/game.h"
#include "render/renderer.h"
#include "ui/ui.h"
#include "ui/sound_host.h"
#include "save/save.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    for (int i = 0; argv && i < argc && argv[i]; i++) {
        if (strcmp(argv[i], "--mesh-test") == 0) return renderer_mesh_selftest();
    }
    /* The native world menu does not need an SDL surface or GPU. In
       particular, a device that refuses the renderer can still open it. */
    puts("CCDMC: opening world list.");
    fflush(stdout);
    sound_host_open();
    int storage_ready = save_open_directory("saves");
    UIContainer ui = ccd_ui_create();
    if (ui<0) {
        fputs("CCDMC: unable to create the CCDUI window.\n",stderr);
        fflush(stderr);
        sound_host_close();
        return 1;
    }
    AutoTree root = build_ui(ui);
    show_world_list(root, storage_ready ? NULL : save_error());
    ccd_ui_attach(ui, root);
    ccd_ui_show(ui, UIShowTypeEditorTab);
    int code = ccd_ui_start();
    if (code) { fprintf(stderr,"CCDMC: CCDUI event loop ended with status %d.\n",code); fflush(stderr); }
    if (save_current()) save_write();
    sound_host_close();
    renderer_stop_meshing();
    world_stream_stop();
    return code;
}
