/* platform.c: platform.h on the RP2350, and the bell. MIT License. */
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/watchdog.h"
#include "debug.h"
#include "keyboard.h"
#include "lines.h"
#include "platform.h"
#include "usbhost.h"
#include "video.h"
#include "vt_board.h"

#define BELL_HZ 790
#define BELL_MS 150

static int bell_mode;
static uint64_t bell_end;

void plat_screen(plat_screen_t *s) {
    s->fb = video_fb;
    s->width = SCREEN_W;
    s->height = SCREEN_H;
    s->pitch = SCREEN_W;
}

void plat_palette(const uint8_t *rgb) {
    video_palette(rgb);
}

uint64_t plat_micros(void) {
    return time_us_64();
}

static void bell_init(void) {
    uint slice = pwm_gpio_to_slice_num(VT_BELL_PIN);
    pwm_config c = pwm_get_default_config();
    pwm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / 1000000.0f);       /* counting microseconds */
    pwm_config_set_wrap(&c, 1000000 / BELL_HZ - 1);
    pwm_init(slice, &c, true);
    pwm_set_gpio_level(VT_BELL_PIN, 0);
    gpio_set_function(VT_BELL_PIN, GPIO_FUNC_PWM);
}

void plat_bell_mode(int mode) {
    bell_mode = mode;
}

void plat_bell(void) {
    static bool ready;
    if(bell_mode == BELL_OFF) return;
    if(bell_mode == BELL_FLASH) video_invert(true);
    else {
        if(!ready) bell_init();
        ready = true;
        pwm_set_gpio_level(VT_BELL_PIN, 1000000 / BELL_HZ / 2);
    }
    bell_end = time_us_64() + BELL_MS * 1000;
}

void plat_poll(void) {
    usbhost_task();
    lines_poll();
    if(bell_end && time_us_64() >= bell_end) {
        bell_end = 0;
        video_invert(false);
        if(bell_mode != BELL_FLASH) pwm_set_gpio_level(VT_BELL_PIN, 0);
    }
}

int plat_key(void) {
    int k = kbd_get();
#if VT_HAS_DEBUG
    if(k < 0) k = debug_key();
#endif
    return k;
}

int plat_key_mods(void) {
    return kbd_mods();
}

int plat_mouse(int m[4]) {
    return usbhost_mouse(m);
}

void plat_keyboard(int layout) {
    kbd_layout(layout);
}

const char *plat_keyboard_name(int layout) {
    return kbd_names[layout];
}

int plat_keyboard_count(void) {
    return KB_COUNT;
}

void plat_restart(void) {
    watchdog_reboot(0, 0, 0);
    for(;;) tight_loop_contents();
}

void plat_bootsel(void) {
    reset_usb_boot(0, 0);
}

void plat_wiring(const uint8_t *w) {
    video_wiring(w);
}

void plat_wiring_default(uint8_t *w) {
    w[0] = VT_HDMI_CLK;
    w[1] = VT_HDMI_D0;
    w[2] = VT_HDMI_D1;
    w[3] = VT_HDMI_D2;
}

/* Tied low, tied high or left open. The pull-down goes first, and with the input off until the moment it is read: on the first
   RP2350s (erratum E9) a pad's input stops a pull-down bringing the pin low. Nothing is left on the pin afterwards */
static int strap(int pin) {
    int down, up;
    gpio_init(pin);
    gpio_set_input_enabled(pin, false);
    gpio_set_pulls(pin, false, true);
    sleep_us(100);
    gpio_set_input_enabled(pin, true);
    sleep_us(2);
    down = gpio_get(pin);
    gpio_set_pulls(pin, true, false);
    sleep_us(100);
    up = gpio_get(pin);
    gpio_set_pulls(pin, false, false);
    gpio_deinit(pin);
    return down ? 2 : up ? 0 : 1;
}

int plat_straps(void) {
    return 3 * strap(VT_STRAP_A) + strap(VT_STRAP_B);
}
