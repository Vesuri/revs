/* PlatformHost — headless development backend.  See PlatformHost.h for why it has no
   renderer. */
#include "PlatformHost.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "../../gen/revs_decl.h"     /* the transpiled 6502 routines, incl. engine_main */
}

extern "C" volatile uint8_t mem[65536];

PlatformHost::PlatformHost(const char* imagePath) : vbi(0), frames(0), traceKeys(false)
{
    const char* t = std::getenv("REVS_TRACE_KEYS");
    traceKeys = (t && t[0] && t[0] != '0');

    if (loadImage(imagePath) != 0) {
        std::fprintf(stderr, "PlatformHost: cannot load memory image '%s'\n",
                     imagePath ? imagePath : "(null)");
        std::fprintf(stderr, "  build it first:  python3 tools/ssd_load.py revs.ssd disasm\n");
        quit = true;
    }
}

PlatformHost::~PlatformHost() {}   /* the base dtor releases the singleton */

int PlatformHost::loadImage(const char* path)
{
    if (!path) return -1;
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return -1;
    unsigned char buf[65536];
    std::size_t n = std::fread(buf, 1, sizeof(buf), f);
    std::fclose(f);
    for (std::size_t i = 0; i < n; i++) mem[i] = buf[i];
    return 0;
}

void PlatformHost::setInterrupt(void (*fn)(void)) { vbi = fn; }
int  PlatformHost::framesPerSecond()              { return 50; }

void PlatformHost::renderFrame()
{
    /* No display.  Just count: this is the hook at the top of the engine's main loop
       ($1701), so `frames` is a game-frame counter and nothing else. */
    frames++;
}

void PlatformHost::tickVBI()
{
    /* ⚠ THE HOST HAS NO PREEMPTION, and that is the one structural way it differs from
       the target.  On the Amiga the game's 50 Hz body runs in the real VERTB ISR and
       interrupts the main loop wherever it is; here the only way it can run is if the
       transliterated code calls out to us.  So the main loop's frame wait ($1760, the
       single SPINWAIT_HOOK) drives it explicitly.

       That makes the host's interrupt phase relative to the foreground DETERMINISTIC and
       different from the machine's.  Which is fine for what this build is for — finding
       stalls and traps in seconds — and is exactly why PlatformHost.h says it is never
       evidence.  Anything that depends on interrupt phase gets measured on the target.

       One call = one full raster-band cycle = one 50 Hz tick, the same rule Revs::vbi()
       follows on the Amiga; see the comment there for why it is a cycle and not a band. */
    for (int band = 0; band < 8; band++) {
        fireIrq1v();
        if (mem[0x4F43] == 0) break;      // $4F43 = irq_band_state; 0 = cycle complete
    }
}

bool PlatformHost::keyDown(uint8_t x)
{
    bool held = autoRun.keyDown(x);
    if (traceKeys)
        std::printf("inkey %5lu step %2u  X=$%02X (-%u) -> %s\n",
                    autoRun.polls(), autoRun.stepIndex(), x, 256u - x, held ? "HELD" : ".");
    return held;
}

void PlatformHost::run()
{
    /* The genuine entry chain — the SAME call the Amiga backend makes, so host and target
       run identical game code and only the platform differs.

       ⚠ This is a DISCOVERY loop, never ground truth (see PlatformHost.h).  What it is
       good for in Phase 4 is finding stalls and traps in seconds rather than in
       FS-UAE round trips; every finding is then confirmed on the target.

       $63BD is the unpack stub's closing JMP target, read out of $79AB — not $1200,
       which is the loader stub and overwrites itself (docs/static-map.md). */
    std::printf("PlatformHost: entering engine_main ($63BD)\n");
    std::fflush(stdout);
    engine_main();
    std::printf("PlatformHost: engine_main returned after %lu frames\n", frames);
}
