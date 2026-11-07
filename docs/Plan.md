# RP2350 standalone VT100 terminal: plan

7 November 2026. For Peter's review: nothing has been built for this project and no board has been touched.

Read from: CMM2V1.5 `apps/vt100` at f315567, PicoMite main at 493d3a4 (TinyUSB efab607b8 + the 4 patches, SDK 2.3.1).

Where a number is **measured** or an **estimate**, it says so.

## 1. Summary

- **640x480x8 fits, but not with stock libvterm.** The framebuffer takes 300 KB of the 512 KB main SRAM. libvterm's cell is 36 bytes and it keeps two screens, so even 80x29 needs 167 KB, against about 157 KB left. Cutting `VTERM_MAX_CHARS_PER_CELL` from 6 to 1 makes the cell 16 bytes. That leaves room for 80x33 with about 180 lines of scrollback.
- **Nothing is lost by that cut.** `vt_term.c` only ever draws `chars[0]`.
- **The port is mostly platform work.** `vt_term.c` makes only 8 distinct `cmm2->` calls, and draws almost everything through `draw_glyph()`. A small `cmm2.h` shim should let it build nearly unchanged.
- **The palette needs a small new scan-out loop.** The PicoMite's existing 640x480x8 (`HDMIloopBTH640`) is RGB332, which cannot show xterm's 256 colours. The RGB555 line path and a `map256` lookup already exist in `HDMIloop0`, so this is about 15 lines on core 1.
- **The main feature gap is files.** XMODEM, logging and send-text-file all need a filing system, which this machine does not have (Q4).

## 2. Scope

Carried over from the CMM2 app: libvterm with `0001-vt100.patch`, the Spleen fonts, 256 colours, CP437 / Latin-1 / UTF-8, VT52, DECCOLM within what fits, double-width and double-height lines, mouse reporting, the host size request (`CSI 8;rows;cols t`), scrollback, the status line and the Shift-F12 / Pause setup menu.

New or changed:

| Area | On the RP2350 |
|---|---|
| Lines | UART0 on GP0/GP1, UART1 on GP4/GP5, up to 4 USB serial devices as host |
| Settings | A flash sector, not `vt100.cfg` |
| Screen | One mode: the menu's mode list goes, fonts stay |
| Quit | Becomes Restart and Firmware update (`reset_usb_boot`), as there is nothing to quit to |
| Bell | No audio on DVI (Q6) |
| Files | See Q4 |

Left out of the PicoMite's USB code: gamepads and touch, which are about 2,000 of `USBKeyboard.c`'s 3,401 lines.

## 3. What comes from where

| Part | Source | How |
|---|---|---|
| Terminal engine | libvterm pin 934bc2f + 0001 patch, plus a new 0002 for the cell size | Vendored copy |
| Fonts | Spleen via `make.py`: 750 glyphs, 121.5 KB (**measured**) | In flash; core 0 draws, so XIP timing does not matter |
| Screen layer | `vt_term.c` / `.h` | Nearly unchanged, behind the shim |
| App | `vt100.c` | Ports, modes and settings edited; XMODEM kept only if Q4 asks for it |
| HDMI | `graphics/HDMI.c`: `dma_irq_handler0`, the HSTX and DMA setup from `HDMICore`, the timing from `Screens.h` | Trimmed copy: no Option struct, layers or tiles |
| Clock start-up | `PicoMite.c` around line 3884: VREG 1.30 V, QSPI pads, QMI CLKDIV | Copied |
| Keyboard | `input/KeyboardMap.c`: 7 layouts, HID decoder, repeat, LEDs | Trimmed copy; `USR_KEYBRD_ProcessData` feeds a key ring. Ctrl-C stays byte 3, as `BreakKey` goes |
| Mouse, USB glue | `USBKeyboard.c`: mount callbacks, `analyze_mouse_descriptor`, `process_mouse_report`, `USB_bus_reset`, `USB_host_controller_tuning`, `USB_fault_service` | Trimmed copy |
| USB serial | `io/Serial.c`: `tuh_cdc_*` callbacks and the OPEN sequence | Trimmed copy |
| TinyUSB | `../tinyusb-master` + patches, `usb_host_files/tusb_config.h` | Same tree, **unchanged**: every patch is there for a bug we actually hit |
| Flash write | IRQs off on core 0, as `disable_interrupts_pico` does | Core 1 keeps scanning from SRAM |

The key codes match on both machines (UP 0x80, F1 0x91, DOWNSEL 0xA1 and so on), so `vtt_key()` should need no change. The Shift and Alt bits of `mods` still have to be checked against `KeyDown[6]` in phase 3.

A `PROVENANCE.md` will record the source commit of every copied file, so that later PicoMite and CMM2 fixes can be carried across.

## 4. Display and memory

### Scan-out

- 252 MHz, clk_hstx 126 MHz, 640x480 at 60 Hz. This is the existing `Freq252P` path at VREG 1.30 V with flash CLKDIV 2.
- Core 1 turns each line of the 8-bit framebuffer into RGB555 through a 256-entry `uint16_t` table, into two line buffers. `PIXELS_PER_WORD` is 2, using the existing `FullColour` TMDS setup.
- A line is 8,000 cycles at 252 MHz. The lookup should take about a third of that (**estimate**), and core 1 has nothing else to do.
- RGB332 straight from the framebuffer by DMA was rejected. Blue has only 4 levels, so xterm's cube and grey ramp do not land on its levels and greys tint. It would save only core 1 time, which is free here.

### SRAM budget

512 KB main = 524,288 bytes. The stacks sit in the two 4 KB scratch banks.

| Item | Bytes | Basis |
|---|---:|---|
| Framebuffer | 307,200 | exact |
| Two RGB555 lines | 2,560 | exact |
| Palette table | 512 | exact |
| TinyUSB host | 10,082 | **measured**, 1 Nov map in `build/` |
| App statics | 11,764 | **measured**, CMM2 `vt100.elf` data + bss |
| RX ring | 16,384 | CMM2's `RXBUF` |
| libvterm state, line info, `sb_buffer` | ~8,000 | estimate |
| Keyboard, mouse, SDK, RAM-resident scan-out | ~10,000 | estimate |
| **Left for libvterm's screens and scrollback** | **~157,000** | |

### What fits

libvterm's main and alternate screens are both allocated, because `vt_term.c` enables the alternate one at open. vim and less need it. The cell sizes below are **measured** by compiling for Cortex-M33.

| Font | Terminal, with status line | Stock, 36 B x 2 | Patched, 16 B x 2 | Scrollback left |
|---|---|---:|---:|---|
| 8x16 | 80x29 | 167,040: no | 74,240 | ~205 lines |
| 8x14 | 80x33 | 190,080: no | 84,480 | ~180 lines |
| 6x12 | 106x39 | 297,648: no | 132,288 | ~45 lines |
| 5x8 | 128x59 | 543,744: no | 241,664: no | full-screen size does not fit |

Scrollback costs 400 bytes a line at 80 columns. Screen memory follows the terminal size, not the font's maximum, so 5x8 stays usable for a host-requested 80x40 (102,400 bytes).

### What the framebuffer rules out

- **132 columns.** 640 pixels with the smallest font gives 128. A host's DECCOLM cannot be met.
- **More than about 200 lines of scrollback.** The CMM2's default is 2,000.

### Alternative: character-cell scan-out

A terminal's picture is always cells, so core 1 could build each line from a cell array and the font, with no framebuffer at all. On screen at 640x480 it looks the same: every cell still has any two of the 256 colours.

- Frees about 300 KB: roughly 850 lines of scrollback at 80x33 (**estimate**), and 128x59 fits.
- Blink, cursor and pointer become free at scan-out; scrolling becomes a row-pointer change.
- 132 columns becomes possible at 800x600 with 6x12 (133x50), but that means `FreqSVGA`, 360 MHz at 1.60 V.
- Costs: replacing `draw_glyph`, `on_moverect` and the cursor code; the active font (12 KB) has to be copied to SRAM; a real-time renderer on core 1; and `vt_term.c` no longer matches the CMM2's.

**Recommendation:** build the framebuffer as you asked. Its loop is about 15 lines, and it proves all the rest with `vt_term.c` barely touched. Since nearly all drawing goes through one function, cell scan-out can replace it later as an optional phase, if the scrollback or 132 columns turn out to matter.

## 5. Software structure

- **Core 1:** scan-out only, everything in RAM, nothing from flash.
- **Core 0, one main loop:** `tuh_task()`, then read at most about 512 bytes from the active line and feed libvterm, then keys, mouse and text sending; `vtt_update()` every 16 ms. The feed is capped so that USB keeps being serviced during a flood of output.
- **UART RX:** an interrupt into the ring.
- **USB callbacks:** only copy into rings, as your rule says. All USB calls stay on the main loop.
- **Settings:** one sector at a fixed offset below the PicoMite's `FLASH_TARGET_OFFSET`, not the end of flash. That area is one a PicoMite UF2 overwrites anyway, so a test board's A: drive and options survive swapping firmware.
- **Image size:** about 300 KB (**estimate**), so 2 MB flash is ample.

## 6. Phases

One step at a time, each run on hardware before the next starts.

| # | Work | Exit test |
|---|---|---|
| 0 | Project skeleton: CMake, SDK 2.3.1, RP2350, Ninja, UF2 | Boots, LED, hello on UART0 |
| 1 | Clock, HSTX, DMA, core 1 palette loop | 256-colour test card stable on your monitors, with core 0 hammering memmove |
| 2 | libvterm + 0002 patch + `vt_term.c` on UART0, no keyboard yet | vttest output correct from the PC; the budget above re-measured; a sustained flood at 921600 with no loss |
| 3 | USB host, keyboard and mouse through a hub | PicoMite EDIT and FM driven over UART0: every key, Ctrl-C, mouse, OPTION DISPLAY, all layouts |
| 4 | UART1 and USB serial, port selection, hot-plug status | CH340, FTDI, CP210x, PL2303 and a PicoMite USB console; lead pulled and put back |
| 5 | Menu, settings in flash, Restart and Firmware update | Settings survive power-off; the picture holds during a save |
| 6 | File features, if Q4 asks for them | XMODEM 193 KB both ways, as tested on the CMM2 |
| 7 | Soak test, manual, per-board UF2s | Overnight run at full speed with a USB keyboard attached |
| opt | Character-cell scan-out, if wanted | Same vttest set, then 132 columns |

The vttest bar is what the CMM2 passed: menus 1, 2, 3, 4, 6, 7, 8, 9, 10 and the layout and cursor keys of 5.

## 7. Risks and known traps

1. **Throughput.** An M33 at 252 MHz is well below the CMM2's M7. Phase 2 measures it. If 921600 will not hold: 315 MHz (75 Hz) gives 25% more, and a row-offset scroll in the scan-out removes the 300 KB move on full-screen scrolls.
2. **Peak memory on resize.** `vterm_set_size` allocates each new buffer before freeing the old one, and `scrollback_alloc` runs after it. The order must change so that scrollback is released first, or a PicoMite's `CSI 8` at each EDIT could fail.
3. **`VTERM_MAX_CHARS_PER_CELL` of 1 is untested upstream.** vttest plus a UTF-8 combining-character test goes in phase 2.
4. **80x40 from a PicoMiteVGA's EDIT.** 480 / 12 is exactly 40 rows, so with the status line on, `resize_to` refuses 6x12 and falls to 5x8: 400x320 pixels in the corner of the screen. Plan: hide the status line while the host needs every row, and add an 8x12 font so that 80x40 fills the screen.
5. **USB host traps are already solved, so must not be undone.** Bus reset at start, `hcd_int_handler` under interrupts off, no I/O in callbacks, the low-speed EP0 delay, toggle roll-back, CDC receive enabled only while open.
6. **Older PicoMites as USB slaves.** Before 85e6cde their console wedges on 1-byte packets, which is what typing sends. Rare by hand, but worth a line in the manual.
7. **Saving settings loses serial input.** An erase turns core 0's IRQs off for tens of milliseconds. Accepted, as it only happens on Save.
8. **No spare console.** USB is the host and both UARTs are the product. Debug builds can send a PIO UART TX to the PC3's CH340 on GP8.
9. **Hardware the firmware cannot fix.** The host port must supply 5 V, and keyboard + mouse + serial need a hub.

## 8. Decisions taken unless you object

- 252 MHz and 60 Hz. The PicoMite's HDMI default is 315 MHz and 75 Hz, but 60 Hz is the mode every DVI display must accept.
- RGB555 palette, not RGB332.
- No PSRAM use, not even when present.
- Copies with provenance, not a shared build with the PicoMite tree ("completely standalone").
- Default 8x14 font, 80x33, status line on, 115200 8N1, UART0, US layout, as the CMM2 except for the port names.
- Gamepads and USB touch left out.

## 9. Questions for Peter

1. **Display:** framebuffer as you asked, accepting about 180 lines of scrollback and no 132 columns, with cell scan-out as a later option? *Recommended: yes.*
2. **Boards:** which ones? There are three HDMI pinouts: default 2,0,6,4; PC3 1,3,5,7; Olimex 1,3,7,5. A wrong one gives no picture and so no menu, so it has to be per-board UF2s or a boot-time choice. The OLIMEX and OLIMEXUSB presets also put SD MISO on GP4, which clashes with your UART1 pins. *Default: PC3 and the default pinout, a UF2 for each.*
3. **Lines:** one active at a time, as on the CMM2, or several live sessions switched by a key? A second session costs about 84 KB and would use up nearly all the scrollback. *Default: one.*
4. **Files:** XMODEM, logging and send-text-file have nowhere to go. Leave them out of the first version, or add a store: USB stick (FatFS and the C: code exist, but a stick must share the hub), SD (needs pins, and which?), or littlefs in flash? *Default: leave them out for now.*
5. **CDC:** I've read this as USB host only: the terminal plugs into USB serial devices, as COM3-COM6 do. It cannot also appear as a serial device to a PC, as the one controller is the host. Right?
6. **Bell:** a visual flash only, or a PWM tone on a pin as well? If so, which pin? *Default: visual only.*
7. **Flow control:** GP0/1 and GP4/5 leave out RTS and CTS (GP2/3 and GP6/7 would carry them). Is an XON/XOFF menu setting enough? *Default: yes, off by default.*
8. **Project:** name and location? It should sit in `D:\Dropbox\PicoMite` beside `tinyusb-master` and `pico-sdk`. New GitHub repo, public or private?
9. **Licence:** MIT for new files, as the CMM2 app, with the PicoMite header kept on copied files? Clause 3 asks for the MMBasic copyright message at startup, which reads oddly on a machine with no interpreter. That is for you and Geoff to settle.
10. **vttest harness:** f315567 says vttest ran in a PC harness with the app's code, but it is not in the CMM2 repo. Where is it? Otherwise I'll write a new one under WSL.
11. **Test hardware:** which board may I use, and may I flash it with non-PicoMite firmware? A PC3 looks right: HDMI, hub, a PC bridge on GP0/GP1 (COM25 in the 1 Nov notes) and GP4/5 free. UART1 will need a second bridge or a GP4-GP5 loopback. Which keyboard, mouse and hub?

## 10. Peter's answers, 7 November

1. Display: the framebuffer; cell scan-out stays a later option.
2. Boards: test on the Pico Computer 3; the target is an RP2350 with the HSTX sock. The code takes a board header (`-DVT_BOARD=pc3` or `hstx_sock`).
3. Lines: one at a time.
4. Files: left out for now.
5. CDC: host only, as COM3-COM6.
6. Bell: PWM tone on GP2. A Bell menu setting also offers Flash and Off.
7. Flow control: an XON/XOFF setting.
8. Project: new repo VT2350, in `D:\Dropbox\PicoMite\VT2350`.
9. Licence: MIT, no MMBasic message; PicoMite code integrated under MIT with Peter's permission.
10. vttest harness: written, `tools/harness`.
11. Test board: COM16, a PC3.

Measured since: 351 KB of BSS, so the heap is about 172 KB, not 157 KB. 80x24 gets 240 lines of scrollback.
