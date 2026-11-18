/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#include "../ui/ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint16_t pixels[800*480];
int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    struct xz_ui ui; struct xz_ui_model model = {0}; xz_ui_init(&ui);
    for (unsigned deck = 0; deck < 2; ++deck) {
        model.deck[deck].ready = XZ_UI_GATE | XZ_UI_SMART | XZ_UI_THEME;
        model.deck[deck].track = "Night Drive (Extended Mix).mp3";
        model.deck[deck].ram.state = XZ_UI_RAM_READY;
        model.deck[deck].ram.bytes = model.deck[deck].ram.copied = 20 * 1048576u;
        snprintf(model.deck[deck].ram.message, sizeof(model.deck[deck].ram.message), "ORIGINAL TRACK IN RAM / PREPARED STEMS STILL NEED USB");
    }
    for (int theme = 0; theme < 4; ++theme) for (int page = 0; page < 2; ++page) {
        model.theme = theme; ui.page = page ? XZ_UI_RAM_SETUP : XZ_UI_SETTINGS;
        if (!xz_ui_render(&ui, &model, pixels, 800*480, 800)) return 1;
        char path[1024]; snprintf(path, sizeof(path), "%s/theme-%d-%s.ppm", argv[1], theme, page ? "ram" : "advanced");
        FILE *file = fopen(path, "wb"); if (!file) return 1;
        fprintf(file, "P6\n800 480\n255\n");
        for (unsigned i = 0; i < 800*480; ++i) {
            unsigned value = pixels[i]; unsigned char rgb[3] = {
                (unsigned char)(((value >> 11) & 31) * 255 / 31),
                (unsigned char)(((value >> 5) & 63) * 255 / 63),
                (unsigned char)((value & 31) * 255 / 31)};
            if (fwrite(rgb, 1, 3, file) != 3) return 1;
        }
        if (fclose(file)) return 1;
    }
    return 0;
}
