/* ⭐ THE TRANSLITERATION TRAP — does a production build execute any 6502 transliteration?
 *
 * Every generated body that is NOT a validation oracle records its own entry under
 * `make TRANS_TRAP=1`; `make transtrap` drives the front end, a 300-frame race, the crash
 * trajectory and all six circuits, and FAILS if anything reports.  Without it the claim
 * "no transliteration runs any more" is a survey, not a gate — and the surveys in this
 * project have a habit of going stale one commit later (docs/postmortem.md).
 *
 * Zero cost when off: the macro compiles to nothing.
 */
#ifndef REVS_TRANS_TRAP_H
#define REVS_TRANS_TRAP_H

#ifdef REVS_TRANS_TRAP
#ifdef __cplusplus
extern "C" {
#endif
void revs_trans_hit(const char* fn);
#ifdef __cplusplus
}
#endif
#define REVS_TRANS_HIT(f) revs_trans_hit(f)
#else
#define REVS_TRANS_HIT(f) ((void)0)
#endif

#endif
