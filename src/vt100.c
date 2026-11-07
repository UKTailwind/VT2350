/* vt100.c: the terminal, for a PicoMite or anything else on a serial line. vt_term.c is the screen; this file is the line, the setup menu and the settings.
   From the Colour Maximite 2's apps/vt100, without what needs a filing system (XMODEM, the log, sending a file) for now.

   Shift-F12 or Pause is the setup menu: all twelve function keys go to the host, as MMBasic's editor uses them, and Ctrl-C goes too. MIT License. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debug.h"
#include "platform.h"
#include "vt_term.h"
#ifndef VT_HARNESS
#include "lines.h"
#include "usbhost.h"
#include "video.h"
#else
#define SCREEN_W 640
#define SCREEN_H 480
#endif

#define KEY_SETUP 0xBC                                  /* Shift-F12 */
#define KEY_PAUSE 0x9E
#define K_UP 0x80
#define K_DOWN 0x81
#define K_LEFT 0x82
#define K_RIGHT 0x83

/* colours of the menu, as xterm's numbers */
#define C_BOX 252
#define C_TEXT 234
#define C_SEL 25
#define C_SELT 15

#define FONT_DEFAULT 3                                  /* 8x14: 80 x 34 */

typedef struct {
    int port, baud, format, flow;
    int font, cols, rows, status;
    int charset, colour, bsdel, entercrlf, lfcrlf, echo, scrollback, cursor;
    int layout, bell;
    uint8_t hdmi[4];
} settings;

static settings set = {.port = PORT_UART0, .baud = 115200, .font = FONT_DEFAULT, .status = 1, .charset = VTT_CP437, .colour = VTT_WHITE, .scrollback = 2000, .cursor = 1};

/* HDMI wirings, as OPTION HDMI PINS: clock, D0, D1, D2. The straps and W choose among these, and the menu takes any other */
static const uint8_t wirings[][4] = {
    {2, 0, 6, 4},                                       /* the HSTX sock, and the PicoMite's default */
    {1, 3, 5, 7},                                       /* Pico Computer 3 */
    {1, 3, 7, 5},                                       /* Olimex */
    {3, 1, 7, 5}, {0, 2, 4, 6}, {0, 2, 6, 4},           /* those three with every pair the other way round */
};
#define NWIRINGS (int)(sizeof(wirings) / sizeof(wirings[0]))
#define WIRING_KEEP_US 10000000                         /* a new one goes back by itself after this */
static const int8_t strap_wiring[3][3] = {              /* [first strap][second], each open, low, high: which of those, -1 none */
    {-1, 2, 4},
    {1, 0, -1},
    {3, -1, 5},
};
static uint8_t wired[4];                                /* in use now */
static int strapped;                                    /* the straps chose it, and it stays */

static const int bauds[] = {300, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
#define NBAUDS (int)(sizeof(bauds) / sizeof(bauds[0]))
static const struct { const char *name; int bits, parity, stop; } formats[] = {{"8N1", 8, 0, 1}, {"8E1", 8, 1, 1}, {"8O1", 8, 2, 1}, {"7E1", 7, 1, 1},
    {"7O1", 7, 2, 1}, {"8N2", 8, 0, 2}};
#define NFORMATS (int)(sizeof(formats) / sizeof(formats[0]))

static int open_com = -1;                               /* the port open, -1 none */
static char line_error[64];
static int line_gone;                                   /* the open port's USB device is away: the port stays open, and comes back with it */
static int in_key;

static const char *port_name(int port) {
    static char b[16];
    if(port == PORT_DEBUG) return "Debug";
    if(port == PORT_UART0) return "UART0";
    if(port == PORT_UART1) return "UART1";
    snprintf(b, sizeof(b), "USB%d", port - PORT_USB1 + 1);
    return b;
}

/* the line */
static void line_close(void) {
    if(open_com >= 0) com_close(open_com);
    open_com = -1;
}

static int line_open(void) {
    int r;
    line_close();
    line_error[0] = 0;
    line_gone = 0;
    r = com_open(set.port, set.baud, formats[set.format].bits, formats[set.format].parity, formats[set.format].stop);
    if(r < 0) {
        snprintf(line_error, sizeof(line_error), "%s", com_error());
        return r;
    }
    open_com = set.port;
    return 0;
}

static void line_write(const uint8_t *buf, int n) {
    if(open_com >= 0) com_write(open_com, buf, n);
}

/* the terminal's output: the keys, and its replies to the host */
static void term_out(const char *s, size_t n) {
    size_t i;
    if(in_key && set.entercrlf) {
        for(i = 0; i < n; i++) {
            line_write((const uint8_t *)s + i, 1);
            if(s[i] == '\r') line_write((const uint8_t *)"\n", 1);
        }
    } else line_write((const uint8_t *)s, n);
    if(in_key && set.echo) vtt_write(s, n);
}

static void status(void) {
    char b[160];
    int c, r;
    vtt_size(&c, &r);
    if(line_error[0]) snprintf(b, sizeof(b), " %s: %s  |  Shift-F12 setup", port_name(set.port), line_error);
    else if(line_gone) snprintf(b, sizeof(b), " %s disconnected, waiting for it to come back  |  Shift-F12 setup", port_name(set.port));
    else snprintf(b, sizeof(b), " %s %d %s  |  %dx%d  |  Shift-F12 setup", port_name(set.port), set.baud, formats[set.format].name, c, r);
    vtt_status(b);
}

/* the menu's drawing */
static int scrc, scrr;

static void fill_box(int col, int row, int w, int h) {
    static char blank[256];
    int i;
    memset(blank, ' ', sizeof(blank) - 1);
    blank[w < 255 ? w : 255] = 0;
    for(i = 0; i < h; i++) vtt_text(col, row + i, blank, C_TEXT, C_BOX);
}

#if VT_HAS_DEBUG
static void debug_commands(void);
#else
#define debug_commands() ((void)0)
#endif

static int waitkey(void) {
    int k;
    for(;;) {
        plat_poll();
        debug_commands();
        if((k = plat_key()) >= 0) return k;
    }
}

/* Every message box is the same size, as one drawn over a wider one would leave the end of it showing */
#define MESSAGE_W 58

static int message_w(void) {
    return MESSAGE_W < scrc - 2 ? MESSAGE_W : scrc - 2;
}

static int message_col(void) {
    return (scrc - message_w()) / 2;
}

static void message(const char *title, const char *text) {
    int w = message_w(), col = message_col(), row = scrr / 2 - 2;
    fill_box(col, row, w, 5);
    vtt_text(col + 2, row + 1, title, C_SEL, C_BOX);
    vtt_text(col + 2, row + 3, text, C_TEXT, C_BOX);
}

static void result(const char *title, const char *text) {
    char b[120];
    snprintf(b, sizeof(b), "%s. Press a key", text);
    message(title, b);
    waitkey();
}

static void term_config(vtt_config *c) {
    c->font = set.font;
    c->cols = set.cols;
    c->rows = set.rows;
    c->status = set.status;
    c->charset = set.charset;
    c->colour = set.colour;
    c->cursor = set.cursor;
    c->backspace_del = set.bsdel;
    c->scrollback = set.scrollback;
}

static int screen_as(vtt_config *c) {
    if(vtt_open(c, NULL) < 0) return -1;
    vtt_screen_cells(&scrc, &scrr);
    return 0;
}

static int apply_screen(void) {
    vtt_config c;
    term_config(&c);
    return screen_as(&c);
}

/* the host's change of size: CSI 8;rows;cols t, which a PicoMite sends at OPTION DISPLAY and at each EDIT and FM, and DECCOLM (rows 0, so the rows stay). There is one mode,
   so the fonts are all there is to try, from the one set up downwards. The status line gives way to a host that wants every row: 80 x 40 fills the screen in 8x12 with
   no room for it. It lasts until the menu changes the screen, and is not saved */
static int resize_to(vtt_config *c, int f, int rows, int cols) {
    int sc = SCREEN_W / vtt_fonts[f].w, sr = SCREEN_H / vtt_fonts[f].h;
    if(sc < cols || sr < rows) return 0;
    c->font = f;
    c->status = set.status && sr > rows;
    if(screen_as(c) < 0) return 0;
    status();
    return 1;
}

static void resize_request(int rows, int cols) {
    vtt_config c;
    int f, nowc, nowr;
    vtt_size(&nowc, &nowr);
    if(rows < 1) rows = nowr;
    if(cols < 1 || (rows == nowr && cols == nowc)) return;
    term_config(&c);
    c.cols = cols;
    c.rows = rows;
    for(f = set.font; f >= 0; f--) if(resize_to(&c, f, rows, cols)) return;
}

/* the menu */
static int wiring_ok(const uint8_t *w) {                /* as OPTION HDMI PINS: four of 0-7, each on a pair of its own */
    int i, used = 0;
    for(i = 0; i < 4; i++) {
        if(w[i] > 7 || (used >> (w[i] >> 1) & 1)) return 0;
        used |= 1 << (w[i] >> 1);
    }
    return 1;
}

static void wiring_keep(const uint8_t *w) {             /* saved by itself: other changes in the menu wait for Save settings */
    settings s;
    int r;
    memcpy(wired, w, 4);
    memcpy(set.hdmi, w, 4);
    if(settings_load(&s, sizeof(s))) {
        memcpy(s.hdmi, w, 4);
        r = settings_save(&s, sizeof(s));
    } else r = settings_save(&set, sizeof(set));
    result("HDMI wiring", r == 0 ? "Saved" : "Can't save it");
}

/* A new wiring takes effect at once and goes back by itself unless it is kept, as the wrong one leaves no screen to read this on.
   W, Left and Right go on through the list, and Enter keeps it: all of that works without a picture */
static void wiring_try(const uint8_t *first) {
    uint8_t w[4];
    char b[64];
    int i, k;
    uint64_t t;
    memcpy(w, first, 4);
    for(;;) {
        plat_wiring(w);
        snprintf(b, sizeof(b), "%d,%d,%d,%d: Enter keeps it, Esc goes back", w[0], w[1], w[2], w[3]);
        message("HDMI wiring", b);
        i = message_col() + 15;                         /* after the title: only colours show D1 and D2 changing places, as white and blue stay as they are */
        vtt_text(i, scrr / 2 - 1, "Red", 196, C_BOX);
        vtt_text(i + 4, scrr / 2 - 1, "Green", 28, C_BOX);
        vtt_text(i + 10, scrr / 2 - 1, "Blue", 21, C_BOX);
        t = plat_micros();
        k = -1;
        while(k < 0 && plat_micros() - t < WIRING_KEEP_US) {
            plat_poll();
            debug_commands();
            k = plat_key();
        }
        if(k == 13) {
            wiring_keep(w);
            return;
        }
        if(k < 0 || k == 27) break;
        if(k == 'w' || k == 'W' || k == K_RIGHT || k == K_LEFT) {
            for(i = 0; i < NWIRINGS && memcmp(wirings[i], w, 4); i++) {}
            i = k == K_LEFT ? (i == NWIRINGS ? NWIRINGS - 1 : (i + NWIRINGS - 1) % NWIRINGS) : (i == NWIRINGS ? 0 : (i + 1) % NWIRINGS);
            memcpy(w, wirings[i], 4);
        }
    }
    plat_wiring(wired);
}

static int wiring_free(void) {
    if(strapped) result("HDMI wiring", "Set by the straps on GP6 and GP7");
    return !strapped;
}

static void wiring_step(int d) {
    int i;
    if(!wiring_free()) return;
    for(i = 0; i < NWIRINGS && memcmp(wirings[i], wired, 4); i++) {}
    i = i == NWIRINGS ? (d > 0 ? 0 : NWIRINGS - 1) : (i + d + NWIRINGS) % NWIRINGS;
    wiring_try(wirings[i]);
}

static void wiring_enter(void) {                        /* any of the 384, by number */
    uint8_t w[4];
    char b[64];
    int n = 0, k, i;
    if(!wiring_free()) return;
    while(n < 4) {
        char c[4];
        for(i = 0; i < 4; i++) c[i] = i < n ? (char)('0' + w[i]) : '_';
        snprintf(b, sizeof(b), "Clock, D0, D1, D2, each 0 to 7: %c,%c,%c,%c", c[0], c[1], c[2], c[3]);
        message("HDMI wiring", b);
        k = waitkey();
        if(k == 27) return;
        if((k == 8 || k == 127) && n > 0) n--;
        if(k >= '0' && k <= '7') w[n++] = (uint8_t)(k - '0');
    }
    if(!wiring_ok(w)) result("HDMI wiring", "Each of the four needs a pair of its own");
    else wiring_try(w);
}

enum { M_PORT, M_BAUD, M_FORMAT, M_FLOW, M_FONT, M_COLS, M_ROWS, M_STATUS, M_CHARSET, M_COLOUR, M_CURSOR, M_KEYBOARD, M_BS, M_ENTER, M_LF, M_ECHO, M_BELL,
       M_WIRING, M_RESET, M_SAVE, M_RESTART, M_FIRMWARE, M_COUNT };
static const char *const labels[M_COUNT] = {"Port", "Speed", "Format", "Flow control", "Font", "Columns", "Rows", "Status line", "Characters", "Text colour", "Cursor", "Keyboard",
    "Backspace sends", "Enter sends", "LF received as", "Local echo", "Bell", "HDMI wiring", "Reset the terminal", "Save settings", "Restart", "Firmware update"};

static void value(int i, char *b, int n) {
    static const char *const bells[BELL_MODES] = {"Tone", "Flash", "Off"};
    b[0] = 0;
    switch(i) {
    case M_PORT:
        if(set.port == PORT_UART0) snprintf(b, n, "UART0 (GP0, GP1)");
        else if(set.port == PORT_UART1) snprintf(b, n, "UART1 (GP4, GP5)");
        else snprintf(b, n, "%s", port_name(set.port));
        break;
    case M_BAUD: snprintf(b, n, "%d", set.baud); break;
    case M_FORMAT: snprintf(b, n, "%s", formats[set.format].name); break;
    case M_FLOW: snprintf(b, n, "%s", set.flow ? "XON/XOFF" : "None"); break;
    case M_FONT: snprintf(b, n, "%s", vtt_fonts[set.font].name); break;
    case M_COLS: if(set.cols) snprintf(b, n, "%d", set.cols); else snprintf(b, n, "All"); break;
    case M_ROWS: if(set.rows) snprintf(b, n, "%d", set.rows); else snprintf(b, n, "All"); break;
    case M_STATUS: snprintf(b, n, "%s", set.status ? "On" : "Off"); break;
    case M_CHARSET: snprintf(b, n, "%s", set.charset == VTT_CP437 ? "CP437" : set.charset == VTT_LATIN1 ? "Latin-1" : "UTF-8"); break;
    case M_COLOUR: snprintf(b, n, "%s", set.colour == VTT_WHITE ? "White" : set.colour == VTT_GREEN ? "Green" : "Amber"); break;
    case M_CURSOR: snprintf(b, n, "%s", set.cursor == 0 ? "Block" : set.cursor == 1 ? "Underline" : "Bar"); break;
    case M_KEYBOARD: snprintf(b, n, "%s", plat_keyboard_name(set.layout)); break;
    case M_BS: snprintf(b, n, "%s", set.bsdel ? "DEL (127)" : "BS (8)"); break;
    case M_ENTER: snprintf(b, n, "%s", set.entercrlf ? "CR LF" : "CR"); break;
    case M_LF: snprintf(b, n, "%s", set.lfcrlf ? "CR LF" : "LF"); break;
    case M_ECHO: snprintf(b, n, "%s", set.echo ? "On" : "Off"); break;
    case M_BELL: snprintf(b, n, "%s", bells[set.bell]); break;
    case M_WIRING: snprintf(b, n, "%d,%d,%d,%d%s", wired[0], wired[1], wired[2], wired[3], strapped ? " (GP6, GP7)" : ""); break;
    default: break;
    }
}

static int step(int v, const int *list, int n, int d) {
    int i;
    for(i = 0; i < n; i++) if(list[i] == v) return list[(i + d + n) % n];
    return list[0];
}

static void change(int i, int d) {
    switch(i) {
    case M_PORT: {
        static const int ports[] = {
#if VT_HAS_DEBUG
            PORT_DEBUG,
#endif
            PORT_UART0, PORT_UART1, PORT_USB1, PORT_USB1 + 1, PORT_USB1 + 2, PORT_USB4};
        set.port = step(set.port, ports, sizeof(ports) / sizeof(ports[0]), d);
        break;
    }
    case M_BAUD: set.baud = step(set.baud, bauds, NBAUDS, d); break;
    case M_FORMAT: set.format = (set.format + d + NFORMATS) % NFORMATS; break;
    case M_FLOW: set.flow = !set.flow; break;
    case M_FONT: set.font = (set.font + d + vtt_font_count) % vtt_font_count; break;
    case M_COLS: {
        static const int c[] = {0, 40, 80, 100, 120};
        set.cols = step(set.cols, c, 5, d);
        break;
    }
    case M_ROWS: {
        static const int r[] = {0, 24, 25, 30, 36, 40, 43, 48, 50, 60};
        set.rows = step(set.rows, r, 10, d);
        break;
    }
    case M_STATUS: set.status = !set.status; break;
    case M_CHARSET: set.charset = (set.charset + d + 3) % 3; break;
    case M_COLOUR: set.colour = (set.colour + d + 3) % 3; break;
    case M_CURSOR: set.cursor = (set.cursor + d + 3) % 3; break;
    case M_KEYBOARD: set.layout = (set.layout + d + plat_keyboard_count()) % plat_keyboard_count(); break;
    case M_BS: set.bsdel = !set.bsdel; break;
    case M_ENTER: set.entercrlf = !set.entercrlf; break;
    case M_LF: set.lfcrlf = !set.lfcrlf; break;
    case M_ECHO: set.echo = !set.echo; break;
    case M_BELL: set.bell = (set.bell + d + BELL_MODES) % BELL_MODES; break;
    default: break;
    }
}

static void apply_input(void) {
    com_flow(set.flow);
    plat_keyboard(set.layout);
    plat_bell_mode(set.bell);
}

static void menu(void) {
    static int sel;
    settings old = set;
    char b[100], v[48];
    int i, w = scrc < 46 ? scrc : 46, h = M_COUNT + 3, col, row, k, redraw = 1;
    if(scrr < h) h = scrr;
    col = (scrc - w) / 2;
    row = (scrr - h) / 2;
    for(;;) {
        int first = sel > h - 4 ? sel - (h - 4) : 0;
        if(redraw) {
            vtt_update();
            fill_box(col, row, w, h);
            vtt_text(col + 2, row, "Terminal setup: Esc goes back", C_SEL, C_BOX);
            redraw = 0;
        }
        for(i = first; i < M_COUNT && i - first < h - 3; i++) {
            value(i, v, sizeof(v));
            snprintf(b, sizeof(b), " %-20s %-21s", labels[i], v);
            b[w - 2] = 0;
            vtt_text(col + 1, row + 2 + i - first, b, i == sel ? C_SELT : C_TEXT, i == sel ? C_SEL : C_BOX);
        }
        k = waitkey();
        if(k == 27 || k == KEY_SETUP || k == KEY_PAUSE) break;
        if(k == K_UP) { sel = (sel + M_COUNT - 1) % M_COUNT; redraw = 1; }
        else if(k == K_DOWN) { sel = (sel + 1) % M_COUNT; redraw = 1; }
        else if(k == 'w' || k == 'W' || (sel == M_WIRING && (k == K_LEFT || k == K_RIGHT))) {
            wiring_step(k == K_LEFT ? -1 : 1);
            redraw = 1;
        } else if(k == K_LEFT) change(sel, -1);
        else if(k == K_RIGHT) change(sel, 1);
        else if(k == 13) {
            if(sel == M_WIRING) {
                wiring_enter();
                redraw = 1;
            } else if(sel <= M_BELL) change(sel, 1);
            else if(sel == M_RESET) { vtt_reset(1); break; }
            else if(sel == M_SAVE) {
                result("Settings", settings_save(&set, sizeof(set)) == 0 ? "Saved" : "Can't save them");
                redraw = 1;
            } else if(sel == M_RESTART) plat_restart();
            else if(sel == M_FIRMWARE) {
                message("Firmware update", "Copy a UF2 to the RP2350 drive");
                vtt_update();
                plat_bootsel();
            }
        }
    }
    apply_input();
    if(set.font != old.font || set.cols != old.cols || set.rows != old.rows || set.status != old.status) {
        if(apply_screen() < 0) {
            set.font = old.font;
            set.cols = old.cols;
            set.rows = old.rows;
            set.status = old.status;
            apply_screen();
        }
    } else {
        vtt_config c;
        term_config(&c);
        vtt_set(&c);
        vtt_redraw();
    }
    if(set.port != old.port || set.baud != old.baud || set.format != old.format) line_open();
    status();
}

#if VT_HAS_DEBUG
static void debug_commands(void) {
    int c = debug_command();
    if(c < 0) return;
    if(c == 'S') {
        size_t heap;
        int lines, cols, rows;
        vtt_memory(&heap, &lines);
        vtt_size(&cols, &rows);
        debug_print("\r\nVT2350 heap %u scrollback %d lines, terminal %dx%d, port %s open %d, USB devices %d\r\nframes %lu rx lost %lu errors %lu peak %lu\r\n", (unsigned)heap, lines, cols, rows,
                    port_name(set.port), open_com, usbhost_devices(), (unsigned long)video_frames(), (unsigned long)lines_lost(), (unsigned long)lines_errors(), (unsigned long)lines_peak());
        debug_print("HDMI %d,%d,%d,%d%s, saved %d,%d,%d,%d\r\n", wired[0], wired[1], wired[2], wired[3], strapped ? " by the straps" : "", set.hdmi[0], set.hdmi[1], set.hdmi[2], set.hdmi[3]);
        usbhost_faults();
    } else if(c == 'Y') {
        lines_trace();
    } else if(c == 'E' || c == 'e') {
        usbhost_events(c == 'e');
    } else if(c == 'U') {
        debug_upload();
    } else if(c == 'O') {
        uint32_t a = debug_word(), v = debug_word();
        *(volatile uint32_t *)a = v;
    } else if(c == 'P') {
        uint32_t a = debug_word();
        debug_print("\r\nVTPEEK %08lx %08lx\r\n", (unsigned long)a, (unsigned long)*(volatile uint32_t *)a);
    } else if(c == 'D') {
        debug_print("\r\nVTDUMP %d %d\r\n", SCREEN_W, SCREEN_H);
        debug_write(video_fb, sizeof(video_fb));
    } else if(c == 'X') {
        static char row[512];
        int cols, rows;
        vtt_update();
        vtt_size(&cols, &rows);
        debug_print("\r\nVTTEXT %d %d\r\n", cols, rows);
        for(int r = 0; r < rows; r++) {
            int n = vtt_row_text(r, row, sizeof(row));
            debug_write(row, n);
            debug_print("\r\n");
        }
        debug_print("VTEND\r\n");
    } else if(c == 'F' || c == 'G') {
        /* How fast the terminal digests text, as a host flooding it would: F plain lines, each one scrolling, G the same with colour changes */
        static char b[512];
        int o = 0, line = 0;
        uint64_t start = plat_micros(), last = start, end = start + 3000000, bytes = 0;
        while(plat_micros() < end) {
            uint64_t t;
            o = 0;
            while(o < 400) {
                if(c == 'G') o += snprintf(b + o, sizeof(b) - o, "\033[38;5;%dm", 16 + line % 216);
                o += snprintf(b + o, sizeof(b) - o, "%06d The quick brown fox jumps over the lazy dog, then does it all over again\r\n", line++);
            }
            vtt_write(b, o);
            bytes += o;
            t = plat_micros();
            if(t - last > 16000) {
                vtt_update();
                last = t;
            }
        }
        debug_print("\r\nVTBENCH %c %lu bytes a second, %d lines\r\n", c, (unsigned long)(bytes / 3), line);
    } else if(c == 'W' || c == 'w') {
        /* The whole line at full speed: W with no wire, the UART looped back inside itself, and w out of the pins and round a wire from TX to RX. DMA sends, so that nothing slows the sender if the terminal falls behind.
           Every byte is counted and checked, with the screen drawing and USB running */
        static uint8_t in[512];
        uint32_t bytes = (uint32_t)set.baud / 10 * 5, got = 0, wrong = 0;
        const char *pattern;
        uint64_t start, last, quiet;
        if(open_com != PORT_UART0 && open_com != PORT_UART1) {
            debug_print("\r\nVTLINE needs a UART open\r\n");
            return;
        }
        lines_clear_counts();
        lines_loopback(open_com, c == 'W');
        start = last = quiet = plat_micros();
        pattern = lines_flood(open_com, bytes);
        for(;;) {
            uint64_t t = plat_micros();
            int n = com_read(open_com, in, sizeof(in));
            for(int i = 0; i < n; i++) if(in[i] != (uint8_t)pattern[(got + i) & 127]) wrong++;
            if(n > 0) {
                got += n;
                quiet = t;
                vtt_write((char *)in, n);
            }
            plat_poll();
            if(t - last > 16000) {
                vtt_update();
                last = t;
            }
            if(!lines_flooding() && t - quiet > 200000) break;
        }
        lines_loopback(open_com, 0);
        debug_print("\r\nVTLINE %s at %d: sent %lu got %lu wrong %lu in %lu ms; lost %lu errors %lu ring peak %lu\r\n", port_name(open_com), set.baud, (unsigned long)bytes, (unsigned long)got,
                    (unsigned long)wrong, (unsigned long)((quiet - start) / 1000), (unsigned long)lines_lost(), (unsigned long)lines_errors(), (unsigned long)lines_peak());
    } else if(c == 'b') {
        /* the line's speed: 'b' and an index into bauds[] by the keys' way would be more than this needs: the next one up, round to the start */
        set.baud = step(set.baud, bauds, NBAUDS, 1);
        line_open();
        status();
        debug_print("\r\nVTBAUD %d\r\n", set.baud);
    } else if(c == 'T') {
        video_testcard();
    } else if(c == 'N') {
        vtt_redraw();
        status();
    } else if(c >= '0' && c <= '7') {
        set.port = c - '0';
        line_open();
        status();
    }
}
#endif

void vt100_main(void) {
    static uint8_t buf[512], tr[1024];
    vtt_config c;
    int i, n, k;
    uint64_t lastdraw = 0, retry = 0, linecheck = 0;

    plat_wiring_default(set.hdmi);
    if(settings_load(&set, sizeof(set)) == 0) {}        /* (the defaults stand) */
    if(!wiring_ok(set.hdmi)) plat_wiring_default(set.hdmi);
    i = plat_straps();
    i = strap_wiring[i / 3][i % 3];
    strapped = i >= 0;
    memcpy(wired, strapped ? wirings[i] : set.hdmi, 4);
    plat_wiring(wired);
    if(set.font < 0 || set.font >= vtt_font_count) set.font = FONT_DEFAULT;
    if(set.format < 0 || set.format >= NFORMATS) set.format = 0;
    apply_input();
    term_config(&c);
    if(vtt_open(&c, term_out) < 0) {
        debug_print("the terminal did not open\r\n");
        for(;;) plat_poll();
    }
    vtt_screen_cells(&scrc, &scrr);
    vtt_resize_request = resize_request;
#ifndef VT_HARNESS
    usbhost_init();
#endif
    line_open();
    status();
    for(;;) {
        uint64_t t;
        plat_poll();
        debug_commands();
        n = open_com >= 0 ? com_read(open_com, buf, sizeof(buf)) : 0;
        if(n > 0) {
            if(set.lfcrlf) {
                int o = 0;
                for(i = 0; i < n; i++) {
                    if(buf[i] == '\n') tr[o++] = '\r';
                    tr[o++] = buf[i];
                }
                vtt_write((char *)tr, o);
            } else vtt_write((char *)buf, n);
        }
        while((k = plat_key()) >= 0) {
            if(k == KEY_SETUP || k == KEY_PAUSE) {
                menu();
                continue;
            }
            in_key = 1;
            vtt_key(k, plat_key_mods());
            in_key = 0;
        }
        vtt_mouse();
        t = plat_micros();
        if(t - lastdraw > 16000) {                      /* a frame's worth: however fast the text comes, it is drawn this often and the scrolls merged */
            vtt_update();
            lastdraw = t;
        }
        if(open_com >= 0 && t - linecheck > 100000) {   /* a USB device may go, and come back */
            int gone = com_connected(open_com) == 0;
            linecheck = t;
            if(gone != line_gone) {
                line_gone = gone;
                status();
            }
        }
        if(open_com < 0 && t - retry > 2000000) {       /* a USB device may arrive */
            retry = t;
            if(line_open() == 0) status();
        }
    }
}
