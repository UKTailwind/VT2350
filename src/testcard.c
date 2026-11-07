/* testcard.c: instead of the terminal when built with -DVT_TESTCARD=ON. The picture alone, for bringing up a board: if this shows, the
   clocks, the HDMI pins and core 1 are right. MIT License. */
#include "pico/stdlib.h"
#include "debug.h"
#include "video.h"
#include "xterm_palette.h"

void vt100_main(void) {
    static uint8_t rgb[768];
    uint32_t last = 0, n = 0;
    xterm_palette(rgb);
    video_palette(rgb);
    video_testcard();
#if VT_LED_PIN >= 0
    gpio_init(VT_LED_PIN);
    gpio_set_dir(VT_LED_PIN, GPIO_OUT);
#endif
    for(;;) {
        uint32_t f = video_frames();
        if(f - last >= 60) {                    /* a heartbeat at the frame rate: if the scan-out stops, this does */
            last = f;
            debug_print("frames %lu\r\n", (unsigned long)f);
#if VT_LED_PIN >= 0
            gpio_put(VT_LED_PIN, ++n & 1);
#else
            n++;
#endif
        }
    }
}
