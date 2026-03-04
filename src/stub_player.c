#include "player.h"

#include <stdio.h>

int run_wallpaper_player(const char *video_path, const char *monitor_selector, int verbose) {
    (void)video_path;
    (void)monitor_selector;
    (void)verbose;

    fprintf(stderr, "wallpaper-runner: Linux/Wayland build required (gtk4-layer-shell backend).\n");
    return 1;
}
