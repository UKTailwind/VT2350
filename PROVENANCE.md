# Where this code came from

So that later fixes can be carried across. The code from the PicoMite is integrated under the MIT licence with Peter Mather's permission (7 November 2026).

| Here | From | At |
|---|---|---|
| `src/vt_term.c`, `src/vt_term.h`, `src/vt100.c` | CMM2V1.5 `apps/vt100` | f315567 |
| `third_party/libvterm` | neovim/libvterm 934bc2f with `0001-vt100.patch` (CMM2V1.5 `apps/vt100/patches`) applied, and `VTERM_MAX_CHARS_PER_CELL` settable from the build | |
| `third_party/spleen` | fcambus/spleen 5116f8e, the sizes used | |
| `tools/make_fonts.py` | CMM2V1.5 `apps/vt100/make.py`, the font half, with 8x12 added | f315567 |
| `src/video.c` | PicoMite `graphics/HDMI.c`, `graphics/Screens.h` | 493d3a4 |
| `src/main.c` | PicoMite `PicoMite.c`: clock start-up | 493d3a4 |
| `src/keyboard.c`, `src/keymaps.c` | PicoMite `input/KeyboardMap.c` | 493d3a4 |
| `src/usbhost.c`, `src/mouse_desc.c`, `.h` | PicoMite `input/USBKeyboard.c`, `Hardware_Includes.h` | 493d3a4 |
| `src/lines.c` | PicoMite `io/Serial.c`: the USB serial side | 493d3a4 |
| `src/settings.c` | PicoMite `misc/FileIO.c`: restoring the flash's timing after a write | 493d3a4 |
| `src/tusb_config.h` | PicoMite `usb_host_files/tusb_config.h` | 493d3a4 |
| `tinyusb-patches/` | PicoMite `tinyusb-patches/`: TinyUSB master efab607b8 and four patches, and VT2350's own `hcd_epx_in_window.patch` and `hcd_int_out_idle.patch` | 493d3a4 |

## Fixed here, and still in the CMM2's apps/vt100 at f315567 (UKTailwind/CMM2V1.5 #12 and #13)

Both found by the harness's check, which draws every screen afresh and compares it with what incremental drawing left.

- `vtt_write`: a stale character after a scroll. `vtt_update` took the cursor off by redrawing its cell after libvterm had scrolled, so the cell got the text from below, and the framebuffer's own scroll then carried that up a row. It depended on timing. The cursor now comes off at the start of `vtt_write`, while libvterm's screen is what the framebuffer shows.
- `draw_rows`: a double-width line lost its right half at every full redraw, closing the menu for one. libvterm's erased right-hand cells are not marked double-width, so they were drawn as single-width blanks over the line. It now goes by column 0, as `on_damage` does.

Also changed here for speed, and worth having on the CMM2 if a flood matters there: `draw_glyph`'s nibble table (four pixels a store) and `ascii_index`.

## Credits

- MMBasic and the PicoMite: Geoff Graham and Peter Mather. Keyboard layouts by Fabrice Muller, Alberto Leibovich and the other contributors.
- libvterm: Paul Evans, MIT (`third_party/libvterm/LICENSE`).
- Spleen: Frederic Cambus, BSD-2-Clause (`third_party/spleen/LICENSE`).
- The HSTX scan-out and the PIO UART derive from Raspberry Pi's pico-examples, BSD-3-Clause.
- TinyUSB: Ha Thach, MIT. The Raspberry Pi Pico SDK: BSD-3-Clause.
