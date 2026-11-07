/* video.c: the scan-out, from the PicoMite's graphics/HDMI.c, itself from Raspberry Pi's pico-examples dvi_out_hstx_encoder
   (BSD-3-Clause). The HSTX's TMDS encoder is given RGB555 lines; two DMA channels take turns to feed it command lists and pixels,
   and core 1 turns each line of the 8-bit framebuffer into RGB555 through the palette just before it is needed. The palette is
   why this is not the PicoMite's RGB332 640x480x8: xterm's 256 colours are not RGB332's. Everything core 1 and its interrupt run is
   in RAM, so the picture holds while core 0 has the flash off for a save. MIT License. */
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/structs/hstx_ctrl.h"
#include "hardware/structs/hstx_fifo.h"
#include "vt_board.h"
#include "video.h"

#if VT_SYS_KHZ == 252000                        /* the pixel clock is clk_sys / 10: 25.2 MHz, 60 Hz */
#define H_FRONT_PORCH 16
#define H_SYNC_WIDTH 96
#define H_BACK_PORCH 48
#define V_FRONT_PORCH 10
#define V_SYNC_WIDTH 2
#define V_BACK_PORCH 33
#elif VT_SYS_KHZ == 315000                      /* 31.5 MHz, 75 Hz */
#define H_FRONT_PORCH 16
#define H_SYNC_WIDTH 64
#define H_BACK_PORCH 120
#define V_FRONT_PORCH 1
#define V_SYNC_WIDTH 3
#define V_BACK_PORCH 16
#else
#error VT_SYS_KHZ must be 252000 or 315000
#endif
#define V_BLANK (V_FRONT_PORCH + V_SYNC_WIDTH + V_BACK_PORCH)
#define V_TOTAL (V_BLANK + SCREEN_H)

#define TMDS_CTRL_00 0x354u
#define TMDS_CTRL_01 0x0abu
#define TMDS_CTRL_10 0x154u
#define TMDS_CTRL_11 0x2abu
#define SYNC_V0_H0 (TMDS_CTRL_00 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V0_H1 (TMDS_CTRL_01 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H0 (TMDS_CTRL_10 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H1 (TMDS_CTRL_11 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))

#define HSTX_CMD_RAW_REPEAT (0x1u << 12)
#define HSTX_CMD_TMDS (0x2u << 12)
#define HSTX_CMD_NOP (0xfu << 12)

#define DMACH_PING 0
#define DMACH_PONG 1

uint8_t video_fb[SCREEN_W * SCREEN_H] __attribute__((aligned(4)));

static uint16_t lut[2][256];                    /* RGB555: [0] as set, [1] inverted */
static volatile int lut_now;
static uint16_t lines[2][SCREEN_W] __attribute__((aligned(4)));
static uint32_t vblank_line_vsync_off[7];
static uint32_t vblank_line_vsync_on[7];
static uint32_t vactive_line[9];
static bool dma_pong;
static bool vactive_cmdlist_posted;
static volatile uint32_t frames;

/* A ping and a pong are cued up at the start, so the first time in here is to cue the second ping: the third scan line overall */
static volatile int32_t v_scanline = 2;

/* Two interrupts a line in the active period: one posts the command list, the next the pixels */
static void __not_in_flash_func(dma_irq_handler)(void) {
    uint ch_num = dma_pong ? DMACH_PONG : DMACH_PING;           /* the channel that just finished, to be reloaded */
    dma_channel_hw_t *ch = &dma_hw->ch[ch_num];
    dma_hw->intr = 1u << ch_num;
    dma_pong = !dma_pong;
    if(v_scanline >= V_FRONT_PORCH && v_scanline < V_FRONT_PORCH + V_SYNC_WIDTH) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_on;
        ch->transfer_count = count_of(vblank_line_vsync_on);
    } else if(v_scanline < V_BLANK) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_off;
        ch->transfer_count = count_of(vblank_line_vsync_off);
    } else if(!vactive_cmdlist_posted) {
        ch->read_addr = (uintptr_t)vactive_line;
        ch->transfer_count = count_of(vactive_line);
        vactive_cmdlist_posted = true;
    } else {
        ch->read_addr = (uintptr_t)lines[v_scanline & 1];
        ch->transfer_count = SCREEN_W / 2;
        vactive_cmdlist_posted = false;
    }
    if(!vactive_cmdlist_posted) {
        int32_t next = v_scanline + 1;
        if(next == V_TOTAL) {
            next = 0;
            frames++;
        }
        v_scanline = next;
    }
}

static inline void __not_in_flash_func(build_line)(const uint8_t *src, uint32_t *dst, const uint16_t *map) {
    const uint32_t *s = (const uint32_t *)src;
    for(int i = 0; i < SCREEN_W / 4; i++) {
        uint32_t w = *s++;
        *dst++ = map[w & 255] | (uint32_t)map[(w >> 8) & 255] << 16;
        *dst++ = map[(w >> 16) & 255] | (uint32_t)map[w >> 24] << 16;
    }
}

static void __attribute__((optimize("-O3"))) __not_in_flash_func(scan_loop)(void) {
    int32_t last_line = 2;
    for(;;) {
        int32_t now = v_scanline;
        if(now == last_line) continue;
        last_line = now;
        int32_t y = now - V_BLANK;
        if(y >= 0 && y < SCREEN_H) {
            __dmb();
            build_line(video_fb + y * SCREEN_W, (uint32_t *)lines[now & 1], lut[lut_now]);
        }
    }
}

static void pair(int bit, uint32_t sel) {       /* the two pins of a pair: the same signal, one inverted */
    hstx_ctrl_hw->bit[bit] = sel;
    hstx_ctrl_hw->bit[bit & 1 ? bit - 1 : bit + 1] = sel | HSTX_CTRL_BIT0_INV_BITS;
}

/* Only the output selection: nothing else in the HSTX knows which pin is which, so this can be done with the picture running */
void video_wiring(const uint8_t *w) {
    pair(w[0], HSTX_CTRL_BIT0_CLK_BITS);
    for(uint lane = 0; lane < 3; lane++)
        pair(w[lane + 1], (lane * 10) << HSTX_CTRL_BIT0_SEL_P_LSB | (lane * 10 + 1) << HSTX_CTRL_BIT0_SEL_N_LSB);
}

static volatile bool hstx_ready;

static void core1_main(void) {
    /* the pixel clock is clk_hstx / 5 */
    clock_configure(clk_hstx, 0, CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS, clock_get_hz(clk_sys), clock_get_hz(clk_sys) / 2);

    vblank_line_vsync_off[0] = HSTX_CMD_RAW_REPEAT | H_FRONT_PORCH;
    vblank_line_vsync_off[1] = SYNC_V1_H1;
    vblank_line_vsync_off[2] = HSTX_CMD_RAW_REPEAT | H_SYNC_WIDTH;
    vblank_line_vsync_off[3] = SYNC_V1_H0;
    vblank_line_vsync_off[4] = HSTX_CMD_RAW_REPEAT | (H_BACK_PORCH + SCREEN_W);
    vblank_line_vsync_off[5] = SYNC_V1_H1;
    vblank_line_vsync_off[6] = HSTX_CMD_NOP;

    vblank_line_vsync_on[0] = HSTX_CMD_RAW_REPEAT | H_FRONT_PORCH;
    vblank_line_vsync_on[1] = SYNC_V0_H1;
    vblank_line_vsync_on[2] = HSTX_CMD_RAW_REPEAT | H_SYNC_WIDTH;
    vblank_line_vsync_on[3] = SYNC_V0_H0;
    vblank_line_vsync_on[4] = HSTX_CMD_RAW_REPEAT | (H_BACK_PORCH + SCREEN_W);
    vblank_line_vsync_on[5] = SYNC_V0_H1;
    vblank_line_vsync_on[6] = HSTX_CMD_NOP;

    vactive_line[0] = HSTX_CMD_RAW_REPEAT | H_FRONT_PORCH;
    vactive_line[1] = SYNC_V1_H1;
    vactive_line[2] = HSTX_CMD_NOP;
    vactive_line[3] = HSTX_CMD_RAW_REPEAT | H_SYNC_WIDTH;
    vactive_line[4] = SYNC_V1_H0;
    vactive_line[5] = HSTX_CMD_NOP;
    vactive_line[6] = HSTX_CMD_RAW_REPEAT | H_BACK_PORCH;
    vactive_line[7] = SYNC_V1_H1;
    vactive_line[8] = HSTX_CMD_TMDS | SCREEN_W;

    /* RGB555: blue in bits 4-0, green 9-5, red 14-10, each lane's five bits brought to the top of its byte. Two 16-bit pixels a word */
    hstx_ctrl_hw->expand_tmds = 29 << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB | 4 << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB |
                                2 << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB | 4 << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB |
                                7 << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB | 4 << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB;
    hstx_ctrl_hw->expand_shift = 2 << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB | 16 << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB |
                                 1 << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB | 0 << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;

    /* The serial side: 5 cycles a pixel, two bits a cycle */
    hstx_ctrl_hw->csr = 0;
    hstx_ctrl_hw->csr = HSTX_CTRL_CSR_EXPAND_EN_BITS | 5u << HSTX_CTRL_CSR_CLKDIV_LSB | 5u << HSTX_CTRL_CSR_N_SHIFTS_LSB |
                        2u << HSTX_CTRL_CSR_SHIFT_LSB | HSTX_CTRL_CSR_EN_BITS;

    static const uint8_t wiring[4] = {VT_HDMI_CLK, VT_HDMI_D0, VT_HDMI_D1, VT_HDMI_D2};
    video_wiring(wiring);
    hstx_ready = true;

    for(int i = 12; i <= 19; i++) {
        gpio_set_function(i, 0);                /* HSTX */
        gpio_set_drive_strength(i, GPIO_DRIVE_STRENGTH_8MA);
        gpio_set_slew_rate(i, GPIO_SLEW_RATE_FAST);
        gpio_set_input_enabled(i, false);
        gpio_set_pulls(i, false, false);
        gpio_set_input_hysteresis_enabled(i, false);
    }

    /* Both channels alike, each chained to the other: as one finishes the interrupt reloads it while the other runs */
    dma_channel_config c = dma_channel_get_default_config(DMACH_PING);
    channel_config_set_chain_to(&c, DMACH_PONG);
    channel_config_set_dreq(&c, DREQ_HSTX);
    dma_channel_configure(DMACH_PING, &c, &hstx_fifo_hw->fifo, vblank_line_vsync_off, count_of(vblank_line_vsync_off), false);
    c = dma_channel_get_default_config(DMACH_PONG);
    channel_config_set_chain_to(&c, DMACH_PING);
    channel_config_set_dreq(&c, DREQ_HSTX);
    dma_channel_configure(DMACH_PONG, &c, &hstx_fifo_hw->fifo, vblank_line_vsync_off, count_of(vblank_line_vsync_off), false);

    dma_hw->ints0 = (1u << DMACH_PING) | (1u << DMACH_PONG);
    dma_hw->inte0 = (1u << DMACH_PING) | (1u << DMACH_PONG);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);
    dma_channel_start(DMACH_PING);
    scan_loop();
}

void video_start(void) {
    dma_channel_claim(DMACH_PING);
    dma_channel_claim(DMACH_PONG);
    multicore_launch_core1(core1_main);
    while(!hstx_ready) tight_loop_contents();   /* so that video_wiring comes after this build's own wiring, and not before it */
}

void video_palette(const uint8_t *rgb) {
    for(int i = 0; i < 256; i++, rgb += 3) {
        uint16_t c = (rgb[0] >> 3) << 10 | (rgb[1] >> 3) << 5 | rgb[2] >> 3;
        lut[0][i] = c;
        lut[1][i] = c ^ 0x7FFF;
    }
}

void video_invert(bool on) {
    lut_now = on;
}

uint32_t video_frames(void) {
    return frames;
}

/* The xterm palette's 256 colours as a 16 x 16 grid, a white frame on the outermost pixels, so that a monitor's overscan and any lost
   column show, and a one-pixel check in each corner block */
void video_testcard(void) {
    memset(video_fb, 0, sizeof(video_fb));
    for(int y = 0; y < SCREEN_H; y++) {
        uint8_t *p = video_fb + y * SCREEN_W;
        for(int x = 0; x < SCREEN_W; x++) {
            int gx = (x - 32) / 36, gy = (y - 24) / 27;
            if(x >= 32 && y >= 24 && gx < 16 && gy < 16 && (x - 32) % 36 < 34 && (y - 24) % 27 < 25) p[x] = gy * 16 + gx;
            else if(x >= 8 && x < 24 && y >= 8 && y < 472) p[x] = (x + y) & 1 ? 15 : 0;
        }
    }
    for(int x = 0; x < SCREEN_W; x++) video_fb[x] = video_fb[(SCREEN_H - 1) * SCREEN_W + x] = 15;
    for(int y = 0; y < SCREEN_H; y++) video_fb[y * SCREEN_W] = video_fb[y * SCREEN_W + SCREEN_W - 1] = 15;
}
