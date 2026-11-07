#!/usr/bin/env python3
"""generate_User_Manual.py: makes docs/VT2350_User_Manual.pdf with fpdf 1.7.2.

Run it twice over itself: the first pass finds the page of each section for the contents. Core fonts only, so the text stays in Latin-1.
"""
import os
import re
from fpdf import FPDF

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'VT2350_User_Manual.pdf')

BLUE = (20, 70, 130)
HEAD = (226, 232, 240)
RULE = (185, 192, 203)
BOX = (243, 245, 248)
TEXT = (30, 30, 30)
QUIET = (110, 110, 110)
LEFT = 20
WIDTH = 170
BOTTOM = 277


class Manual(FPDF):
    def __init__(self, known):
        FPDF.__init__(self, 'P', 'mm', 'A4')
        self.known = known
        self.found = []
        self.section = 0
        self.set_margins(LEFT, 20, LEFT)
        self.set_auto_page_break(True, 20)
        self.alias_nb_pages()
        self.set_title('VT2350 User Manual')
        self.set_subject('A VT100 terminal on an RP2350')

    def footer(self):
        if self.page_no() == 1:
            return
        self.set_y(-14)
        self.set_font('Helvetica', '', 8)
        self.set_text_color(*QUIET)
        self.cell(WIDTH / 2, 5, 'VT2350 User Manual', 0, 0, 'L')
        self.cell(WIDTH / 2, 5, 'Page %d of {nb}' % self.page_no(), 0, 0, 'R')

    def room(self, need):
        if self.get_y() + need > BOTTOM:
            self.add_page()

    def h1(self, title):
        self.room(34)
        self.section += 1
        self.ln(5)
        self.found.append((title, self.page_no()))
        self.set_font('Helvetica', 'B', 15)
        self.set_text_color(*BLUE)
        self.cell(0, 8, '%d  %s' % (self.section, title), 0, 1)
        self.set_draw_color(*BLUE)
        self.set_line_width(0.4)
        self.line(LEFT, self.get_y(), LEFT + WIDTH, self.get_y())
        self.ln(3)

    def h2(self, title):
        self.room(22)
        self.ln(2.5)
        self.set_font('Helvetica', 'B', 11.5)
        self.set_text_color(*BLUE)
        self.cell(0, 6.5, title, 0, 1)
        self.ln(0.5)

    def rich(self, text, h=5.2, size=10.5):
        """**bold** and `code` in running text"""
        self.set_text_color(*TEXT)
        for part in re.split(r'(\*\*.+?\*\*|`.+?`)', text):
            if not part:
                continue
            if part.startswith('**'):
                self.set_font('Helvetica', 'B', size)
                self.write(h, part[2:-2])
            elif part.startswith('`'):
                self.set_font('Courier', '', size - 0.5)
                self.write(h, part[1:-1])
            else:
                self.set_font('Helvetica', '', size)
                self.write(h, part)

    def para(self, text):
        self.rich(text)
        self.ln(5.2)
        self.ln(2.2)

    def items(self, rows, numbered=False):
        for n, text in enumerate(rows):
            self.room(11)
            self.set_font('Helvetica', '', 10.5)
            self.set_text_color(*TEXT)
            self.set_x(LEFT + 1)
            self.cell(6, 5.2, '%d.' % (n + 1) if numbered else '-', 0, 0)
            self.set_left_margin(LEFT + 7)
            self.set_x(LEFT + 7)
            self.rich(text)
            self.set_left_margin(LEFT)
            self.ln(5.2)
            self.ln(1.2)
        self.ln(1)

    def code(self, lines):
        h = 4.8
        total = h * len(lines) + 4
        self.room(total + 3)
        y = self.get_y()
        self.set_fill_color(*BOX)
        self.set_draw_color(*RULE)
        self.set_line_width(0.2)
        self.rect(LEFT, y, WIDTH, total, 'DF')
        self.set_font('Courier', '', 9.5)
        self.set_text_color(*TEXT)
        for i, line in enumerate(lines):
            self.text(LEFT + 3, y + 2 + h * (i + 0.75), line)
        self.set_y(y + total + 3)

    def wrap(self, text, width):
        lines = []
        for para in str(text).split('\n'):
            line = ''
            for word in para.split(' '):
                trial = word if not line else line + ' ' + word
                if self.get_string_width(trial) <= width:
                    line = trial
                else:
                    if line:
                        lines.append(line)
                    line = word
            lines.append(line)
        return lines

    def table(self, widths, head, rows, mono=(), size=9.5):
        """Each row is measured first and goes on the page whole, with auto page break off: nothing can spill"""
        assert abs(sum(widths) - WIDTH) < 0.01, sum(widths)
        lh = size * 0.47
        pad = 1.3

        def font(col, bold=False):
            if bold:
                self.set_font('Helvetica', 'B', size)
            elif col in mono:
                self.set_font('Courier', '', size - 0.5)
            else:
                self.set_font('Helvetica', '', size)

        def measure(cells, bold=False):
            out = []
            for c, text in enumerate(cells):
                font(c, bold)
                out.append(self.wrap(text, widths[c] - 2 * pad - 0.5))
            return out, max(len(x) for x in out) * lh + 2 * pad

        def draw(cells, lines, h, bold=False):
            y = self.get_y()
            x = LEFT
            self.set_draw_color(*RULE)
            self.set_line_width(0.2)
            self.set_text_color(*TEXT)
            for c in range(len(cells)):
                font(c, bold)
                if bold:
                    self.set_fill_color(*HEAD)
                    self.rect(x, y, widths[c], h, 'DF')
                else:
                    self.rect(x, y, widths[c], h, 'D')
                for i, line in enumerate(lines[c]):
                    self.text(x + pad, y + pad + lh * (i + 0.78), line)
                x += widths[c]
            self.set_y(y + h)

        self.set_auto_page_break(False)
        hl, hh = measure(head, True)
        first = measure(rows[0])[1]
        if self.get_y() + hh + first > BOTTOM:
            self.add_page()
        draw(head, hl, hh, True)
        for row in rows:
            lines, h = measure(row)
            if self.get_y() + h > BOTTOM:
                self.add_page()
                draw(head, hl, hh, True)
            draw(row, lines, h)
        self.set_auto_page_break(True, 20)
        self.ln(4)


def cover(m, titles):
    m.add_page()
    m.ln(14)
    m.set_font('Helvetica', 'B', 34)
    m.set_text_color(*BLUE)
    m.cell(0, 15, 'VT2350', 0, 1)
    m.set_font('Helvetica', '', 15)
    m.set_text_color(*TEXT)
    m.cell(0, 8, 'A VT100 terminal on an RP2350', 0, 1)
    m.ln(1)
    m.set_font('Helvetica', '', 11)
    m.set_text_color(*QUIET)
    m.cell(0, 6, 'User Manual, November 2026', 0, 1)
    m.ln(12)
    m.set_font('Helvetica', 'B', 12)
    m.set_text_color(*BLUE)
    m.cell(0, 7, 'Contents', 0, 1)
    m.ln(1)
    m.set_text_color(*TEXT)
    for n, (title, page) in enumerate(titles):
        m.set_font('Helvetica', '', 10.5)
        m.cell(150, 6.2, '%d  %s' % (n + 1, title), 0, 0)
        m.cell(20, 6.2, str(page), 0, 1, 'R')


def body(m):
    m.add_page()

    m.h1('What it is')
    m.para('VT2350 turns an RP2350 board into a stand-alone serial terminal. You give it an HDMI monitor, a USB keyboard and a serial line, and it shows whatever is at the other end: a PicoMite, a Linux machine, a router\'s console or anything else that talks to a terminal. No PC is needed, and no PSRAM.')
    m.items([
        '640 x 480 in 256 colours on HDMI, with seven font sizes from 128 x 60 characters down to 40 x 15.',
        'Two UARTs, or up to four USB serial devices, as the line.',
        'A USB keyboard in six layouts, and a USB mouse.',
        'VT100 and VT52, with xterm\'s 256 colours, UTF-8, CP437 and Latin-1, double-width and double-height lines, an alternate screen, mouse reporting and scrollback.',
        'One firmware for boards with different HDMI wiring.',
    ])

    m.h1('What you need')
    m.items([
        'An RP2350 board with HDMI on GP12 to GP19, such as a Pico 2 with an HSTX DVI sock.',
        'An HDMI monitor or television.',
        'A USB keyboard. A mouse is optional.',
        'A USB hub, if you want more than one USB device: a keyboard with a mouse or a USB serial device needs one.',
        'Something to talk to, on a UART or a USB serial device.',
    ])
    m.h2('The connections')
    m.table([34, 136], ['Pin', 'What it is for'], [
        ['GP0', 'UART0 transmit: goes to the other end\'s receive'],
        ['GP1', 'UART0 receive: comes from the other end\'s transmit'],
        ['GP2', 'The bell, a tone for a piezo sounder or an amplifier (see section 11)'],
        ['GP4', 'UART1 transmit'],
        ['GP5', 'UART1 receive'],
        ['GP6, GP7', 'Optional straps that choose the HDMI wiring (see section 10)'],
        ['GP12 to GP19', 'HDMI'],
        ['USB port', 'USB host, for the keyboard, mouse and USB serial devices'],
    ])
    m.para('**The UARTs are 3.3 V logic.** Join the two grounds, and cross the lines over: transmit on one end goes to receive on the other. A real RS-232 port needs a level converter.')
    m.para('**The USB port has to supply 5 V.** It is working as a host, so the power for the keyboard and hub comes from your board. On a Pico 2 that means an OTG adapter on the USB socket, and 5 V fed to the VBUS pin (pin 40). A powered hub takes the load off the board.')

    m.h1('Installing it')
    m.items([
        'Hold down the board\'s BOOTSEL button and connect its USB port to a PC. A drive called RP2350 appears.',
        'Copy `VT2350.uf2` to that drive. The board restarts by itself and the drive goes away.',
        'Unplug the PC, and connect the keyboard or hub, the monitor and the serial line.',
    ], numbered=True)
    m.h2('Updating it')
    m.para('Choose **Firmware update** in the setup menu, or hold BOOTSEL while powering up. Then connect the board\'s USB port to a PC instead of the hub, and copy the new UF2 to the RP2350 drive. Saved settings stay, unless the new version saves different things, when it starts from the defaults.')

    m.h1('First use')
    m.para('Power it up. The screen clears, and the last line is the status line:')
    m.code([' UART0 115200 8N1  |  80x33  |  Shift-F12 setup'])
    m.para('That is the line in use, its speed and format, and the size of the terminal. As it comes, the terminal uses UART0 at 115200, 8 bits, no parity, one stop bit.')
    m.para('If the other end is already running, press Enter and you should see it answer. If it needs a different speed or port, press **Shift-F12** to open the setup menu.')
    m.para('If there is no picture, or the colours are wrong, your board\'s HDMI wiring differs from the default: go to section 10.')

    m.h1('The setup menu')
    m.para('**Shift-F12** or **Pause** opens the menu.')
    m.table([46, 124], ['Key', 'In the menu'], [
        ['Up, Down', 'Move to another line'],
        ['Left, Right', 'Change the value'],
        ['Enter', 'Change the value, or do what the line says'],
        ['Esc, Shift-F12, Pause', 'Leave the menu'],
    ])
    m.para('Changes take effect when you leave the menu, and last until the power goes off. To keep them, choose **Save settings**. The HDMI wiring is the exception: it takes effect at once, and has its own way of being kept (section 10).')
    m.table([33, 100, 37], ['Line', 'Choices', 'As it comes'], [
        ['Port', 'UART0 (GP0, GP1), UART1 (GP4, GP5), USB1 to USB4', 'UART0'],
        ['Speed', '300, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600', '115200'],
        ['Format', '8N1, 8E1, 8O1, 7E1, 7O1, 8N2', '8N1'],
        ['Flow control', 'None, XON/XOFF (section 9)', 'None'],
        ['Font', '5x8, 6x12, 8x12, 8x14, 8x16, 12x24, 16x32', '8x14'],
        ['Columns', 'All, 40, 80, 100, 120', 'All'],
        ['Rows', 'All, 24, 25, 30, 36, 40, 43, 48, 50, 60', 'All'],
        ['Status line', 'On, Off', 'On'],
        ['Characters', 'CP437, Latin-1, UTF-8: how the terminal reads bytes above 127', 'CP437'],
        ['Text colour', 'White, Green, Amber', 'White'],
        ['Cursor', 'Block, Underline, Bar', 'Underline'],
        ['Keyboard', 'US, UK, German, French, Spanish, Belgian', 'US'],
        ['Backspace sends', 'BS (8), DEL (127)', 'BS (8)'],
        ['Enter sends', 'CR, CR LF', 'CR'],
        ['LF received as', 'LF, CR LF', 'LF'],
        ['Local echo', 'Off, On: On shows what you type, for a host that does not', 'Off'],
        ['Bell', 'Tone, Flash, Off', 'Tone'],
        ['HDMI wiring', 'Section 10', '2,0,6,4'],
    ])
    m.table([42, 128], ['Line', 'What it does'], [
        ['Reset the terminal', 'Clears the screen and the scrollback and resets the terminal, then leaves the menu'],
        ['Save settings', 'Keeps everything in the menu through a power cycle'],
        ['Restart', 'Restarts the board'],
        ['Firmware update', 'Goes to the bootloader, for a new UF2'],
    ])
    m.h2('Fonts and screen sizes')
    m.para('The font sets how much fits on the screen. The status line takes one row. Columns and Rows make the terminal smaller than that, if a program needs a set size: asking for more than the font allows gives you the most it can.')
    m.table([34, 68, 68], ['Font', 'With the status line', 'Without it'], [
        ['5x8', '128 x 59', '128 x 60'],
        ['6x12', '106 x 39', '106 x 40'],
        ['8x12', '80 x 39', '80 x 40'],
        ['8x14', '80 x 33', '80 x 34'],
        ['8x16', '80 x 29', '80 x 30'],
        ['12x24', '53 x 19', '53 x 20'],
        ['16x32', '40 x 14', '40 x 15'],
    ])

    m.h1('The keyboard')
    m.para('Almost every key goes to the host, including all twelve function keys and Ctrl-C. Only Shift-F12, Pause and the scrollback keys stay with the terminal.')
    m.table([52, 118], ['Key', 'What it does'], [
        ['Shift-F12, Pause', 'Opens the setup menu'],
        ['Shift-Page Up, Page Down', 'Moves back and forward through the scrollback'],
        ['Shift-Home, Shift-End', 'Goes to the oldest line kept, and back to the live screen'],
        ['F1 to F12', 'The usual function key codes'],
        ['Shift-F3 to Shift-F10', 'VT220\'s F13 to F20, which a PicoMite reads as its own Shift-F keys'],
        ['Home, End', '`ESC [ 1 ~` and `ESC [ 4 ~`, which are what a PicoMite expects'.replace('`', '')],
        ['Alt with a key', 'ESC, then the key'],
        ['Backspace', 'As the Backspace sends line in the menu'],
        ['Enter', 'As the Enter sends line in the menu'],
    ])
    m.para('The arrows, Insert, Delete, Page Up, Page Down and Shift-Tab send the usual codes. In VT52 mode the arrows are `ESC A` to `ESC D`, and F1 to F4 are PF1 to PF4.')

    m.h1('Scrollback and the mouse')
    m.para('Lines that scroll off the top are kept for as long as there is memory for them. How many depends on the size of the terminal, as a wider screen uses more for each line: about 160 lines at 80 x 33, and about 48 at 128 x 59.')
    m.para('Use Shift with Page Up, Page Down, Home and End, or the mouse wheel, which moves three lines at a click. Any other key takes you back to the live screen.')
    m.para('**That is all the mouse does until a program asks for it.** When a host program turns on mouse reporting, a pointer appears, and movement, the buttons and the wheel go to the program. The wheel stops scrolling the terminal until the program turns reporting off.')

    m.h1('Using it with a host')
    m.h2('With a PicoMite')
    m.para('Join the terminal to the PicoMite\'s serial console, or plug a PicoMite into the hub as a USB serial device and choose USB1. The defaults suit it: CP437 characters, and 115200 on a UART. The keys are sent the way the PicoMite expects them, for EDIT and FM.')
    m.para('The PicoMite tells the terminal what size it wants at each EDIT and FM, and at OPTION DISPLAY. If that does not fit, the terminal moves to a smaller font until it does, and gives up the status line if the host needs every row.')
    m.h2('With anything else')
    m.items([
        'For Linux and other Unix hosts, set **Characters** to UTF-8. `TERM=vt100` is the safe choice, and `xterm-256color` is worth trying for colour.',
        'If each line starts where the last one ended, set **LF received as** to CR LF.',
        'If the host needs a line feed after Enter, set **Enter sends** to CR LF.',
        'If Backspace does the wrong thing, change **Backspace sends**.',
    ])
    m.para('There is not room for 132 columns. A program that asks for them gets as many as the font allows.')

    m.h1('The serial lines')
    m.h2('USB serial devices')
    m.para('Up to four can be connected, as USB1 to USB4 in the order they are found. Devices that follow the USB CDC standard work, including PicoMites, as do those with FTDI, CP210x, CH34x and PL2303 chips.')
    m.para('If the device you choose is not there, the status line says `no USB serial device there`, and the terminal keeps looking for it. If it is unplugged, the status line says so, and the port opens again when it comes back.')
    m.h2('How fast it keeps up')
    m.para('What takes the time is lines, not bytes, as each new line scrolls the screen. Short lines sent without a pause are the hardest thing a host can do. With 64-byte lines:')
    m.table([46, 40, 84], ['Font', '460800', '921600'], [
        ['8x12, 8x14, 8x16', 'Nothing lost', 'Nothing lost'],
        ['6x12', 'Nothing lost', 'Loses data: it keeps up with about 73 KB/s'],
        ['5x8', 'Nothing lost', 'Loses data: it keeps up with about 65 KB/s'],
    ])
    m.para('The two largest fonts have not been measured. The terminal holds 16 KB of incoming data, so short bursts are safe in any case, and longer lines are easier. A USB serial device has been measured at 46 to 81 KB/s with nothing lost.')
    m.para('**To be sure of losing nothing, set Flow control to XON/XOFF.** The terminal then asks the host to pause when its buffer fills. In the hardest case, 921600 in the 5x8 font, this lost nothing, at about 67 KB/s. The host has to support XON/XOFF too.')

    m.h1('HDMI wiring')
    m.para('Boards put the four HDMI pairs on GP12 to GP19 in different orders. The wiring is four numbers, the same as the PicoMite\'s `OPTION HDMI PINS`, for the clock, D0, D1 and D2 pairs. Each number is 0 for GP12 up to 7 for GP19, and gives the pair\'s positive pin. There are 384 possible wirings, and any of them can be used.')
    m.para('The terminal takes the wiring from the first of these that gives one: the straps, then the saved wiring, then the default, 2,0,6,4.')
    m.h2('Straps')
    m.para('Tie GP6 and GP7 to ground or 3.3 V, or leave them open. They are read each time the board starts.')
    m.table([24, 24, 26, 96], ['GP6', 'GP7', 'Wiring', 'Board'], [
        ['Ground', 'Ground', '2,0,6,4', 'The HSTX sock, and the PicoMite\'s default'],
        ['Ground', 'Open', '1,3,5,7', 'Pico Computer 3'],
        ['Open', 'Ground', '1,3,7,5', 'Olimex'],
        ['3.3 V', 'Open', '3,1,7,5', '2,0,6,4 with every pair the other way round'],
        ['Open', '3.3 V', '0,2,4,6', '1,3,5,7 with every pair the other way round'],
        ['3.3 V', '3.3 V', '0,2,6,4', '1,3,7,5 with every pair the other way round'],
    ])
    m.para('Open on both, or ground on one and 3.3 V on the other, chooses nothing. A wiring set by the straps can\'t be changed in the menu.')
    m.h2('With no picture')
    m.para('The keyboard is enough. You can do this without being able to see anything:')
    m.items([
        'Press **Shift-F12** to open the menu.',
        'Press **W**. The next wiring in the list above takes effect at once.',
        'Keep pressing **W** until the picture is right.',
        'Press **Enter** to keep that wiring. It is saved straight away. Press any key to clear the message.',
    ], numbered=True)
    m.para('A new wiring goes back by itself after 10 seconds unless you keep it, and Esc takes it back at once, so a wrong one can\'t lose the picture for good.')
    m.para('**A picture is not enough to go on.** The wrong wiring can give one. Look at the message: it shows the words Red, Green and Blue in those colours, on a light grey box.')
    m.table([70, 100], ['What you see', 'What is wrong'], [
        ['No picture', 'The clock or D0 is on the wrong pair'],
        ['Red and green have changed places', 'D1 and D2 are swapped'],
        ['Colours inverted, like a negative', 'The pairs are the wrong way round. Other monitors may show no picture for this'],
    ])
    m.h2('A wiring that is not in the list')
    m.para('With a picture, go to the **HDMI wiring** line in the menu. Left and Right go through the list. Enter asks for four numbers, each 0 to 7, and each of the four needs a pair of its own. Then Enter keeps the wiring, as above.')

    m.h1('The bell')
    m.para('**Tone** gives a 790 Hz beep lasting 150 ms on GP2. It is a logic-level square wave: use a piezo sounder, or an amplifier for a speaker. **Flash** inverts the screen\'s colours for a moment instead. **Off** ignores the bell.')

    m.h1('If something is wrong')
    m.table([64, 106], ['What happens', 'What to try'], [
        ['No picture, or the wrong colours', 'The HDMI wiring: section 10'],
        ['The keyboard does nothing', 'Check the USB port is getting 5 V, and try a powered hub. Give the board about 12 seconds after it starts to find what is on a hub'],
        ['Nothing appears from the host', 'Check the status line shows the right port and speed. On a UART, check transmit and receive are crossed over and the grounds are joined'],
        ['Rubbish on the screen', 'The Speed or Format differs from the host\'s'],
        ['Every line starts where the last one ended', 'Set LF received as to CR LF'],
        ['Lines are double spaced', 'Set LF received as to LF'],
        ['Boxes and lines show the wrong characters', 'Set Characters to what the host sends: CP437 for a PicoMite, UTF-8 for Linux'],
        ['You see each character twice', 'Set Local echo to Off'],
        ['You can\'t see what you type', 'The host is not echoing: set Local echo to On'],
        ['Text goes missing in a fast listing', 'Set Flow control to XON/XOFF, or use a larger font'],
        ['The font changed by itself', 'The host asked for a size that needs it: section 8'],
        ['The mouse wheel stopped scrolling', 'A program has asked for the mouse. It stops when the program turns reporting off'],
        ['Settings are lost at power-off', 'Choose Save settings in the menu'],
        ['USB1 shows no USB serial device', 'Check it is plugged in. If several are, it may be USB2 to USB4'],
    ])

    m.h1('Licence and credits')
    m.para('VT2350 is free software under the MIT License. The source is at github.com/UKTailwind/VT2350.')
    m.para('It uses libvterm for the terminal, the Spleen fonts, TinyUSB and the Raspberry Pi Pico SDK. Its HDMI, keyboard and mouse code comes from the PicoMite, and the terminal from the Colour Maximite 2\'s VT100 application. The file `PROVENANCE.md` in the source gives the details and the licences.')


def build(pages):
    m = Manual(dict(pages))
    cover(m, pages or [('', 0)] * 13)
    body(m)
    return m


first = build([])
final = build(first.found)
assert final.found == first.found, 'the contents moved the pages'
final.output(OUT, 'F')
print('%s: %d pages, %d sections' % (OUT, final.page_no(), len(final.found)))
