/* video.h: 640x480, 8 bits a pixel, out of the HSTX as DVI. Core 1 does nothing else. */
#ifndef VIDEO_H
#define VIDEO_H
#include <stdbool.h>
#include <stdint.h>

#define SCREEN_W 640
#define SCREEN_H 480

/* The system clock, which the pixel clock comes from: 252000 gives 640x480 at 60 Hz, 315000 at 75 Hz */
#ifndef VT_SYS_KHZ
#define VT_SYS_KHZ 252000
#endif

extern uint8_t video_fb[SCREEN_W * SCREEN_H];

void video_start(void);                         /* core 1 started: the picture runs from here on, from RAM alone */
void video_wiring(const uint8_t *w);            /* clock, D0, D1, D2 as OPTION HDMI PINS, at once, with the picture running */
void video_palette(const uint8_t *rgb);         /* 256 RGB triples */
void video_invert(bool on);                     /* every colour inverted, for a visual bell */
void video_testcard(void);                      /* the 256 colours and a frame, drawn in the framebuffer */
uint32_t video_frames(void);                    /* frames since the start */
#endif
