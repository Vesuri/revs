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
