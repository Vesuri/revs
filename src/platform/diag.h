/* diag.h — REVS_DIAG(): a statement that exists only for the instruments.
 *
 * The port carries ~120 always-compiled diagnostic counters and latches (g_bandRejects,
 * g_decodeGapFrames, g_irqClobberCount, g_sndQueueDrops, ...) that gdb scripts and the host
 * harness read and nothing in the game ever does.  The RELEASE build strips them (user decision,
 * docs/phases.md §Phase 7): `make DIST=1` in amiga/ defines REVS_DIST, every such statement is
 * written `REVS_DIAG(g_x++);`, and with the statements gone --gc-sections drops the variables.
 *
 * ⚠ Only a statement whose EVERY effect is on diagnostic state may go in here — never one with a
 * call on its right-hand side (`g_decodeCells = convertRace(...)` does the work), and never a
 * counter the game branches on (`g_trackUnhonoured` IS the install verdict).  The guard is the
 * release ELF itself: `make DIST=1` fails if any symbol listed in DIAG_SYMS (amiga/Makefile)
 * survives the link, because a survivor is a READ the macro did not remove.
 * Outside DIST the macro expands to its argument, so every other build is byte-identical.
 */
#ifndef REVS_DIAG_H
#define REVS_DIAG_H

#ifdef REVS_DIST
#define REVS_DIAG(...) ((void)0)
#else
#define REVS_DIAG(...) __VA_ARGS__
#endif

#endif
