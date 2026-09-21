#ifndef REVS_TYRES_H
#define REVS_TYRES_H
/* RevsTyres.h — the front-wheel dither as two precomputed sprite states (§12, user directive).
 * The rationale, the outline/pattern split and the period-2 check are all at RevsTyres.cpp.
 * This header exists so RevsScreen.cpp can size and position the sprites without duplicating
 * the geometry, and so the two files cannot drift apart on it. */

/* The patch: BBC cells [0,1] and [38,39] over display lines 130..140.  Two cells is eight MODE 5
   pixels is SIXTEEN Amiga pixels — one sprite channel a side, no squeeze. */
#define REVS_TYRE_Y0      130u
#define REVS_TYRE_LINES    11u
#define REVS_TYRE_L_CELL    0u
#define REVS_TYRE_R_CELL   38u
/* Screen x of each side, in Amiga pixels (a MODE 5 pixel is two of them). */
#define REVS_TYRE_L_X       0u
#define REVS_TYRE_R_X     304u

#ifdef __cplusplus
extern "C" {
#endif

/* Build the two states into four sprite images — leftA/leftB/rightA/rightB, each pointing at the
   first DATA word of a REVS_TYRE_LINES-tall sprite (past its two control words).  Returns 0 and
   builds nothing if the EOR turns out not to have period 2, in which case two images cannot
   represent the animation and the caller must leave these rows to the decode.
   ⚠⚠ MAIN-LOOP CONTEXT ONLY.  It applies the game's own EOR to the live frame buffer twice to
   discover state B and to check the period, snapshotting and restoring around it — an ISR
   running `tick_wheel_spin` in the middle of that would see a half-transformed patch.
   ⚠ It also REPLACES the patch in `mem[]` with the outline (the static pixels), which is what
   the playfield is supposed to hold from then on. */
int  revs_tyres_build(unsigned short* leftA, unsigned short* leftB,
                      unsigned short* rightA, unsigned short* rightB);

/* Which image is showing: 0 = A, 1 = B.  Toggled where the EOR used to happen. */
extern volatile unsigned char g_tyrePhase;
extern volatile unsigned long g_tyreBuilds;
extern volatile unsigned long g_tyrePeriodBad;   /* ⚠⚠ MUST BE 0 */
/* How many ANIMATED pixels are colour 0 in one of the two states.  A sprite cannot draw colour 0
   — it is transparent — so a non-zero count says the final step (owning these rows and stopping
   the sweep) must make the playfield colour 0 under exactly those pixels rather than relying on
   the sprite to cover them.  Reported rather than assumed. */
extern volatile unsigned long g_tyreZeroAnim;

#ifdef __cplusplus
}
#endif
#endif /* REVS_TYRES_H */
