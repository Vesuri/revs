/* PlatformHost — headless development backend.  See PlatformHost.h for why it has no
   renderer. */
#include "PlatformHost.h"
#include "../bbc_screen.h"
#include "../platform_c.h"   /* g_irqClobberCount/Which — the interrupt register contract */
#include "../track.h"       /* circuit selection — the model and the refusal contract */
#include "../shape.h"       /* REVS_SHAPE: the render path's input-distribution counters */
#include "../../gen/mem.h"  /* MEM_<name> offsets, generated from disasm/symbols.csv */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>

extern "C" {
#include "../../gen/revs_decl.h"     /* the transpiled 6502 routines, incl. engine_main */
}

#include "../../cpu/mem_decl.h"
extern "C" MEM_QUAL uint8_t mem[65536];

/* ⭐ THE INTERMEDIATE 64 KB SNAPSHOT — platform_mem_snapshot_at()'s host body.
   A twin calls it at the entry of a render stage; this writes all 64 KB the FIRST time the
   requested PC is reached at or after REVS_MEM_DUMP_AT_FRAME, then never again.  Its whole
   reason for existing is that the frame-boundary dump answers a different question: by the
   time a frame ends the object plotter has rewritten cells the road pass produced, so an
   edge_* diff there says nothing about what draw_road was handed.  The counterpart is
   `tools/bbc_refloop_race.mjs --mem-at=<pc>`; give both the same PC.
   ⚠ It reads its configuration once and caches the ANSWER, including "not asked for" — the
   call sits on the 50 Hz path and a getenv per frame is not free.  A build that was not asked
   is a load and a branch. */
static const char* snapAtPath   = 0;      /* where to write, or 0 */
static unsigned    snapAtPc     = 0;
static unsigned long snapAtFrame = 0;
static int         snapAtInit   = 0;
static int         snapAtDone   = 0;
static unsigned long* snapAtFrames = 0;   /* the host's own frame counter, once constructed */

extern "C" void revs_host_mem_snapshot_at(uint16_t pc)
{
    if (!snapAtInit) {
        snapAtInit = 1;
        const char* w = std::getenv("REVS_MEM_DUMP_AT");
        const char* d = std::getenv("REVS_SCREEN_DUMP");
        if (w && w[0] && d && d[0]) {
            snapAtPc   = (unsigned)std::strtoul(w, 0, 16);
            snapAtPath = d;
            const char* f = std::getenv("REVS_MEM_DUMP_AT_FRAME");
            snapAtFrame = (f && f[0]) ? (unsigned long)std::strtoul(f, 0, 0) : 40ul;
        }
    }
    if (!snapAtPath || snapAtDone || pc != snapAtPc) return;
    if (snapAtFrames && *snapAtFrames < snapAtFrame) return;

    char path[512];
    std::snprintf(path, sizeof path, "%s.memat.%04x", snapAtPath, (unsigned)pc);
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return;
    for (unsigned i = 0; i < 0x10000; i++) std::fputc(mem[i], f);
    std::fclose(f);
    snapAtDone = 1;
    std::fprintf(stderr, "PlatformHost: mem snapshot at $%04X (frame %lu) -> %s\n",
                 (unsigned)pc, snapAtFrames ? *snapAtFrames : 0ul, path);
}

PlatformHost::PlatformHost(const char* imagePath) : vbi(0), frames(0), traceKeys(false)
{
    const char* t = std::getenv("REVS_TRACE_KEYS");
    traceKeys = (t && t[0] && t[0] != '0');

    snapAtFrames = &frames;   /* the snapshot hook's frame gate reads the live counter */
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

    /* ⭐ SHAPE builds: the per-paint frame-buffer delta, at the same point the Amiga takes it
       (immediately before the decode it prices).  See src/platform/shape.h. */
    PROBE_SHAPE_FRAME();

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

    /* ⭐ THE DASHBOARD STATE, on the same bytes `make refloop --park` prints and
       amiga/dash_state.gdb reads — REVS_DASH_WATCH=N logs it every N frames.  The three
       machines have to be comparable on the SAME addresses for a claim like "the port's
       engine never stalls" to be checkable at all, and a single late sample cannot tell
       "never started" from "started and stalled": that needs the series. */
    if (const char* dw = std::getenv("REVS_DASH_WATCH")) {
        unsigned long every = std::strtoul(dw, 0, 0); if (!every) every = 25;
        if (frames % every == 0)
            std::printf("DASH frame %lu: $61=%02X $3C=%02X $63=%02X $40=%02X $3E=%02X "
                        "$3F=%02X $2D=%02X $09=%02X  script step %u done=%d "
                        "(FE68 reads %lu, last $%02X)\n",
                        frames, mem[MEM_engine_running], mem[MEM_engine_revs],
                        mem[MEM_road_speed], mem[MEM_gear_index], mem[0x3e],
                        mem[0x3f], mem[0x2d], mem[0x09], autoRun.stepIndex(),
                        (int)autoRun.done(), g_viaT2Reads, g_viaT2Last);
    }

#ifdef REVS_SHAPE
    /* ⭐ THE SHAPE OF THE DASHBOARD SWEEP — src/platform/shape.h has what it means.
       REVS_SHAPE_WATCH=N prints every N frames.  ⚠ The MEAN is printed beside the LAST sweep
       and the histogram on purpose: "18 of 40 columns dirty on average" is compatible with
       "always 18" and with "clean most frames, all 40 occasionally", and those two size the
       dirty-flag and sprite items completely differently. */
    if (const char* sw = std::getenv("REVS_SHAPE_WATCH")) {
        unsigned long every = std::strtoul(sw, 0, 0); if (!every) every = 50;
        if (frames && frames % every == 0 && g_shapeDashCalls) {
            const unsigned long n = g_shapeDashCalls;
            std::printf("SHAPE frame %lu: sweeps=%lu  dirty/sweep=%lu.%02lu of 1440  "
                        "consumed=%lu.%02lu  cols=%lu.%02lu of 40  tests/sweep=%lu  "
                        "last(dirty=%u left=%u cols=%u tests=%u)\n",
                        frames, n,
                        g_shapeDashDirty / n, (g_shapeDashDirty * 100 / n) % 100,
                        (g_shapeDashDirty - g_shapeDashLeft) / n,
                        ((g_shapeDashDirty - g_shapeDashLeft) * 100 / n) % 100,
                        g_shapeDashCols / n, (g_shapeDashCols * 100 / n) % 100,
                        g_shapeDashUnits / n,
                        g_shapeDashLastDirty, g_shapeDashLastLeft, g_shapeDashLastCols,
                        g_shapeDashLastUnits);
            std::printf("SHAPE   col-count histogram (buckets of 4):");
            for (unsigned i = 0; i < 11; i++) std::printf(" %lu", g_shapeDashColHist[i]);
            std::printf("\nSHAPE   per-column dirty sweeps:");
            for (unsigned i = 0; i < 40; i++) std::printf(" %lu", g_shapeDashPerCol[i]);
            if (g_shapeRoadCalls)
                std::printf("SHAPE   road pass ($1A20): %lu calls, %lu of 8320 frame-buffer bytes "
                            "per call over %lu lines  last(%u bytes, %u lines, %u..%u)\n",
                            g_shapeRoadCalls, g_shapeRoadBytes / g_shapeRoadCalls,
                            g_shapeRoadLines / g_shapeRoadCalls, g_shapeRoadLastBytes,
                            g_shapeRoadLastLines, g_shapeRoadFirstLine, g_shapeRoadLastLine);
            if (g_shapeFrameCalls)
                std::printf("SHAPE   per-paint DELTA: %lu of 8320 bytes changed per painted frame "
                            "(max %u, last %u over %u lines, %u..%u)\n",
                            g_shapeFrameBytes / g_shapeFrameCalls, g_shapeFrameMax,
                            g_shapeFrameLast, g_shapeFrameLines,
                            g_shapeFrameFirstLine, g_shapeFrameLastLine);
            std::printf("SHAPE   frame-buffer bytes WRITTEN per main-loop phase "
                        "(phase: bytes/frame, lines):\n");
            for (unsigned i = 0; i < 40; i++) {
                if (!g_shapePhaseBytes[i]) continue;
                std::printf("SHAPE     phase %2u: %6lu bytes/frame  lines %u..%u  (%lu frames)\n",
                            i, g_shapePhaseBytes[i] / frames, g_shapePhaseFirst[i],
                            g_shapePhaseLast[i], g_shapePhaseFrames[i]);
            }
            /* ⭐⭐ THE PER-LINE CENSUS (src/platform/shape.h) — the three counts that decide
               whether a producer-maintained dirty flag can delete part of the scan. */
            if (g_shapeLineSweeps) {
                const unsigned long n = g_shapeLineSweeps, u = g_shapeLineUnits;
                std::printf("SHAPE   PER-LINE CENSUS over %lu sweeps: %lu lines painted per "
                            "sweep, %lu units\n"
                            "SHAPE     REDUNDANT (changed no byte): %lu.%02lu lines/sweep, "
                            "%lu of %lu units = %lu%% of the scan\n"
                            "SHAPE     CLEAN SOURCES (a producer flag could see this): "
                            "%lu.%02lu lines/sweep, %lu units = %lu%%\n"
                            "SHAPE     disagreement: clean-but-changed %lu, "
                            "dirty-but-unchanged %lu (lines, summed)\n",
                            n, g_shapeLineVisited / n, u / n,
                            g_shapeLineRedundant / n, (g_shapeLineRedundant * 100 / n) % 100,
                            g_shapeLineUnitsRedundant, u,
                            u ? g_shapeLineUnitsRedundant * 100 / u : 0,
                            g_shapeLineCleanSrc / n, (g_shapeLineCleanSrc * 100 / n) % 100,
                            g_shapeLineUnitsCleanSrc,
                            u ? g_shapeLineUnitsCleanSrc * 100 / u : 0,
                            g_shapeLineCleanButChanged, g_shapeLineDirtyNoChange);
                {
                    extern volatile unsigned long g_shapeCleanChangedBgMoved;
                    extern volatile unsigned long g_shapeCleanChangedBgSame;
                    std::printf("SHAPE     ...of the clean-but-changed: %lu had a MOVED "
                                "background byte, %lu did not\n",
                                g_shapeCleanChangedBgMoved, g_shapeCleanChangedBgSame);
                    {
                        extern volatile unsigned long
                            g_shapeCleanChangedBgSamePerLine[128];
                        std::printf("SHAPE     ...and the unexplained ones, per line:");
                        for (unsigned x = 0x03; x <= 0x4F; x++)
                            if (g_shapeCleanChangedBgSamePerLine[x])
                                std::printf(" $%02X:%lu", x,
                                            g_shapeCleanChangedBgSamePerLine[x]);
                        std::printf("\n");
                    }
                    {
                        extern volatile unsigned long g_shapeSkippablePredicate;
                        extern volatile unsigned long g_shapeSkippableUnits;
                        extern volatile unsigned long g_shapeSkippableWrong;
                        std::printf("SHAPE     THE 3-PART SKIP (clean sources + unmoved "
                                    "background + last paint flat): %lu lines, %lu units = "
                                    "%lu%% of the scan, %lu of them WRONG\n",
                                    g_shapeSkippablePredicate, g_shapeSkippableUnits,
                                    u ? g_shapeSkippableUnits * 100 / u : 0,
                                    g_shapeSkippableWrong);
                    }
                    {
                        extern volatile unsigned long g_shapeMarkWritten;
                        extern volatile unsigned long g_shapeMarkMarked;
                        extern volatile unsigned long g_shapeMarkUnmarked;
                        extern volatile unsigned long g_shapeMarkOver;
                        extern volatile unsigned long g_shapeMarkPerUnmarked[128];
                        std::printf("SHAPE     MARKING COMPLETENESS: %lu line-writes measured, "
                                    "%lu marked, %lu WRITTEN-BUT-UNMARKED (must be 0), "
                                    "%lu marked-but-clean\n",
                                    g_shapeMarkWritten, g_shapeMarkMarked,
                                    g_shapeMarkUnmarked, g_shapeMarkOver);
                        if (g_shapeMarkUnmarked) {
                            std::printf("SHAPE       unmarked by line:");
                            for (unsigned x = 0; x < 128; x++)
                                if (g_shapeMarkPerUnmarked[x])
                                    std::printf(" $%02X=%lu", x, g_shapeMarkPerUnmarked[x]);
                            std::printf("\n");
                        }
                    }
                }
                std::printf("SHAPE     per line $03..$4F  "
                            "visits/redundant/units-per-visit/clean-but-changed:\n");
                for (unsigned x = 0x03; x <= 0x4F; x++) {
                    if (!g_shapeLinePerVisit[x]) continue;
                    std::printf("SHAPE       line $%02X  %5lu %5lu  %3lu  %5lu\n", x,
                                g_shapeLinePerVisit[x], g_shapeLinePerRedundant[x],
                                g_shapeLinePerUnits[x] / g_shapeLinePerVisit[x],
                                g_shapeLinePerCleanChanged[x]);
                }
            }
            std::printf("SHAPE   per-row ($2C..$4F) dirty sweeps:");
            for (unsigned i = 0; i < 36; i++) std::printf(" %lu", g_shapeDashPerRow[i]);
            std::printf("\n");
            std::fflush(stdout);
        }
    }
#endif

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
            /* ⭐ The band-record reuse PRINTS ITS OWN STATE, and that is not a nicety: the
               first sabotage of it (drop the horizon from the digest) PASSED, and the reason
               was that neither number had ever been looked at.  A skip rate of 0 and a
               correct fast path are indistinguishable from every byte this dump compares.
               runs/skips: both large ⇒ the path under test actually ran. */
            std::fprintf(stderr, "PlatformHost: screen dump frame %lu -> %s  "
                         "(circuit %u, hook calls %lu, missing %lu; "
                         "band cycles run %lu, skipped %lu)\n",
                         frames, path, g_trackInstalled, g_trackHookCalls, g_trackHookMissing,
                         g_bandRuns, g_bandSkips);
#ifdef REVS_HOOK_PROFILE
            /* ⭐ Per-ENTRY counts, which is the number that decides what to twin first — the
               total above cannot, because one circuit's 18 entries differ by three orders of
               magnitude and the same address is different code on every circuit.  A row of 0
               means the engine never took the arm that reaches that hook in this window, NOT
               that the circuit lacks the body (revs_track_hook_has says that). */
            std::fprintf(stderr, "hook profile: circuit %u, %u distinct entries"
                         " (overflow %lu)\n",
                         g_trackInstalled, g_hookProfUsed, g_hookProfOverflow);
            for (unsigned i = 0; i < g_hookProfUsed; i++)
                std::fprintf(stderr, "  hook $%04X  %lu\n",
                             g_hookProfAddr[i], g_hookProfCount[i]);
#endif
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
        /* ⭐ REVS_QUIT_AFTER_DUMP=1 — stop once the last requested frame is written.
           Without it the host races on forever after the dump, so a scripted check has to rely
           on an external timeout to end each run, and then WAITS OUT that timeout for every
           circuit even though the measurement finished in seconds.  (That is exactly how
           `make track-run` first appeared to hang on circuit 1: it was still circuit 0, dumped
           and running.)  Not the default — an interactive or perf run wants to keep going.
           ⚠ LAST in this block, after the mem dump: it used to sit between the two dumps, so
           asking for BOTH silently produced no memory dump — a missing file that reads exactly
           like a feature that does not exist.
           ⚠ exit(), not `quit = true`: `quit` is only read before run(), and the engine's main
           loop has no return path — it is 6502 code that never ends.  There is nothing to unwind
           to, so leaving is the only way out. */
        if (frames + 1 >= dumpFrame + dumpCount && std::getenv("REVS_QUIT_AFTER_DUMP")) {
            std::fflush(0);
            std::exit(0);
        }
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
    // The loop, its bound, and the record-reuse fast path are in Platform::fireIrq1vField
    // (src/platform/bbc_hw.cpp) — shared with the Amiga deliberately, so that this build's
    // differentials (`make determinism`, `mode7`, `tracks`) are the oracle for the skip.
    fireIrq1vField();
    if (dumpPath && frames >= dumpFrame && frames < dumpFrame + 3) dumpBands();
}

/* $FE68's clock (see PlatformHost.h).  steady_clock, not the frame counter: the value has
   to move between two reads a few hundred cycles apart, which is exactly what the real T2
   does and what the measurement at $635F showed (28 distinct values in 32 reads).
   ⚠ Deliberately NOT deterministic.  Nothing in `make validate` reads $FE68 today (no
   fixture does), and a host run is a discovery tool, not a pinned trajectory — the pinned
   builds are the Amiga's, where REVS_FIXED_RNG substitutes the deterministic fallback. */
uint32_t PlatformHost::hwMicros()
{
    /* ⭐ `REVS_FIXED_RNG=1` pins it to the shared deterministic fallback (bbc_hw.cpp), which
       is what makes a WHOLE-IMAGE host differential possible: run the engine N frames twice,
       dump all 64 KB, require byte equality.  That is the only test that covers a transpiler
       change across the entire corpus — `make validate` compares one twin against an oracle
       generated by the same transpiler, so it cannot see a codegen bug that hits both.
       Used by `make determinism` (see the Makefile). */
    static const bool fixed = [] {
        const char* e = std::getenv("REVS_FIXED_RNG");
        return e && e[0] && e[0] != '0';
    }();
    if (fixed) return Platform::hwMicros();

    using namespace std::chrono;
    static const steady_clock::time_point t0 = steady_clock::now();
    return (uint32_t)duration_cast<microseconds>(steady_clock::now() - t0).count();
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
