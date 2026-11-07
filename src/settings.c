/* settings.c: the terminal's settings in one flash sector. It is at 1 MB and not the end of the flash: that is below where any RP2350 PicoMite keeps its options and
   A: drive, so a test board goes back to MMBasic with those untouched, and it fits the smallest flash. Core 1 and the scan-out's interrupt run from RAM, so the picture
   holds through a save; core 0's interrupts are off for the erase, which a fast line may notice. The flash's divider and format go back as they were, as the boot ROM's
   routines set their own. MIT License. */
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/structs/qmi.h"
#include "hardware/sync.h"
#include "platform.h"

#define OFFSET (1024 * 1024)
#define MAGIC 0x56543235u                       /* "VT25" */

typedef struct {
    uint32_t magic, size, sum;
} header;

static uint32_t sum_of(const uint8_t *p, size_t n) {
    uint32_t s = 2166136261u;                   /* FNV-1a */
    while(n--) s = (s ^ *p++) * 16777619u;
    return s;
}

int settings_load(void *p, size_t n) {
    const header *h = (const header *)(XIP_BASE + OFFSET);
    if(h->magic != MAGIC || h->size != n) return 0;
    if(sum_of((const uint8_t *)(h + 1), n) != h->sum) return 0;
    memcpy(p, h + 1, n);
    return 1;
}

static void __no_inline_not_in_flash_func(write_sector)(const uint8_t *page, size_t bytes) {
    uint32_t timing = qmi_hw->m[0].timing, rfmt = qmi_hw->m[0].rfmt;
    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(OFFSET, page, bytes);
    qmi_hw->m[0].timing = timing;
    qmi_hw->m[0].rfmt = rfmt;
    restore_interrupts(irq);
}

int settings_save(const void *p, size_t n) {
    static uint8_t page[512] __attribute__((aligned(4)));
    header h = {MAGIC, n, sum_of(p, n)};
    if(n + sizeof(h) > sizeof(page)) return -1;
    memset(page, 0xFF, sizeof(page));
    memcpy(page, &h, sizeof(h));
    memcpy(page + sizeof(h), p, n);
    write_sector(page, sizeof(page));
    return settings_load(page + sizeof(h), n) ? 0 : -2;
}
