/* glyph_test.c: draw_glyph's four-pixels-a-store row against the plain loop it replaced, for every width of font and every row a glyph can have, at every alignment.
   gcc -O2 glyph_test.c -o glyph_test && ./glyph_test */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t nib[16];

static void make(int fg, int bg) {
    for(int i = 0; i < 16; i++) nib[i] = (i & 8 ? fg : bg) | (i & 4 ? fg : bg) << 8 | (i & 2 ? fg : bg) << 16 | (uint32_t)(i & 1 ? fg : bg) << 24;
}

static void plain(uint8_t *p, uint32_t bits, int cw, int fg, int bg) {
    for(int i = 0; i < cw; i++, bits <<= 1) p[i] = bits & 0x8000 ? fg : bg;
}

static void fast(uint8_t *p, uint32_t bits, int cw, int fg, int bg) {
    int i;
    for(i = cw; i >= 4; i -= 4, p += 4, bits <<= 4) memcpy(p, &nib[bits >> 12 & 15], 4);
    for(; i > 0; i--, bits <<= 1) *p++ = bits & 0x8000 ? fg : bg;
}

int main(void) {
    static const int widths[] = {5, 6, 8, 12, 16};
    static const int colours[][2] = {{7, 0}, {0, 15}, {255, 1}, {16, 231}};
    long tests = 0;
    for(unsigned c = 0; c < 4; c++) {
        make(colours[c][0], colours[c][1]);
        for(unsigned w = 0; w < 5; w++) {
            int cw = widths[w];
            for(uint32_t bits = 0; bits < 65536; bits++) {
                for(int off = 0; off < 4; off++) {
                    uint8_t a[32], b[32];
                    memset(a, 0xAA, sizeof(a));
                    memset(b, 0xAA, sizeof(b));
                    plain(a + 4 + off, bits, cw, colours[c][0], colours[c][1]);
                    fast(b + 4 + off, bits, cw, colours[c][0], colours[c][1]);
                    if(memcmp(a, b, sizeof(a))) {
                        printf("differ: width %d bits %04x offset %d\n", cw, bits, off);
                        return 1;
                    }
                    tests++;
                }
            }
        }
    }
    printf("ok: %ld rows the same, and nothing written outside the cell\n", tests);
    return 0;
}
