/* vt_term.h: a terminal on an 8-bit screen, libvterm's emulation drawn straight into the framebuffer. Nothing here knows where the
   bytes come from: vt100.c joins it to a serial port. From the Colour Maximite 2's apps/vt100. MIT License. */
#ifndef VT_TERM_H
#define VT_TERM_H
#include <stddef.h>
#include <stdint.h>

enum { VTT_CP437, VTT_LATIN1, VTT_UTF8 };                   /* how a byte above 127 from the host is read */
enum { VTT_WHITE, VTT_GREEN, VTT_AMBER };                   /* the default text colour */

typedef struct {
    int font;               /* an index into vtt_fonts[] */
    int cols, rows;         /* 0: as many as fit */
    int status;             /* a line kept below the terminal, for vtt_status() */
    int charset;            /* VTT_CP437... */
    int colour;             /* VTT_WHITE... */
    int cursor;             /* 0 a block, 1 an underline, 2 a bar: what a host that has not chosen, or asks for a block, gets */
    int backspace_del;      /* the Backspace key sends 127, not 8 */
    int scrollback;         /* lines kept: fewer if there is not the memory */
} vtt_config;

typedef struct { const char *name; int w, h; const uint8_t *bits; } vtt_font;
extern const vtt_font vtt_fonts[];
extern const int vtt_font_count;

/* out: what the terminal sends to the host, keys and the replies to the host's questions. Returns 0, or negative if there is not the
   memory, and then nothing has changed. Called again it changes the screen and keeps what is on it, if there is the room to. */
int  vtt_open(const vtt_config *cfg, void (*out)(const char *bytes, size_t n));
void vtt_close(void);

void vtt_write(const char *bytes, size_t n);                /* bytes from the host */
void vtt_update(void);                                      /* the screen brought up to date: call it at every frame or so */
int  vtt_key(int key, int mods);                            /* a key from plat_key, mods from plat_key_mods: 1 if the terminal used it */
void vtt_mouse(void);                                       /* the mouse's state read, reported if the host asked: call it often */
void vtt_status(const char *text);                          /* the status line */
void vtt_reset(int hard);                                   /* as RIS (hard) or DECSTR, and the screen cleared */
void vtt_redraw(void);                                      /* after something else has drawn on the screen */
void vtt_size(int *cols, int *rows);
int  vtt_row_text(int row, char *buf, int n);                /* a row as UTF-8, for tests */
void vtt_set(const vtt_config *cfg);                        /* the settings that need no new screen: charset, colour, backspace */

/* something for vt100.c to draw with, over the terminal, until vtt_redraw: in cells of the screen, colours as xterm's 256 */
void vtt_text(int col, int row, const char *s, int fg, int bg);
void vtt_screen_cells(int *cols, int *rows);
void vtt_bell(void);
void vtt_memory(size_t *heap, int *lines);                  /* what the heap holds, and the scrollback's lines */

/* the host's request to change the terminal's size (CSI 8;rows;cols t): the program decides. Called from vtt_write, with libvterm at rest */
extern void (*vtt_resize_request)(int rows, int cols);
#endif
