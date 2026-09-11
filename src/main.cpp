/* Revs — shared entry point.
 *
 * One main() for every target: it constructs the concrete PlatformClass (selected
 * by a build define), then hands control to Platform::run(), which owns all the
 * platform-specific setup and drives the genuine entry chain.
 *
 *   REVS_PLATFORM_AMIGA -> PlatformAmiga  (src/platform/amiga, m68k cross-build)
 *   default             -> PlatformHost  (headless macOS dev build)
 */
#if defined(REVS_VIEWSKIP) && !defined(REVS_PLATFORM_AMIGA)
extern "C" void revs_announce_viewskip(void);
#endif

#if defined(REVS_PLATFORM_AMIGA)
  #include "PlatformAmiga.h"        /* src/platform/amiga — on the cross-build's -I path */
#else
  #include "platform/host/PlatformHost.h"
#endif

/* Default to the post-load memory image built from revs.ssd by tools/ssd_load.py,
   so every build boots the SAME initial state and code path.
   NOTE: the Amiga freestanding CRT (_start) calls main() with NO arguments, so the
   Amiga main takes none — a mismatched signature reads garbage off the stack. */
#if defined(REVS_PLATFORM_AMIGA)
int main(void) {
    const char* image = "revs.bin";   /* unused: the Amiga image is linked in (incbin.s) */
#else
int main(int argc, char* argv[]) {
    /* ⚠ The RUNTIME image (`make runtime`), not revs_mem.bin: REVS2 unpacks itself
       before running and src/gen/revs_gen.c is a transliteration of the unpacked
       layout, so the pre-unpack image would put every address in the wrong place.
       docs/static-map.md. */
    const char* image = (argc > 1) ? argv[1] : "disasm/revs_runtime.bin";
#endif

    /* Constructing PlatformClass brings the platform up (window/DMA/audio, loads
       the memory image) and sets the global Platform* the C bridge uses. */
    PlatformClass plt(image);
    if (plt.quit) return 1;

    /* ⚠ Any one-shot lookup-table build goes HERE, before any game code runs —
       never lazily on first use.  On the Atari port a 64 KB table built lazily
       inside the first flight interrupt froze the display for ~3.6 s at the worst
       possible moment.  See docs/m68k-optimisation.md. */

#if defined(REVS_VIEWSKIP) && !defined(REVS_PLATFORM_AMIGA)
    /* ⭐ An A/B switch must PRINT its own state (CLAUDE.md §Performance) — a build measured
       against a control that silently had the switch in the same position measures nothing.
       On the Amiga the same fact is read off g_viewSkipLines through gdb. */
    revs_announce_viewskip();
#endif

    plt.run();   /* runs the game; returns when the user quits */
    return 0;
}
