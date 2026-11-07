"""board.py: a test build's debug link (src/debug.h).

    python -I tools/board.py COM16 stat             the memory and the line's counters
    python -I tools/board.py COM16 text             the terminal's screen as text
    python -I tools/board.py COM16 shot out.png     the framebuffer as a picture
    python -I tools/board.py COM16 card             the test card; "term" goes back to the terminal
    python -I tools/board.py COM16 port N           the line: 0 debug, 1 UART0, 2 UART1, 4-7 USB
    python -I tools/board.py COM16 send TEXT        bytes on the debug line (with port 0), \\e and \\r as they read
    python -I tools/board.py COM16 key N            a key's code, as if typed
    python -I tools/board.py COM16 listen SECS      what the board says
"""
import sys
import time

import serial

BAUD = 921600
ESC = b'\xfe'


def palette():
    ansi = [(0, 0, 0), (205, 0, 0), (0, 205, 0), (205, 205, 0), (0, 0, 238), (205, 0, 205), (0, 205, 205), (229, 229, 229),
            (127, 127, 127), (255, 0, 0), (0, 255, 0), (255, 255, 0), (92, 92, 255), (255, 0, 255), (0, 255, 255), (255, 255, 255)]
    lev = [0, 95, 135, 175, 215, 255]
    p = list(ansi)
    p += [(lev[i // 36], lev[i // 6 % 6], lev[i % 6]) for i in range(216)]
    p += [(8 + i * 10,) * 3 for i in range(24)]
    return p


def read_until(s, marker, secs):
    got = b''
    end = time.time() + secs
    while time.time() < end and marker not in got:
        got += s.read(4096)
    return got


def main():
    port, cmd = sys.argv[1], sys.argv[2]
    args = sys.argv[3:]
    s = serial.Serial(port, BAUD, timeout=0.1)
    s.reset_input_buffer()
    if cmd == 'stat':
        s.write(ESC + b'S')
        print(read_until(s, b'peak', 2).decode('latin-1') + s.read(40).decode('latin-1'))
    elif cmd == 'text':
        s.write(ESC + b'X')
        t = read_until(s, b'VTEND', 3).decode('utf-8', 'replace')
        print(t[t.find('VTTEXT'):])
    elif cmd == 'shot':
        s.write(ESC + b'D')
        head = read_until(s, b'480\r\n', 3)
        data = head[head.find(b'480\r\n') + 5:]
        end = time.time() + 12
        while len(data) < 640 * 480 and time.time() < end:
            data += s.read(65536)
        print('%d bytes' % len(data))
        from PIL import Image
        im = Image.frombytes('P', (640, 480), data[:640 * 480].ljust(640 * 480, b'\0'))
        im.putpalette([c for rgb in palette() for c in rgb])
        im.convert('RGB').save(args[0])
    elif cmd == 'card':
        s.write(ESC + b'T')
    elif cmd == 'term':
        s.write(ESC + b'N')
    elif cmd == 'port':
        s.write(ESC + args[0].encode())
    elif cmd == 'send':
        s.write(args[0].replace('\\e', '\x1b').replace('\\r', '\r').encode('latin-1'))
    elif cmd == 'key':
        s.write(ESC + b'K' + bytes([int(args[0], 0)]))
    elif cmd == 'listen':
        end = time.time() + float(args[0])
        while time.time() < end:
            b = s.read(4096)
            if b:
                sys.stdout.write(b.decode('latin-1'))
                sys.stdout.flush()
    else:
        sys.exit(__doc__)
    s.close()


if __name__ == '__main__':
    main()
