"""make_fonts.py: src/vt_fonts.c from Spleen's BDF files (third_party/spleen, BSD-2-Clause, Frederic Cambus). From the CMM2's apps/vt100/make.py.

    python -I tools/make_fonts.py

Every size has the same characters. Spleen has no glyphs for the VT100's HT, FF, CR, LF, NL, VT and scan-line graphics, nor for not-equal, so they are
made here, as a real VT100 made them: two small letters across the cell, a line at a height.

Two sizes are not Spleen's own. 8x14 is 8x16 less its first and last rows, as no ASCII character or line uses them. 8x12 is 6x12 in a wider cell, for 640 x 480's
exact 80 x 40, which is what a PicoMiteVGA's editor asks for: letters sit in the middle of the cell, and lines and blocks run on to its edges so that they still join.
"""
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SPLEEN = os.path.join(ROOT, 'third_party', 'spleen')
OUT = os.path.join(ROOT, 'src', 'vt_fonts.c')

# vtt_fonts[] in this order, smallest first, as vt100.c's resize_request looks down it
SIZES = [(5, 8), (6, 12), (8, 12), (8, 14), (8, 16), (12, 24), (16, 32)]

# tiny letters for the control symbols, three wide and five high
TINY = {'H': ['101', '101', '111', '101', '101'], 'T': ['111', '010', '010', '010', '010'], 'F': ['111', '100', '110', '100', '100'],
        'C': ['011', '100', '100', '100', '011'], 'R': ['110', '101', '110', '101', '101'], 'L': ['100', '100', '100', '100', '111'],
        'N': ['101', '111', '111', '111', '101'], 'V': ['101', '101', '101', '101', '010']}
PAIRS = {0x2409: 'HT', 0x240A: 'LF', 0x240B: 'VT', 0x240C: 'FF', 0x240D: 'CR', 0x2424: 'NL'}
SCAN = {0x23BA: 1, 0x23BB: 3, 0x23BC: 7, 0x23BD: 9}         # the VT100's lines at scan 1, 3, 7 and 9 of its 10


def read_bdf(path, w, h):
    """{code: h rows of w-bit integers, the left pixel the top bit}"""
    text = open(path, encoding='latin-1').read()
    box = re.search(r'FONTBOUNDINGBOX (\d+) (\d+) (-?\d+) (-?\d+)', text)
    fx, fy = int(box.group(3)), int(box.group(4))
    out = {}
    for m in re.finditer(r'ENCODING (-?\d+).*?BBX (\d+) (\d+) (-?\d+) (-?\d+).*?BITMAP\n(.*?)ENDCHAR', text, re.S):
        code, bw, bh, bx, by = (int(m.group(i)) for i in range(1, 6))
        rows = [0] * h
        data = m.group(6).split()
        top = h - (by - fy) - bh                # the glyph's first row in the cell
        for i, hexrow in enumerate(data):
            bits = int(hexrow, 16) >> (len(hexrow) * 4 - bw)
            if 0 <= top + i < h:
                shift = w - bw - (bx - fx)
                rows[top + i] = (bits << shift if shift >= 0 else bits >> -shift) & ((1 << w) - 1)
        out[code] = rows
    return out


def widen(font):
    """6 wide to 8: a pixel of margin each side, which a line or a block fills from the pixel beside it"""
    out = {}
    for c, g in font.items():
        joins = 0x2500 <= c <= 0x259F or c in SCAN
        rows = []
        for r in g:
            n = r << 1
            if joins:
                n |= (r & 0x20) << 2 | (r & 1)
            rows.append(n)
        out[c] = rows
    return out


def pair_glyph(a, b, w, h):
    """two small letters across the cell, the first high on the left and the second low on the right"""
    rows = [0] * h
    s = max(1, min(w // 8, h // 16))
    for letter, x, y in ((a, 0, h // 12), (b, w - 3 * s - (1 if w >= 8 else 0), h - 5 * s - h // 8 - 1)):
        for r, line in enumerate(TINY[letter]):
            for c, bit in enumerate(line):
                if bit == '1':
                    for i in range(s):
                        for j in range(s):
                            yy, xx = y + r * s + i, x + c * s + j
                            if 0 <= yy < h and 0 <= xx < w:
                                rows[yy] |= 1 << (w - 1 - xx)
    return rows


def main():
    fonts = []
    for w, h in SIZES:
        if (w, h) == (8, 14):
            fonts.append({c: g[1:15] for c, g in read_bdf(os.path.join(SPLEEN, 'spleen-8x16.bdf'), 8, 16).items()})
        elif (w, h) == (8, 12):
            fonts.append(widen(read_bdf(os.path.join(SPLEEN, 'spleen-6x12.bdf'), 6, 12)))
        else:
            fonts.append(read_bdf(os.path.join(SPLEEN, 'spleen-%dx%d.bdf' % (w, h)), w, h))
    wanted = sorted(c for c in fonts[SIZES.index((8, 16))] if c < 0x10000 and not 0x2800 <= c <= 0x28FF and not 0xE000 <= c <= 0xF8FF)
    made = sorted(set(wanted) | set(PAIRS) | set(SCAN) | {0x2260, 0xFFFD})
    data = []
    for (w, h), font in zip(SIZES, fonts):
        rows = []
        for c in made:
            if c in PAIRS:
                g = pair_glyph(PAIRS[c][0], PAIRS[c][1], w, h)
            elif c in SCAN:
                g = [0] * h
                for t in range(max(1, h // 16)):
                    g[min(h - 1, h * SCAN[c] // 10 + t)] = (1 << w) - 1
            elif c == 0x2260:
                g = [a | b for a, b in zip(font.get(ord('='), [0] * h), font.get(ord('/'), [0] * h))]
            elif c == 0xFFFD and c not in font:
                g = [0] * h
                for r in range(h // 8, h - h // 8):
                    g[r] = (1 << (w - 1)) | 1 if h // 8 < r < h - h // 8 - 1 else (1 << w) - 1
            else:
                g = font.get(c, font.get(0xFFFD, [0] * h))
            rows.append(g)
        data.append(rows)
    src = ['/* made by tools/make_fonts.py from Spleen (BSD-2-Clause, Copyright (c) 2018-2026, Frederic Cambus) */', '#include "vt_term.h"', '']
    src.append('const uint16_t vtt_map[] = {' + ','.join(str(c) for c in made) + '};')
    src.append('const int vtt_map_count = %d;' % len(made))
    for (w, h), rows in zip(SIZES, data):
        stride = (w + 7) // 8
        b = []
        for g in rows:
            for r in g:
                r <<= stride * 8 - w            # the left pixel in the top bit of the first byte
                b += [(r >> (8 * (stride - 1 - i))) & 255 for i in range(stride)]
        src.append('static const uint8_t f%dx%d[] = {%s};' % (w, h, ','.join(str(x) for x in b)))
    src.append('const vtt_font vtt_fonts[] = {' + ', '.join('{"%dx%d", %d, %d, f%dx%d}' % (w, h, w, h, w, h) for w, h in SIZES) + '};')
    src.append('const int vtt_font_count = %d;' % len(SIZES))
    open(OUT, 'w', newline='\n').write('\n'.join(src) + '\n')
    print('fonts: %d characters in %d sizes' % (len(made), len(SIZES)))


if __name__ == '__main__':
    main()
