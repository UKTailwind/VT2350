/* usbhost.c: from the PicoMite's input/USBKeyboard.c, without the gamepads and touch panels. tuh_task runs from the main loop only, and the callbacks only copy:
   a print or a long job in one stalls enumeration of whatever else is behind the hub. MIT License. */
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/irq.h"
#include "hardware/regs/usb.h"
#include "hardware/structs/usb.h"
#include "tusb.h"
#include "debug.h"
#include "keyboard.h"
#include "mouse_desc.h"
#include "usbhost.h"
#include "video.h"

#define SLOTS 4
#define START_MS 2000                           /* as the PicoMite: nothing before the devices have had time to wake */

typedef struct {
    bool active, report_requested, lights_sent;
    uint8_t addr, inst, type;                   /* HID_ITF_PROTOCOL_KEYBOARD or _MOUSE */
    uint8_t lights;
    int report_timer, report_rate;              /* ms */
    mouse_info_t mouse;
} slot_t;

static slot_t slots[SLOTS];
static int mouse_x = SCREEN_W / 2, mouse_y = SCREEN_H / 2, mouse_buttons, mouse_wheel;
static int devices;

/* what the callbacks saw, for the debug link to say afterwards */
static char events[16];
static volatile uint32_t ev_head, ev_tail;
static inline void note(char c) {
    if(ev_head - ev_tail < sizeof(events)) events[ev_head++ % sizeof(events)] = c;
}

/* An SE0 bus reset. hcd_port_reset does nothing on this silicon, and an externally powered hub keeps its address across a restart and ignores enumeration
   from address 0 unless it is reset. The interrupt stays masked through the reset and its recovery, so that the edges coalesce into one attach on a stable bus */
#define OVERRIDE_EN (USB_USBPHY_DIRECT_OVERRIDE_TX_DM_OE_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_TX_DM_OVERRIDE_EN_BITS | \
                     USB_USBPHY_DIRECT_OVERRIDE_TX_DP_OE_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_TX_DP_OVERRIDE_EN_BITS)
static void bus_reset(void) {
    irq_set_enabled(USBCTRL_IRQ, false);
    hw_set_bits(&usb_hw->phy_direct_override, OVERRIDE_EN);
    hw_set_bits(&usb_hw->phy_direct, USB_USBPHY_DIRECT_TX_DM_OE_BITS | USB_USBPHY_DIRECT_TX_DP_OE_BITS);
    hw_clear_bits(&usb_hw->phy_direct, USB_USBPHY_DIRECT_TX_DM_BITS | USB_USBPHY_DIRECT_TX_DP_BITS);
    busy_wait_us(20000);
    hw_clear_bits(&usb_hw->phy_direct, USB_USBPHY_DIRECT_TX_DM_OE_BITS | USB_USBPHY_DIRECT_TX_DP_OE_BITS);
    hw_clear_bits(&usb_hw->phy_direct_override, OVERRIDE_EN);
    busy_wait_us(50000);
    irq_set_enabled(USBCTRL_IRQ, true);
}

void usbhost_init(void) {
    static const tusb_rhport_init_t host_init = {.role = TUSB_ROLE_HOST, .speed = TUSB_SPEED_AUTO};
    tusb_init(BOARD_TUH_RHPORT, &host_init);
    /* Longer inter-packet and turnaround timeouts, for hubs. Set after tuh_init, which owns these registers */
    hw_set_bits((io_rw_32 *)(USBCTRL_REGS_BASE + USB_LINESTATE_TUNING_OFFSET), USB_LINESTATE_TUNING_MULTI_HUB_FIX_BITS);
    bus_reset();
}

void usbhost_task(void) {
    static uint64_t last;
    uint64_t now = time_us_64();
    if(now < START_MS * 1000ull) return;
    if(last == 0) last = now;
    tuh_task();
    uint32_t ms = (now - last) / 1000;
    if(ms) {
        last += ms * 1000ull;
        kbd_tick(ms);
    }
    for(int i = 0; i < SLOTS; i++) {
        slot_t *s = &slots[i];
        if(!s->active) continue;
        if(s->report_timer < 10000) s->report_timer += ms;
        if(s->type == HID_ITF_PROTOCOL_KEYBOARD && kbd_leds_changed) {
            s->lights = kbd_leds();
            tuh_hid_set_report(s->addr, s->inst, 0, HID_REPORT_TYPE_OUTPUT, &s->lights, 1);
        }
        if(s->report_requested || s->report_timer < s->report_rate) continue;
        if(s->type == HID_ITF_PROTOCOL_KEYBOARD && !s->lights_sent) {
            s->lights_sent = true;
            s->lights = kbd_leds();
            tuh_hid_set_report(s->addr, s->inst, 0, HID_REPORT_TYPE_OUTPUT, &s->lights, 1);
        }
        s->report_requested = true;
        if(!tuh_hid_receive_report(s->addr, s->inst)) {
            s->report_requested = false;        /* another go next time */
            s->report_timer = 0;
        }
    }
    kbd_leds_changed = false;
#if VT_HAS_DEBUG
    while(ev_tail != ev_head) {
        static const char *const names[256] = {['K'] = "keyboard on", ['k'] = "keyboard off", ['M'] = "mouse on", ['m'] = "mouse off", ['D'] = "device on", ['d'] = "device off"};
        debug_print("USB: %s\r\n", names[(uint8_t)events[ev_tail++ % sizeof(events)]]);
    }
#endif
}

int usbhost_mouse(int m[4]) {
    for(int i = 0; i < SLOTS; i++) {
        if(slots[i].active && slots[i].type == HID_ITF_PROTOCOL_MOUSE) {
            m[0] = mouse_x;
            m[1] = mouse_y;
            m[2] = mouse_buttons;
            m[3] = mouse_wheel;
            return 1;
        }
    }
    return 0;
}

int usbhost_devices(void) {
    return devices;
}

#if VT_HAS_DEBUG
extern volatile uint16_t pm_usb_fault_n[], pm_usb_fault_ep[], pm_usb_fault_total;
void usbhost_faults(void) {
    static const char *const name[5] = {"", "DATA_SEQ", "BUFCTRL32", "BUFCTRL16", "EP_FULL"};
    debug_print("USB faults %u:", pm_usb_fault_total);
    for(int c = 1; c <= 4; c++) if(pm_usb_fault_n[c]) debug_print(" %s %u @%x", name[c], pm_usb_fault_n[c], pm_usb_fault_ep[c]);
    debug_print("\r\n");
}
#else
void usbhost_faults(void) {}
#endif

#if VT_HAS_DEBUG && defined(VT_USB_EVLOG)
typedef struct { uint32_t t, tag, a, b, c; } ev_t;
extern volatile ev_t pc3_evlog[256];
extern volatile uint32_t pc3_evlog_n;
extern volatile int32_t pc3_evlog_freeze;
void usbhost_events(int rearm) {
    if(rearm) {
        pc3_evlog_freeze = -1;
        return;
    }
    int32_t was = pc3_evlog_freeze;
    pc3_evlog_freeze = 0;                       /* still, while it is read */
    uint32_t n = pc3_evlog_n, from = n > 256 ? n - 256 : 0;
    debug_print("\r\nVTEV %lu freeze %ld\r\n", (unsigned long)n, (long)was);
    for(uint32_t i = from; i < n; i++) {
        volatile ev_t *e = &pc3_evlog[i % 256];
        debug_print("%lu %lx %lx %lx %lx\r\n", (unsigned long)e->t, (unsigned long)e->tag, (unsigned long)e->a, (unsigned long)e->b, (unsigned long)e->c);
    }
    debug_print("VTEVEND\r\n");
    pc3_evlog_freeze = -1;
}
#else
void usbhost_events(int rearm) {
    (void)rearm;
}
#endif

void tuh_mount_cb(uint8_t dev_addr) {
    (void)dev_addr;
    devices++;
    note('D');
}

void tuh_umount_cb(uint8_t dev_addr) {
    (void)dev_addr;
    devices--;
    note('d');
}

static int free_slot(uint8_t protocol) {
    if(protocol == HID_ITF_PROTOCOL_KEYBOARD && !slots[0].active) return 0;
    if(protocol == HID_ITF_PROTOCOL_MOUSE && !slots[1].active) return 1;
    for(int i = SLOTS - 1; i >= 0; i--) if(!slots[i].active) return i;
    return -1;
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len) {
    __dsb();
    uint8_t protocol = tuh_hid_interface_protocol(dev_addr, instance);
    if(protocol != HID_ITF_PROTOCOL_KEYBOARD && protocol != HID_ITF_PROTOCOL_MOUSE) return;     /* a composite device's other interfaces */
    /* A keyboard with media keys can present two boot-keyboard interfaces. Every key comes on the first */
    if(protocol == HID_ITF_PROTOCOL_KEYBOARD)
        for(int i = 0; i < SLOTS; i++)
            if(slots[i].active && slots[i].addr == dev_addr && slots[i].type == protocol) return;
    int n = free_slot(protocol);
    if(n < 0) return;
    slot_t *s = &slots[n];
    memset(s, 0, sizeof(*s));
    s->addr = dev_addr;
    s->inst = instance;
    s->type = protocol;
    s->report_rate = 20;
    s->report_timer = -(10 + (n + 2) * 500);
    if(protocol == HID_ITF_PROTOCOL_MOUSE) {
        /* Report protocol for the wheel. A device that refuses it goes on sending boot reports, which process_mouse decodes by the fixed layout */
        if(desc_len > 0 && desc_report != NULL) analyze_mouse_descriptor(desc_report, desc_len, &s->mouse);
        tuh_hid_set_protocol(dev_addr, instance, HID_PROTOCOL_REPORT);
    }
    s->active = true;
    note(protocol == HID_ITF_PROTOCOL_KEYBOARD ? 'K' : 'M');
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    __dsb();
    for(int i = 0; i < SLOTS; i++) {
        if(slots[i].active && slots[i].addr == dev_addr && slots[i].inst == instance) {
            note(slots[i].type == HID_ITF_PROTOCOL_KEYBOARD ? 'k' : 'm');
            memset(&slots[i], 0, sizeof(slots[i]));
            return;
        }
    }
}

static void process_mouse(slot_t *s, const uint8_t *report, uint16_t len) {
    const mouse_info_t *mi = &s->mouse;
    int dx, dy, wheel = 0, buttons;
    if(report == NULL || len == 0) return;
    if(tuh_hid_get_protocol(s->addr, s->inst) == HID_PROTOCOL_BOOT || !mi->layout_valid) {
        if(len < 3) return;
        buttons = report[0];
        dx = (int8_t)report[1];
        dy = (int8_t)report[2];
        if(len > 3) wheel = (int8_t)report[3];
    } else {
        if(mi->uses_report_id) {
            if(report[0] != mi->report_id) return;      /* another of the interface's reports */
            report++;
            len--;
        }
        if(len < mi->min_len) return;
        buttons = mouse_field_value(report, &mi->f_buttons);
        dx = mouse_field_value(report, &mi->f_x);
        dy = mouse_field_value(report, &mi->f_y);
        wheel = mouse_field_value(report, &mi->f_wheel);
    }
    mouse_buttons = buttons & 7;
    mouse_x += dx / 2;
    mouse_y += dy / 2;
    if(mouse_x < 0) mouse_x = 0;
    if(mouse_x >= SCREEN_W) mouse_x = SCREEN_W - 1;
    if(mouse_y < 0) mouse_y = 0;
    if(mouse_y >= SCREEN_H) mouse_y = SCREEN_H - 1;
    mouse_wheel += wheel;
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len) {
    __dsb();
    for(int i = 0; i < SLOTS; i++) {
        slot_t *s = &slots[i];
        if(!s->active || s->addr != dev_addr || s->inst != instance) continue;
        if(s->type == HID_ITF_PROTOCOL_KEYBOARD) {
            if(len >= 8) kbd_report(report);
        } else process_mouse(s, report, len);
        s->report_requested = false;
        s->report_timer = 0;
        return;
    }
}
