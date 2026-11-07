/* debug.c: see debug.h. A PIO UART, as the board's two hardware UARTs are the terminal's. MIT License. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/structs/psm.h"
#include "hardware/structs/watchdog.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "debug.h"
#include "video.h"

#if VT_HAS_DEBUG
#include "debug_uart.pio.h"

#define DPIO pio0
#define SM_TX 0
#define SM_RX 1
#define RING 4096
#define ESCAPE 0xFE

static uint8_t ring[RING];
static volatile uint32_t head, tail;
static uint8_t keys[64];
static volatile uint32_t khead, ktail;
static volatile int command = -1;
static int state;                               /* 0 line bytes, 1 after the escape, 2 a key's code to come */

static void __not_in_flash_func(debug_irq)(void) {
    while(!pio_sm_is_rx_fifo_empty(DPIO, SM_RX)) {
        uint8_t c = *((io_rw_8 *)&DPIO->rxf[SM_RX] + 3);
        if(state == 2) {
            keys[khead % sizeof(keys)] = c;
            khead++;
            state = 0;
        } else if(state == 1) {
            state = 0;
            if(c == 'B') reset_usb_boot(0, 0);
            else if(c == 'R') watchdog_reboot(0, 0, 0);
            else if(c == 'K') state = 2;
            else if(c == ESCAPE) goto byte;     /* the byte itself, twice */
            else command = c;
        } else if(c == ESCAPE) state = 1;
        else {
        byte:
            if(head - tail < RING) ring[head++ % RING] = c;
        }
    }
}

void debug_init(void) {
    uint off = pio_add_program(DPIO, &debug_tx_program);
    pio_sm_config c = debug_tx_program_get_default_config(off);
    float div = (float)clock_get_hz(clk_sys) / (8 * DEBUG_BAUD);
    sm_config_set_out_shift(&c, true, false, 32);
    sm_config_set_out_pins(&c, VT_DEBUG_TX, 1);
    sm_config_set_sideset_pins(&c, VT_DEBUG_TX);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&c, div);
    pio_sm_set_pins_with_mask(DPIO, SM_TX, 1u << VT_DEBUG_TX, 1u << VT_DEBUG_TX);
    pio_sm_set_pindirs_with_mask(DPIO, SM_TX, 1u << VT_DEBUG_TX, 1u << VT_DEBUG_TX);
    pio_gpio_init(DPIO, VT_DEBUG_TX);
    pio_sm_init(DPIO, SM_TX, off, &c);
    pio_sm_set_enabled(DPIO, SM_TX, true);

    off = pio_add_program(DPIO, &debug_rx_program);
    c = debug_rx_program_get_default_config(off);
    sm_config_set_in_pins(&c, VT_DEBUG_RX);
    sm_config_set_in_shift(&c, true, true, 8);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    sm_config_set_clkdiv(&c, div);
    pio_sm_set_consecutive_pindirs(DPIO, SM_RX, VT_DEBUG_RX, 1, false);
    pio_gpio_init(DPIO, VT_DEBUG_RX);
    gpio_pull_up(VT_DEBUG_RX);
    pio_sm_init(DPIO, SM_RX, off, &c);
    pio_sm_set_enabled(DPIO, SM_RX, true);

    pio_set_irq0_source_enabled(DPIO, pis_sm1_rx_fifo_not_empty, true);
    irq_set_exclusive_handler(PIO0_IRQ_0, debug_irq);
    irq_set_enabled(PIO0_IRQ_0, true);
}

void debug_putc(char c) {
    pio_sm_put_blocking(DPIO, SM_TX, (uint8_t)c);
}

void debug_write(const void *p, size_t n) {
    const char *s = p;
    while(n--) debug_putc(*s++);
}

void debug_print(const char *fmt, ...) {
    char b[160];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    debug_write(b, n < (int)sizeof(b) ? n : (int)sizeof(b) - 1);
}

int debug_read(uint8_t *buf, int n) {
    int i = 0;
    while(i < n && tail != head) buf[i++] = ring[tail++ % RING];
    return i;
}

int debug_command(void) {
    uint32_t s = save_and_disable_interrupts();         /* or a command arriving between the two lines is lost, and its arguments stay in the ring */
    int c = command;
    command = -1;
    restore_interrupts(s);
    return c;
}

int debug_key(void) {
    if(ktail == khead) return -1;
    return keys[ktail++ % sizeof(keys)];
}

uint32_t debug_word(void) {
    uint32_t v = 0;
    uint64_t end = time_us_64() + 200000;
    for(int i = 0; i < 4; i++) {
        uint8_t b = 0;
        while(debug_read(&b, 1) == 0 && time_us_64() < end) {}
        v |= (uint32_t)b << (i * 8);
    }
    return v;
}

static int raw_getc(uint64_t end) {
    while(pio_sm_is_rx_fifo_empty(DPIO, SM_RX)) if(time_us_64() > end) return -1;
    return *((io_rw_8 *)&DPIO->rxf[SM_RX] + 3);
}

/* From here on nothing may run from the flash: it is being replaced. The picture goes on from RAM, showing the image coming in */
static void __no_inline_not_in_flash_func(program_and_reboot)(const uint8_t *image, uint32_t bytes) {
    save_and_disable_interrupts();
    flash_range_erase(0, (bytes + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1u));
    flash_range_program(0, image, (bytes + FLASH_PAGE_SIZE - 1) & ~(FLASH_PAGE_SIZE - 1u));
    hw_set_bits(&psm_hw->wdsel, PSM_WDSEL_BITS & ~(PSM_WDSEL_ROSC_BITS | PSM_WDSEL_XOSC_BITS));
    watchdog_hw->scratch[4] = 0;
    watchdog_hw->ctrl = WATCHDOG_CTRL_TRIGGER_BITS;
    for(;;) {}
}

/* tools/flash.py --link. The length and an FNV-1a sum of the image, then the image, into the framebuffer, which is the only RAM big enough. Nothing has touched the flash
   until the whole of it is here and the sum agrees */
void debug_upload(void) {
    uint32_t head[2] = {0, 0}, sum = 2166136261u;
    uint8_t *p = (uint8_t *)head;
    uint64_t end;
    irq_set_enabled(PIO0_IRQ_0, false);
    debug_print("\r\nVTUPLOAD\r\n");
    end = time_us_64() + 3000000;
    for(int i = 0; i < 8; i++) {
        int c = raw_getc(end);
        if(c < 0) goto failed;
        p[i] = c;
    }
    if(head[0] == 0 || head[0] > sizeof(video_fb)) goto failed;
    for(uint32_t i = 0; i < head[0]; i++) {
        int c = raw_getc(time_us_64() + 1000000);
        if(c < 0) goto failed;
        video_fb[i] = c;
        sum = (sum ^ c) * 16777619u;
    }
    if(sum != head[1]) goto failed;
    debug_print("VTGOOD\r\n");
    busy_wait_ms(20);
    program_and_reboot(video_fb, head[0]);
failed:
    debug_print("VTBAD\r\n");
    busy_wait_ms(20);
    watchdog_reboot(0, 0, 0);
    for(;;) {}
}

/* What was running, then the bootloader: there is no one here to press Reset */
void __attribute__((used)) debug_fault(uint32_t *frame, uint32_t lr) {
    char b[80];
    int n = snprintf(b, sizeof(b), "\r\nHARD FAULT core %d pc %08lx lr %08lx xpsr %08lx\r\n", (int)get_core_num(), (unsigned long)frame[6], (unsigned long)frame[5], (unsigned long)frame[7]);
    for(int i = 0; i < n; i++) {
        while(pio_sm_is_tx_fifo_full(DPIO, SM_TX)) {}
        pio_sm_put(DPIO, SM_TX, (uint8_t)b[i]);
    }
    busy_wait_ms(50);
    reset_usb_boot(0, 0);
}

void __attribute__((naked, used)) isr_hardfault(void) {
    __asm volatile("tst lr, #4\n ite eq\n mrseq r0, msp\n mrsne r0, psp\n mov r1, lr\n b debug_fault\n");
}
#endif
