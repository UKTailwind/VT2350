/* debug.h: a link for testing, on boards that have pins for it (VT_DEBUG_TX). Bytes are a line for the terminal like any other, port 0,
   but 0xFE and a letter is a command to the board: B to the bootloader, R restart, S statistics, D the framebuffer, T the test card, K and a
   key's code, as if typed. B works from the interrupt, however stuck the main loop is, and a hard fault goes to the bootloader too, so a
   bad build can always be replaced from the PC. */
#ifndef DEBUG_H
#define DEBUG_H
#include <stddef.h>
#include <stdint.h>
#include "vt_board.h"

#define VT_HAS_DEBUG (VT_DEBUG_TX >= 0)
#define DEBUG_BAUD 921600

#if VT_HAS_DEBUG
void debug_init(void);
void debug_putc(char c);
void debug_write(const void *p, size_t n);
void debug_print(const char *fmt, ...);
int  debug_read(uint8_t *buf, int n);           /* line bytes received */
int  debug_command(void);                       /* a command waiting for the main loop, or -1 */
int  debug_key(void);                           /* a key injected, or -1 */
void debug_upload(void);                        /* a new image over the link, into the flash: for a board whose USB is taken by its hub. Does not return if it works */
uint32_t debug_word(void);                      /* four more bytes of the link, for a command's argument */
#else
#define debug_init() ((void)0)
#define debug_print(...) ((void)0)
#define debug_command() (-1)
#define debug_key() (-1)
#endif
#endif
