/* lines.c: UART0, UART1 and four USB serial devices, one open at a time, into one receive ring. The USB side is the PicoMite's io/Serial.c: reception only while
   the port is open, as an IN transfer left pending takes turns on the pipe that control transfers share; the requests of an open are blocking, so never from a
   callback; a device that goes keeps the port open, and is set up again by lines_poll when it is back. Callbacks here only copy bytes. MIT License. */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/structs/usb.h"
#include "hardware/uart.h"
#include "tusb.h"
#include "debug.h"
#include "lines.h"
#include "platform.h"
#include "vt_board.h"

#define RXBUF 16384                             /* a power of two */
#define XOFF_AT (RXBUF * 3 / 4)
#define XON_AT (RXBUF / 4)
#define XON 0x11
#define XOFF 0x13

static uint8_t rx[RXBUF];
static volatile uint32_t head, tail;
static uint32_t lost, errors, peak;
static int active = -1;                         /* the port open */
static bool flow, xoff_sent;
static char error_text[48];
static struct { int baud, bits, parity, stop; } line;
static volatile bool mounted_again[CFG_TUH_CDC];

#if VT_HAS_DEBUG
static uint8_t trace[256];
static uint32_t trace_n, cb_count, rx_count, tx_count;
static uint32_t cb_time[48], cb_sof[48];

void lines_trace(void) {
    debug_print("\r\nVTTRACE callbacks %lu rx bytes %lu tx bytes %lu:", (unsigned long)cb_count, (unsigned long)rx_count, (unsigned long)tx_count);
    uint32_t from = trace_n > sizeof(trace) ? trace_n - sizeof(trace) : 0;
    for(uint32_t i = from; i < trace_n; i++) debug_print(" %02x", trace[i % sizeof(trace)]);
    debug_print("\r\nVTCB");
    for(uint32_t i = 1; i < 48; i++) {
        uint32_t k = (cb_count + i) % 48, j = (cb_count + i - 1) % 48;
        debug_print(" %lu/%lu", (unsigned long)(cb_time[k] - cb_time[j]), (unsigned long)((cb_sof[k] - cb_sof[j]) & 0x7FF));
    }
    debug_print("\r\n");
}
#define NOTE_RX(c) (trace[trace_n++ % sizeof(trace)] = (c), rx_count++)
#define NOTE_CB() (cb_time[cb_count % 48] = time_us_32(), cb_sof[cb_count % 48] = usb_hw->sof_rd, cb_count++)
#define NOTE_TX(n) (tx_count += (n))
#else
void lines_trace(void) {}
#define NOTE_RX(c) ((void)0)
#define NOTE_CB() ((void)0)
#define NOTE_TX(n) ((void)0)
#endif

static inline void put(uint8_t c) {
    uint32_t n = head - tail;
    if(n >= RXBUF) {
        lost++;
        return;
    }
    rx[head++ % RXBUF] = c;
    if(n >= peak) peak = n + 1;
}

/* What is in the UART's FIFO, from the interrupt and from com_read, so that a slow line is not left waiting for the FIFO's level or its timeout */
static inline void drain(uart_inst_t *u) {
    while(uart_is_readable(u)) {
        uint32_t d = uart_get_hw(u)->dr;
        if(d & 0xF00) errors++;
        put(d);
    }
}

static void __not_in_flash_func(uart0_irq)(void) {
    drain(uart0);
}

static void __not_in_flash_func(uart1_irq)(void) {
    drain(uart1);
}

static int uart_open(int port, int baud, int bits, int parity, int stop) {
    uart_inst_t *u = port == PORT_UART0 ? uart0 : uart1;
    int tx = port == PORT_UART0 ? VT_UART0_TX : VT_UART1_TX, rxp = port == PORT_UART0 ? VT_UART0_RX : VT_UART1_RX;
    int irq = port == PORT_UART0 ? UART0_IRQ : UART1_IRQ;
    uart_init(u, baud);
    gpio_set_function(tx, GPIO_FUNC_UART);
    gpio_set_function(rxp, GPIO_FUNC_UART);
    gpio_pull_up(rxp);                          /* an unconnected line stays at its idle level */
    uart_set_hw_flow(u, false, false);
    uart_set_format(u, bits, stop, parity == 1 ? UART_PARITY_EVEN : parity == 2 ? UART_PARITY_ODD : UART_PARITY_NONE);
    uart_set_fifo_enabled(u, true);
    irq_set_exclusive_handler(irq, port == PORT_UART0 ? uart0_irq : uart1_irq);
    irq_set_enabled(irq, true);
    hw_write_masked(&uart_get_hw(u)->ifls, 2 << UART_UARTIFLS_RXIFLSEL_LSB, UART_UARTIFLS_RXIFLSEL_BITS);       /* half full */
    uart_get_hw(u)->imsc = UART_UARTIMSC_RXIM_BITS | UART_UARTIMSC_RTIM_BITS;
    return 0;
}

static void uart_close(int port) {
    uart_inst_t *u = port == PORT_UART0 ? uart0 : uart1;
    uart_get_hw(u)->imsc = 0;
    irq_set_enabled(port == PORT_UART0 ? UART0_IRQ : UART1_IRQ, false);
    irq_remove_handler(port == PORT_UART0 ? UART0_IRQ : UART1_IRQ, port == PORT_UART0 ? uart0_irq : uart1_irq);
    uart_deinit(u);
    gpio_set_function(port == PORT_UART0 ? VT_UART0_TX : VT_UART1_TX, GPIO_FUNC_NULL);
    gpio_set_function(port == PORT_UART0 ? VT_UART0_RX : VT_UART1_RX, GPIO_FUNC_NULL);
}

static int cdc_setup(int idx) {
    cdc_line_coding_t coding = {(uint32_t)line.baud, line.stop == 2 ? CDC_LINE_CODING_STOP_BITS_2 : CDC_LINE_CODING_STOP_BITS_1,
                                line.parity == 1 ? CDC_LINE_CODING_PARITY_EVEN : line.parity == 2 ? CDC_LINE_CODING_PARITY_ODD : CDC_LINE_CODING_PARITY_NONE, (uint8_t)line.bits};
    /* Turned on first: what the device held while closed (a CH340 keeps 64 bytes) comes in during the requests, and goes at the end */
    tuh_cdc_rx_enable(idx, true);
    xfer_result_t res = XFER_RESULT_INVALID;
    if(!tuh_cdc_set_line_coding(idx, &coding, NULL, (uintptr_t)&res) && res == XFER_RESULT_INVALID)
        tuh_cdc_set_baudrate(idx, line.baud, NULL, (uintptr_t)&res);    /* FTDI and CP210x */
    if(res != XFER_RESULT_INVALID && res != XFER_RESULT_SUCCESS) {
        snprintf(error_text, sizeof(error_text), "the device refused the speed");
        return -1;
    }
    res = XFER_RESULT_INVALID;
    tuh_cdc_set_control_line_state(idx, CDC_CONTROL_LINE_STATE_DTR | CDC_CONTROL_LINE_STATE_RTS, NULL, (uintptr_t)&res);
    if(res != XFER_RESULT_INVALID && res != XFER_RESULT_SUCCESS) {
        snprintf(error_text, sizeof(error_text), "the device refused DTR");
        return -1;
    }
    /* The device can hand over what it held in more than one transfer: five frames for the rest of it, then all of it goes */
    for(uint32_t t0 = time_us_32(); time_us_32() - t0 < 5000;) tuh_task();
    tuh_cdc_read_clear(idx);
    head = tail = 0;
    return 0;
}

int com_open(int port, int baud, int bits, int parity, int stop) {
    int r = 0;
    error_text[0] = 0;
    head = tail = 0;
    xoff_sent = false;
    line.baud = baud;
    line.bits = bits;
    line.parity = parity;
    line.stop = stop;
    if(port == PORT_UART0 || port == PORT_UART1) r = uart_open(port, baud, bits, parity, stop);
    else if(port >= PORT_USB1 && port <= PORT_USB4) {
        int idx = port - PORT_USB1;
        if(!tuh_cdc_mounted(idx)) {
            snprintf(error_text, sizeof(error_text), "no USB serial device there");
            return -4;
        }
        active = port;                          /* the callback's bytes are for this port from here on */
        mounted_again[idx] = false;
        r = cdc_setup(idx);
        if(r < 0) {
            tuh_cdc_rx_enable(idx, false);
            active = -1;
            return r;
        }
    } else if(port != PORT_DEBUG) return -1;
    active = port;
    return r;
}

const char *com_error(void) {
    return error_text;
}

void com_close(int port) {
    if(port == PORT_UART0 || port == PORT_UART1) uart_close(port);
    else if(port >= PORT_USB1 && port <= PORT_USB4) {
        int idx = port - PORT_USB1;
        if(tuh_cdc_mounted(idx)) {
            tuh_cdc_set_control_line_state(idx, 0, NULL, 0);
            tuh_cdc_rx_enable(idx, false);
        }
    }
    active = -1;
}

static void flow_check(void) {
    uint32_t n = head - tail;
    uint8_t c;
    if(!flow) return;
    if(!xoff_sent && n >= XOFF_AT) {
        c = XOFF;
        xoff_sent = true;
        com_write(active, &c, 1);
    } else if(xoff_sent && n <= XON_AT) {
        c = XON;
        xoff_sent = false;
        com_write(active, &c, 1);
    }
}

int com_read(int port, uint8_t *buf, int n) {
    int i = 0;
#if VT_HAS_DEBUG
    if(port == PORT_DEBUG) return debug_read(buf, n);
#endif
    if(port == PORT_UART0 || port == PORT_UART1) {
        uint32_t s = save_and_disable_interrupts();
        drain(port == PORT_UART0 ? uart0 : uart1);
        restore_interrupts(s);
    }
    while(i < n && tail != head) buf[i++] = rx[tail++ % RXBUF];
    flow_check();
    return i;
}

void com_write(int port, const uint8_t *buf, int n) {
#if VT_HAS_DEBUG
    if(port == PORT_DEBUG) {
        debug_write(buf, n);
        return;
    }
#endif
    if(port == PORT_UART0 || port == PORT_UART1) uart_write_blocking(port == PORT_UART0 ? uart0 : uart1, buf, n);
    else if(port >= PORT_USB1 && port <= PORT_USB4) {
        int idx = port - PORT_USB1;
        uint32_t t0 = time_us_32();
        NOTE_TX(n);
        while(n > 0 && tuh_cdc_mounted(idx)) {
            uint32_t done = tuh_cdc_write(idx, buf, n);
            buf += done;
            n -= done;
            tuh_cdc_write_flush(idx);
            if(n == 0) break;
            if(time_us_32() - t0 > 200000) break;   /* a device that has stopped taking data does not stop the terminal */
            tuh_task();
        }
    }
}

int com_connected(int port) {
    if(port >= PORT_USB1 && port <= PORT_USB4) return tuh_cdc_mounted(port - PORT_USB1);
    return 1;
}

void com_flow(int on) {
    flow = on;
}

void lines_poll(void) {
    if(active >= PORT_USB1 && active <= PORT_USB4 && mounted_again[active - PORT_USB1]) {
        int idx = active - PORT_USB1;
        mounted_again[idx] = false;
        if(tuh_cdc_mounted(idx)) cdc_setup(idx);
    }
}

void lines_loopback(int port, int on) {
    uart_inst_t *u = port == PORT_UART0 ? uart0 : uart1;
    if(on) hw_set_bits(&uart_get_hw(u)->cr, UART_UARTCR_LBE_BITS);
    else hw_clear_bits(&uart_get_hw(u)->cr, UART_UARTCR_LBE_BITS);
}

static int flood_ch = -1;

const char *lines_flood(int port, uint32_t bytes) {
    static char pattern[128] __attribute__((aligned(128)));
    uart_inst_t *u = port == PORT_UART0 ? uart0 : uart1;
    for(int i = 0; i < 128; i++) pattern[i] = i == 62 || i == 126 ? '\r' : i == 63 || i == 127 ? '\n' : ' ' + (i * 7) % 90;
    if(flood_ch < 0) flood_ch = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(flood_ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_ring(&c, false, 7);
    channel_config_set_dreq(&c, uart_get_dreq_num(u, true));
    dma_channel_configure(flood_ch, &c, &uart_get_hw(u)->dr, pattern, bytes, true);
    return pattern;
}

int lines_flooding(void) {
    return flood_ch >= 0 && dma_channel_is_busy(flood_ch);
}

void lines_clear_counts(void) {
    lost = errors = peak = 0;
}

uint32_t lines_lost(void) {
    return lost;
}

uint32_t lines_errors(void) {
    return errors;
}

uint32_t lines_peak(void) {
    return peak;
}

void tuh_cdc_mount_cb(uint8_t idx) {
    if(idx < CFG_TUH_CDC) mounted_again[idx] = true;
}

void tuh_cdc_umount_cb(uint8_t idx) {
    (void)idx;                                  /* the port stays open: com_connected tells the user */
}

void tuh_cdc_rx_cb(uint8_t idx) {
    uint8_t buf[64];
    uint32_t n;
    bool mine = active == PORT_USB1 + idx;
    NOTE_CB();
    while((n = tuh_cdc_read(idx, buf, sizeof(buf))) > 0)
        if(mine) for(uint32_t i = 0; i < n; i++) {
            NOTE_RX(buf[i]);
            put(buf[i]);
        }
}

void tuh_cdc_tx_complete_cb(uint8_t idx) {
    (void)idx;
}
