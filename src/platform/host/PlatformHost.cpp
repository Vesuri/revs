/* PlatformHost — headless development backend.  See PlatformHost.h for why it has no
   renderer. */
#include "PlatformHost.h"
#include "../bbc_screen.h"
#include "../platform_c.h"   /* g_irqClobberCount/Which — the interrupt register contract */
#include "../track.h"       /* circuit selection — the model and the refusal contract */

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

    dumpPath = std::getenv("REVS_SCREEN_DUMP");
    const char* df = std::getenv("REVS_SCREEN_FRAME");
    dumpFrame = (df && df[0]) ? (unsigned long)std::strtoul(df, 0, 0) : 400ul;
    /* REVS_SCREEN_COUNT > 1 dumps that many CONSECUTIVE frames as <path>.<frame>.
       One frame per process is far too slow to hunt an artefact that comes and goes: the
       engine has to be re-driven from the disc image every time, and a run to frame 400
       costs minutes.  Consecutive frames also make "present in most frames" checkable,
       which a single frame cannot be. */
    const char* dc = std::getenv("REVS_SCREEN_COUNT");
    dumpCount = (dc && dc[0]) ? (unsigned long)std::strtoul(dc, 0, 0) : 1ul;
    if (dumpCount < 1) dumpCount = 1;

    if (loadImage(imagePath) != 0) {
        std::fprintf(stderr, "PlatformHost: cannot load memory image '%s'\n",
                     imagePath ? imagePath : "(null)");
        std::fprintf(stderr, "  build it first:  python3 tools/ssd_load.py revs.ssd disasm\n");
        quit = true;
        return;
    }

    /* ⭐ CIRCUIT SELECTION, and it must be HERE: after the boot image and before any engine code
       reads $5300-$5A25.  src/platform/track.h is the model.  $REVS_TRACK overrides the build
       default on the host only — the target has no environment, so there it is `make TRACK=n`. */
    {
        const char* tr = std::getenv("REVS_TRACK");
        unsigned char want = tr && tr[0] ? (unsigned char)std::strtoul(tr, 0, 0)
                                         : (unsigned char)REVS_TRACK_DEFAULT;
        int ok = (want < REVS_TRACK_COUNT) && revs_track_install(want);
        if (!ok) {
            /* ⚠ Say WHICH of the two reasons, and never continue with the requested circuit's
               geometry under Silverstone's code — install() refuses that, and this reports it. */
            if (want >= REVS_TRACK_COUNT)
                std::fprintf(stderr, "PlatformHost: no circuit %u (this build has %d)\n",
                             want, REVS_TRACK_COUNT);
            else
                /* ⚠ TWO reasons, reported separately: patch bytes the transliteration does not
                   read, and hook bodies this build does not have.  One number for both would have
                   read identically before and after the SMC work landed (docs/phases.md §5b). */
                std::fprintf(stderr, "PlatformHost: circuit %u (%s) REFUSED — %u patch bytes with "
                             "no SMC site (first $%04X), %u hook bodies unbuilt (first $%04X); "
                             "see src/platform/track.h\n",
                             want, revs_tracks[want].name, g_trackUnhonoured,
                             g_trackUnhonouredAddr, g_trackHooksUnbuilt, g_trackHooksUnbuiltAddr);
            revs_track_install(0);
        }
        std::fprintf(stderr, "PlatformHost: circuit %u = %s\n",
                     g_trackInstalled, revs_tracks[g_trackInstalled].name);
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

    /* ⭐ THE 50 Hz BODY MUST RUN EVEN WHEN THE MAIN LOOP DOES NOT WAIT FOR IT.
       The only host driver for the band cycle used to be the frame-wait hook at $1760 —
       but the main loop reaches $1760 only when $62F6 is non-zero ($1753: `LDA $62F6 /
       BEQ $178F`), and on the other arm it loops straight back to $1701.  Measured: in
       the driving loop the wait is skipped, so the host ran the foreground with NO
       interrupt body at all — no palette bands, no $52A4 — while everything visible still
       looked healthy.  On a BBC the User VIA fires regardless of what the foreground is
       doing, so the honest host model is one band cycle per game frame; the $1760 hook
       then still drives the spin when the game does use it.
       (The Amiga backend is unaffected: there the body is the real VERTB ISR.) */
    if (!tickedThisFrame) tickVBI();
    tickedThisFrame = false;

    /* ⭐ THE STRIPE DETECTOR.  REVS_STRIPE_WATCH reports any band-2 line whose zero run
       reaches the right edge — the signature of the horizon stripes: the fill stopped
       part-way and everything to its right stayed black.
       ⚠ The criterion is calibrated against a real BBC, not invented: `make refloop` shows
       band 2 (display lines 81-100, the only window where pen 0 is black) holding at most a
       3-cell zero run on any sampled frame, so a run of 8 or more reaching cell 39 is an
       artefact and not the road.  Dumping frames and eyeballing them cannot answer "does
       this EVER happen on the host?", which is the question that decides whether the bug can
       be debugged here at all or only on the target. */
    if (std::getenv("REVS_STRIPE_WATCH")) {
        static unsigned long lastClobber = 0;
        if (g_irqClobberCount != lastClobber) {
            lastClobber = g_irqClobberCount;
            std::printf("IRQ-CLOBBER frame %lu: count=%lu which=%s%s%s\n", frames,
                        g_irqClobberCount,
                        (g_irqClobberWhich & 1) ? "A" : "", (g_irqClobberWhich & 2) ? "X" : "",
                        (g_irqClobberWhich & 4) ? "Y" : "");
            std::fflush(stdout);
        }
        for (unsigned y = 81; y <= 101; y++) {
            const unsigned row = y / BBC_SCREEN_LINES, line = y % BBC_SCREEN_LINES;
            unsigned run = 0;
            for (unsigned c = 0; c < BBC_SCREEN_CELLS; c++) {
                const unsigned off = row * BBC_SCREEN_BPR + c * BBC_SCREEN_LINES + line;
                if (mem[BBC_SCREEN_BASE + off] == 0) run++; else run = 0;
            }
            if (run >= 8) {
                std::printf("STRIPE frame %lu line %u: zero run of %u cells to the right edge\n",
                            frames, y, run);
                std::fflush(stdout);
            }
        }
    }

    if (dumpPath && frames >= dumpFrame && frames < dumpFrame + dumpCount) {
        char path[512];
        if (dumpCount > 1) std::snprintf(path, sizeof path, "%s.%lu", dumpPath, frames);
        else               std::snprintf(path, sizeof path, "%s", dumpPath);
        std::FILE* f = std::fopen(path, "wb");
        if (f) {
            for (unsigned i = 0; i < BBC_SCREEN_BYTES; i++)
                std::fputc(mem[BBC_SCREEN_BASE + i], f);
            std::fclose(f);
            /* ⭐ The circuit's own hook traffic goes out WITH the dump, unbuffered.  "The
               expansion circuit installed and did not crash" is compatible with its hooks never
               being reached, i.e. with the engine quietly running Silverstone's control flow over
               another circuit's geometry — and that is the failure this whole seam exists to
               prevent, so the dump a comparison is made from must carry the proof beside it.
               ⚠ stderr, not stdout: a run killed by a timeout loses buffered stdout, which is how
               an earlier measurement came back empty and read as "the code never ran". */
            std::fprintf(stderr, "PlatformHost: screen dump frame %lu -> %s  "
                         "(circuit %u, hook calls %lu, missing %lu)\n",
                         frames, path, g_trackInstalled, g_trackHookCalls, g_trackHookMissing);
        }
        /* ⭐ REVS_QUIT_AFTER_DUMP=1 — stop once the last requested frame is written.
           Without it the host races on forever after the dump, so a scripted check has to rely
           on an external timeout to end each run, and then WAITS OUT that timeout for every
           circuit even though the measurement finished in seconds.  (That is exactly how
           `make track-run` first appeared to hang on circuit 1: it was still circuit 0, dumped
           and running.)  Not the default — an interactive or perf run wants to keep going. */
        /* ⚠ exit(), not `quit = true`: `quit` is only read before run(), and the engine's main
           loop has no return path — it is 6502 code that never ends.  There is nothing to unwind
           to, so leaving is the only way out. */
        if (frames + 1 >= dumpFrame + dumpCount && std::getenv("REVS_QUIT_AFTER_DUMP")) {
            std::fflush(0);
            std::exit(0);
        }
        /* ⭐ The WHOLE 64 KB beside the frame buffer, when asked.  A frame-buffer-only dump
           can say the picture is wrong but never why: the fill chain in the $7B00 overlay
           reads its columns from $3000-$4400 and translates through $6000, and its span ends
           are SMC opcode slots in its own code.  Dumping everything lets a host/target diff
           localise upstream-vs-downstream without guessing the regions first. */
        if (std::getenv("REVS_MEM_DUMP")) {
            char mpath[512];
            std::snprintf(mpath, sizeof mpath, "%s.mem.%lu", dumpPath, frames);
            std::FILE* mf = std::fopen(mpath, "wb");
            if (mf) {
                for (unsigned i = 0; i < 0x10000; i++) std::fputc(mem[i], mf);
                std::fclose(mf);
                std::printf("PlatformHost: mem dump frame %lu -> %s\n", frames, mpath);
            }
        }
        std::fflush(stdout);
    }
}

/* The band record beside the framebuffer dump: the same raster schedule the Amiga copper
   is built from, in a form screen_ppm.py can colour the dump with.  One line per band:
   state duration(us) ulaControl pal0..pal15.
   ⚠ Written from the END of a band cycle, not from renderFrame(): the record describes
   the cycle just dispatched, and reading it at an arbitrary point in the main loop shows
   a partial one.  That is not a detail — the first attempt read it from renderFrame() and
   saw ONE band, which reads exactly like a broken recorder. */
void PlatformHost::dumpBands()
{
    char sidecar[512];
    std::snprintf(sidecar, sizeof sidecar, "%s.bands", dumpPath);
    std::FILE* b = std::fopen(sidecar, "a");
    if (!b) return;
    std::fprintf(b, "# frame %lu anchor_us %d overflow %u horizon_latch $%02X%02X\n",
                 frames, (int)BBC_BAND0_ANCHOR_US, (unsigned)g_bandOverflow,
                 mem[0x4F20], mem[0x4F1F]);
    for (unsigned i = 0; i < g_bandCount; i++) {
        std::fprintf(b, "%02X %5u %02X", g_bandState[i], g_bandDuration[i],
                     g_bandControl[i]);
        for (unsigned c = 0; c < 16; c++)
            std::fprintf(b, " %02X", g_bandPalette[i][c]);
        std::fputc('\n', b);
    }
    std::fclose(b);
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
    tickedThisFrame = true;
    bbc_begin_band_cycle();
    for (int band = 0; band < 8; band++) {
        fireIrq1v();
        if (mem[0x4F43] == 0) break;      // $4F43 = irq_band_state; 0 = cycle complete
    }
    if (dumpPath && frames >= dumpFrame && frames < dumpFrame + 3) dumpBands();
}

bool PlatformHost::keyDown(uint8_t x)
{
    /* The host has no keyboard at all, so a script that has handed control back leaves
       every key up — which is the honest answer here, not a bug. */
    bool held = autoRun.done() ? false : autoRun.keyDown(x);
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
