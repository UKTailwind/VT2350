"""flash.py: put a UF2 on a board over its serial port, with no one at the board.

    python -I tools/flash.py COM16 build/VT2350.uf2 [--from picomite|vt2350]

--from says what the board runs now: picomite takes UPDATE FIRMWARE at the prompt, vt2350 takes 0xFE B on its debug link. On a Pico Computer 3 the boot
drive only reaches the PC with the USB switch in its programming position: no drive within the wait means that, so this stops and says so. It then
listens for a VT2350 board's first words, unless the image is a PicoMite's.
"""
import argparse
import shutil
import struct
import subprocess
import sys
import time

import serial

DEBUG_BAUD = 921600


def drives():
    cmd = "Get-Volume | Where-Object FileSystemLabel -eq 'RP2350' | ForEach-Object { $_.DriveLetter }"
    out = subprocess.run(['powershell', '-NoProfile', '-Command', cmd], capture_output=True, text=True).stdout
    return [d.strip() for d in out.split() if d.strip()]


def link(port, path):
    image = open(path, 'rb').read()
    total = 2166136261
    for b in image:
        total = ((total ^ b) * 16777619) & 0xFFFFFFFF
    s = serial.Serial(port, DEBUG_BAUD, timeout=0.2)
    s.reset_input_buffer()
    s.write(b'\xfeU')
    got = b''
    end = time.time() + 3
    while time.time() < end and b'VTUPLOAD' not in got:
        got += s.read(200)
    if b'VTUPLOAD' not in got:
        sys.exit('no answer from a running VT2350 on %s' % port)
    s.write(struct.pack('<II', len(image), total))
    time.sleep(0.05)
    t0 = time.time()
    for i in range(0, len(image), 4096):
        s.write(image[i:i + 4096])
    got = b''
    end = time.time() + 6
    while time.time() < end and b'VTGOOD' not in got and b'VTBAD' not in got:
        got += s.read(200)
    if b'VTGOOD' not in got:
        sys.exit('the image did not arrive whole: %r. The flash is untouched and the board restarts.' % got[-60:])
    print('%d bytes in %.1f s; the board is programming itself' % (len(image), time.time() - t0))
    got = b''
    end = time.time() + 20
    while time.time() < end and b'VT2350 on' not in got:
        got += s.read(200)
    time.sleep(0.2)
    got += s.read(400)
    s.close()
    print(got.decode('latin-1')[-120:].strip() if b'VT2350 on' in got else 'nothing heard after programming: check the board')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('port')
    ap.add_argument('uf2')
    ap.add_argument('--from', dest='source', choices=['picomite', 'vt2350'], default='vt2350')
    ap.add_argument('--wait', type=int, default=12)
    ap.add_argument('--listen', type=int, default=4)
    ap.add_argument('--link', action='store_true', help='a .bin over the debug link, into a running VT2350: no boot drive, so no USB switch')
    a = ap.parse_args()

    if a.link:
        link(a.port, a.uf2)
        return
    if drives():
        sys.exit('A boot drive is already there: %s. Which board is that? Stopping.' % drives())

    if a.source == 'picomite':
        s = serial.Serial(a.port, 115200, timeout=0.2)
        s.write(b'\r')
        time.sleep(0.4)
        if not s.read(2000).rstrip().endswith(b'>'):
            sys.exit('%s is not at a prompt: leaving it alone.' % a.port)
        s.write(b'UPDATE FIRMWARE\r')
        s.close()
    else:
        s = serial.Serial(a.port, DEBUG_BAUD, timeout=0.2)
        s.write(b'\xfeB')
        s.close()

    end = time.time() + a.wait
    while time.time() < end:
        d = drives()
        if d:
            break
        time.sleep(0.5)
    else:
        sys.exit('No boot drive in %d s: the board is waiting in its bootloader. On a PC3 the USB switch has to be in the programming position.' % a.wait)

    time.sleep(0.5)
    shutil.copy(a.uf2, d[0] + ':/')
    print('copied %s to %s:' % (a.uf2, d[0]))
    if a.listen:
        time.sleep(0.5)
        s = serial.Serial(a.port, DEBUG_BAUD, timeout=0.2)
        end = time.time() + a.listen
        got = b''
        while time.time() < end:
            got += s.read(4096)
        s.close()
        print(got.decode('latin-1')[-600:] if got else 'nothing heard on %s at %d' % (a.port, DEBUG_BAUD))


if __name__ == '__main__':
    main()
