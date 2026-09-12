#ifndef REVS_FRAMEBUFFER_DIRTY_H
#define REVS_FRAMEBUFFER_DIRTY_H
/* Change-aware BBC framebuffer stores for the Amiga decoder.
 *
 * The engine owns one BBC-shaped framebuffer, while the Amiga display owns two
 * alternating bitplane buffers. A changed BBC byte therefore dirties the
 * corresponding 8-line cell in BOTH bitplane pages. decode() consumes only the
 * map belonging to its current backbuffer.
 *
 * One bit represents one BBC cell column:
 *
 *   cell = (addr - BBC_SCREEN_BASE) >> 3
 *
 * because the BBC layout is row*320 + column*8 + line. Forty cells make exactly
 * five bytes per character row, 26 rows make 130 bytes per map.
 *
 * The cheap equality test stays inline at each store. The uncommon changed
 * case calls one shared marker: keeping its bit arithmetic out of the viewport
 * loop avoids register pressure and duplicated 68000 code. */
#if !defined(REVS_PLATFORM_AMIGA)
#include <stdint.h>
#endif
#include "bbc_screen.h"
#include "../cpu/mem_decl.h"

#ifdef __cplusplus
extern "C" {
#endif
extern MEM_QUAL uint8_t mem[65536];

#define REVS_FB_DIRTY_ROW_BYTES ((BBC_SCREEN_CELLS + 7u) >> 3)
#define REVS_FB_DIRTY_BYTES     (BBC_SCREEN_ROWS * REVS_FB_DIRTY_ROW_BYTES)

extern unsigned char g_frameDirtyMap[2][REVS_FB_DIRTY_BYTES];
#if defined(REVS_PLATFORM_AMIGA) && defined(REVS_CHANGE_DIRTY) && !defined(REVS_NO_DIRTY)
void revs_fb_mark_changed(uint16_t addr);
#endif
#ifdef __cplusplus
}
#endif

#if defined(REVS_PLATFORM_AMIGA) && defined(REVS_CHANGE_DIRTY) && !defined(REVS_NO_DIRTY)
/* Address is already proved to be inside the BBC framebuffer. */
static inline void revs_fb_store_screen(uint16_t addr, uint8_t value)
{
    if (mem[addr] == value) return;
    mem[addr] = value;
    revs_fb_mark_changed(addr);
}

/* For indirect/indexed stores whose effective address is known only at runtime. */
static inline void revs_fb_store_maybe(uint16_t addr, uint8_t value)
{
    if ((unsigned)(addr - BBC_SCREEN_BASE) < BBC_SCREEN_BYTES)
        revs_fb_store_screen(addr, value);
    else
        mem[addr] = value;
}
#else
/* Host validation and `make DIRTY=0` retain ordinary RAM-store semantics and cost. */
static inline void revs_fb_store_screen(uint16_t addr, uint8_t value)
{
    mem[addr] = value;
}
static inline void revs_fb_store_maybe(uint16_t addr, uint8_t value)
{
    mem[addr] = value;
}
#endif

static inline int revs_fb_dirty_take(unsigned char* map, unsigned cell)
{
    const unsigned byte = cell >> 3;
    const uint8_t  bit  = (uint8_t)(1u << (cell & 7u));
    const uint8_t  old  = map[byte];
    if (!(old & bit)) return 0;
    /* Writers and decode run serially in the shipping main-loop model.  The
       BODY_IN_ISR diagnostic build does not provide that ownership guarantee. */
    map[byte] = (uint8_t)(old & (uint8_t)~bit);
    return 1;
}

static inline void revs_fb_dirty_mark_row(unsigned char* map, unsigned row)
{
    unsigned char* p = map + row * REVS_FB_DIRTY_ROW_BYTES;
    for (unsigned i = 0; i < REVS_FB_DIRTY_ROW_BYTES; i++) p[i] = 0xFFu;
}

#endif /* REVS_FRAMEBUFFER_DIRTY_H */
