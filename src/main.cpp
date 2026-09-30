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
#if defined(REVS_SPAN_SCANCHECK) && !defined(REVS_PLATFORM_AMIGA)
extern "C" void revs_announce_spanscan(void);
#endif

#if defined(REVS_PLATFORM_AMIGA)
  #include "PlatformAmiga.h"        /* src/platform/amiga — on the cross-build's -I path */
  #include <proto/exec.h>
  #include <proto/dos.h>
  #include <dos/dosextens.h>        /* struct Process — pr_CLI, pr_MsgPort */
  #include <workbench/startup.h>    /* struct WBStartup */
#else
  #include "platform/host/PlatformHost.h"
#endif

#if defined(REVS_PLATFORM_AMIGA)
/* --- Workbench launch protocol -------------------------------------------------------
   A program started from an icon is a NEW DOS process, and Workbench posts it a WBStartup
   message: it must be taken off our port before any dos.library call (the port belongs to DOS),
   and replied as the very last thing the program does — the reply is what lets Workbench
   UnLoadSeg() us.  A compiler's startup module would do both; this port's freestanding CRT
   (_start) does neither.  RKM Libraries ch. 14 (14-2-2, 14-5-1, the warning at the end of
   14-5-2); the same code as the Rescue on Fractalus port's.
   pr_CLI is non-NULL for a Shell launch and for the WHDLoad slave (a real CLI process under the
   kickemu), and then there is no message. */
static struct WBStartup* wbGetStartupMessage(void) {
    struct Process* me = (struct Process*)FindTask(0);
    if (me->pr_CLI != 0) return 0;
    WaitPort(&me->pr_MsgPort);
    return (struct WBStartup*)GetMsg(&me->pr_MsgPort);
}

/* Forbid() first and never Permit(): once replied, Workbench may unload the code we are still
   returning through; the forbid holds it off until the task is gone. */
static void wbReplyStartupMessage(struct WBStartup* msg) {
    if (!msg) return;
    Forbid();
    ReplyMsg(&msg->sm_Message);
}
#endif

/* A function of its own so that on the Amiga the PlatformClass destructor has run before main()
   replies the Workbench message — nothing may happen after that reply. */
static int runGame(const char* image) {
    /* Constructing PlatformClass brings the platform up (window/DMA/audio, loads
       the memory image) and sets the global Platform* the C bridge uses. */
    PlatformClass plt(image);
    if (plt.quit) {
#if defined(REVS_PLATFORM_AMIGA)
        return 20;   /* AmigaDOS FAIL: the WHDLoad slave turns it into a message */
#else
        return 1;
#endif
    }

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
#if defined(REVS_SPAN_SCANCHECK) && !defined(REVS_PLATFORM_AMIGA)
    /* ⭐ Same rule, and here it is load-bearing twice over: an oracle that silently did not run
       reads exactly like an oracle that found nothing (CLAUDE.md §an A/B switch must print its
       own state; `revs_verify_the_instrument`). */
    revs_announce_spanscan();
#endif

    plt.run();   /* runs the game; returns when the user quits */
    return 0;
}

/* NOTE: the Amiga freestanding CRT (_start) calls main() with NO arguments, so the Amiga main
   takes none — a mismatched signature reads garbage off the stack.  The Amiga image is built
   from the player's disc (PlatformAmiga's constructor), so the path is unused there. */
#if defined(REVS_PLATFORM_AMIGA)
int main(void) {
    struct WBStartup* wbMsg = wbGetStartupMessage();     /* before any DOS call */
    DOSBase = (struct DosLibrary*)OpenLibrary((CONST_STRPTR)"dos.library", 33);
    BPTR oldDir = 0;
    const bool changeDir = DOSBase && wbMsg && wbMsg->sm_NumArgs > 0 && wbMsg->sm_ArgList[0].wa_Lock;
    if (changeDir) oldDir = CurrentDir(wbMsg->sm_ArgList[0].wa_Lock);   /* the icon's drawer */
    int rc = DOSBase ? runGame(0) : 20;
    if (changeDir) CurrentDir(oldDir);
    if (DOSBase) { CloseLibrary((struct Library*)DOSBase); DOSBase = 0; }
    wbReplyStartupMessage(wbMsg);   /* strictly last */
    return rc;
}
#else
int main(int argc, char* argv[]) {
    /* ⚠ The RUNTIME image (`make runtime`), not revs_mem.bin: REVS2 unpacks itself
       before running and src/gen/revs_gen.c is a transliteration of the unpacked
       layout, so the pre-unpack image would put every address in the wrong place.
       docs/static-map.md. */
    return runGame((argc > 1) ? argv[1] : "disasm/revs_runtime.bin");
}
#endif
