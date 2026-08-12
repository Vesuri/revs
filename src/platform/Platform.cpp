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

uint8_t Platform::hwRead(uint16_t)           { return 0x00; }
void    Platform::hwWrite(uint16_t, uint8_t) {}
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
    fprintf(stderr, "\nBRK at $%04X — the 6502 trapped through BRKV.  If this is one of the\n"
                    "  $7Bxx targets, the engine called into a page nothing ever loads;\n"
                    "  docs/static-map.md Open items has the evidence so far.\n", pc);
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
