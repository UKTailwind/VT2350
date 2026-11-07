/* boards/pc3.h: the Pico Computer 3, the test board. RP2350B, HDMI on GP12-19 in its own order.
   GP8/GP9 go to the board's CH340, so a test build has a debug link there beside the two UARTs. */
#ifndef VT_BOARD_H
#define VT_BOARD_H

#define VT_BOARD_NAME "Pico Computer 3"

/* HDMI: the HSTX output bit (0-7, GP12-19) of each pair's first pin, as OPTION HDMI PINS.
   This build's wiring when the straps choose none and none has been saved */
#define VT_HDMI_CLK 1
#define VT_HDMI_D0 3
#define VT_HDMI_D1 5
#define VT_HDMI_D2 7

#define VT_STRAP_A 6            /* each tied low, tied high or left open at power-on: see README.md */
#define VT_STRAP_B 7

#define VT_UART0_TX 0
#define VT_UART0_RX 1
#define VT_UART1_TX 4
#define VT_UART1_RX 5
#define VT_BELL_PIN 2

#define VT_LED_PIN (-1)         /* its LED is on the radio chip, which stays off */
#define VT_DEBUG_TX 8           /* a PIO UART: not on the other boards */
#define VT_DEBUG_RX 9
#endif
