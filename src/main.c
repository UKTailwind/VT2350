/* main.c: the clocks, then the picture, then the terminal. MIT License. */
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/structs/pads_qspi.h"
#include "hardware/structs/qmi.h"
#include "hardware/vreg.h"
#include "debug.h"
#include "video.h"

extern void vt100_main(void);

/* As the PicoMite: the core voltage first, then the flash's pads and divider, and those again as setting the clock can change them.
   Flash stays at or under 126 MHz: CLKDIV 2 to 288 MHz, 4 over it */
static void flash_timing(void) {
    pads_qspi_hw->io[0] = 0x67;
    pads_qspi_hw->io[1] = 0x67;
    pads_qspi_hw->io[2] = 0x67;
    pads_qspi_hw->io[3] = 0x6B;
    pads_qspi_hw->io[4] = 0x6B;
    pads_qspi_hw->io[5] = 0x6B;
    qmi_hw->m[0].timing = VT_SYS_KHZ <= 288000 ? 0x40006202 : 0x40006204;      /* COOLDOWN 1, RXDELAY 2, MIN_DESELECT 6, CLKDIV */
    sleep_ms(2);
}

static void clocks_up(void) {
    vreg_disable_voltage_limit();
    vreg_set_voltage(VREG_VOLTAGE_1_30);
    sleep_ms(10);
    flash_timing();
    set_sys_clock_khz(VT_SYS_KHZ, false);
    flash_timing();
}

int main(void) {
    clocks_up();
    debug_init();
    debug_print("\r\nVT2350 on %s, %d MHz\r\n", VT_BOARD_NAME, VT_SYS_KHZ / 1000);
    video_start();
    vt100_main();
    return 0;
}
