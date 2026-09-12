#ifndef BUS_H
#define BUS_H
#include <stdint.h>

/* Memory bus — routes hardware-register accesses through the platform layer and
   everything else straight to mem[].

   BBC Micro memory-mapped I/O (all NOT backed by mem[]; reads/writes go to the
   platform implementation):

     $FC00-$FCFF  FRED    1 MHz bus expansion  (Revs: expect no traffic)
     $FD00-$FDFF  JIM     1 MHz bus paged      (Revs: expect no traffic)
     $FE00-$FEFF  SHEILA  the real hardware:
                    $FE00-$FE07  6845 CRTC        (address/data pair)
                    $FE08-$FE0F  6850 ACIA        (serial)
                    $FE10-$FE1F  serial ULA
                    $FE20-$FE2F  Video ULA        (control $FE20, palette $FE21)
                    $FE30-$FE3F  ROM select latch
                    $FE40-$FE5F  System VIA       (keyboard, sound via SN76489,
                                                   ADC start, vsync/timer IRQs)
                    $FE60-$FE7F  User VIA         (printer, user port)
                    $FE80-$FE9F  1770/8271 FDC    (disc)
                    $FEC0-$FEDF  uPD7002 ADC      (ANALOGUE JOYSTICK — Revs steering)
                    $FEE0-$FEFF  Tube

   OS vector page ($0200-$0235: BRKV, IRQ1V, IRQ2V, EVNTV and the OS entry
   vectors) stays in mem[]; the platform is NOTIFIED of writes there so it can
   react immediately when the game claims a vector — the analogue of the Atari
   port's VVBLKI/VDSLST shadow tracking.  See docs/bbc-hardware.md.

   ⚠ The MOS entry points at $FFCE-$FFF7 (OSBYTE/OSWORD/OSRDCH/OSFILE/OSFIND/
   OSBGET/OSARGS/OSGBPB) are NOT bus addresses — they are JSR targets into ROM.
   Those are intercepted as MOS calls in the transpiler / native layer, not here.
   See docs/bbc-hardware.md §MOS calls.
*/

#include "cpu.h"
#include "../platform/platform_c.h"
#include "../platform/framebuffer_dirty.h"

/* SHEILA and the two 1 MHz bus pages.  One range test covers all three: the
   BBC's whole I/O window is the contiguous $FC00-$FEFF. */
#define BBC_IO_LO 0xFC00u
#define BBC_IO_HI 0xFF00u

static inline uint8_t bus_read(uint16_t addr) {
    if (addr >= BBC_IO_LO && addr < BBC_IO_HI)
        return platform_hw_read(addr);
    return mem[addr];
}

/* ⭐ THE INK WATCH (host, opt-in, `make INK_WATCH=1`) — which C routine wrote THIS byte?
   The frame-buffer differential says a cell is wrong; it never says who wrote it, and the
   plotters reach the screen through the INDIRECT modes, which means through here.  So this is
   the one choke point that can name the writer.  Compile-time gated: bus_write is the hottest
   function in the build and the normal path must not grow a test.  `make fbwrites` is the same
   question asked of a real BBC (per-PC); this is its host-side twin (per-C-routine). */
#ifdef REVS_INK_WATCH
#ifdef __cplusplus
extern "C"
#endif
void revs_ink_watch(uint16_t addr, uint8_t val);
#endif

static inline void bus_write(uint16_t addr, uint8_t val) {
    if (addr >= BBC_IO_LO && addr < BBC_IO_HI) {
        platform_hw_write(addr, val);
        return;
    }
#ifdef REVS_PLATFORM_AMIGA
    /* Indirect 6502 stores are the one generated write form whose target is not
       statically knowable. Catch framebuffer writes here; ordinary RAM stays a
       single store in revs_fb_store_maybe's fall-through arm. */
    revs_fb_store_maybe(addr, val);
#else
    mem[addr] = val;
#endif
#ifdef REVS_INK_WATCH
    revs_ink_watch(addr, val);
#endif
#ifndef REVS_PLATFORM_AMIGA
    /* Notify the platform about OS-vector writes so it can react immediately
       (e.g. the game claiming IRQ1V/EVNTV).  On the Amiga backend the copper owns
       the display and the VBI reads the vector cells straight from mem[], so this
       notify would be a C-bridge + virtual dispatch to an empty base method on
       every page-2 write — pure overhead.  Compile it out there; the host/validate
       build keeps it.  (The equivalent test was measurable on the Atari port —
       see docs/perf-method.md.) */
    if (addr >= 0x0200u && addr < 0x0300u)
        platform_shadow_write(addr, val);
#endif
}

/* Helpers for zero-page direct access (no hardware routing needed). */
static inline uint8_t  zp_read(uint8_t addr)               { return mem[addr]; }
static inline void     zp_write(uint8_t addr, uint8_t val) { mem[addr] = val; }

#endif /* BUS_H */
