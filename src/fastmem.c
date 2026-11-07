/* fastmem.c: memmove, in RAM and a block of 32 bytes at a time. A scroll moves 300 KB of framebuffer a frame and libvterm's cells for every line: this
   was worth 12% in a flood of text over the C library's. Rows are word-aligned, so the block loop is what runs. It replaces the library's for the whole program. MIT License. */
#include <stddef.h>
#include <stdint.h>
#include "pico.h"

void *__not_in_flash_func(memmove)(void *dst, const void *src, size_t n) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    if(d == s || n == 0) return dst;
    if(d < s || d >= s + n) {                   /* forwards */
        if((((uintptr_t)d ^ (uintptr_t)s) & 3) == 0) {
            while(((uintptr_t)d & 3) && n) {
                *d++ = *s++;
                n--;
            }
            size_t blocks = n >> 5;
            if(blocks) {
                n &= 31;
                __asm volatile("1: ldmia %[s]!, {r4,r5,r6,r8,r9,r10,r11,r12}\n"
                               "   stmia %[d]!, {r4,r5,r6,r8,r9,r10,r11,r12}\n"
                               "   subs %[b], #1\n"
                               "   bne 1b\n"
                               : [d] "+r"(d), [s] "+r"(s), [b] "+r"(blocks)
                               :
                               : "r4", "r5", "r6", "r8", "r9", "r10", "r11", "r12", "cc", "memory");
            }
            while(n >= 4) {
                *(uint32_t *)d = *(const uint32_t *)s;
                d += 4;
                s += 4;
                n -= 4;
            }
        }
        while(n--) *d++ = *s++;
    } else {                                    /* backwards: the blocks overlap upwards */
        d += n;
        s += n;
        if((((uintptr_t)d ^ (uintptr_t)s) & 3) == 0) {
            while(((uintptr_t)d & 3) && n) {
                *--d = *--s;
                n--;
            }
            size_t blocks = n >> 5;
            if(blocks) {
                n &= 31;
                __asm volatile("1: ldmdb %[s]!, {r4,r5,r6,r8,r9,r10,r11,r12}\n"
                               "   stmdb %[d]!, {r4,r5,r6,r8,r9,r10,r11,r12}\n"
                               "   subs %[b], #1\n"
                               "   bne 1b\n"
                               : [d] "+r"(d), [s] "+r"(s), [b] "+r"(blocks)
                               :
                               : "r4", "r5", "r6", "r8", "r9", "r10", "r11", "r12", "cc", "memory");
            }
            while(n >= 4) {
                d -= 4;
                s -= 4;
                *(uint32_t *)d = *(const uint32_t *)s;
                n -= 4;
            }
        }
        while(n--) *--d = *--s;
    }
    return dst;
}
