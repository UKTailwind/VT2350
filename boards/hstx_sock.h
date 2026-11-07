/* boards/hstx_sock.h: the target. A Pico 2 (RP2350A) with the HSTX DVI sock:
   GP12/13 D0, GP14/15 clock, GP16/17 D2, GP18/19 D1. */
#ifndef VT_BOARD_H
#define VT_BOARD_H

#define VT_BOARD_NAME "RP2350 HSTX sock"

/* HDMI: the HSTX output bit (0-7, GP12-19) of each pair's first pin, as OPTION HDMI PINS.
   This build's wiring when the straps choose none and none has been saved */
#define VT_HDMI_CLK 2
#define VT_HDMI_D0 0
#define VT_HDMI_D1 6
#define VT_HDMI_D2 4

#define VT_STRAP_A 6            /* each tied low, tied high or left open at power-on: see README.md */
#define VT_STRAP_B 7

#define VT_UART0_TX 0
#define VT_UART0_RX 1
#define VT_UART1_TX 4
#define VT_UART1_RX 5
#define VT_BELL_PIN 2

#define VT_LED_PIN 25
#define VT_DEBUG_TX (-1)        /* no debug link: both UARTs are the product */
#define VT_DEBUG_RX (-1)
#endif
