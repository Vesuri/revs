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

/* An MOS call's register file: the A/X/Y the 6502 hands the OS and gets back,
   plus carry.  ⭐ This is the WHOLE of what the dispatcher reads or writes — it
   never touches N/Z/V — so threading this struct through the boundary lets
   mos.cpp be entirely free of the global cpu struct.  See docs/bbc-hardware.md
   §MOS calls and docs/cpu-elimination-audit.md §the MOS boundary. */
typedef struct { uint8_t a, x, y, c; } MosRegs;

/* Service an intercepted MOS entry ($FFCE-$FFF7): OSBYTE, OSWORD, OSRDCH, …
   Two ways in, ONE dispatcher (Platform::mosCall):
     - platform_mos_call marshals the global cpu struct in and out.  This is the
       form the GENERATED corpus uses (the transpiler emits `platform_mos_call(entry)`
       for every JSR into the MOS block), so its signature must not change.
     - platform_mos_call_typed passes the register file explicitly and returns the
       exit file.  This is the form the native twins' typed wrappers use, so a twin
       need not touch cpu to make an OS call.
   See docs/bbc-hardware.md §MOS calls. */
void    platform_mos_call(uint16_t entry);
MosRegs platform_mos_call_typed(uint16_t entry, MosRegs in);

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

/* A merged loop region was entered at an address its dispatch switch does not cover.
   A region is a set of 6502 segments that form a control-flow cycle, emitted as ONE C
   function so the cycle is a goto loop rather than unbounded mutual recursion
   (tools/transpile.py build_regions, docs/static-map.md §Open items 9).  The switch is
   generated from exactly the region's entry set and the only callers are the generated
   thin wrappers, so this is unreachable by construction — which is precisely why it
   reports instead of falling through: "unreachable by construction" is the assumption
   this project keeps being wrong about. */
void    platform_bad_region_entry(uint16_t region, uint16_t entry);

/* Counters for the above, readable from gdb (see amiga/PROBE_SYMS). */
extern unsigned long g_badRegionCount;
extern uint16_t      g_badRegionEntry;

/* ⭐ THE ENGINE→TRACK-FILE SEAM.  A per-circuit extent (SMC kind 'extent') holds a JSR or JMP
   whose target lands in $5300-$5A25 — the window the unpack swap fills with the selected
   circuit's own file.  The SAME address is a DIFFERENT routine per circuit, so the engine's C
   cannot resolve it: it hands the address here and this owns the per-circuit map.
   Implemented in src/platform/track.c.  docs/phases.md §5, src/platform/track.h. */
void    revs_track_hook(uint16_t addr);

/* Hook calls that had no body for the selected circuit — see revs_track_hook().  ⚠ A missing
   body must be a NUMBER, not a shrug: running on past it would be the engine executing
   Silverstone's control flow over another circuit's geometry. */
extern unsigned long g_trackHookMissing;
extern uint16_t      g_trackHookMissingAddr;

/* ⭐ …and the calls that DID dispatch.  "The circuit installed and nothing crashed" is entirely
   compatible with the hooks never being reached — which is precisely what an expansion circuit
   silently running Silverstone's control flow would look like.  This is the number that says the
   per-circuit code executes; on an expansion circuit it must be > 0. */
extern unsigned long g_trackHookCalls;

/* The interrupt register contract (see platform_cbridge.cpp): A, X and Y must come back out of
   irq1v_band_schedule unchanged, as they do on a real BBC.  Non-zero here means a foreground routine
   can be resumed with a corrupted register — which surfaces as a drawing artefact somewhere
   far away, not as a crash. */
extern unsigned long g_irqClobberCount;
extern uint8_t       g_irqClobberWhich;

/* The same contract for the C-only state: the two-level-RTS flag (which is the 6502's S, and
   therefore interrupt-saved on real hardware) and the 6502 stack pointer.  Pending is expected to
   be non-zero — it only says interrupts do land inside the drop window; Touched and Imbalance must
   both stay 0. */
/* $FE68 (User VIA T2 counter-low), the game's only entropy source — bbc_hw.cpp.  Exported
   because a constant answer and a live clock produce the same picture, which is how the
   constant-$0 model survived for months: the counter and the last value make the model
   checkable from a probe (`amiga/dash_state.gdb`, REVS_DASH_WATCH on the host). */
extern unsigned long g_viaT2Reads;
extern uint8_t       g_viaT2Last;

extern unsigned long g_irqUnwindPending;
extern unsigned long g_irqUnwindTouched;
extern unsigned long g_irqStackImbalance;

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
