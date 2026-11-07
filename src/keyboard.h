/* keyboard.h: a USB keyboard's reports turned into the codes vt_term.c takes, as the PicoMite's and CMM2's keyboards give them. */
#ifndef KEYBOARD_H
#define KEYBOARD_H
#include <stdbool.h>
#include <stdint.h>

/* the codes */
#define F1 0x91
#define F2 0x92
#define F3 0x93
#define F4 0x94
#define F5 0x95
#define F6 0x96
#define F7 0x97
#define F8 0x98
#define F9 0x99
#define F10 0x9A
#define F11 0x9B
#define F12 0x9C
#define UP 0x80
#define DOWN 0x81
#define LEFT 0x82
#define RIGHT 0x83
#define UPSEL 0xA4
#define DOWNSEL 0xA1
#define LEFTSEL 0xA2
#define RIGHTSEL 0xA3
#define INSERT 0x84
#define HOME 0x86
#define END 0x87
#define PUP 0x88
#define PDOWN 0x89
#define SLOCK 0x8C
#define ALT 0x8B
#define SHIFT_TAB 0x9F
#define SHIFT_DEL 0xA0
#define TAB 0x09
#define BKSP 0x08
#define ENTER 0x0D
#define ESC 0x1B
#define DEL 0x7F
#define NUM_ENT ENTER

enum { KB_US, KB_UK, KB_DE, KB_FR, KB_ES, KB_BE, KB_COUNT };

extern const int USkeyValue[202], UKkeyValue[202], DEkeyValue[202], FRkeyValue[202], ESkeyValue[202], BEkeyValue[202];
extern const char *const kbd_names[KB_COUNT];

void kbd_layout(int layout);
void kbd_report(const uint8_t *report);         /* an 8-byte boot report, from the USB callback: it only queues keys */
void kbd_tick(uint32_t ms);                     /* the time gone by: the held key's repeat */
int  kbd_get(void);                             /* the next key, or -1 */
int  kbd_mods(void);                            /* 1 left Alt, 2 left Ctrl, 4 left GUI, 8 left Shift, 16-128 the same on the right */
uint8_t kbd_leds(void);                         /* the lock keys, as a HID output report */
extern volatile bool kbd_leds_changed;
#endif
