"""vttest_run.py: walks vttest's menus through the terminal in the harness, and keeps every screen as text and as a picture.

    python -I tools/harness/vttest_run.py OUTDIR [--heap N] [--harness PATH] [--menus 1,2,3,4,6,7,8,9,10]

Run twice, once with the build under test and once with a reference, and compare the two OUTDIRs with --compare A B. It needs WSL, with the harness built (Makefile) and vttest.
"""
import argparse
import hashlib
import os
import re
import subprocess
import sys
import time

VTTEST = '/home/peter/vt2350/vtt/x/usr/bin/vttest'
PROMPT = re.compile(r'Enter choice number \(0 - (\d+)\)')


def wsl_path(p):
    p = os.path.abspath(p).replace('\\', '/')
    return '/mnt/' + p[0].lower() + p[2:]


class Term:
    def __init__(self, harness, heap, outdir, rows=24, cols=80):
        self.out = outdir
        self.n = 0
        self.stale = 0
        self.log = open(os.path.join(outdir, 'walk.txt'), 'w', encoding='utf-8', newline='\n')
        os.makedirs(outdir, exist_ok=True)
        env = dict(os.environ, MSYS_NO_PATHCONV='1')
        self.p = subprocess.Popen(['wsl.exe', '-e', 'env', 'ASAN_OPTIONS=detect_leaks=0', harness, '-h', str(heap), '-r', str(rows), '-c', str(cols), '--', VTTEST],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0, env=env)

    def send(self, s):
        self.p.stdin.write((s + '\n').encode())
        self.p.stdin.flush()

    def line(self):
        b = bytearray()
        while True:
            c = self.p.stdout.read(1)
            if not c:
                raise SystemExit('the harness stopped:\n' + b.decode('utf-8', 'replace'))
            if c == b'\n':
                return b.decode('utf-8', 'replace')
            b += c

    def screen(self):
        self.send('screen')
        rows = []
        while True:
            l = self.line()
            if l == 'end':
                return rows
            if l.startswith('|'):
                rows.append(l[1:].rstrip())

    def settle(self):
        """until the screen has not changed for a while: vttest can wait a second for a reply"""
        last, same = None, 0
        for _ in range(80):
            time.sleep(0.15)
            s = self.screen()
            same = same + 1 if s == last else 0
            if same >= 9:
                return s
            last = s
        return last

    def keep(self, label):
        s = self.settle()
        self.n += 1
        name = '%03d' % self.n
        self.send('check')
        l = self.line()
        while not l.startswith('check'):
            l = self.line()
        if not l.startswith('check 0 '):
            self.stale += 1
            print('STALE PIXELS at %s %s: %s' % (name, label, l), flush=True)
        open(os.path.join(self.out, name + '.txt'), 'w', encoding='utf-8', newline='\n').write('\n'.join(s) + '\n')
        self.send('dump ' + wsl_path(os.path.join(self.out, name + '.ppm')))
        while self.line() != 'dumped':
            pass
        self.log.write('%s %s\n' % (name, label))
        self.log.flush()
        return s

    def key(self, n):
        self.send('key %d' % n)

    def text(self, t):
        self.send('text ' + t)


def prompt_of(s):
    for row in s:
        m = PROMPT.search(row)
        if m:
            return int(m.group(1))
    return None


def walk(t, path, depth):
    """from a menu's prompt: each choice, to the prompt again"""
    s = t.keep(path + ' menu')
    n = prompt_of(s)
    title = s[0] if s else ''
    if n is None:
        return
    for i in range(1, n + 1):
        if depth > 0 and any(w in ' '.join(s) for w in ('Modify', 'Exit')) and False:
            continue
        run(t, path + '.%d' % i, i, depth + 1)
    t.text('0')
    t.key(13)


def run(t, path, choice, depth):
    t.text(str(choice))
    t.key(13)
    for step in range(60):
        s = t.keep('%s step %d' % (path, step))
        n = prompt_of(s)
        if n is not None:
            if depth < 2 and step == 0:         # a sub-menu
                for i in range(1, n + 1):
                    run(t, path + '.%d' % i, i, depth + 1)
                t.text('0')
                t.key(13)
                t.settle()
            return
        t.key(13)


def compare(a, b):
    """screen by screen by what the walk calls them. A difference the size of a cell is the cursor, caught in the other half of its blink"""
    from PIL import Image, ImageChops
    wa = {l.split(' ', 1)[1]: l.split(' ', 1)[0] for l in open(os.path.join(a, 'walk.txt'), encoding='utf-8').read().splitlines()}
    wb = {l.split(' ', 1)[1]: l.split(' ', 1)[0] for l in open(os.path.join(b, 'walk.txt'), encoding='utf-8').read().splitlines()}
    both = [k for k in wa if k in wb]
    print('%d screens and %d, %d in both' % (len(wa), len(wb), len(both)))
    for k in wa:
        if k not in wb:
            print('only in the first:', k)
    for k in wb:
        if k not in wa:
            print('only in the second:', k)
    same = cursor = 0
    bad = []
    for k in both:
        fa, fb = os.path.join(a, wa[k]), os.path.join(b, wb[k])
        if open(fa + '.txt', 'rb').read() != open(fb + '.txt', 'rb').read():
            bad.append((k, 'text'))
            continue
        d = ImageChops.difference(Image.open(fa + '.ppm').convert('RGB'), Image.open(fb + '.ppm').convert('RGB')).convert('L').point(lambda v: 255 if v else 0)
        box = d.getbbox()
        if box is None:
            same += 1
        elif box[2] - box[0] <= 16 and box[3] - box[1] <= 32:
            cursor += 1
        else:
            bad.append((k, 'pixels %s' % (box,)))
    print('%d identical, %d differ by the cursor, %d differ' % (same, cursor, len(bad)))
    for k, why in bad:
        print('  ', k, why)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out', nargs='?')
    ap.add_argument('--heap', type=int, default=173000)
    ap.add_argument('--harness', default='/home/peter/vt2350/h/harness')
    ap.add_argument('--menus', default='1,2,3,4,6,7,8,9,10')
    ap.add_argument('--compare', nargs=2)
    a = ap.parse_args()
    if a.compare:
        compare(*a.compare)
        return
    os.makedirs(a.out, exist_ok=True)
    t = Term(a.harness, a.heap, a.out)
    time.sleep(1.0)
    s = t.keep('start')
    if prompt_of(s) is None:
        print('\n'.join(s))
        sys.exit('no menu')
    for m in a.menus.split(','):
        run(t, m, int(m), 1)
    t.send('stat')
    print(t.line())
    t.send('quit')
    print('%d screens in %s, %d with stale pixels' % (t.n, a.out, t.stale))


if __name__ == '__main__':
    main()
