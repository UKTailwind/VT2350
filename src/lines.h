/* lines.h: the side of lines.c that platform.h does not show. */
#ifndef LINES_H
#define LINES_H
#include <stdint.h>

void lines_poll(void);                          /* a USB device come back is set up again */
uint32_t lines_lost(void);                      /* bytes the receive ring had no room for */
uint32_t lines_errors(void);                    /* framing, parity, break and overrun errors from the UART */
uint32_t lines_peak(void);                      /* the most bytes the ring has held */
void lines_loopback(int port, int on);          /* for tests: the UART's own loopback, TX to RX with no wire */
const char *lines_flood(int port, uint32_t bytes);  /* for tests: DMA sends this many bytes at the line's full speed, whatever the main loop is doing: the 128 returned, over and over */
int  lines_flooding(void);
void lines_clear_counts(void);
void lines_trace(void);                         /* for tests: what the USB line last received, a callback at a time */
#endif
