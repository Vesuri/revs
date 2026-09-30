/* engine_image.c — the engine's runtime image from the player's disc.  engine_image.h has the
 * model; tools/relocate.py has the derivation of every constant in the unpack replay below and
 * its verification against a real BBC. */
#include "engine_image.h"
#include "../gen/revs_tracks.h"

/* ---- DFS catalogue ------------------------------------------------------------------------
   Sector 0: bytes 8+8i are entry i's name (7 bytes, space padded) and directory (bit 7 = the
   lock flag).  Sector 1: byte 5 is 8 x the entry count; bytes 8+8i are load lo/hi, exec lo/hi,
   length lo/hi, one byte of high bits (b0-1 start sector, b2-3 load, b4-5 length, b6-7 exec)
   and the start sector's low byte. */
int engine_find_revs2(const unsigned char* cat, unsigned long* offset)
{
    static const char name[7] = { 'R', 'E', 'V', 'S', '2', ' ', ' ' };
    unsigned n = cat[256 + 5] >> 3, i, k;
    for (i = 0; i < n && i < 31; i++) {
        const unsigned char* e = cat + 8 + 8 * i;
        const unsigned char* f = cat + 256 + 8 + 8 * i;
        unsigned long len, sector;
        unsigned load;
        for (k = 0; k < 7; k++) if ((e[k] & 0x7F) != (unsigned char)name[k]) break;
        if (k < 7 || (e[7] & 0x7F) != '$') continue;
        load   = f[0] | (unsigned)f[1] << 8;
        len    = f[4] | (unsigned long)f[5] << 8 | (unsigned long)((f[6] >> 4) & 3) << 16;
        sector = f[7] | (unsigned long)(f[6] & 3) << 8;
        if (load != ENGINE_REVS2_LOAD || len != ENGINE_REVS2_LENGTH) return ENGINE_BAD_REVS2;
        *offset = sector << 8;
        return ENGINE_OK;
    }
    return ENGINE_NO_REVS2;
}

/* CRC-32 (IEEE, reflected), a nibble at a time: sixteen table entries, and shifts and XORs only —
   no multiply or divide for the 68000 to lack.  ~0.1 s over REVS2 on an A500, once. */
static unsigned long crc32(const unsigned char* p, unsigned long n)
{
    static const unsigned long t[16] = {
        0x00000000uL, 0x1DB71064uL, 0x3B6E20C8uL, 0x26D930ACuL,
        0x76DC4190uL, 0x6B6B51F4uL, 0x4DB26158uL, 0x5005713CuL,
        0xEDB88320uL, 0xF00F9344uL, 0xD6D6A3E8uL, 0xCB61B38CuL,
        0x9B64C2B0uL, 0x86D3D2D4uL, 0xA00AE278uL, 0xBDBDF21CuL };
    unsigned long c = 0xFFFFFFFFuL;
    while (n--) {
        c ^= *p++;
        c = (c >> 4) ^ t[c & 15];
        c = (c >> 4) ^ t[c & 15];
    }
    return c ^ 0xFFFFFFFFuL;
}

static void copy_bytes(unsigned char* dst, const unsigned char* src, unsigned long n)
{
    while (n--) *dst++ = *src++;
}

/* ---- the engine's own self-unpack, $1200 → $79AA (relocate.py numbers the steps) ---------- */
#define STUB            0x7900u
#define SWAP_SRC        0x5300u
#define SWAP_DST        0x70DBu
#define TBL_SRC_LO      0x79AFu
#define TBL_SRC_HI      0x79B4u
#define TBL_END_LO      0x79B9u
#define TBL_END_HI      0x79BEu
#define TBL_DST_LO      0x79C3u
#define TBL_DST_HI      0x79C8u
#define PATCH_FROM      0x79ADu
#define PATCH_TO        0x7978u

static unsigned word_at(const unsigned char* m, unsigned lo, unsigned hi, unsigned x)
{
    return m[lo + x] | (unsigned)m[hi + x] << 8;
}

static void relocate(unsigned char* m)
{
    unsigned x;

    /* 1. the entry page is copied up to $7900, where the rest of the stub runs from. */
    copy_bytes(m + STUB, m + ENGINE_REVS2_LOAD, 0x100);

    /* 3. the swap $70DB-$77FF <-> $5300-$5A24: the loop ends at Y = $25 on dest page $77.  The
       rolling checksum it keeps in $7800-$7803 is not replayed (engine_image.h); those four
       cells are inside the circuit tail laid over the result below. */
    for (x = 0; x < 0x7800u - SWAP_DST; x++) {
        unsigned char t = m[SWAP_DST + x];
        m[SWAP_DST + x] = m[SWAP_SRC + x];
        m[SWAP_SRC + x] = t;
    }

    /* 4. four block copies, X = 4 down to 1 — the ORDER is load-bearing (X=2 overwrites X=4's
       and X=3's sources) — driven by the tables in the stub COPY, which X=2 would overwrite at
       $12xx. */
    for (x = 4; x >= 1; x--) {
        unsigned src = word_at(m, TBL_SRC_LO, TBL_SRC_HI, x);
        unsigned end = word_at(m, TBL_END_LO, TBL_END_HI, x);
        unsigned dst = word_at(m, TBL_DST_LO, TBL_DST_HI, x);
        unsigned i;
        for (i = 0; i < end - src; i++) m[dst + i] = m[src + i];   /* ascending, as the 6502 */
    }

    /* 5. the stub patches its own mover into a zero-filler and runs X = 0. */
    m[PATCH_TO]     = m[PATCH_FROM];
    m[PATCH_TO + 1] = m[PATCH_FROM + 1];
    {
        unsigned src = word_at(m, TBL_SRC_LO, TBL_SRC_HI, 0);
        unsigned end = word_at(m, TBL_END_LO, TBL_END_HI, 0);
        unsigned dst = word_at(m, TBL_DST_LO, TBL_DST_HI, 0);
        unsigned i;
        for (i = 0; i < end - src; i++) m[dst + i] = 0;
    }
    /* 6. JMP $63BD — engine_main, which the transliteration enters itself. */
}

int engine_build_image(unsigned char* mem)
{
    unsigned long i;
    if (crc32(mem + ENGINE_REVS2_LOAD, ENGINE_REVS2_LENGTH) != ENGINE_REVS2_CRC32)
        return ENGINE_BAD_CRC;

    /* What the menu's `*LO.SILVER` + `*RUN REVS2` leave: REVS2 at $1200 and nothing else the
       replay reads — the track file's bytes all land in the two extents replaced below. */
    for (i = 0; i < ENGINE_REVS2_LOAD; i++) mem[i] = 0;
    for (i = ENGINE_REVS2_LOAD + ENGINE_REVS2_LENGTH; i < 0x10000uL; i++) mem[i] = 0;

    relocate(mem);

    /* Silverstone, from the exe: the circuit the image boots with, as revs_runtime.bin has it.
       The circuit menu installs any other one over this exactly as it always has. */
    copy_bytes(mem + REVS_TRACK_BLOCK_LO, revs_tracks[0].block, REVS_TRACK_BLOCK_LEN);
    copy_bytes(mem + REVS_TRACK_TAIL_LO,  revs_tracks[0].tail,  REVS_TRACK_TAIL_LEN);
    return ENGINE_OK;
}

const char* engine_status_text(int status)
{
    switch (status) {
    case ENGINE_OK:          return "ok";
    case ENGINE_NO_REVS2:    return "the disc image has no REVS2 file";
    case ENGINE_BAD_REVS2:   return "the disc's REVS2 is not the 1986 engine";
    case ENGINE_BAD_CRC:     return "the disc's REVS2 does not match the supported version";
    default:                 return "the disc image could not be read";
    }
}
