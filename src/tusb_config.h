/* tusb_config.h: the PicoMite's usb_host_files/tusb_config.h, without mass storage. See there for why each number is what it is. */
#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifndef BOARD_TUH_RHPORT
#define BOARD_TUH_RHPORT 0
#endif
#ifndef BOARD_TUH_MAX_SPEED
#define BOARD_TUH_MAX_SPEED OPT_MODE_DEFAULT_SPEED
#endif

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif
#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS OPT_OS_NONE
#endif
#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG 0
#endif

#define CFG_TUH_ENABLED 1
#define CFG_TUH_MAX_SPEED BOARD_TUH_MAX_SPEED

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif
#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))
#endif

#define CFG_TUH_ENUMERATION_BUFSIZE 1024    /* usbh.patch reads each string in one 255-byte request: it must be at least that */

#define CFG_TUH_HUB 2
#define CFG_TUH_CDC 4                       /* four USB serial devices */
#define CFG_TUH_DEVICE_MAX (3 * CFG_TUH_HUB + 1)
#define CFG_TUH_HID (4 * CFG_TUH_DEVICE_MAX)
#define CFG_TUH_TASK_QUEUE_SZ 64
#define CFG_TUH_MSC 0

#define CFG_TUH_HID_EPIN_BUFSIZE 64
#define CFG_TUH_HID_EPOUT_BUFSIZE 64

#define CFG_TUH_CDC_FTDI 1
#define CFG_TUH_CDC_CP210X 1
#define CFG_TUH_CDC_CH34X 1
#define CFG_TUH_CDC_PL2303 1

#define CFG_TUH_CDC_LINE_CONTROL_ON_ENUM 0x00                   /* DTR and RTS wait until the port is opened */
#define CFG_TUH_CDC_LINE_CODING_ON_ENUM {115200, CDC_LINE_CODING_STOP_BITS_1, CDC_LINE_CODING_PARITY_NONE, 8}

#endif
