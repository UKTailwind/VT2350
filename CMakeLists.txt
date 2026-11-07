cmake_minimum_required(VERSION 3.13)

# VT2350: a VT100 terminal on an RP2350.
#   cmake -G Ninja -DVT_BOARD=hstx_sock ..      a Pico 2 with the HSTX sock: the target
#   cmake -G Ninja -DVT_BOARD=pc3 ..            the Pico Computer 3, for testing, with the debug link
set(VT_BOARD hstx_sock CACHE STRING "The board: a header in boards/")

if(VT_BOARD STREQUAL "pc3")
    set(_vt_pico_board pimoroni_pga2350)        # an RP2350B with 16 MB of flash: only the chip and flash size matter here
elseif(VT_BOARD STREQUAL "hstx_sock")
    set(_vt_pico_board pico2)
else()
    message(FATAL_ERROR "VT_BOARD must be pc3 or hstx_sock")
endif()
set(PICO_PLATFORM rp2350-arm-s)
set(PICO_BOARD ${_vt_pico_board} CACHE STRING "From VT_BOARD" FORCE)

# TinyUSB master with the PicoMite's patches and VT2350's own: tinyusb-patches/README.md, tinyusb-patches/setup-tinyusb.sh
set(VT_TINYUSB_PATH ${CMAKE_CURRENT_LIST_DIR}/../tinyusb-vt2350 CACHE PATH "The patched TinyUSB tree")
set(PICO_TINYUSB_PATH ${VT_TINYUSB_PATH} CACHE PATH "TinyUSB" FORCE)

include(pico_sdk_import.cmake)
project(VT2350 C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

set(VTERM ${CMAKE_CURRENT_LIST_DIR}/third_party/libvterm)
option(VT_TESTCARD "The picture alone, for bringing up a board" OFF)
if(VT_TESTCARD)
    add_executable(VT2350 src/main.c src/video.c src/debug.c src/xterm_palette.c src/testcard.c)
else()
    add_executable(VT2350
        src/main.c
        src/video.c
        src/debug.c
        src/xterm_palette.c
        src/vt100.c
        src/vt_term.c
        src/vt_fonts.c
        src/lines.c
        src/usbhost.c
        src/keyboard.c
        src/keymaps.c
        src/settings.c
        src/platform.c
        src/fastmem.c
        src/mouse_desc.c
        ${VTERM}/src/encoding.c
        ${VTERM}/src/keyboard.c
        ${VTERM}/src/mouse.c
        ${VTERM}/src/parser.c
        ${VTERM}/src/pen.c
        ${VTERM}/src/screen.c
        ${VTERM}/src/state.c
        ${VTERM}/src/unicode.c
        ${VTERM}/src/vterm.c
    )
endif()
pico_generate_pio_header(VT2350 ${CMAKE_CURRENT_LIST_DIR}/src/debug_uart.pio)

# The patched host driver's event ring, read over the debug link: not for a release
set(VT_TEST_DEFS "" CACHE STRING "Definitions for a diagnostic build")
if(VT_TEST_DEFS)
    target_compile_definitions(VT2350 PRIVATE ${VT_TEST_DEFS})
endif()
option(VT_USB_EVLOG "USB host event ring" OFF)
if(VT_USB_EVLOG)
    set_source_files_properties(
        ${PICO_TINYUSB_PATH}/src/portable/raspberrypi/rp2040/rp2040_usb.c
        ${PICO_TINYUSB_PATH}/src/portable/raspberrypi/rp2040/hcd_rp2040.c
        PROPERTIES COMPILE_DEFINITIONS "PC3_USB_EVLOG;PC3_EVLOG_N=256")
    target_compile_definitions(VT2350 PRIVATE VT_USB_EVLOG)
endif()

target_include_directories(VT2350 PRIVATE
    src
    ${VTERM}/include
    ${VTERM}/src
)
# boards/<name>.h is found as vt_board.h
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/gen/vt_board.h "#include \"${CMAKE_CURRENT_LIST_DIR}/boards/${VT_BOARD}.h\"\n")
target_include_directories(VT2350 PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gen)

target_compile_definitions(VT2350 PRIVATE
    VTERM_MAX_CHARS_PER_CELL=1          # 36 bytes a cell become 16: vt_term.c only draws the first character
    PICO_MALLOC_PANIC=0                 # vt_term.c decides what fits by what malloc gives it
    PICO_STACK_SIZE=0x1000
    PICO_USE_STACK_GUARDS=0
)
target_compile_options(VT2350 PRIVATE -O2 -Wall)
set_source_files_properties(${VTERM}/src/encoding.c ${VTERM}/src/keyboard.c ${VTERM}/src/mouse.c ${VTERM}/src/parser.c
    ${VTERM}/src/pen.c ${VTERM}/src/screen.c ${VTERM}/src/state.c ${VTERM}/src/unicode.c ${VTERM}/src/vterm.c
    PROPERTIES COMPILE_OPTIONS "-Wno-all;-Wno-unused-parameter;-O3")
set_source_files_properties(src/vt_term.c PROPERTIES COMPILE_OPTIONS "-O3")

target_link_libraries(VT2350
    pico_stdlib
    pico_multicore
    hardware_clocks
    hardware_dma
    hardware_flash
    hardware_irq
    hardware_pio
    hardware_pwm
    hardware_uart
    hardware_watchdog
)
if(NOT VT_TESTCARD)
    target_link_libraries(VT2350 tinyusb_host tinyusb_board)
endif()

pico_enable_stdio_uart(VT2350 0)
pico_enable_stdio_usb(VT2350 0)
pico_add_extra_outputs(VT2350)
