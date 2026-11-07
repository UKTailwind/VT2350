/* platform.h: what vt_term.c and vt100.c need from the machine. The RP2350's side is in platform.c, lines.c and settings.c; tools/harness has another,
   so that the same two files run on a PC under vttest. */
#ifndef PLATFORM_H
#define PLATFORM_H
#include <stddef.h>
#include <stdint.h>

typedef struct { uint8_t *fb; uint32_t width, height, pitch; } plat_screen_t;

void     plat_screen(plat_screen_t *s);         /* the 8-bit framebuffer */
void     plat_palette(const uint8_t *rgb);      /* 256 RGB triples */
uint64_t plat_micros(void);
void     plat_poll(void);                       /* USB and the rest: call it often */
int      plat_key(void);                        /* the next key, or -1 */
int      plat_key_mods(void);                   /* as KEYDOWN(7): 1 left Alt, 8 left Shift, 128 right Shift... */
int      plat_mouse(int m[4]);                  /* x, y, buttons, wheel: 0 if there is no mouse */
void     plat_bell(void);
void     plat_bell_mode(int mode);              /* BELL_TONE... */
void     plat_keyboard(int layout);
const char *plat_keyboard_name(int layout);
int      plat_keyboard_count(void);
void     plat_wiring(const uint8_t *w);         /* the HDMI wiring, as OPTION HDMI PINS: clock, D0, D1, D2 */
void     plat_wiring_default(uint8_t *w);       /* this build's */
int      plat_straps(void);                     /* the two strap pins, each 0 open, 1 low, 2 high: 3 * first + second */
void     plat_restart(void);
void     plat_bootsel(void);                    /* the bootloader, for a new UF2 */

enum { BELL_TONE, BELL_FLASH, BELL_OFF, BELL_MODES };

/* the lines: one is open at a time */
enum { PORT_DEBUG = 0, PORT_UART0 = 1, PORT_UART1 = 2, PORT_USB1 = 4, PORT_USB4 = 7 };

int  com_open(int port, int baud, int bits, int parity, int stop);      /* parity 0 none, 1 even, 2 odd. 0, or negative: com_error says why */
const char *com_error(void);
void com_close(int port);
int  com_read(int port, uint8_t *buf, int n);
void com_write(int port, const uint8_t *buf, int n);
int  com_connected(int port);                   /* 0 if a USB device has gone: the port stays open, and is set up again when it is back */
void com_flow(int on);                          /* XON/XOFF sent to the host as the receive ring fills and empties */

/* the settings, kept over a power cycle */
int  settings_load(void *p, size_t n);          /* 1 if there were some of this size */
int  settings_save(const void *p, size_t n);    /* 0, or negative */
#endif
