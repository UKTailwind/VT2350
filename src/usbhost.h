/* usbhost.h: the USB host: a keyboard and a mouse here, the serial devices in lines.c. */
#ifndef USBHOST_H
#define USBHOST_H
void usbhost_init(void);
void usbhost_task(void);                        /* from the main loop only: never from a callback */
int  usbhost_mouse(int m[4]);                   /* x, y in pixels, buttons, wheel: 0 if there is no mouse */
int  usbhost_devices(void);
void usbhost_faults(void);                      /* for tests: what the host driver counted */
void usbhost_events(int rearm);                 /* for tests: the driver's event ring, or start it again */
#endif
