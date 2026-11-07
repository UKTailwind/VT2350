/* keyboard.c: from the PicoMite's input/KeyboardMap.c. What differs is what a terminal needs: Ctrl-C is a byte like any other, Enter is CR and Ctrl-J an LF,
   Ctrl works on the punctuation that has a control code, every key repeats, and the keys go to a ring rather than a console. MIT License. */
#include <string.h>
#include "pico/stdlib.h"
#include "keyboard.h"

#define REPEAT_START 600                        /* ms: the PicoMite's defaults */
#define REPEAT_RATE 150
#define RING 64

#define MOD_LCTRL 0x01
#define MOD_LSHIFT 0x02
#define MOD_LALT 0x04
#define MOD_LGUI 0x08
#define MOD_RCTRL 0x10
#define MOD_RSHIFT 0x20
#define MOD_RALT 0x40
#define MOD_RGUI 0x80

/* HID usages this file names */
#define K_A 0x04
#define K_Z 0x1D
#define K_ENTER 0x28
#define K_CAPS 0x39
#define K_SCROLL 0x47
#define K_NUM 0x53
#define K_KP_SLASH 0x54
#define K_KP_ENTER 0x58
#define K_KP_DOT 0x63
#define K_NON_US_SLASH 0x64

const char *const kbd_names[KB_COUNT] = {"US", "UK", "German", "French", "Spanish", "Belgian"};
static const int *const tables[KB_COUNT] = {USkeyValue, UKkeyValue, DEkeyValue, FRkeyValue, ESkeyValue, BEkeyValue};

static int layout = KB_US;
static const int *keylayout = USkeyValue;
static int caps_lock, num_lock, scroll_lock;
static uint8_t held;                            /* the key that repeats, 0 none */
static int mods;
static uint32_t keytimer, repeattime = REPEAT_START;
static uint8_t ring[RING];
static uint32_t rhead, rtail;
volatile bool kbd_leds_changed;

void kbd_layout(int l) {
    layout = l >= 0 && l < KB_COUNT ? l : KB_US;
    keylayout = tables[layout];
}

uint8_t kbd_leds(void) {
    return (num_lock ? 1 : 0) | (caps_lock ? 2 : 0) | (scroll_lock ? 4 : 0);
}

static void push(uint8_t c) {
    static uint8_t last;
    static uint64_t lasttime;
    if(c == last && time_us_64() - lasttime < 25000) return;
    lasttime = time_us_64();
    last = c;
    if(rhead - rtail < RING) ring[rhead++ % RING] = c;
}

int kbd_get(void) {
    if(rtail == rhead) return -1;
    return ring[rtail++ % RING];
}

int kbd_mods(void) {
    return mods;
}

/* AltGr's characters, which the tables have no column for */
static int altgr(uint8_t k) {
    static const uint8_t de[][2] = {{0x24, 0x7B}, {0x25, 0x5B}, {0x26, 0x5D}, {0x27, 0x7D}, {0x2D, 0x5C}, {0x14, 0x40}, {0x64, 124}, {0x30, 126}};
    static const uint8_t fr[][2] = {{0x1F, 126}, {0x20, 35}, {0x21, 123}, {0x22, 91}, {0x23, 124}, {0x24, 96}, {0x25, 92}, {0x26, 94}, {0x27, 64}, {0x2D, 93}, {0x2E, 125}};
    static const uint8_t es[][2] = {{0x35, 92}, {0x1E, 124}, {0x1F, 64}, {0x20, 35}, {0x2F, 91}, {0x30, 93}, {0x31, 125}, {0x34, 123}};
    static const uint8_t be[][2] = {{0x64, 92}, {0x20, 35}, {0x2F, 91}, {0x30, 93}, {0x31, 96}, {0x34, 39}, {0x38, 126}};
    const uint8_t (*t)[2] = NULL;
    int n = 0;
    if(layout == KB_DE) t = de, n = count_of(de);
    else if(layout == KB_FR) t = fr, n = count_of(fr);
    else if(layout == KB_ES) t = es, n = count_of(es);
    else if(layout == KB_BE) t = be, n = count_of(be);
    for(int i = 0; i < n; i++) if(t[i][0] == k) return t[i][1];
    return -1;
}

static uint8_t map_key(uint8_t k, int m) {
    bool shift = m & (MOD_LSHIFT | MOD_RSHIFT), ctrl = m & (MOD_LCTRL | MOD_RCTRL);
    uint8_t c;
    if(k == K_ENTER || k == K_KP_ENTER) return ENTER;
    if(k == K_CAPS || k == K_SCROLL || k == K_NUM || k < K_A || k > K_NON_US_SLASH) return 0;
    if(m & MOD_RALT) {
        int a = altgr(k);
        if(a >= 0) return a;
    }
    if(k >= K_KP_SLASH && k <= K_KP_DOT) c = keylayout[(k << 1) + (num_lock ? 0 : 1)];
    else if(k >= K_A && k <= K_Z) c = keylayout[(k << 1) + ((caps_lock != 0) != shift ? 1 : 0)];
    else c = keylayout[(k << 1) + (shift ? 1 : 0)];
    if(ctrl) {                                  /* the keys with a control code: letters, and @ [ \ ] ^ _ wherever the layout has them */
        if(c >= 'a' && c <= 'z') return c - 96;
        if(c >= 'A' && c <= 'Z') return c - 64;
        if(c >= '@' && c <= '_') return c & 0x1F;
        if(c == '?') return DEL;
    }
    return c;
}

static void press(uint8_t k, int m) {
    keytimer = 0;
    if(k == K_CAPS || k == K_NUM || k == K_SCROLL) {
        if(k == K_CAPS) caps_lock = !caps_lock;
        else if(k == K_NUM) num_lock = !num_lock;
        else scroll_lock = !scroll_lock;
        kbd_leds_changed = true;
        return;
    }
    uint8_t c = map_key(k, m);
    if(c) {
        push(c);
        repeattime = REPEAT_START;
    }
}

void kbd_report(const uint8_t *report) {
    static uint8_t prev[6];
    static bool lasterror;
    uint8_t now[6] = {0};
    int total = 0, m = report[0];
    const uint8_t *keycode = report + 2;
    for(int i = 0; i < 6; i++) {
        if(keycode[i]) total++;
        if(keycode[i] > 0 && keycode[i] < 4) {  /* a rollover or error report */
            lasterror = true;
            return;
        }
    }
    for(int i = 0; i < total; i++) now[i] = keycode[total - i - 1];
    /* the modifiers first: a key is taken with those that came with it */
    mods = (m & MOD_LALT ? 1 : 0) | (m & MOD_LCTRL ? 2 : 0) | (m & MOD_LGUI ? 4 : 0) | (m & MOD_LSHIFT ? 8 : 0) |
           (m & MOD_RALT ? 16 : 0) | (m & MOD_RCTRL ? 32 : 0) | (m & MOD_RGUI ? 64 : 0) | (m & MOD_RSHIFT ? 128 : 0);
    if(total) {
        if(lasterror && total == 2) {
            uint8_t t = now[0];
            now[0] = now[1];
            now[1] = t;
        }
        for(int k = total - 1; k >= 0; k--) {
            bool seen = false;
            for(int i = 0; i < 6; i++) if(now[k] == prev[i]) seen = true;
            if(!seen) press(now[k], m);
        }
        if(now[0] != prev[0]) keytimer = 0;
    } else keytimer = 0;
    lasterror = false;
    memcpy(prev, now, sizeof(prev));
    held = now[0] ? map_key(now[0], m) : 0;
}

void kbd_tick(uint32_t ms) {
    keytimer += ms;
    if(held && keytimer > repeattime) {
        push(held);
        keytimer = 0;
        repeattime = REPEAT_RATE;
    }
}
