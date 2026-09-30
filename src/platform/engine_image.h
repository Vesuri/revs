/* engine_image.h — build the engine's 64 KB runtime image from the player's own disc.
 *
 * ⭐ THE RELEASE CARRIES NO ORIGINAL ENGINE BYTES (user decision, docs/phases.md §Phase 7).  The
 * port's transliteration, twins, circuit data and hook bodies ship in the executable; the engine
 * image they run against — REVS2's code, tables and graphics — is read at STARTUP from a disc
 * image the player supplies.  Two discs are supported, and their REVS2 files are byte-identical:
 *
 *   Revs Plus Revs 4 Tracks  (bbcmicro.co.uk id 2203, = revs.ssd)
 *   Revs+ [hack]             (bbcmicro.co.uk id 4179)
 *
 * so the check is on REVS2 itself (length + CRC32), not on the disc, and any DFS image carrying
 * that exact file is accepted.  Anything else is refused before the display is taken over.
 *
 * What this module does is what `tools/ssd_load.py` + `tools/relocate.py` do at build time for
 * the dev image (`disasm/revs_runtime.bin`): lay REVS2 at $1200, replay the engine's own
 * self-unpack (relocate.py has the derivation and the real-BBC verification), then lay
 * Silverstone's two circuit extents over the result from the exe's own circuit table.  The
 * result is byte-identical to `disasm/revs_runtime.bin` on both supported discs, and
 * `make engine-image` (tools/engine_image_test.c) keeps it so.
 *
 * ⚠ The unpack's own integrity check (the rolling checksum over the $70DB track file) is NOT
 * replayed: the track file is not on the disc image's side of this seam any more — the circuit
 * data comes from the exe — so the CRC over REVS2 is what stands in for it.
 *
 * Pure C over a caller-supplied buffer, no I/O: the Amiga backend reads the two catalogue
 * sectors and REVS2's bytes with dos.library, the host test with stdio.
 */
#ifndef REVS_ENGINE_IMAGE_H
#define REVS_ENGINE_IMAGE_H

#define ENGINE_CATALOGUE_BYTES 512u        /* DFS sectors 0 and 1 */
#define ENGINE_REVS2_LOAD      0x1200u
#define ENGINE_REVS2_LENGTH    0x5E00u
#define ENGINE_REVS2_CRC32     0x83E95A44uL /* measured: identical on discs 2203 and 4179 */

enum {
    ENGINE_OK = 0,
    ENGINE_NO_REVS2,       /* the catalogue has no $.REVS2 (the 1985 discs, or not a Revs disc) */
    ENGINE_BAD_REVS2,      /* REVS2 is there but not the 1986 engine: wrong load address/length */
    ENGINE_BAD_CRC,        /* ...or the right shape with different bytes */
    ENGINE_READ_FAILED     /* the caller's own I/O status, so one message table covers both */
};

#ifdef __cplusplus
extern "C" {
#endif

/* Find REVS2 in a DFS catalogue.  On ENGINE_OK, *offset is its byte offset in the disc image
   and its ENGINE_REVS2_LENGTH bytes are what the caller must read to mem + ENGINE_REVS2_LOAD. */
int engine_find_revs2(const unsigned char* catalogue, unsigned long* offset);

/* mem[ENGINE_REVS2_LOAD .. + ENGINE_REVS2_LENGTH) holds REVS2 as read off the disc; the rest of
   mem is overwritten.  Verifies the CRC, then builds the runtime image in place. */
int engine_build_image(unsigned char* mem);

/* One line of text for a status, for the startup failure message. */
const char* engine_status_text(int status);

#ifdef __cplusplus
}
#endif

#endif
