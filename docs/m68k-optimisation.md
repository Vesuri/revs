# Optimising a native twin for the 68000 (hard-won; apply when a function is hot)

> **⚑ INHERITED VERBATIM from the *Rescue on Fractalus!* Atari-8-bit→Amiga port.**  Every rule
> below was measured on a 7 MHz A500 with the same toolchain (m68k-amiga-elf-gcc + vasm), the
> same `mem[]`-based 6502 transliteration model, and the same vendored framework this port
> uses — so it all applies unchanged.  The worked examples name *that* project's functions
> (`terrain_column_rasterize_core`, the SFX mixer, …); read them as evidence, not as
> references to code in this tree.  Add Revs's own measured cases as they arrive, and do not
> soften a rule without a measurement that contradicts it.
>
> **Read this before optimising any hot function or writing an asm twin.** The two rules that
> must not be violated even without reading this file (RAM is uniformly slow; never emit a
> 32-bit software mul/div) are stated in `CLAUDE.md` §Hard rules.
> Companions: `docs/perf-method.md` (how to get a number you can trust),
> `docs/amiga-lessons.md`, `docs/method-lessons.md`.

The transliteration→native step gets you a *correct* twin; it is NOT fast. The transliterated
style (`mem[addr]` for every access, `bus_read`/`bus_write`, per-op temporaries) is memory-bound
on the 68000. **The dominant cost is the number of memory accesses, not arithmetic** — the
68000 has no cache, every load/store goes to RAM, and `mem[]` is `volatile` (shared with the
VBI/audio ISRs) so the compiler can't cache, batch, or reorder a single access.

⚠ **RAM is slow REGARDLESS of address — do NOT reason in terms of "FAST RAM vs CHIP RAM".** The
target is a bare **A500 with NO real fast RAM**. Any "fast RAM" an A500 has is almost always
"slow RAM" (trapdoor/ranger) on the SAME bus as chip RAM, and even genuine fast RAM is not much
faster. So treat **every** memory access — `mem[]`, chip bitmaps/sprites, the stack (hence every
subroutine call/return) — as uniformly expensive. The lever is **reducing the number of reads and
writes**, full stop; never justify one buffer being cheaper than another by which "kind" of RAM it
lives in, and never dismiss a copy as cheap because it's "fast RAM". (This has been a recurring
mistake — the old `&mem[0]`≈`0x264fe8` "it's fast RAM" note was wrong-headed and is retired.)
A zero-copy scheme that avoids moving data beats any scheme that moves it, independent of address.
Rewrite hot functions in idiomatic C:

- **Keep loop scratch / running pointers / loop-invariants in locals (registers), not `mem[]`.**
  The transliteration re-reads/-writes ZP scratch every iteration (e.g. `terrain_collision_and_silhouette` hit
  `$80/$81/$95/$96` ~13×/iter). Hoist them into locals; write back only the *final* value the
  6502 oracle leaves in `mem[]` (the harness only compares post-return state, so intermediate
  ZP writes that the next iteration overwrites are dead — skip them). Cache invariants
  (`$00A0-$00A3` etc.) into locals once before the loop.
- **Pointer-walk with autoincrement, never multiply+index in a loop.** Replace
  `M[base + i*stride + Y]` (a 68000 `mulu` + indexed load each step) with a pointer advanced by
  `p += stride` / `p -= stride` (`move (a0)+` / `-(a0)`). Reuse a walked pointer across phases
  where the geometry allows (collision's scan leaves the pointer at row k, so the waterfall
  steps it back down with no fresh multiply).
  ⚠ **This rule kills a `mulu`+index — it does NOT beat an UNROLLED absolute scan.** Over a
  short fixed-length array GCC often emits straight-line absolute code (`move.b (base+i).l,dn`
  16 cyc + `beq.s` 10 = 26/element), which is *cheaper* than a pointer loop (`tst.b (a0)+` 8 +
  `beq.s` 10 + `addq.l #1,a1` 8 + `dbra` 10 = 36/element): the 18 cycles of loop bookkeeping
  exceed the 8 that autoincrement saves on addressing. Measured on the SFX mixer's 12-slot
  scans, where a "clean" pointer-walked asm twin came out **5% slower than the C**; the fix was
  to unroll as well and keep `(a0)+` only for the per-element test. **So: disassemble what GCC
  emitted BEFORE designing the asm** — if it already inlined and unrolled, you must beat
  straight-line code, and the headroom is small. Watch the prologue too: a 10-register `movem`
  costs ~180 cycles against GCC's 3-register ~68, which can exceed the whole win.
  ⚠⚠ **GCC UNDOES this rule when a loop walks 3+ pointers — and the exit test is the one-line fix.**
  ivopts strength-reduces N pointer IVs into ONE index register plus N invariant bases, so every
  access becomes `(0,An,Dn.L)`: **14 cycles of EA for a long against `(An)+`'s 8**, on top of losing
  the free increment. The full recipe, all three parts measured (RoF log §14/§15):
  1. **Exit test = a POINTER COMPARE against a precomputed end** (`do { … } while (p != pEnd);`).
     `for (int n = count; n--; )` invites the strength-reduction; the pointer compare forces one IV
     to be a real pointer and GCC then keeps them all. Band paint **170 → 121 cyc/long**.
  2. **Post-increment EVERY pointer.** Leaving one as `*p` with a separate `addq` cost **16
     cyc/long** — GCC emitted `move.l (a0),d0` plus two `addq.l #4` instead of two `(a0)+`.
  3. **Constant trip count ⇒ `#pragma GCC unroll N`**, which deletes the loop bookkeeping outright
     (`(d16,An)` displacement compares, no counter at all). Change-detect scan **70 → 46**.
  Also **split a fused loop that carries pointers only its RARE path needs** — the band's scan was
  maintaining a decode-loop bound on every unchanged long. Four wins in this tree have now turned on
  this one pathology, so **read the disassembly of any hot multi-pointer loop before assuming its
  cost is the work it does.**
- **Batch bulk clears/copies with `move.l` through a NON-VOLATILE alias** of `mem[]`
  (`uint8_t* M = (uint8_t*)mem;`). Casting away `volatile` lets the compiler emit 4-byte stores
  and a tight loop. SAFE only for buffers the ISR doesn't touch concurrently — the main loop
  owns the `$1010+` terrain field (verified the flight VBI never writes it); ZP and ISR-shared
  regions must stay `volatile`. `move.l` needs an even/4-aligned address (odd → 68000 address
  fault) — align first (see `zero_run`). ⚠ The win is from **`move.l` batching of SEQUENTIAL
  bytes**, NOT from dropping `volatile` per se. For SCATTERED single-byte access (e.g. the terrain
  rasterizer's per-column PLOT + Y-walked interpolation arrays) a non-volatile alias is a measured
  **no-op** — GCC already keeps the base in a register, so there is nothing to batch. Don't chase
  volatile-vs-non-volatile for scattered access; that whole class of "cheaper mem access" is
  exhausted there — the cost is instruction count / algorithm, not the `volatile` barrier.
  ⚠ **Endianness when aliasing `mem[]` as `uint16_t*`/`uint32_t*`:** `mem[]` is little-endian
  (6502: `mem[a]`=lo). The Amiga 68000 is **big-endian**, so a word/long read through such an
  alias returns the **byte-swapped** value — and worse, the SDL validation host is little-endian,
  so `make validate` passes GREEN while the Amiga silently renders garbage. So do NOT alias for
  general 16/32-bit values; lift them into `uint16_t`/`int16_t` LOCALS and touch `mem[]` byte-wise
  at the boundaries (`mem[a] | (mem[a+1]<<8)`), as the rasterizer/`MIDPOINT` twins do. The ONE
  safe alias case is a **uniform-byte broadcast** store (e.g. fill 4 lanes with the same byte via
  `grp = b*0x01010101u`, walk a `uint32_t*`): all bytes equal ⇒ endianness-neutral (identical on
  host + Amiga). Used for `terrain_draw_frame_core`'s `$BD00` column-id fill (commit ac3a9a8) —
  46 long stores; still needs the 4-aligned + ISR-untouched + non-overflowing-lane conditions.
  ⚠⚠ **GCC CAN SILENTLY UNDO THE BATCHING — always re-read the disassembly.** A uniform fill written
  as a plain `uint32_t*` loop is recognised as a **memset** and becomes `jsr memset`, and this build's
  freestanding memset (`support/gcc8_c_support.c`) is a byte-at-a-time `move.b d0,(a0)+`/`cmpa.l`/
  `bne` loop at ~24 cycles a byte — i.e. it hands every byte write straight back and the "batching"
  is worth nothing. Keeping the pointer **`volatile`** is what pins the long stores (commit 688069d,
  `terrain_draw_frame_core`'s `$264E..$26D1` fill). ⚠ But volatile is not a free win either: over a
  SHORT trip count with two interleaved volatile long pointers GCC emitted a redundant volatile READ
  before every byte store — measured on the adjacent `$67` fill and reverted. So: batch, then LOOK at
  what was emitted; the source saying `move.l` guarantees nothing.
  ⚠ Also worth knowing: **"odd address" can mean odd OFFSET, not odd address.** The four `$6B` runs
  at `$264E/$266F/$2690/$26B1` carried a comment saying they could not be batched because
  `$266F`/`$26B1` are odd — they are odd only as offsets from `$260E`; every actual address is even,
  which is all `move.l` needs on a 68000 (it faults on ODD, 4-alignment is a 68020+ perf matter). And
  those four `$21`-byte runs ABUT, so they are really one contiguous 132-byte fill = 33 longs.
- **Skip redundant work the original wasted.** Avoid re-decoding/-scanning what hasn't changed
  (per-writer dirty flags; dirty row/cell ranges, cf. planet viewport `g_planetRowLo/Hi` and the
  cockpit plan the RoF cockpit render plan). Shadow-compare scans are themselves a full
  volatile scan — a 68000 no-go; prefer dirty flags.
- **A transliterated loop's 6502 shape can BLIND GCC's loop analysis — that costs far more than the
  instructions it emits.** Two habits do it, and both look harmless: a loop counter/index typed
  `uint8_t` because the 6502 held it in a register (every use then pays an `andi.l #255` + a
  `moveq`/`move.b` zero-extend, and the wrap semantics hide the stride), and a `mem[]` round trip
  the 6502 needed to save a register across a `JSR` (which, being `volatile`, is an opaque write GCC
  must assume changes the index). Remove both and GCC can suddenly see a constant stride and a fixed
  trip count. On `terrain_draw_objects` (RoF log §11) that turned an un-analysable loop into a ×3 unroll
  with ONE exit test — amortising the loop tail 22 → 6 cycles a pair, the largest single component
  of the win. **So when a hot loop's index is a byte or round-trips through `mem[]`, fix that
  first and re-read the disassembly before designing anything cleverer.**
- **When the idiomatic-C twin is still hot, ESCALATE to hand-written m68k asm** (vasm). GCC won't
  emit `(a0)+`, has no scaled index, and spills under the register pressure these loops create — so
  the C floor is GCC's floor, not the algorithm's. In asm you control the regs (pin the working set,
  walk a private stack with `(a3)±3`), force the addressing, and shave every redundant insn
  (`movea` copies, `and.w #$FF` after a `sub.b` into an already-zero-extended reg, `moveq#0;move.b`
  → `move.l` of a clean reg). This beat the C on `terrain_column_rasterize_core` (~27%) where four C
  restructurings had all regressed. Verify with the in-process differential (see `docs/perf-method.md`),
  NOT cross-run. See the RoF asm-migration plan + its `TerrainRasterizeAssembler.s`.
- **⚠ NEVER emit a 32-bit software multiply/divide (`__mulsi3`/`__divsi3`/`__udivsi3`/`__modsi3`/
  `__umodsi3`).** The 68000 has NO 32-bit mul/div — GCC lowers any `uint32_t`/`int32_t` `*` / `/` / `%`
  into those slow (~200-600 cyc) software routines. It has only `MULU.W`/`MULS.W` (16×16→32) and
  `DIVU.W`/`DIVS.W` (32÷16→16q+16r). Use the helpers in **`src/cpu/m68k_math.h`** — `revs_mulu16`,
  `revs_divu16`, `revs_modu16`, `revs_muls16`, `revs_divs16`, `revs_mods16` (inline asm on Amiga, plain-C
  on the SDL/validate host) — wherever a product's factors fit 16 bits and a quotient fits 16 bits
  (verify the ranges!). Techniques when a value looks 32-bit: fold constant factors with the exact
  identity `⌊n/(a·b)⌋ = ⌊⌊n/a⌋/b⌋` so the runtime divide shrinks to 16-bit (see `pokey_period`);
  reduce with `(a·b)%m = ((a%m)·(b%m))%m` (see `build_poly_dist`); replace a small-modulus wrap in a
  loop with compare-subtract (`if (x>=m) x-=m`); clamp an input so `2·x` stays <2^16. **Audit after
  any perf/math change:** `m68k-amiga-elf-objdump -d out/Revs.elf | grep -E '__(u?div|u?mod|mul)si3'`
  must be empty (bodies unreferenced → not even linked). In this port `amiga/Makefile` runs that
  audit on EVERY link (the `muldiv-audit` target), so a regression is caught the moment it is
  introduced rather than months later.

## ⚠⚠ `ROR`/`ROL` DO NOT AFFECT X — and BCD arithmetic needs X

`ABCD` / `SBCD` / `ADDX` / `SUBX` / `NEGX` take their carry-in from **X**, not C.  Getting a C
variable into X is therefore not `ror.b #1,<reg>`: **ROR and ROL leave X untouched.**  Only the
shifts (`ASL`/`ASR`/`LSL`/`LSR`) and `ROXL`/`ROXR` write it — and ROXR/ROXL also *read* X, so
they are the wrong tool for seeding it.  **Use `lsr.b #1,<reg>`** on a 0/1 byte.

Getting X back out: `moveq #0,<d>` then `addx.b <d>,<d>` — MOVEQ writes N/Z/V/C but **not** X,
so it can sit anywhere before the ADDX.

⚠ This class of bug assembles cleanly and disassembles to exactly the instructions you intended,
so an objdump review cannot catch it.  `src/cpu/bcd.h` shipped it briefly and it was found only
by an on-target sweep (`make BCDSELFTEST=1 PROBES=1`, 4500 add / 10000 sub failures out of
40000).  **Inline asm for this target is unverified until it has RUN on the target.**

## ⭐⭐ THE 68000 HAS EIGHT ADDRESS REGISTERS AND A HOT LOOP CAN EXHAUST THEM — read the objdump

The framebuffer decode's per-cell scan cost ~140 cycles of which only ~50 were the four longword
reads it exists to do. The rest was register pressure, and the objdump names it in two tells:

- **`tst.l <n>(sp)` / any `<n>(sp)` operand on a value that cannot change inside the loop.** Two of
  those (`tst.l 48(sp)` + `tst.l 52(sp)`, re-reading `shadowRow` and `rowDirty`) were **32 cycles a
  cell**, 23% of the loop, spent re-deciding something settled before it started. GCC did not hoist
  them because it had nowhere to hoist them TO.
- **Pointers living in `d` registers**, copied into `a0`/`a1` at the top of each iteration, plus a
  spill/reload of an `a` register around an inner call site. An address in a data register is not a
  style choice the compiler made; it is the allocator telling you it ran out.

⭐ **The fix is usually to SPLIT the loop, not to hand-optimise it.** Fusing "find what changed"
with "act on what changed" is what creates the pressure: each half needs its own set of pointers and
they are live simultaneously. Two passes over a small stack array (`uint8_t changed[40]`) each fit
in registers, and the array never leaves cache. 128 → ~80 cycles on the common path, and the second
pass unrolls cleanly because it no longer carries the first pass's induction variables.

⭐ **Three companion tricks from the same rewrite:**
- **`#pragma GCC unroll N` works on this toolchain (GCC 15.1.0), and -O3 will NOT unroll a
  constant-trip-count 8-iteration loop unasked.** Do not assume a small fixed loop is already flat —
  check, then ask for it. Unrolling also turns indexed `(0,a4,a0.l)` addressing into constant
  displacements off one base.
- **`__builtin_expect` on the *condition*, spelled as the mismatch, not the match.** Written as
  `if (clean) continue;` GCC hoisted the second compare out of line and routed the COMMON case
  through two taken `beq.w`. Written `if (__builtin_expect(a != b || c != d, 0))` the common case
  falls straight through. Same semantics, ~8% of the loop.
- **A test that is invariant across the inner loop belongs in a specialisation, not in the loop.**
  Classifying a row once and dispatching a 3-arm switch into `always_inline` variants deletes a
  per-line `mode[]` test from ~17 of 19 rows — the general form of the `always_inline` rule already
  in this file, applied to a *predicate* rather than to a descriptor pointer.

⚠ Converting a cycle count into a predicted millisecond figure on this target needs a **contention
factor of ~1.6** (measured: 30 µs for a ~140-cycle cell at 7.09 MHz), and it applies to instruction
fetch as much as to data. With it the prediction here landed within 15% of the measurement.

## ⚠⚠ MAKING A FUNCTION SMALLER CAN MAKE IT SLOWER — GCC's inlining threshold is part of the change

Measured on the span rasteriser, 2026-09-13, and it is a **1.4-percentage-point swing from one
inlining decision**:

`span_entry_decode(const SpanArm *arm, ...)` searched three `static const uint8_t[8]` tables to
turn a patched entry offset into a column index. Replacing the two linear scans with `switch`
statements deleted ~300 cycles of `move.b <abs.l>` table reads per span — and **measured −0.6%**.
The cause was in the objdump: the smaller body dropped under GCC's inlining threshold, so the
routine that had been *inlined into all four arm specialisations* became **one shared out-of-line
copy** — and a shared copy has to take `arm` as a **pointer** again, which puts `arm->steep` back
in memory and adds a five-argument call per span. The same switch with
`inline __attribute__((always_inline))` measured **+0.8%**.

⭐⭐ **The rule: when a hot routine is fast BECAUSE it is specialised, its `always_inline` is part of
its correctness-for-speed, and any edit that changes its size can silently revoke it.** Pin it
explicitly rather than relying on the size heuristic, and **re-read the call list in the objdump
after the edit** (`jsr <name>` appearing where there were none is the whole tell).

⭐ And the sibling half of the descriptor rule, from the same pass: **`always_inline` on the LEAF
does not fold a descriptor — the SELECTION has to be specialised too.** `span_plot_core` was
`always_inline` exactly so `SPAN_PLOT_1`/`SPAN_PLOT_2`'s fields would become immediates, but its
caller picked `usePlot2 ? &SPAN_PLOT_2 : &SPAN_PLOT_1`, so the descriptor was a runtime value
*inside* the inlined leaf and the objdump read `lea SPAN_PLOT_2,a2` / `move.l (a2),d2` /
`move.l 8(a2),d3`. Two `noinline` twins, one per descriptor, is the fix — `noinline` deliberately,
because the leaf is ~370 instructions and there are twenty call sites.

⚠⚠ **And the framing error that sent this pass at the wrong target: divide by the right
denominator.** "24 ms in the span walk / 49 DDA scan lines = ~3 500 cycles per scan line" made the
walk's inner loop look catastrophic. The honest denominator was **43 spans**, and the weight was in
per-span setup, not per-line work. **Before optimising a loop, check how many times it actually
runs** — `docs/perf-method.md` §the span kernel's call and search surface.
