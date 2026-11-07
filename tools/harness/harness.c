/* harness.c: src/vt100.c, src/vt_term.c and libvterm on a PC, with a program on a pty as the host, for vttest. platform.h's other side.

     harness [-h heapbytes] [-r rows] [-c cols] [-f font] -- program args...

   The heap is an arena of the given size, so that the terminal lives in what the board has, and a block malloc cannot give is a NULL as it would be there. Commands come on
   stdin, a line each: "key N" a key code, "text S" keys, "dump FILE" the screen as a PPM, "stat" the memory, "quit". MIT License. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include "platform.h"
#include "vt_term.h"

extern void vt100_main(void);

/* the arena: first fit, blocks in address order, as near as this needs to be to newlib */
typedef struct blk { size_t size; int used; struct blk *next; } blk;
static blk *arena;
static size_t arena_size = 173000;

static void arena_init(void) {
    arena_size &= ~(size_t)15;
    arena = aligned_alloc(16, arena_size);
    arena->size = arena_size - sizeof(blk);
    arena->used = 0;
    arena->next = NULL;
}

void *h_malloc(size_t n) {
    n = (n + 15) & ~(size_t)15;
    for(blk *b = arena; b; b = b->next) {
        if(b->used || b->size < n) continue;
        if(b->size >= n + sizeof(blk) + 16) {
            blk *r = (blk *)((char *)(b + 1) + n);
            r->size = b->size - n - sizeof(blk);
            r->used = 0;
            r->next = b->next;
            b->next = r;
            b->size = n;
        }
        b->used = 1;
        return b + 1;
    }
    return NULL;
}

void h_free(void *p) {
    if(p == NULL) return;
    ((blk *)p - 1)->used = 0;
    for(blk *b = arena; b && b->next;) {
        if(!b->used && !b->next->used) {
            b->size += b->next->size + sizeof(blk);
            b->next = b->next->next;
        } else b = b->next;
    }
}

static uint8_t fb[640 * 480];
static uint8_t pal[768];
static int master = -1;
static pid_t child;
static int keys[4096], khead, ktail;
static char linebuf[4096];
static int linelen;
static int want_rows, want_cols, want_font = -1;

void plat_screen(plat_screen_t *s) {
    s->fb = fb;
    s->width = 640;
    s->height = 480;
    s->pitch = 640;
}

void plat_palette(const uint8_t *rgb) {
    memcpy(pal, rgb, sizeof(pal));
}

uint64_t plat_micros(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}

static void dump(const char *name) {
    FILE *f = fopen(name, "wb");
    if(f == NULL) return;
    fprintf(f, "P6\n640 480\n255\n");
    for(int i = 0; i < 640 * 480; i++) fwrite(pal + fb[i] * 3, 3, 1, f);
    fclose(f);
}

static void command(char *s) {
    if(strncmp(s, "key ", 4) == 0) keys[khead++ % 4096] = atoi(s + 4);
    else if(strncmp(s, "text ", 5) == 0) for(s += 5; *s; s++) keys[khead++ % 4096] = (uint8_t)*s;
    else if(strncmp(s, "dump ", 5) == 0) {
        vtt_update();
        dump(s + 5);
        printf("dumped\n");
    } else if(strcmp(s, "check") == 0) {
        /* What drawing bit by bit left in the framebuffer, against all of it drawn afresh from libvterm's screen: a difference is a stale pixel */
        static uint8_t before[640 * 480];
        int n = 0, x0 = 640, y0 = 480, x1 = -1, y1 = -1;
        vtt_update();
        memcpy(before, fb, sizeof(fb));
        vtt_redraw();
        for(int i = 0; i < 640 * 480; i++) {
            if(before[i] != fb[i]) {
                int x = i % 640, y = i / 640;
                n++;
                if(x < x0) x0 = x;
                if(x > x1) x1 = x;
                if(y < y0) y0 = y;
                if(y > y1) y1 = y;
            }
        }
        printf("check %d pixels box %d %d %d %d\n", n, x0, y0, x1, y1);
    } else if(strcmp(s, "screen") == 0) {
        static char row[1024];
        int c, r;
        vtt_update();
        vtt_size(&c, &r);
        for(int i = 0; i < r; i++) {
            int n = vtt_row_text(i, row, sizeof(row) - 1);
            row[n] = 0;
            printf("|%s\n", row);
        }
        printf("end\n");
    } else if(strcmp(s, "stat") == 0) {
        size_t heap;
        int lines, c, r, used = 0, freeb = 0, big = 0;
        vtt_memory(&heap, &lines);
        vtt_size(&c, &r);
        for(blk *b = arena; b; b = b->next) {
            if(b->used) used += b->size;
            else {
                freeb += b->size;
                if((int)b->size > big) big = b->size;
            }
        }
        printf("stat heap %zu lines %d terminal %dx%d used %d free %d biggest %d\n", heap, lines, c, r, used, freeb, big);
    } else if(strcmp(s, "quit") == 0) {
        kill(child, SIGTERM);
        exit(0);
    }
    fflush(stdout);
}

void plat_poll(void) {
    struct pollfd p = {0, POLLIN, 0};
    while(poll(&p, 1, 0) > 0 && (p.revents & POLLIN)) {
        char c;
        if(read(0, &c, 1) != 1) exit(0);
        if(c == '\n') {
            linebuf[linelen] = 0;
            linelen = 0;
            command(linebuf);
        } else if(linelen < (int)sizeof(linebuf) - 1) linebuf[linelen++] = c;
    }
    usleep(200);
}

int plat_key(void) {
    if(ktail == khead) return -1;
    return keys[ktail++ % 4096];
}

int plat_key_mods(void) { return 0; }
int plat_mouse(int m[4]) { (void)m; return 0; }
void plat_bell(void) { printf("bell\n"); fflush(stdout); }
void plat_bell_mode(int mode) { (void)mode; }
void plat_keyboard(int layout) { (void)layout; }
const char *plat_keyboard_name(int layout) { (void)layout; return "US"; }
int plat_keyboard_count(void) { return 1; }
void plat_wiring(const uint8_t *w) { (void)w; }
void plat_wiring_default(uint8_t *w) { w[0] = 2; w[1] = 0; w[2] = 6; w[3] = 4; }
int plat_straps(void) { return 0; }
void plat_restart(void) { exit(0); }
void plat_bootsel(void) { exit(0); }

int com_open(int port, int baud, int bits, int parity, int stop) {
    (void)port; (void)baud; (void)bits; (void)parity; (void)stop;
    return 0;
}
const char *com_error(void) { return ""; }
void com_close(int port) { (void)port; }
int com_connected(int port) { (void)port; return 1; }
void com_flow(int on) { (void)on; }

int com_read(int port, uint8_t *buf, int n) {
    (void)port;
    int r = read(master, buf, n);
    if(r < 0) return 0;
    return r;
}

void com_write(int port, const uint8_t *buf, int n) {
    (void)port;
    if(write(master, buf, n) != n) perror("write");
}

/* the settings vt100.c starts with: the same struct, so its fields by position */
typedef struct {
    int port, baud, format, flow, font, cols, rows, status, charset, colour, bsdel, entercrlf, lfcrlf, echo, scrollback, cursor, layout, bell;
} hset;

int settings_load(void *p, size_t n) {
    hset *s = p;
    if(n != sizeof(hset)) {
        fprintf(stderr, "settings struct changed\n");
        exit(1);
    }
    memset(s, 0, sizeof(*s));
    s->port = PORT_UART0;
    s->baud = 115200;
    s->font = want_font >= 0 ? want_font : 3;
    s->cols = want_cols;
    s->rows = want_rows;
    s->status = 1;
    s->charset = VTT_UTF8;
    s->scrollback = 2000;
    s->cursor = 1;
    return 1;
}

int settings_save(const void *p, size_t n) {
    (void)p;
    (void)n;
    return 0;
}

int main(int argc, char **argv) {
    int i;
    for(i = 1; i < argc; i++) {
        if(strcmp(argv[i], "--") == 0) break;
        if(strcmp(argv[i], "-h") == 0) arena_size = atol(argv[++i]);
        else if(strcmp(argv[i], "-r") == 0) want_rows = atoi(argv[++i]);
        else if(strcmp(argv[i], "-c") == 0) want_cols = atoi(argv[++i]);
        else if(strcmp(argv[i], "-f") == 0) want_font = atoi(argv[++i]);
    }
    arena_init();
    struct winsize w = {want_rows ? want_rows : 24, want_cols ? want_cols : 80, 0, 0};
    child = forkpty(&master, NULL, NULL, &w);
    if(child < 0) {
        perror("forkpty");
        return 1;
    }
    if(child == 0) {
        setenv("TERM", "xterm", 1);
        execvp(argv[i + 1], argv + i + 1);
        perror("exec");
        _exit(127);
    }
    fcntl(master, F_SETFL, O_NONBLOCK);
    setvbuf(stdout, NULL, _IOLBF, 0);
    vt100_main();
    return 0;
}
