/* track.h — CIRCUIT SELECTION, and the model behind it.
 *
 * ═══════════════════════════════════════════════════════════════════════════════════════════
 * WHAT A CIRCUIT IS, ON THE REAL MACHINE
 * ═══════════════════════════════════════════════════════════════════════════════════════════
 * A BBC Revs circuit is a FILE, and four of the five are *programs*, not data:
 *
 *   1. `REVSMEN` (BASIC) does `*LO.<TRACK>` and then runs the engine with the BBC's
 *      run-a-file command (a star, a slash, then `REVS2` — not written out here, because the
 *      two characters in that order would close this comment).  So the track file lands at
 *      $70DB and the engine is loaded on top.
 *   2. REVS2's unpack SWAPS $70DB-$7800 with $5300-$5A25, so the track file's first $725 bytes
 *      end up at $5300 — and the REST of the file ($7800 up) stays where the loader put it.
 *   3. `engine_init` calls `CallTrackHook` ($5A22).  Silverstone's is `RTS` (exec address $0000
 *      on the disc: passive data).  The other four hold `JMP $5700` — the track's own
 *      `ModifyGameCode`, which PATCHES THE ENGINE, 54-60 bytes of it, and then returns.
 *
 * So on hardware the bytes the engine executes differ per circuit.  This port is a
 * transliteration: there is no 6502 to run a patcher with, so the patcher's OUTPUT is applied as
 * data.  `tools/track_patch.py` replays `ModifyGameCode` exactly (twelve instruction forms) and
 * `make track-patch VERIFY=1` cross-checks the result against the patch surface measured on a
 * real BBC — only-in-replay is 0 for every circuit.  `tools/gen_tracks.py` bakes that output.
 *
 * ═══════════════════════════════════════════════════════════════════════════════════════════
 * WHY ONE BINARY IS ENOUGH — and it is a measurement, not a hope
 * ═══════════════════════════════════════════════════════════════════════════════════════════
 * Outside TWO extents the post-unpack engine image is byte-identical across every circuit:
 *
 *     BLOCK  $5300-$5A25  1830 B   the swapped track file
 *     TAIL   $7800-$78AA   171 B   the part of the file the swap does not move
 *
 * ⚠ The TAIL bound comes from the longest track FILE, not from diffing the commercial five.
 * Those files are $738-$73C long so they cannot reach past $7816 — and their diff stops exactly
 * there, which makes $7816 look like the answer.  The Nürburgring file is padded to $7D0, and it
 * USES the space: a table of 5-byte records at $786B-$78AA that every commercial circuit leaves
 * zero.  Sizing this extent off the five would have truncated the sixth, silently.
 *
 * ═══════════════════════════════════════════════════════════════════════════════════════════
 * ⚠⚠ THE PART THAT IS NOT DONE YET, AND WHY THIS REFUSES RATHER THAN TRIES
 * ═══════════════════════════════════════════════════════════════════════════════════════════
 * Installing a patch byte into mem[] is necessary and NOT sufficient.  The transliteration bakes
 * operands and opcodes into C: `LDA $5905,Y` is compiled, so rewriting mem[$1248] from $B9 to $20
 * changes nothing about what the C does.  Only bytes declared in `SMC_SITES`
 * (tools/transpile.py) are read from mem[] at run time.
 *
 * Therefore a circuit whose patch set is not fully covered by SMC_SITES would run SILVERSTONE'S
 * CODE over another circuit's geometry — which is not a crash, it is a plausible-looking wrong
 * game, the exact failure class this project keeps paying for.  So:
 *
 *   ⭐ `revs_track_install()` checks every patch address against `revs_smc_bytes.h`, which the
 *      TRANSPILER generates from SMC_SITES.  Unhonoured bytes are counted, the first is recorded,
 *      and the install is REFUSED.  A hand-maintained list would drift the moment a site was
 *      added; a generated one cannot.
 *
 * Silverstone has an empty patch set, so it installs unconditionally and the mechanism is
 * exercised by every ordinary run rather than only by an expansion circuit.
 */
#ifndef REVS_TRACK_H
#define REVS_TRACK_H

#include "../gen/revs_tracks.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The selected circuit, as an index into revs_tracks[].  0 = Silverstone = the engine's own
   default, which is what the embedded image already holds. */
extern unsigned char g_track;

/* Diagnostics.  ⚠ Reported, never assumed — a refused install must be visible as a NUMBER, not
   as "the circuit looked odd".  All three are in the Amiga PROBE_SYMS list. */
extern unsigned char  g_trackInstalled;      /* index actually installed (0xFF = none yet) */
extern unsigned char  g_trackRequested;      /* index the BUILD asked for — differs on a fallback */
extern unsigned short g_trackUnhonoured;     /* patch bytes SMC_SITES does not cover */
extern unsigned short g_trackUnhonouredAddr; /* the first such address, for the report */

/* Install a circuit into mem[].  Returns 1 on success, 0 if REFUSED (see the header note).
 *
 * ⚠ MUST run before the engine reads any of it — i.e. straight after the boot image is loaded
 * and before engine_main.  Installing mid-session would swap the geometry under a running race.
 */
int revs_track_install(unsigned char index);

/* How many of this circuit's patch bytes SMC_SITES does NOT cover.  0 means installable.
   Also sets g_trackUnhonoured / g_trackUnhonouredAddr.  ($FFFF = no such circuit.) */
unsigned short revs_track_check(unsigned char index);

/* ⚠⚠ THE DIFFERENTIAL'S ENTRY POINT, AND NOTHING ELSE MAY CALL IT.  Installs a circuit's data
 * with the SMC check SKIPPED, so tools/validate_tracks.c can verify the DATA path for all six
 * circuits today — while the expansion circuits are still (correctly) refused by the real entry
 * point.  Without it the harness could only ever install Silverstone, and a harness that
 * exercises one circuit out of six is most of the way to proving nothing.
 *
 * ⚠ It does NOT make an expansion circuit playable: the patch bytes land in mem[] but the
 * transliteration is not yet reading them, which is the whole reason revs_track_install()
 * refuses.  Calling this from the game would produce exactly the plausible-looking wrong game
 * that track.h's header warns about. */
int revs_track_install_forced(unsigned char index);

/* Which circuit a plain build boots.  ⚠ 0 (Silverstone) until the front end has a track menu —
 * `make TRACK=n` on either backend overrides it, which is how an expansion circuit gets tested
 * before the menu exists.  An out-of-range or refused choice falls back to 0 and leaves
 * g_trackUnhonoured set, so the fallback is visible rather than silent. */
#ifndef REVS_TRACK_DEFAULT
#define REVS_TRACK_DEFAULT 0
#endif

/* Install REVS_TRACK_DEFAULT, falling back to Silverstone.  Call once, right after the boot image
   is loaded.  Returns 1 if the requested circuit went in, 0 if it fell back. */
int revs_track_boot(void);

/* How many circuits this build actually has.  ⚠ Not a constant across machines: the Nürburgring
   block is generated from a git-ignored disc, so a checkout without it has five, not six
   (docs/reference-sources.md §The Nürburgring file, MEASURED). */
#define REVS_TRACK_AVAILABLE REVS_TRACK_COUNT

#ifdef __cplusplus
}
#endif

#endif
