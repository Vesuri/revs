#ifndef MEM_DECL_H
#define MEM_DECL_H
/* MEM_QUAL — the cv-qualifier on the 6502 RAM image.
 *
 * ⭐⭐ `mem[]` USED TO BE `volatile` UNCONDITIONALLY, AND THAT COST 10% OF THE FRAME
 * (measured 2026-08-16: 1.56 → 1.72 FPS on the target, .text 5.7 KB smaller).  Volatile
 * forbids the compiler every optimisation over the array — no common-subexpression on an
 * address, no keeping a byte in a register across two uses, no reordering — and the whole
 * transliterated engine is `mem[]` accesses.  It is the single largest tax the port pays
 * for a qualifier.
 *
 * WHY IT WAS THERE, AND WHY IT IS NOT NEEDED NOW.  The declaration's comment said "shared
 * between main thread and VBI audio thread" — inherited from the predecessor project,
 * which had one.  This port does not.  What it has is the Amiga's VERTB ISR, and in the
 * shipping model that handler runs the copper work, the teletext flash counter, the audio
 * scheduler and a `++` on a pending-tick counter: **none of them touch `mem[]`.**  The 50 Hz
 * game body — the one thing in the port that writes `mem[]` from anywhere — was moved OUT of
 * the ISR to main-loop context in Phase 5 (`docs/amiga-arch.md` §the game body), for
 * unrelated reasons (it DRAWS, and running it under the rasteriser tore every frame).
 * RevsScreen::decodeTeletext already states the same conclusion at its own cast.
 *
 * ⚠ EXCEPT UNDER `make BODY_IN_ISR=1`, which restores the old model for A/B measurement.
 * There the body genuinely does run in the ISR and genuinely does write `mem[]` under the
 * main loop's feet, so the qualifier comes back — otherwise the comparison build would be
 * unsound in a way the shipping build is not, and the A/B would measure that instead.
 *
 * ⚠ Dropping `volatile` is NOT a licence to alias `mem[]` as a wider pointer.  That rule is
 * about endianness, is unrelated to this one, and `make endian-lint` still enforces it.
 *
 * Declared in its own header because eight translation units re-declare `mem[]` themselves
 * rather than include cpu.h, and the qualifier must be identical in every one of them.
 */
/* `make MEMVOL=1` is the CONTROL for the 10% measurement above — the old always-volatile
   model, one flag, so the comparison can be re-run rather than re-argued. */
#if defined(REVS_BODY_IN_ISR) || defined(REVS_MEM_VOLATILE)
#define MEM_QUAL volatile
#else
#define MEM_QUAL
#endif

#endif /* MEM_DECL_H */
