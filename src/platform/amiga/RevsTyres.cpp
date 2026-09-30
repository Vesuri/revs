/* RevsTyres.cpp — THE FRONT-WHEEL DITHER AS TWO HARDWARE SPRITES (§12, user directive)
 * ================================================================================================
 * `tick_wheel_spin` ($52A4) is the ONLY game work in the whole IRQ1V band cycle, and all it does
 * is EOR six short runs of the frame buffer at a rate set by the road speed — the rotation
 * flicker at the top of each front-wheel arch.  On a BBC that is 206-233 us of every 6113 us
 * field.  Here it is worse than that: it is 50 Hz of read-modify-write on `mem[]` inside display
 * lines 117..157, which is the one band whose `mem[]` stores have to go before the decode can
 * stop converting it.
 *
 *   "Even the tires don't need to be drawn; the tire animation can be achieved with pre-made
 *    sprites that only need switching between two sprites as needed."   — the user, §12
 *
 * ⭐ AND THE GEOMETRY FITS EXACTLY.  The six runs land in BBC cells 0, 1, 38 and 39 over display
 * lines 130..140 (derived from the addresses in symbols.csv and confirmed by `make fbwrites
 * FILL=117-207`, which reports `tick_wheel_spin  cells 0,1,38,39  lines 133..140`).  Two cells is
 * eight MODE 5 pixels is SIXTEEN Amiga pixels — one sprite channel per side, eleven lines tall,
 * at screen x=0 and x=304.  Nothing about that is a squeeze.
 *
 * ⭐⭐⭐ THE SPLIT: THE PLAYFIELD KEEPS THE OUTLINE, THE SPRITE IS THE PATTERN (user, 2026-09-21).
 *
 *   "the playfield should contain only the tyre outlines.  There should be two precomputed
 *    sprites for the two possible states of the tyre pattern (currently done XORing).  The code
 *    should just alternate between the two by setting the sprite pointers to the appropriate
 *    sprite."
 *
 * ⭐ AND THAT SPLIT ALSO DISPOSES OF THE ONE HAZARD IN PUTTING AN *EOR* ON A SPRITE, which is
 * that SPRITE COLOUR 0 IS TRANSPARENT: a sprite cannot say "this pixel becomes colour 0", and the
 * masks ($F0 / $C0 / $30 and the two 5-byte tables) all flip MODE 5 colour bit 1, so 2 -> 0
 * happens wherever a pixel is colour 2 — most of the arch.  Split the patch by what MOVES and the
 * problem cannot arise:
 *
 *   ANIMATED pixel  =  one whose colour differs between state A and state B.
 *   playfield       =  state A with every animated pixel set to colour 0   (the OUTLINE)
 *   sprite image A  =  the animated pixels in their state-A colour, 0 elsewhere
 *   sprite image B  =  the same pixels in their state-B colour,     0 elsewhere
 *
 * A transparent sprite pixel now always reveals colour 0, because the playfield is colour 0 at
 * exactly the pixels the sprite is responsible for.  Every pixel is drawn by exactly one of the
 * two layers, so there is no compositing, no mask and no read-modify-write anywhere.
 *
 * ⭐⭐ AND THE TWO IMAGES COME FROM THE GAME'S OWN CODE, NOT FROM A REDRAWING OF IT.  `tyresBuild`
 * snapshots the 44 bytes, applies `tick_wheel_spin`'s own transform once to get state B, and
 * applies it again to check it lands back on state A.  That check is not ceremony: the twin's
 * loop is
 *       if (x < 3) { run_c[x] ^= $F0; if ((run_d[x] ^= $F0) != 0) continue; }
 *       run_e[x] ^= $C0;  run_f[x] ^= $30;
 * so WHICH bytes are EOR'd depends on the value `run_d` currently holds — the mask is
 * state-dependent and "period 2" is a property to verify, not to assume.  `g_tyrePeriodBad`
 * counts a failure and the build falls back to leaving the rows to the decode.
 *
 * ⚠ COLOURS.  The patch is entirely inside raster band 3 (display lines 100.5..166.1), so exactly
 * one palette is in force over it and COLOR17..19 can be programmed once with that band's
 * COLOR01..03.  A sprite spanning a band boundary could not be done this way.
 */

/* ⚠ NO FRAMEWORK HEADERS.  RevsScreen.h drags in the AmigaOS type headers and this file needs
   none of them: it fills raw sprite DATA WORDS and the caller (RevsScreen.cpp) owns the Sprite
   objects, their control words and their positions.  Same convention as RevsPlot.cpp. */
#include "RevsTyres.h"
#include "../diag.h"
#include "../bbc_screen.h"
#include "../../cpu/mem_decl.h"

extern MEM_QUAL unsigned char mem[65536];

volatile unsigned long g_tyreBuilds    = 0;   /* how many times the two images were built      */
volatile unsigned long g_tyrePeriodBad = 0;   /* ⚠⚠ MUST BE 0 — the EOR did not have period 2  */
volatile unsigned char g_tyrePhase     = 0;   /* which image is showing: 0 = A, 1 = B          */
volatile unsigned long g_tyreZeroAnim  = 0;   /* animated pixels that are colour 0 in a state  */

/* The six runs, exactly as tick_wheel_spin walks them (src/gen/revs_native.c twin #122).  Kept
   as addresses rather than as (cell, line) so that this file and the twin can be diffed against
   the same six symbols.csv rows. */
#define TYRE_RUN_A  0x6FC0   /* cell 0,  lines 136..140, EOR wheel_spin_xor_tbl_a[x] */
#define TYRE_RUN_B  0x70F8   /* cell 39, lines 136..140, EOR wheel_spin_xor_tbl_b[x] */
#define TYRE_RUN_C  0x6E85   /* cell 0,  lines 133..135, EOR $F0   (x < 3 only)      */
#define TYRE_RUN_D  0x6FBD   /* cell 39, lines 133..135, EOR $F0   (x < 3 only)      */
#define TYRE_RUN_E  0x6E8A   /* cell 1,  lines 130..134, EOR $C0                     */
#define TYRE_RUN_F  0x6FB2   /* cell 38, lines 130..134, EOR $30                     */
#define TYRE_XOR_A  0x52F6   /* wheel_spin_xor_tbl_a */
#define TYRE_XOR_B  0x52FB   /* wheel_spin_xor_tbl_b */

/* The patch geometry is the HEADER's, not a second copy — RevsScreen.cpp sizes and positions the
   sprites from the same four numbers. */
#define TYRE_Y0      REVS_TYRE_Y0
#define TYRE_LINES   REVS_TYRE_LINES
#define TYRE_L_CELL  REVS_TYRE_L_CELL
#define TYRE_R_CELL  REVS_TYRE_R_CELL

/* A snapshot of the 44 patch bytes, indexed [side][line][cellInPair]. */
struct TyrePatch { unsigned char b[2][TYRE_LINES][2]; };

/* ⭐⭐⭐ THE OUTLINE, AS A MASK OVER THE BITPLANES (user, 2026-09-21: "the static bitmap graphics
   still have the band stripes in them - they should be removed from the bitplanes").
   The split at the top of this file needs the playfield to read colour 0 under every ANIMATED
   pixel; without that the sprite is merely additive and state A's dither stays baked into the
   picture.  It cannot be done by writing an outline into `mem[]`: the view sweep repaints the
   arch every frame (`make fbwrites FILLREADS=1` names `view_cell_chain_a` and
   `view_cell_chain_b_mid` on display lines 130..140, cells 0,1,38,39), so the write would be
   undone before the next decode read it.  ⚠ And a gdb poke cannot even test that — writing
   `mem[]` through the FS-UAE stub silently does nothing, which reads as "the sweep repainted it".
   ⇒ Apply it where nothing can overwrite it: to the PLANE BYTES, at the end of the decode that
   just expanded them.  One AND per plane per cell pair, 44 word read-modify-writes a frame.
   ⚠ Both plane bytes get the same mask: clearing a MODE 5 pixel to colour 0 clears both of its
   colour bits, and the two planes carry one bit each (bbc_screen.h). */
static unsigned short s_keepW[2][TYRE_LINES];   /* [side][line] — cells (0,1) / (38,39)        */
static int            s_keepReady = 0;          /* 0 until revs_tyres_build has succeeded once */

/* One plane's bytes per display line, and the interleaved pair's stride — DERIVED, so a display
   width change cannot leave this file behind. */
#define TYRE_PLANE_GAP    (BBC_SCREEN_WIDTH / 8u)
#define TYRE_LINE_STRIDE  (TYRE_PLANE_GAP * 2u)

/* Where a patch byte lives in mem[]: the BBC cell layout, charRow*320 + cell*8 + lineInRow. */
static unsigned tyreAddr(unsigned cell, unsigned y)
{
    const unsigned row = y >> 3;
    return BBC_SCREEN_BASE + row * BBC_SCREEN_BPR + cell * BBC_SCREEN_LINES + (y & 7u);
}

static void tyreSnapshot(TyrePatch* p)
{
    unsigned s, l, c;
    for (s = 0; s < 2u; s++)
        for (l = 0; l < TYRE_LINES; l++)
            for (c = 0; c < 2u; c++)
                p->b[s][l][c] = mem[tyreAddr((s ? TYRE_R_CELL : TYRE_L_CELL) + c, TYRE_Y0 + l)];
}

static void tyreRestore(const TyrePatch* p)
{
    unsigned s, l, c;
    for (s = 0; s < 2u; s++)
        for (l = 0; l < TYRE_LINES; l++)
            for (c = 0; c < 2u; c++)
                mem[tyreAddr((s ? TYRE_R_CELL : TYRE_L_CELL) + c, TYRE_Y0 + l)] = p->b[s][l][c];
}

/* ⭐ THE GAME'S OWN TRANSFORM, applied to `mem[]` in place — a copy of twin #122's EOR body and
   nothing else (no field counter, no accumulator, no rate gate: those decide WHEN it fires, and
   this decides WHAT it does).  Kept here rather than called through `tick_wheel_spin` so that
   building the images cannot advance the game's own field counter. */
static void tyreApplyEor(void)
{
    int x;
    for (x = 4; x >= 0; x--) {
        mem[TYRE_RUN_A + x] ^= mem[TYRE_XOR_A + x];
        mem[TYRE_RUN_B + x] ^= mem[TYRE_XOR_B + x];
        if (x < 3) {
            mem[TYRE_RUN_C + x] ^= 0xF0u;
            if ((unsigned char)(mem[TYRE_RUN_D + x] ^= 0xF0u) != 0u) continue;
        }
        mem[TYRE_RUN_E + x] ^= 0xC0u;
        mem[TYRE_RUN_F + x] ^= 0x30u;
    }
}

/* One patch byte (four MODE 5 pixels) into one sprite word pair.  A MODE 5 byte holds the four
   pixels' colour bit 1 in bits 7..4 and bit 0 in bits 3..0 (bbc_screen.h), and a sprite's two
   data words are plane 0 and plane 1 of the same sixteen pixels — but each MODE 5 pixel is TWO
   Amiga pixels wide, so every source bit becomes two sprite bits. */
static void tyreExpandByte(unsigned char v, unsigned anim, unsigned shift,
                           unsigned short* w0, unsigned short* w1)
{
    unsigned p;
    for (p = 0; p < 4u; p++) {
        const unsigned hi = (v >> (7u - p)) & 1u;   /* colour bit 1 */
        const unsigned lo = (v >> (3u - p)) & 1u;   /* colour bit 0 */
        const unsigned b  = shift + p * 2u;         /* first of the two doubled pixels */
        if (!((anim >> p) & 1u)) continue;          /* static: the PLAYFIELD draws this one */
        if (lo) *w0 = (unsigned short)(*w0 | (0x3u << (14u - b)));
        if (hi) *w1 = (unsigned short)(*w1 | (0x3u << (14u - b)));
    }
}

/* Which of a byte's four MODE 5 pixels differ between the two states — the ANIMATED set, and the
   whole basis of the split above.  Bit p set = pixel p moves. */
static unsigned tyreAnimMask(unsigned char a, unsigned char b)
{
    unsigned p, m = 0;
    for (p = 0; p < 4u; p++) {
        const unsigned ca = (((a >> (7u - p)) & 1u) << 1) | ((a >> (3u - p)) & 1u);
        const unsigned cb = (((b >> (7u - p)) & 1u) << 1) | ((b >> (3u - p)) & 1u);
        if (ca != cb) m |= 1u << p;
    }
    return m;
}

/* State A with every animated pixel forced to colour 0 — the OUTLINE the playfield keeps. */
static unsigned char tyreOutlineByte(unsigned char a, unsigned anim)
{
    unsigned p;
    unsigned char v = a;
    for (p = 0; p < 4u; p++)
        if ((anim >> p) & 1u)
            v = (unsigned char)(v & ~((1u << (7u - p)) | (1u << (3u - p))));
    return v;
}

/* Fill one 16-pixel-wide, TYRE_LINES-tall sprite image.  `d` points at the first DATA word, i.e.
   past the sprite's two control words — the caller sets those, because position is its business
   and not this module's. */
static void tyreFillSprite(unsigned short* d, const TyrePatch* st, const TyrePatch* other,
                           unsigned side)
{
    unsigned l;
    for (l = 0; l < TYRE_LINES; l++) {
        unsigned short w0 = 0, w1 = 0;
        unsigned c;
        for (c = 0; c < 2u; c++)
            tyreExpandByte(st->b[side][l][c],
                           tyreAnimMask(st->b[side][l][c], other->b[side][l][c]),
                           c * 8u, &w0, &w1);
        d[l * 2u]      = w0;
        d[l * 2u + 1u] = w1;
    }
}

/* ⚠⚠ BUILD IS DESTRUCTIVE AND MUST PUT `mem[]` BACK EXACTLY.  It applies the game's own EOR to
   the live frame buffer to discover state B, so it snapshots first and restores last — and the
   period check is what proves the restore is a restore rather than a second EOR that happened to
   look like one.  Called once, from main-loop context, never from the ISR. */
int revs_tyres_build(unsigned short* leftA, unsigned short* leftB,
                     unsigned short* rightA, unsigned short* rightB)
{
    TyrePatch a, b, back;
    tyreSnapshot(&back);          /* whatever is there now — restored at the end */

    tyreSnapshot(&a);
    tyreApplyEor();
    tyreSnapshot(&b);
    tyreApplyEor();
    {   /* ⭐ THE PERIOD-2 CHECK.  The EOR's mask is state-dependent (see the header), so this is
           a real property and not a tautology: if it fails, two images cannot represent the
           animation and the caller must leave these rows to the decode. */
        TyrePatch again;
        unsigned i, bad = 0;
        const unsigned char* pa = &a.b[0][0][0];
        const unsigned char* pg;
        tyreSnapshot(&again);
        pg = &again.b[0][0][0];
        for (i = 0; i < 2u * TYRE_LINES * 2u; i++) if (pa[i] != pg[i]) bad++;
        if (bad) { REVS_DIAG(g_tyrePeriodBad += bad); tyreRestore(&back); return 0; }
    }
    tyreRestore(&back);      /* ⚠ back to the state the game left, before the outline replaces it */

    tyreFillSprite(leftA,  &a, &b, 0u);
    tyreFillSprite(leftB,  &b, &a, 0u);
    tyreFillSprite(rightA, &a, &b, 1u);
    tyreFillSprite(rightB, &b, &a, 1u);

    /* ⚠⚠ THE OUTLINE IS **NOT** WRITTEN BACK HERE, AND THAT IS A CORRECTION.
       The first cut of this file laid the static pixels into `mem[]` on the assumption that the
       wheel arch is dashboard furniture.  It is not: `make fbwrites FILLREADS=1` shows
       `view_cell_chain_a` and `view_cell_chain_b_mid` repainting cells 0,1,38,39 on display lines
       130..140 EVERY frame — the arch is off-road terrain the sweep paints, and the EOR dithers
       on top of it (symbols.csv: "off-road, and survives the road repaint").  An outline written
       here would be overwritten by the next sweep.
       ⇒ The playfield's copy of the outline is simply what the sweep already paints, and making
       the animated pixels read as colour 0 underneath the sprite belongs with the step that
       OWNS these rows and stops the sweep writing `mem[]` at all.  Until then the sprite is
       additive: it draws the pattern over the sweep's undithered arch.
       ⭐ `g_tyreZeroAnim` answers the one question that decides whether that final step can use a
       sprite at all — whether any ANIMATED pixel is colour 0 in either state, because a sprite
       cannot draw colour 0 and would show the playfield there instead.  Data, not an assumption. */
    {
        unsigned s2, l2, c2;
        for (s2 = 0; s2 < 2u; s2++)
            for (l2 = 0; l2 < TYRE_LINES; l2++)
                for (c2 = 0; c2 < 2u; c2++) {
                    const unsigned char va = a.b[s2][l2][c2], vb = b.b[s2][l2][c2];
                    const unsigned anim = tyreAnimMask(va, vb);
                    unsigned pp;
                    for (pp = 0; pp < 4u; pp++) {
                        if (!((anim >> pp) & 1u)) continue;
                        if (((((va >> (7u - pp)) & 1u) << 1) | ((va >> (3u - pp)) & 1u)) == 0u ||
                            ((((vb >> (7u - pp)) & 1u) << 1) | ((vb >> (3u - pp)) & 1u)) == 0u)
                            REVS_DIAG(g_tyreZeroAnim++);
                    }
                }
    }

    /* ⭐ AND THE OUTLINE MASK, from the same animated set the sprites were filled from — so the
       playfield can only ever be cleared at pixels the sprite is responsible for. */
    {
        unsigned s2, l2, c2;
        for (s2 = 0; s2 < 2u; s2++)
            for (l2 = 0; l2 < TYRE_LINES; l2++) {
                unsigned short keep = 0xFFFFu;
                for (c2 = 0; c2 < 2u; c2++) {
                    const unsigned anim = tyreAnimMask(a.b[s2][l2][c2], b.b[s2][l2][c2]);
                    unsigned pp, clear = 0;
                    for (pp = 0; pp < 4u; pp++)
                        if ((anim >> pp) & 1u) clear |= 0xC0u >> (pp * 2u);
                    /* Big-endian plane order: the FIRST cell of the pair is the word's high byte,
                       which is the byte order the bitplane itself is in.  (⚠ This is a PLANE
                       buffer, not `mem[]` — the little-endian `mem[]` aliasing rule does not
                       apply, and `make endian-lint` scopes itself to `mem[]` for that reason.) */
                    keep &= (unsigned short)~(clear << (c2 ? 0u : 8u));
                }
                s_keepW[s2][l2] = keep;
            }
        s_keepReady = 1;
    }

    REVS_DIAG(g_tyreBuilds++);
    return 1;
}

void revs_tyres_outline(unsigned char* planeBase)
{
    unsigned short* p;
    unsigned        l;
    if (!s_keepReady || !planeBase) return;
    /* ⚠ NO MULTIPLY: the first line's byte offset is a compile-time constant and the walk is
       += one line (m68k has no 32-bit multiply, and `make muldiv-audit` fails a link that emits
       the software one). */
    p = (unsigned short*)(planeBase + TYRE_Y0 * TYRE_LINE_STRIDE);
    for (l = 0; l < TYRE_LINES; l++) {
        const unsigned short kl = s_keepW[0][l];
        const unsigned short kr = s_keepW[1][l];
        p[0]  = (unsigned short)(p[0]  & kl);   /* plane 1, cells 0,1   */
        p[19] = (unsigned short)(p[19] & kr);   /* plane 1, cells 38,39 */
        p[20] = (unsigned short)(p[20] & kl);   /* plane 2, cells 0,1   */
        p[39] = (unsigned short)(p[39] & kr);   /* plane 2, cells 38,39 */
        p += TYRE_LINE_STRIDE / 2u;
    }
}
