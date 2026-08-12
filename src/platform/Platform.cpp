#include "platform.h"
#if !defined(REVS_PLATFORM_AMIGA)
#include <stdio.h>    /* host only: the Amiga build is freestanding (no stdio/stdlib) */
#include <stdlib.h>
#endif

extern volatile uint8_t mem[65536];

Platform* platform = nullptr;

/* ⚠ The singleton is claimed HERE, in the base constructor, not in each backend's — the
   C bridge (platform_cbridge.cpp) and the scene both reach the platform through it, so a
   backend that forgot to set it produced a null-deref crash AFTER a clean takeover: the
   VBI counter kept ticking while nothing rendered, which reads like a render bug rather
   than a null pointer.  (Cost one headless round trip during scaffolding.) */
Platform::Platform() : quit(false) { platform = this; }
Platform::~Platform() { if (platform == this) platform = 0; }

/* hwRead/hwWrite live in bbc_hw.cpp; mosCall and the input hooks in mos.cpp. */
void    Platform::shadowWrite(uint16_t, uint8_t) {}

/* ---------------------------------------------------------------------------
   Self-modifying-code escape hatch.

   The transpiler emits each of the 24 patched instructions as a runtime dispatch over
   the values its writers are known to store (tools/transpile.py SMC_SITES).  Reaching
   this function means a writer stored something the evidence does not cover — so the
   generated C has no faithful continuation, and the honest response is to say so at the
   moment it happens rather than to pick a branch.

   The three globals are the Amiga read-out: the freestanding build has no stderr, so
   diag_run.sh / gdb read them by name (they are listed in PROBE_SYMS so --gc-sections
   cannot drop them and leave gdb printing instruction bytes as a value — the exact trap
   docs/method-lessons.md records).  The host build aborts, because a validation run that
   continues past this has already stopped comparing what it thinks it is comparing.
   --------------------------------------------------------------------------- */
volatile uint16_t      g_smcSite    = 0;
volatile uint16_t      g_smcValue   = 0;
volatile unsigned long g_smcUnhandled = 0;

volatile uint16_t      g_brkPC   = 0;
volatile unsigned long g_brkCount = 0;

void Platform::brk(uint16_t pc) {
    g_brkPC = pc;
    g_brkCount++;
#if !defined(REVS_PLATFORM_AMIGA)
    /* ⚠ ABORT IS THE DEFAULT AND MUST STAY THE DEFAULT.  A run that continues past a BRK
       has stopped executing the program it thinks it is executing.

       The one opt-out, REVS_BRK_CONTINUE=1, exists because Phase 4 turned this trap from a
       hypothetical into a routine event: three of the seven $7Bxx calls are in the engine's
       MAIN LOOP ($1704, $1739, $1748) and fire EVERY FRAME, so aborting on the first one
       stops the discovery run at the same instruction forever and hides everything behind
       it.  The opt-out treats the call as a no-op — which is what the Amiga backend already
       does, counting rather than aborting — and says so on the way past.  Never quote
       behaviour, and never quote a measurement, from a run with this set without saying it
       was set: the $7Bxx routines' real cost and real effect are both missing from it. */
    static int continueOnBrk = -1;
    if (continueOnBrk < 0) {
        const char* e = getenv("REVS_BRK_CONTINUE");
        continueOnBrk = (e && e[0] && e[0] != '0') ? 1 : 0;
    }
    static unsigned long reported = 0;
    if (continueOnBrk) {
        if (reported < 8)
            fprintf(stderr, "BRK at $%04X — TREATED AS A NO-OP (REVS_BRK_CONTINUE=1). "
                            "This run is not faithful.\n", pc);
        else if (reported == 8)
            fprintf(stderr, "BRK: further reports suppressed; read g_brkCount at exit.\n");
        reported++;
        return;
    }
    fprintf(stderr, "\nBRK at $%04X — the 6502 trapped through BRKV.  If this is one of the\n"
                    "  $7Bxx targets, the engine called into a page nothing ever loads;\n"
                    "  docs/static-map.md Open items has the evidence so far.\n"
                    "  Set REVS_BRK_CONTINUE=1 to no-op them for a discovery run.\n", pc);
    abort();
#endif
}

void Platform::smcUnhandled(uint16_t site, uint16_t value) {
    g_smcSite  = site;
    g_smcValue = value;
    g_smcUnhandled++;
#if !defined(REVS_PLATFORM_AMIGA)
    fprintf(stderr, "\nSMC UNHANDLED: site $%04X holds $%04X — no emitted form covers it.\n"
                    "  tools/transpile.py SMC_SITES needs this value, with evidence for it\n"
                    "  (which writer stores it).  docs/transpile.md / docs/static-map.md.\n",
            site, value);
    abort();
#endif
}
