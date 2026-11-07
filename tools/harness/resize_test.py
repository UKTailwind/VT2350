"""resize_test.py: what a PicoMite does to the terminal's size, in a heap the size of the board's. A host's size requests, up and down, each followed by text at the far
corner and a check it landed there; the fonts' extremes; a flood after each. A heap that is too small fails here as a NULL in libvterm, which is what this is for.

    python -I tools/harness/resize_test.py [--heap N]
"""
import argparse
import subprocess
import time


class T:
    def __init__(self, harness, heap, *args):
        cmd = ['wsl.exe', '-e', 'env', 'ASAN_OPTIONS=detect_leaks=0', harness, '-h', str(heap)] + list(args) + ['--', 'sh', '-c', 'stty raw -echo; exec cat']
        self.p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
        time.sleep(1.2)

    def send(self, s):
        self.p.stdin.write((s + '\n').encode())
        self.p.stdin.flush()

    def line(self):
        b = bytearray()
        while True:
            c = self.p.stdout.read(1)
            if not c:
                raise SystemExit('THE HARNESS DIED:\n' + b.decode('utf-8', 'replace') + self.p.stdout.read().decode('utf-8', 'replace'))
            if c == b'\n':
                return b.decode('utf-8', 'replace')
            b += c

    def stat(self):
        self.send('stat')
        while True:
            l = self.line()
            if l.startswith('stat'):
                return l

    def screen(self):
        self.send('screen')
        rows = []
        while True:
            l = self.line()
            if l == 'end':
                return rows
            if l.startswith('|'):
                rows.append(l[1:])

    def host(self, s):
        """bytes as if from the host: the keys go to cat on the pty, which sends them back"""
        for i in range(0, len(s), 200):
            self.send('text ' + s[i:i + 200])
        time.sleep(0.25)


# a key 0x1B goes as ESC: "text" sends bytes as keys, and vtt_key passes them on, and cat returns them
ESC = '\x1b'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--heap', type=int, default=173000)
    ap.add_argument('--harness', default='/home/peter/vt2350/h/harness')
    a = ap.parse_args()
    t = T(a.harness, a.heap)
    print(t.stat())
    fails = 0
    sizes = [(40, 80), (24, 80), (37, 100), (33, 80), (40, 106), (30, 80), (60, 128), (25, 40), (59, 128), (40, 80), (33, 80), (48, 120), (34, 80), (40, 80)]
    for rows, cols in sizes:
        t.host('%s[8;%d;%dt' % (ESC, rows, cols))
        s = t.stat()
        got = s.split('terminal ')[1].split()[0]
        ok = got == '%dx%d' % (cols, rows)
        t.host('%s[%d;1HBOTTOM%s[1;%dHR%s[1;1HTOP' % (ESC, rows, ESC, cols, ESC))
        sc = t.screen()
        placed = len(sc) >= rows and sc[0].startswith('TOP') and sc[rows - 1].startswith('BOTTOM') and sc[0].rstrip().endswith('R')
        print('%3d x %3d -> %-8s %s %s  %s' % (rows, cols, got, 'size ok' if ok else 'SIZE REFUSED', 'text ok' if placed else 'TEXT WRONG', s[s.find('lines'):]))
        if not placed and ok:
            fails += 1
        for i in range(rows + 10):
            t.send('text line %d of a flood' % i)
            t.send('key 13')
            t.send('key 10')
        time.sleep(0.3)
    print(t.stat())
    t.send('quit')
    print('FAILS', fails)


if __name__ == '__main__':
    main()
