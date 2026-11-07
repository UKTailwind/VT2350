/* xterm_palette.c: xterm's 256 colours, as vt_term.c's colour numbers are pixels. MIT License. */
#include <stdint.h>
#include <string.h>
#include "xterm_palette.h"

static const uint8_t ansi16[16][3] = {{0, 0, 0}, {205, 0, 0}, {0, 205, 0}, {205, 205, 0}, {0, 0, 238}, {205, 0, 205}, {0, 205, 205}, {229, 229, 229},
    {127, 127, 127}, {255, 0, 0}, {0, 255, 0}, {255, 255, 0}, {92, 92, 255}, {255, 0, 255}, {0, 255, 255}, {255, 255, 255}};

void xterm_palette(uint8_t *rgb) {
    static const uint8_t lev[6] = {0, 95, 135, 175, 215, 255};
    int i;
    memcpy(rgb, ansi16, sizeof(ansi16));
    for(i = 0; i < 216; i++) {
        rgb[(16 + i) * 3] = lev[i / 36];
        rgb[(16 + i) * 3 + 1] = lev[i / 6 % 6];
        rgb[(16 + i) * 3 + 2] = lev[i % 6];
    }
    for(i = 0; i < 24; i++) rgb[(232 + i) * 3] = rgb[(232 + i) * 3 + 1] = rgb[(232 + i) * 3 + 2] = 8 + i * 10;
}
