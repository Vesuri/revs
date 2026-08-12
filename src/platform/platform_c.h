#ifndef PLATFORM_C_H
#define PLATFORM_C_H
/* C-compatible bridge header — included by src/cpu/bus.h so the C-compiled 6502
   transliteration can reach hardware emulation.  Implemented in
   platform_cbridge.cpp, which forwards to the C++ Platform singleton. */
#if !defined(REVS_PLATFORM_AMIGA)
#include <stdint.h>   /* The Amiga C++ build gets the integer types from the
                         force-included framework/SASCCompat.h; its compat
                         <stdint.h> would clash. */
#endif

#ifdef __cplusplus
extern "C" {
#endif

uint8_t platform_hw_read (uint16_t addr);
void    platform_hw_write(uint16_t addr, uint8_t val);
void    platform_shadow_write(uint16_t addr, uint8_t val);
int     platform_load_image(const char* path);

/* Register a BBC address → C function mapping for interrupt dispatch (the
   game's own IRQ1V / EVNTV handler bodies). */
void platform_register_vbi(uint16_t addr, void (*fn)(void));

/* Runtime indirect JMP dispatch — for the JMP ($xx) vector patterns.  Looks up
   addr in the handler table and calls the match; a 0/unknown addr is a no-op. */
void platform_indirect_jmp(uint16_t addr);

/* Service an intercepted MOS entry ($FFCE-$FFF7): OSBYTE, OSWORD, OSRDCH, …
   A/X/Y are passed and returned through the global cpu struct.
   See docs/bbc-hardware.md §MOS calls. */
void platform_mos_call(uint16_t entry);

/* A BRK was executed at `pc`.  On the BBC this is a software interrupt, not a no-op: it
   vectors through BRKV ($0202) into the MOS error handler and does NOT return to the
   following instruction.  Revs contains four routines that are a single $00 byte, called
   from seven sites (docs/static-map.md §Open items) — reaching one means the engine
   called into memory that holds no code, so this reports rather than returning quietly. */
void    platform_brk(uint16_t pc);

/* A self-modifying instruction was reached holding a value the transpiler's SMC_SITES
   table does not cover — an opcode slot with an unlisted byte, or a patched branch offset
   pointing outside the enclosing routine's instruction starts.  This is NOT a recoverable
   condition: the emitted C cannot represent what the 6502 would now execute, so the
   platform reports it loudly (and the host build aborts).  A silent no-op here would look
   exactly like a working rasteriser that draws nothing.  docs/transpiler.md §SMC. */
void    platform_smc_unhandled(uint16_t site, uint16_t value);

/* Present the current display state if a new frame has been produced since the
   last call.  Safe to call from a spin-wait — returns immediately if none is
   pending. */
void platform_render_frame(void);

/* Pump the platform event loop without rendering.  Call from any spin-wait so
   the host OS doesn't mark the window unresponsive. */
void platform_poll_events(void);

/* Fire a vsync tick if enough time has accumulated.  Call ONLY from spin-waits
   that own a whole frame boundary — never from a raster-position wait, which the
   tick resets, so the wait could never exit. */
void platform_tick_vbi(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_C_H */
