"""make_mouse_desc.py: src/mouse_desc.c and .h, the HID mouse descriptor parser, taken as it is from the PicoMite. Run once; kept to show where it came from.

    python -I tools/make_mouse_desc.py ../PicoMite
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read(p):
    return open(p, encoding='utf-8', errors='replace').read().replace('\r\n', '\n')


def main():
    hw = read(os.path.join(sys.argv[1], 'Hardware_Includes.h'))
    usb = read(os.path.join(sys.argv[1], 'input', 'USBKeyboard.c'))
    a = hw.index('// Mouse report type enumeration')
    b = hw.index('} mouse_info_t;') + len('} mouse_info_t;')
    types = hw[a:b]
    a = usb.index('#define MOUSE_MAX_USAGES 8')
    b = usb.index('/**\n * Print mouse info for debugging')
    code = usb[a:b]
    # the one the rest of the firmware needs, no longer private to the file
    code = code.replace('static int mouse_field_value(', 'int mouse_field_value(')
    open(os.path.join(ROOT, 'src', 'mouse_desc.h'), 'w', newline='\n').write(
        '/* mouse_desc.h: from the PicoMite (see mouse_desc.c). */\n#ifndef MOUSE_DESC_H\n#define MOUSE_DESC_H\n#include <stdbool.h>\n#include <stdint.h>\n\n' + types +
        '\n\nmouse_report_type_t analyze_mouse_descriptor(const uint8_t *desc_report, uint16_t desc_len, mouse_info_t *info);\n'
        'int mouse_field_value(const uint8_t *p, const mouse_field_t *f);\n#endif\n')
    open(os.path.join(ROOT, 'src', 'mouse_desc.c'), 'w', newline='\n').write(
        '/* mouse_desc.c: the mouse report descriptor parser, from the PicoMite\'s input/USBKeyboard.c by tools/make_mouse_desc.py. MIT License. */\n'
        '#include <string.h>\n#include "mouse_desc.h"\n\n' + code)


if __name__ == '__main__':
    main()
