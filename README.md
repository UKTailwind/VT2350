# VT2350

A VT100 terminal on an RP2350, with no PSRAM needed: 640x480 in 256 colours on HDMI, a USB keyboard and mouse, and a choice of two UARTs or up to four USB serial devices as the line.

libvterm does the terminal's work, so it is more than a VT100: xterm's 256 colours, UTF-8, CP437 and Latin-1, VT52 mode, double-width and double-height lines, mouse reporting, an alternate screen and scrollback.

The [user manual](docs/VT2350_User_Manual.pdf) is a PDF, for downloading and printing. It covers wiring one up, the setup menu, the keys and what to do when something is wrong.

## Hardware

| | |
|---|---|
| Display | HDMI (DVI) from the HSTX, GP12-GP19, in any order: see [HDMI wiring](#hdmi-wiring) |
| UART0 | TX GP0, RX GP1 |
| UART1 | TX GP4, RX GP5 |
| Bell | PWM tone on GP2 |
| USB | The RP2350's port as a host, supplying 5 V. A keyboard, a mouse and a USB serial device need a hub |

## Using it

Shift-F12 or Pause opens the setup menu. All twelve function keys and Ctrl-C go to the host. Shift with Page Up, Page Down, Home and End moves through the scrollback, as does the mouse wheel unless the host has asked for the mouse.

A PicoMite's size request (`CSI 8;rows;cols t`, sent at each EDIT and FM and at OPTION DISPLAY) is met by choosing a smaller font. The status line gives way if the host needs every row. There is not room for 132 columns: a host's DECCOLM gets the most the width allows.

## HDMI wiring

Boards put the HDMI pairs on GP12-GP19 in different orders. The wiring is four numbers, as the PicoMite's `OPTION HDMI PINS`: the HSTX bit (0 for GP12 to 7 for GP19) of the first pin of the clock, D0, D1 and D2 pairs. Odd numbers put the positive pin on the odd GPIO. Any of the 384 ways of doing it can be used.

The wiring is chosen at power-on from the first of these that gives one:

1. **Straps on GP6 and GP7.** Each is tied to ground, tied to 3V3 or left open:

   | GP6 | GP7 | Wiring | |
   |---|---|---|---|
   | Ground | Ground | 2,0,6,4 | The HSTX sock, the PicoMite's default |
   | Ground | Open | 1,3,5,7 | Pico Computer 3 |
   | Open | Ground | 1,3,7,5 | Olimex |
   | 3V3 | Open | 3,1,7,5 | As 2,0,6,4 with every pair the other way round |
   | Open | 3V3 | 0,2,4,6 | As 1,3,5,7 with every pair the other way round |
   | 3V3 | 3V3 | 0,2,6,4 | As 1,3,7,5 with every pair the other way round |

   Open on both, or ground on one and 3V3 on the other, chooses nothing. A wiring from the straps can't be changed in the menu.

2. **The saved wiring.**
3. **The build's own:** 2,0,6,4, or 1,3,5,7 for the Pico Computer 3 test build.

With a picture, the setup menu's HDMI wiring line shows the wiring. Enter takes any four numbers, and Left and Right go through the list above.

With no picture, or the wrong colours, press Shift-F12 to open the menu, which you can't see, and then W. Each W moves to the next wiring in the list at once. When the picture is right, press Enter to keep it and save it, or Esc to go back. A new wiring goes back by itself after 10 seconds unless it is kept.

A picture is not enough to go on, as the wrong wiring can give one. With the data pairs the wrong way round a monitor may still show the picture, with its colours inverted, and with D1 and D2 swapped red and green change places. Keep pressing W until the colours are right: the message shows the words Red, Green and Blue in those colours, on a light grey box.

## How fast it keeps up

What costs the time is lines, not bytes: each one scrolls. Measured on a Pico Computer 3 at 252 MHz with 64-byte lines, the worst case, sent without a pause:

| Font | Terminal | 460800 | 921600 |
|---|---|---|---|
| 8x12, 8x14, 8x16 | 80 columns | no loss | no loss |
| 6x12 | 106 x 39 | no loss | loses data: keeps up with about 73 KB/s |
| 5x8 | 128 x 59 | no loss | loses data: keeps up with about 65 KB/s |

Longer lines are easier. Where a host can send that fast for more than the 16 KB receive ring lasts, set Flow control to XON/XOFF in the menu. A USB serial device has been measured at 46 to 81 KB/s with no loss. The Microbridge (the USB serial chip on a Colour Maximite 2 Generation 1) works at up to 115200 baud, without DTR or RTS, and the MCP2221A only at up to 57600: it has no flow control.

## Building

It needs the Pico SDK (`PICO_SDK_PATH`) and the patched TinyUSB tree beside this one, which `tinyusb-patches/setup-tinyusb.sh` makes as `../tinyusb-vt2350`.

```
cmake -G Ninja -DVT_BOARD=hstx_sock -DCMAKE_BUILD_TYPE=Release -S . -B build
cmake --build build
```

`VT_BOARD` is a header in `boards/`: `hstx_sock`, a Pico 2 with the HSTX sock, or `pc3`, the Pico Computer 3 used for testing. A new board is a new header and a line in `CMakeLists.txt`.

`-DVT_TESTCARD=ON` builds the picture alone, for bringing up a board.

## Testing

A test build has a debug link if its board header gives it pins: `tools/board.py` reads the terminal's screen and memory over it, and `tools/flash.py` sends the board to its bootloader through it. A hard fault goes to the bootloader too.

`tools/harness` runs the real `vt100.c`, `vt_term.c` and libvterm on a PC under vttest, in a heap the size of the board's. `tools/harness/vttest_run.py` walks vttest's menus and compares two builds screen by screen.

## Licence

MIT. See `PROVENANCE.md` for where the code came from, and the licences of what is in `third_party`.
