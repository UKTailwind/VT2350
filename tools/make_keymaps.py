"""make_keymaps.py: src/keymaps.c, the keyboard layouts, taken as they are from the PicoMite's input/KeyboardMap.c. Run once; kept to show where they came from.

    python -I tools/make_keymaps.py ../PicoMite/input/KeyboardMap.c
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    text = open(sys.argv[1], encoding='utf-8', errors='replace').read().replace('\r\n', '\n')
    out = ['/* keymaps.c: the layouts, a pair of codes (plain, shifted) for each HID usage, from the PicoMite\'s input/KeyboardMap.c by tools/make_keymaps.py.',
           '   Layouts by Fabrice Muller, Alberto Leibovich and the other contributors to MMBasic. MIT License. */', '#include "keyboard.h"', '']
    for name in ('USkeyValue', 'UKkeyValue', 'DEkeyValue', 'FRkeyValue', 'ESkeyValue', 'BEkeyValue'):
        m = re.search(r'const int %s\[202\] = \{.*?\n\};' % name, text, re.S)
        out.append(m.group(0))
        out.append('')
    open(os.path.join(ROOT, 'src', 'keymaps.c'), 'w', newline='\n').write('\n'.join(out))


if __name__ == '__main__':
    main()
