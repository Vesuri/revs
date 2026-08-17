# The static map — Phase 2 findings

> What the binary actually looks like, derived from `revs.ssd` and cross-checked against a real
> BBC via jsbeeb.  `docs/entrypoint-sweep.md` is the *method*; this is the *result*.
>
> Regenerate the evidence any time: `make runtime && make sweep TRACE=tmp/trace_SILVER.bin`
> (`disasm/sweep.txt`, git-ignored because it is regenerable).

## ⭐⭐ The finding that reorders everything: REVS2 unpacks itself

**`disasm/revs_mem.bin` is not the layout the engine executes.**  The first thing REVS2 does is
relocate itself, and the code the game spends its life in lives at addresses that hold something
else entirely in the loaded image.  Disassembling `revs_mem.bin` produces a listing that is wrong
precisely where it matters.

This was found by importing `revs_mem.bin` into Ghidra and reading the *seven instructions* that
resulted — the auto-analysis followed `$1200` and stopped dead at a `JMP` into a region that is
all `$00` before the copy happens.  A listing that short is a gift; the expensive version of this
mistake is a listing long enough to look plausible.

### What it does, in order

| Step | Where | What |
|---|---|---|
| 1 | `$1200` | copy the 256-byte entry page `$1200-$12FF` to `$7900`, `JMP $790E` — from here the stub runs from the copy, so it may overwrite `$1200` |
| 2 | `$790E` | `OSBYTE 200,3` (disable ESCAPE, clear memory on BREAK), `OSBYTE 140,0` |
| 3 | `$792E` | **swap** `$70DB-$7800` with `$5300-$5A25`, accumulating a rolling 4-way checksum in `$7800-$7803` (`AND #3 / TAX / DEC $7800,X`).  All four cells must end zero or the stub does `JMP ($FFFC)` — a **reset**.  An integrity check, and the reason the track data ends up at `$5300` instead of where DFS put it |
| 4 | `$7960` | five block moves from three 5-byte tables at `$79AF/$79B4` (src lo/hi), `$79B9/$79BE` (src end), `$79C3/$79C8` (dst), run **X=4 down to X=1** |
| 5 | `$799B` | **patches its own copy loop** — writes `A9 00` (`LDA #0`) over the `LDA ($70),Y` at `$7978`, then runs the X=0 entry, so the last "move" *zero-fills* instead of copying |
| 6 | `$79AA` | `JMP $63BD` — into the unpacked engine |

The moves, in execution order:

```
X=4   copy $1500-$15DB -> $7000   (219)
X=3   copy $1300-$1500 -> $0B00   (512)
X=2   copy $5A80-$645C -> $0D00   (2524)
X=1   copy $64D0-$6C00 -> $5FD0   (1840)
X=0   zero-fill        $5A80-$5E40 (960)
```

**Two traps, both of which produced a plausible wrong answer before being caught:**

- **The order is load-bearing.** X=4 and X=3 read `$1500-$15DB` and `$1300-$1500`; X=2 then
  overwrites `$0D00-$16DB`, which covers both sources.  Replaying the moves in any other order
  yields a clean-looking, wrong image.
- **The tables must be read from the stub copy at `$79xx`, never from `$12xx`.**  X=2's
  destination runs to `$16DB`, so it overwrites the tables *at `$12AB-$12CD`* halfway through the
  sequence.  Reading them from `$12xx` gives a correct first move and then garbage — it reported
  a confident "engine entry `$919D`" before being fixed.  The self-copy in step 1 exists exactly
  so the stub outlives its own source bytes; the replay has to honour that.

### Confidence: measured, not reasoned

`tools/relocate.py` replays all six steps.  Applied to a jsbeeb RAM dump taken at the REVS2 entry
and compared with a dump taken after the engine started, it accounts for **9640 of the 10168
bytes that differ**.  The 528-byte residue is zero page, the stack, the MOS vector page, MODE 7
screen RAM, and **54 bytes inside the engine range** — the latter being genuine runtime state and
self-modification, not relocation.  The integrity checksum clears (`$7800-$7803` = `00 00 00 00`)
in both the replay and the real machine.

```sh
make runtime VERIFY="tmp/dump_SILVER_before.bin tmp/dump_SILVER_after.bin"
```

### Consequences

1. **`disasm/revs_runtime.bin` (`make runtime`) is the disassembly input.**  `revs_mem.bin`
   remains the honest record of what the loader produces, and the input to the replay.
2. Addresses in `ghidra_scripts/entrypoints.csv`, `disasm/symbols.csv` and every note from here on
   are **runtime-image addresses**.
3. A `.ssd`-derived byte offset is not an engine address.  Two different regions of the loaded
   image can map to the same runtime address, and one runtime address can hold two different
   things over time.
4. The staging regions the moves read from (`$64D0-$6C00`, `$1300-$15DB`, `$5A80-$645C`) are
   **dead after startup** — plausibly a large part of the unclassified residue below.

## The entry-point sweep

`tools/sweep_entrypoints.py` — a recursive-descent 6502 walk over the runtime image, deliberately
independent of Ghidra so the two can be diffed.  They agree: **7079 instructions / 222 called
subroutines** (sweep) vs **7083 instructions / 236 functions** (Ghidra).  Two tools, one
answer — which is worth more than either number alone.

### Roots that no static walk can reach

| Root | What | Evidence |
|---|---|---|
| `$63BD` | **the engine entry** | target of the unpack stub's closing `JMP`, read from `$79AB` |
| `$4E5C` | **`IRQ1V` handler — the 50 Hz game body** | `LDA #$5C / STA $0204` at `$4E54`, `LDA #$4E / STA $0205` at `$4E4F` |

`$4E5C` is postmortem finding #1.1 in its purest form: nothing in the binary jumps to it, the MOS
does, so a plain walk misses it and every conclusion drawn from the resulting listing is wrong.
The sweep now resolves `LDA #lo / STA vector` pairs into a handler address and re-walks to a
fixpoint; that one rule added 116 instructions the first pass never saw.

The handler chains on to the previous `IRQ1V` via `JMP ($4F1D)` at `$4E59`, and `$4F23-$4F2D`
restores the saved vector from `$4F1D/$4F1E` on the way out.  A vector written from a *saved copy*
is a restore, not a Revs entry point — the sweep classifies the two separately, because seeding a
restore sends the walk off into MOS ROM.

### What the sweep did NOT find

- **No `JMP ($xxxx)` anywhere except `$4E59`** (the IRQ chain-on).
- **No RTS-trick dispatch** (`PHA`/`PHA`/…/`RTS`) on reachable code.
- **No `JMP abs` jump tables** indexed by a multiply-by-3.

That is a genuinely simpler dispatch structure than the postmortem's worst case, and worth
stating plainly rather than leaving as an absence.  ⚠ It is an absence of evidence in *this*
track's runtime image: the four expansion tracks patch the engine, so re-run the sweep per track
before treating it as settled (open item below).

### Only `IRQ1V` is claimed

`$0204/$0205`, from `$4E4F/$4E54`.  Nothing writes BRKV, EVNTV, or any filing-system vector.

⚠ The sweep also reports 33 **indexed** stores whose 256-byte span could technically reach the
vector page — `STA $013B,X`, `STA $018C,Y`, and so on.  All have bases in `$0114-$01A4`: page 1
used as per-car object arrays.

✅ **Now settled, and proven rather than judged.**  The naming pass found the two index-wrap
helpers — `$507E` (DEX, wrapping to 19) and `$5084` (INX, wrapping to 0 at 20) — which fix the
field at **20 cars**.  So the maximum index is 19, the highest reachable address is
`$01A4 + 19 = $01B7`, and none of the 33 stores can touch `$0202`.  Worth noting how the answer
arrived: not by bounding the loops, but by finding the routine whose whole job is to say what the
bound *is*.

### MOS calls Revs makes — the complete list, with reason codes

**Four MOS entries, 17 call sites** — that is the whole OS surface `Platform::mosCall` has to
service, and it is far smaller than `docs/bbc-hardware.md`'s speculative table (which listed the
whole filing-system family; **Revs calls none of it** — the engine never touches OSFILE, OSARGS,
OSBGET, OSBPUT, OSGBPB or OSFIND, because the BASIC front end did all the loading).

But "Revs calls OSBYTE" says almost nothing: OSBYTE dispatches on A, so the 10 sites are **8
different OS services**.  The sweep recovers each by walking back to the nearest preceding
`LDA #imm`:

| Entry | Site | A | Service |
|---|---|---|---|
| OSBYTE | `$0E54` | `$81` (129) | **negative INKEY** — is key code X held?  *the* input primitive |
| OSBYTE | `$168E` | `$80` (128) | **read ADC channel** — ⭐ *the steering input* |
| OSBYTE | `$5041` | `$80` (128) | **read ADC channel** |
| OSBYTE | `$3879` | `$BE` (190) | read/write ADC conversion type (uPD7002 resolution) |
| OSBYTE | `$4DF5` | `$9A` (154) | write the Video ULA control register (`$FE20`) via its OS copy |
| OSBYTE | `$0E6B` | `$15` (21) | flush a buffer (X = buffer) |
| OSBYTE | `$6311` | `$15` (21) | flush a buffer |
| OSBYTE | `$3856` | `$04` (4) | cursor-key / copy-key behaviour |
| OSBYTE | `$630A` | `$02` (2) | select input stream |
| OSBYTE | `$6349` | `$7E` (126) | acknowledge ESCAPE |
| OSWORD | `$0B70` | `$08` (8) | **define a sound ENVELOPE** — so sound goes through the MOS, not the SN76489 directly |
| OSWORD | `$50A7` | `$0A` (10) | read a character definition |
| OSWRCH | `$3EF3` | `$7F` | VDU 127 (delete) |
| OSWRCH | `$633F` | `$07` | VDU 7 (bell) |
| OSWRCH | `$6651` | `$9C` | VDU output |
| OSWRCH | `$50F6` | from a variable | VDU output — A is not statically knowable |
| OSRDCH | `$6316` | `$15` | read a character |

Plus, in the loader stub only: `OSBYTE 200,3` (disable ESCAPE / clear on BREAK) and `OSBYTE 140,0`
(select filing system) at `$7914`/`$791B`.

⚠ The reason code is recovered by a **nearest-preceding-`LDA #imm` heuristic**, so it is wrong
wherever A arrives from another path; `$50F6` is correctly reported as unknown rather than guessed.
Treat the table as **[DERIVED, heuristic]** until a site is confirmed by a trace.

**Three consequences for the port, all of which change design:**

1. **The steering is a MOS call, not a register.**  `OSBYTE 128` / `OSBYTE 190`, never
   `$FEC0-$FEDF`.  The uPD7002 is never addressed directly, so the mouse-replaces-ADC seam
   (`docs/bbc-hardware.md`) sits in `Platform::mosCall`, and there is nothing for `bus_write` to
   intercept.
2. **Sound is a MOS call too** — `OSWORD 8` defines an envelope, which means the SN76489 is driven
   by the MOS's sound scheduler and Revs is issuing high-level SOUND requests.  Mapping that to
   Paula is a different job from poking a sound chip, and the envelope semantics have to be
   reproduced.
3. **Keyboard reading is `OSBYTE 129` negative INKEY**, i.e. "is this specific key down", not a
   buffered character stream. That maps cleanly onto an Amiga key-state array.

### Hardware registers Revs actually touches

Measured, replacing the **[ASSUMED]** rows in `docs/bbc-hardware.md`.  **FRED (`$FC00-$FCFF`) and
JIM (`$FD00-$FDFF`) are untouched, as assumed.  The uPD7002 ADC at `$FEC0-$FEDF` is *never
addressed directly*** — the ADC is reached through `OSBYTE 128` instead, which matters for the
port: the steering seam is a MOS call, not a register.

**19 registers, 4 devices.**  Two independent tools agree on the set: `tools/sweep_entrypoints.py`
(recursive-descent) and `ghidra_scripts/DumpHwAccesses.java` (Ghidra's listing), the latter also
counting the two `IRQ1V` cells.  `make sweep` and `disasm/hw-access.md` regenerate them.

| Addr | Device | RW | Sites |
|---|---|---|---|
| `$FE00` `$FE01` | **6845 CRTC** address/data | W | `$4DE0` / `$4DE6` |
| `$FE20` | **Video ULA control** (mode / pixel rate / flash) | W | `$4E7E` `$4E9D` ⭐ *both in the IRQ handler* |
| `$FE21` | **Video ULA palette** | W | `$4DFB`, and `$4E86` `$4EA3` `$4EC8` `$4EDB` `$4EEC` ⭐ *five in the IRQ handler* |
| `$FE45` `$FE46` `$FE47` | System VIA T1 counter/latch | W | `$4E3A` / `$4E35` `$4E3F` / `$4E47` |
| `$FE4B` | System VIA ACR | R+W | `$4E1B` `$4E1E` |
| `$FE4D` | System VIA IFR | R | `$4E11` |
| `$FE4E` | System VIA IER | W | `$4E26` |
| `$FE64` `$FE65` | User VIA T1 counter | W | `$4E2B` `$4E30` |
| `$FE66` `$FE67` | User VIA T1 latch | W | `$4E42` `$4F04` / `$4E4A` `$4F01` |
| `$FE68` | User VIA T2 counter-low (read = timer value) — ⭐ **the game's only entropy source**, and [MEASURED] to be a 1 MHz free-running DOWN COUNTER, not a PRNG: `make refloop --park --via-t2` finds 22 of 31 successive samples exactly at "previous minus the elapsed microseconds" (`src/platform/bbc_hw.cpp`) | R | `$0E7C` `$274E` `$498C` `$49BD` `$4C06` `$635F` (+ `$7FB6` in the overlay) |
| `$FE69` | User VIA T2 counter-high | W | `$4EFA` |
| `$FE6B` | User VIA ACR | W | `$4E18` |
| `$FE6D` | User VIA IFR | R+W | `$4E5C` `$4E63` |
| `$FE6E` | User VIA IER | W | `$4E23` `$4F32` |

Four things fall out, and each one is a port design decision:

- ⭐⭐ **The display is composed by a raster-band state machine inside the IRQ handler**, not by a
  static mode.  The handler dispatches on a band counter at `$4F43` (values `0`, `1`, `2`, `3`,
  `$FF`), and each band writes the ULA control register and then a full 16-entry palette:

  ```
  4E5C  LDA $FE6D / AND #$40 / BEQ $4E59   ; not our User VIA T1 timeout -> chain on
  4E63  STA $FE6D                          ; acknowledge
  4E69  LDA $4F43                          ; the band counter — 5-way dispatch
  4E7C  LDA #$88 / STA $FE20               ; band 0: one ULA mode…
  4E83  LDA $3468,X / STA $FE21 / DEX/BPL  ;   …+ 16 palette entries from a table at $3468
  4E9B  LDA #$C4 / STA $FE20               ; band 1: a DIFFERENT ULA mode…
  4EA1  LDA #$03 / STA $FE21 / ADC #$10    ;   …+ 16 palette entries generated by stepping
        / BCC $4EA3                        ;      the high nibble, not read from a table
  ```

  `docs/bbc-hardware.md` marked "a two-part display via mid-frame register changes"
  **[ASSUMED]**; it is now **[DERIVED]**, with the sites and the mechanism.  Two consequences for
  the copper list: the viewport needs **at least four bands**, and one band's palette is *computed*
  (`$03`, `$13`, `$23`, … stepping the high nibble) while another is *table-driven* from `$3468` —
  so the copper list is partly static and partly rebuilt per frame.
- **The interrupt is a USER VIA T1 timer, not the System VIA vsync.**  `LDA $FE6D / AND #$40` tests
  the User VIA **T1 timeout**; the handler acknowledges via `$FE6D`, reloads T1 (`$4F01`/`$4F04`)
  and T2 (`$4EFA`), and chains to the saved previous handler with `JMP ($4F1D)` when the interrupt
  is not its own.  So the 50 Hz body is a **programmable timer chain used to hit specific
  scanlines**.  `docs/amiga-arch.md`'s "System VIA 50 Hz vsync IRQ → `INTB_VERTB`" mapping is
  therefore too simple: a raster-timed interrupt that reloads the palette mid-frame is copper work,
  with only the band-0 boundary belonging in VERTB.  ⚠ This needs revisiting before Phase 5
  designs the copper list — it is the kind of assumption the postmortem warns about calcifying.
- **All initial setup is one contiguous routine, `$4DE0-$4E4A`** (`FUN_4ddd`) — CRTC, ULA and both
  VIAs. The platform boundary is one place, not scattered.
- **The User VIA T2 timer is read from six sites all over the engine**, including the keyboard
  helper at `$0E7C`. A free-running timer used as a clock/entropy source — the Amiga backend needs
  a real answer for it, not a stub returning 0.

### Self-modifying code — 24 sites, precisely located

The sweep reports a store as self-modifying only when it lands **inside a byte the walk decoded
as part of an instruction**.  (Flagging every store into `$0B00-$78FF` instead gives 133 hits, of
which the other 109 are ordinary variables that happen to live between routines — a report nobody
would read.)

| Site | Patched | Written from |
|---|---|---|
| `$1970` | operand of `STA $196F` | `$193E` |
| `$1DD5` | operand of `BNE $1DD4` | `$1DA9` |
| `$1DDC` `$1DDE` | operands of `LDA $1DDB` / `STA $1DDD` | `$1DAC` / `$1DA6` |
| `$2D28` `$2DAB` `$2E2F` `$2EA8` | four `BCC` branch offsets | `$2D1A` `$2D9D` `$2E23` `$2E9C` |
| `$2F18` `$2F47` `$2F89` | `NOP`/opcode slots | `$2F15` / `$2C09` `$2CC8` / `$2C0C` `$2CCB` |
| `$2F4F` `$2F50` `$2F91` `$2F92` | operand bytes of `STA $2F4E` / `STA $2F90` | `$19C9` `$19C0` `$19CC` `$19C3` |
| `$2F60` `$2FA2` | `INY` opcode slots | `$2C01` `$2CD0` / `$2C04` `$2CD3` |
| `$2FC0` `$2FD7` | `CPX` opcode slots | `$2CAF` `$2CB9` / `$2CAC` `$2CB6` |

⚠⚠ **A 25th site, found by reading `draw_corner_markers` and NOT by the sweep: `$38FE`.**
`$1B12` writes `$0F` there before drawing a marker whose `marker_flags` has bit 5 set, and `$F0`
back at `$1B74`.  The sweep cannot see it because `$38FE` is `surface_colours + 2` — a *data*
byte, not a decoded instruction — but it is patched code state all the same: it is the background
colour `view_next_scanline` loads for every line, so both values are legal and **no `mem[]`
differential can ever see a frozen one.**  Same class as the rev-counter case in §Open items.
It needs a `data` SMC declaration in `tools/transpile.py` before `$1B12` or `$7EF3` is twinned.

The shape is clear: **`$2C00-$2FFF` is a self-modifying inner loop** — opcode slots switched
between `NOP`/`INY`/`CPX` and store addresses rewritten from `$19C0-$19CC`.  That is a rasteriser
or span-filler specialised at runtime, and it is the single most important region to get right.
Everything in the table above is destined for `src/gen/revs_manual.c` (`docs/faithfulness-seam.md`).

## The per-track engine patches — the hook inventory

`tools/track_hooks.py`.  `docs/entrypoint-sweep.md` called this "the known, named case"; here it
is, measured.

**The method matters, because the obvious diff gives the wrong answer.**
`tools/bbc_refloop_track_diff.mjs` diffs RAM at the REVS2 entry against RAM 1M cycles later and
reads the result as the hook patch surface — its own header predicts *zero* diffs for Silverstone,
whose track file is passive data.  Silverstone shows **5805**.  The bulk of any track's diff is the
engine's own unpack, which happens for every track.  Subtract it first:

```
patch surface(T) = { a : relocate(before_T)[a] != after_T[a] }  −  runtime-state(SILVER)
```

Silverstone is the control: exec `$0000`, no hook code, so its residue *is* the engine's runtime
state.  The `$5300-$5A25` window is excluded — that is where the swap deposits the track's own
geometry, so it differs per track by construction, not by patching.

### The result: five shared patch sites, six or seven hooks per track

| Track | residue | beyond control | code hooks | rebound data operands |
|---|---|---|---|---|
| BRANDS | 113 | 59 | 6 | 11 |
| DONING | 115 | 61 | 7 | 13 |
| OULTON | 111 | 57 | 6 | 11 |
| SNETTER | 111 | 58 | 7 | 12 |

**Every expansion track patches the same 52 bytes**, and the code hooks land at the same five
sites in every one:

| Patched site | Becomes | Target (BRANDS / DONING / OULTON / SNETTER) |
|---|---|---|
| `$1248` | `JSR` | `$5672` — **the same in all four** |
| `$12FB` | `JSR` | `$54F1` / `$54EF` / `$54EF` / `$54EF` |
| `$248B` | `JMP` | `$56BC` — **all four** |
| `$261A` | `JMP` | `$56AF` — **all four** |
| `$2538` | `JSR` | `$5772` — **all four** |
| `$45CB` | `JSR` | `$59E9` / `$59C9` / `$59E7` / `$59C7` |
| `$2F23` | `JSR` | — / `$59ED` / — / `$59E8`  (**Donington and Snetterton only**) |

`$2F23` is the one that earns attention: it is a hook **inside the self-modifying
`$2C00-$2FFF` region**, present for only two of the four circuits.  So the region that is already
runtime-specialised is *also* per-track patched, for some tracks.  That is the hardest single spot
in the binary and it should be treated as such.

### Code hooks vs data rebinding — do not conflate them

Two thirds of the patch bytes are **not** entry points.  A 2-byte patch rewrites an absolute
*operand*, leaving the instruction where it is and pointing it at a table inside the track file:
`$4CC1` → `$5762`, `$4CC9` → `$5662`, `$4CD1` → `$5562`, `$4CD7`/`$4CE1` → `$5462`.  The `$100`
spacing gives it away — that is a row-indexed table, not four hand-written routines.  Only the
3-byte `JSR`/`JMP` patches create new reachable code.

Two 2-byte changes at `$1310` (Donington, Snetterton) point outside the track file and are
**unclassified** — reported rather than rounded off into the pointer group.

### `ModifyGameCode` read directly — and it confirms the differential

The differential above observes memory changing.  The patcher can also just be *read*, and the two
must agree.  They are independent in the way that matters: **a differential cannot tell a patch from
ordinary state, and a static read cannot tell a live table from a dead one.**  Agreement rules out
both failures at once.

`CallTrackHook ($5A22) → JMP $5700` — the same entry in all four expansion tracks — then a chain
`$5700 → $5800 → $5600` (Donington adds a fourth stage at `$53F3`):

```
5700  LDX #$12          ; ⚠ $12 on Brands/Oulton, $13 on Donington/Snetterton
5702  LDA $5414,X / STA $75     ; target hi
5707  LDA $5400,X / STA $74     ; target lo
570E  LDA $5500,X / STA ($74),Y ; first byte
5714  LDA $5514,X / STA ($74),Y ; second byte
571A  BPL $5702                 ; 19 or 20 two-byte patches
571C  LDA #$4C / STA $261A / STA $248B      ; the JMP opcodes
5800  LDA #$20 / STA $1248/$12FB/$2538/$45CB ; the JSR opcodes (+ $2F23 on DONING/SNETTER)
      … then single-byte pokes, RTS
```

So the two-byte "operand rebinding" patches are one table-driven loop, and the opcodes are separate
straight-line pokes.  `tools/track_hooks.py` now checks the tables against the measured diff:

| Track | table entries | decoded pokes | in the tables but never seen changing |
|---|---|---|---|
| BRANDS | 19 | 16 | **0** ✅ |
| DONING | 20 | 20 | **0** ✅ |
| OULTON | 19 | 16 | **0** ✅ |
| SNETTER | 20 | 18 | **0** ✅ |

The only bytes the differential sees that the patcher does not explain are `$5FC9-$5FCD`, which is
engine runtime state (Snetterton reports **0 and 0** — an exact match both ways).

⚠ **Getting there needed one bound read from the code rather than assumed**, the same lesson the
unpack's move tables taught: the patch count is `LDX #n` at `$5701` and it is **not the same for
every track** — `$12` (19) for Brands and Oulton, `$13` (20) for Donington and Snetterton.
Hardcoding 19 made Donington and Snetterton report `$2F24/$2F25` as unexplained, which read like a
real gap in the differential and was an off-by-one in the checker.

### The track program's code extents

Measured for Brands Hatch by sweeping from `$5700` plus the six hook targets: **238 instructions**
inside `$5300-$5A25`, in ten extents — `$5472-$54EA`, `$54F1-$54FF`, `$5582-$55BC`, `$55C4-$5619`,
`$5672-$56C5`, `$5700-$5724`, `$5772-$57A0`, `$5800-$5825`, `$59E9-$59F7`.  Everything else in the
window is track geometry.  So the "executable track file" is ~240 instructions of patcher and hook
bodies wrapped around ~1600 bytes of data.

### Consequences

1. **The hook targets are entry points that exist in no static image**, seeded in
   `ghidra_scripts/entrypoints.csv` as track-conditional.
2. **The five shared patch sites are self-modifying by definition**, on top of the 24 the engine
   does to itself — so `src/gen/revs_manual.c` owes stubs for both sets.
3. **`$5300-$5A25` is code as well as data** for four of the five circuits: the track file is a
   program.  A disassembly pass over that window, per track, is still owed.

## The front end, and the key table

`engine_init` (`$3850`) does `OSBYTE 4,1` (cursor keys act as normal keys), clears `$05F4-$05FD`,
saves the stack pointer to `$6B`, calls **`$5A22`**, sets the ADC conversion type via `OSBYTE 190`,
and `JMP $63E0` — a chain of menus, each one a `JSR key_config_menu ($6571)` that waits until one
of the keys in the table at `$39E0` is held.  The five call sites are `$63F7` (X=2), `$6416` (X=3),
`$6426` (X=3), `$646D` (X=2) and `$64E7`; X is the highest table index on offer.

### ⭐ `$5A22` is `CallTrackHook` — a fixed engine→track-file entry

Not one of the patch sites: it arrives *with* the track data, because the unpack swap deposits the
track file over `$5300-$5A25` and `$5A22` is the last usable slot in it.

| Track | Bytes at `$5A22` | Meaning |
|---|---|---|
| SILVER | `60 EA EA` | `RTS` / `NOP` / `NOP` — a deliberate no-op, since Silverstone has no hook code |
| BRANDS | `4C 00 57` | `JMP $5700` — into the track program |

So the engine always calls the track file at one known address, and the passive circuit supplies a
stub.  That is a much cleaner seam for the port than the patch sites: one call, one target.

### The key table, decoded

`$39E0` holds **negative-INKEY** codes, i.e. the value `OSBYTE 129` wants in X, which is `256 − n`
for `INKEY(−n)`:

| Cell | Byte | `INKEY` | Key |
|---|---|---|---|
| `$39E0` | `$9D` | −99 | **SPACE** |
| `$39E1` | `$CF` | −49 | **1** |
| `$39E2` | `$CE` | −50 | **2** |
| `$39E3` | `$EE` | −18 | **3** |
| `$39E4` | `$DD` → `$00` | −35 | E — **zeroed at runtime** |
| `$39E5` | `$EE` → `$00` | −18 | 3 — **zeroed at runtime** |

⚠ **The static image has six entries; the running engine has four.**  Reading this table out of
`revs_runtime.bin` alone would have produced two keys that are never tested.  The live values came
from a jsbeeb dump — the same "measure, don't reason" rule that the relocation needed.

The decode was cross-checked against jsbeeb's own key matrix rather than trusted from a manual:
its table gives `[row, col]` per key, and `n = col × 16 + row + 1` reproduces all four values
(SPACE `[2,6]` → 99, `1` `[0,3]` → 49, `2` `[1,3]` → 50, `3` `[1,1]` → 18).

### ⚠ Driving the engine into a real race: attempted, and deliberately abandoned

The goal was to turn the weak coverage cross-check below into a strong one.  It did not get there,
and the reason is worth recording so nobody retries it the same way:

- Keys **do** register (`tools/bbc_probe_keys.mjs`: holding `1` yields 22 new engine addresses,
  SPACE yields 4).  The first trace's problem was a missing *sequence*, not a missing keypress.
- A pulse chain gets **past the first menu** and raises coverage from 262 to 356 engine addresses,
  then stops dead.
- The 6502 stack says why.  At the start the outstanding return address is `$63FA` — inside
  `menu@$63F7`.  After four pulses it is `$640A` **and `$6563`**: the engine has moved on and is now
  spinning at `$6560` on `BIT $05F4 / BVS $6560` — **not a key wait at all**, but a wait for bit 6
  of `$05F4` to be cleared by something else.

So the front end is not a pure keyboard state machine, and getting to a race means understanding
what clears `$05F4` bit 6.  **That is a worthwhile question but it is not this phase's question.**
`docs/entrypoint-sweep.md`'s definition of done is *"`listing.txt` has no referenced-but-
undisassembled address"* — a static property, settled by classifying the unclassified runs, not by
winning at Revs.  Recorded as an open item with the three facts a future attempt needs: the menu
call sites, the live key table, and `$05F4` bit 6.

⚠ Note the instrumentation trap this ran into: counting executions of the five menu call sites
reported "none reached", because the hook was installed *after* the settle period, by which time
the engine was already parked inside a menu.  A call-site counter cannot see a call that is already
outstanding — the stack can.  That is why the stack read is what settled it.

### The measured cross-check

A static tool cannot report its own blind spots, so `tools/bbc_trace.mjs` traces real execution
under jsbeeb and `make sweep TRACE=…` diffs the two.  Of 310 engine addresses executed, **the only
two the static walk did not reach are `$1202` and `$1209`** — inside the pre-relocation loader
stub, which by construction is not in the post-relocation image.  No missed entry points.

⚠ **This trace is weak and must not be over-read.**  The engine parks in a key-configuration wait
loop at `$657C` (polling `$0E50` against a key table at `$39E0`), so only 310 of ~7000 known
instructions ever ran.  It proves the walk covers what executed; it proves nothing about the rest.
Driving Revs to real coverage needs the key table decoded — open item.

## Static coverage — 5327 unclassified bytes down to 685

Of `$0B00-$78FF`: **14669 bytes decode as instructions** and 10665 are named by an absolute
operand.  The first pass left **5327** bytes that nothing accounted for.  Three additions cut that
to **685**:

**1. A zero-page pointer pass.**  An absolute-operand scan cannot see through `(zp),Y`, and Revs
reaches most of its bulk data that way.  `Sweep.resolve_pointers()` finds what writes each half of
a dereferenced pointer pair and resolves it:

| Pointer | Source | Entries |
|---|---|---|
| `($70),Y` | table pair `$07A8` (lo) / `$397C` (hi), set at `$5126`/`$5202` | 42 in-range |
| `($72),Y` | table pair `$3AD0` / `$3B50`, set at `$4D88`/`$4D83` | 47 in-range — the text interpreter |
| literals | `$3000`, `$3080`, `$4400`, `$4404` | |

**2. Runs split at unpack-region boundaries.**  A run that straddles a boundary was being labelled
by its first byte alone: `$66FF-$6E84` read as 1926 unexplained bytes, when `$66FF` was a one-byte
overhang and `$6700-$6BFF` is the dead tail of the X=1 move's source.

**3. The unpack's own leftovers named.**  The zero-filled workspace, the dead move sources, the
`$70DB-$7800` window the swap filled with engine data, and — verified in a live dump — the track
file's surviving tail: four checksum cells at `$7800-$7803` and then **the track name as ASCII at
`$7804`** (`REVSSilverstone` + `$0D`).

### A number that got better for the wrong reason, and was reverted

Widening the pointer-source lookback from one instruction to four took "unexplained" to **zero**.
It was wrong: the wider window matched unrelated `LDA table,X` instructions, and the index walk
then fabricated a pointer target at nearly every page boundary — every run came back "body of a
pointer-reached block".  **A coverage number that improves because the tool got looser is worse
than no number**, so the lookback is back to one instruction and the comment in
`tools/sweep_entrypoints.py` says why. The honest figure is 685, not 0.

### The residual: 685 bytes, two runs, mechanism unknown

| Run | Size | Content |
|---|---|---|
| `$6C00-$6E84` | 645 | 523 of 645 bytes zero; 58 are `$F0`; 27 distinct values |
| `$6F8A-$6FB1` | 40 | 36 of 40 zero |

Neither is inside a move source, a fill, or the swap window, and neither is pointed at.  Both are
~85% zero with sparse values, which reads like a pre-initialised buffer.  What was ruled out:

- **Not the runtime-patched rasteriser stores.**  Those looked like the obvious candidate — the
  span plotters' `STA` operands are rewritten at `$19C0-$19CC` from a table pair, which is exactly
  a mechanism that hides a destination from a static scan.  But the table (`$2B1E` hi / `$2B22` lo)
  holds only four addresses and they are all in pages 5-6: **`$0554`, `$05A4`, `$0600`, `$0650`**.
  Worth the detour anyway — it explains how one copy of the plotter serves four row buffers.
- **Not proven dead by the trace.**  Zero writes landed there during the traced run, but that run
  never left the front end, so this is no evidence either way.

Recorded as the residual rather than argued away.  685 bytes of 27 K is a small enough surface to
resolve by reading once the naming pass reaches the 3D pipeline, and `make sweep` prints the figure
every time so it cannot quietly drift.

## What the naming pass mapped

Three subsystems came out coherent rather than as isolated names, which is the payoff for ranking
by caller count instead of by address:

- **The road renderer joins up.**  The four addresses the span plotters patch into their own `STA`
  operands are per-**column** edge buffers (`$0554`, `$05A4`, `$0600`, `$0650`), and
  `surface_colour_at` (`$1E9E`) compares a height against all four to decide which track
  surface a column falls in, returning a colour from a 4-entry table at `$38FC`.  The plotters
  write those buffers; the resolver reads them.  Two halves of one mechanism.
- **Lap and race timing is packed BCD.**  `car_reset_best_lap`, `lap_complete`, `sort_cars_by_key`
  and `clear_race_clock` all work on 3-byte BCD with `SED` set.  `src/cpu/cpu.h`'s ADC/SBC already
  honour `cpu.D` — it anticipated exactly this — so the *transliteration* is safe.  The hazard is a
  **native twin** doing it in plain C ints, which yields plausible-looking wrong lap times.
- **The track model.**  A circuit is a segment list and a car's position is (segment index, offset
  within segment).  `track_pos_advance`/`track_pos_retreat` step it, wrap at `segment_count_x8`,
  and call `lap_complete` when the distance counter reaches `lap_length`.  ⚠ The segment tables
  live in the **track file** (`$5900-$5A25` after the swap), so those addresses hold different data
  per circuit.

### The race loop's own anatomy (named 2026-08-17, clearing `docs/rename.md`)

Asking "which C function is the main loop?" exposed that the loop had no name at all, and neither
did three of the things it calls every frame:

- **`race_main_loop` (`$16DC`)** is the function.  `$1701-$1748` is its 24-JSR body, `$174B-$17B7`
  its tail, and it is a *label range*, not a callee.  It is JSRed from `wait_flag_05F4` and RTSes
  back to the front end after stowing the `$7B00` overlay (`copy_dash_data` with A=`$80`).
- **`shift_key_commands` (`$0EE5`)** is the whole in-race command set: SHIFT plus one of twelve
  function keys, each writing a nibble into the `state_flags` block, which the same call then
  services — the documented SHIFT+f0 return-to-pits, a real PAUSE that spins until key `$A6`, and
  master volume, which is a scale on the *envelope's* attack level, not on the SOUND call.
- **`engine_sound_update` (`$0E74`)** was `sfx_trigger_random`: the random noise pitch is its first
  eight instructions, and the other ninety percent is the engine-note ramp.
- **`draw_dash_needles` (`$513A`)** erases through an **undo list** — `plot_line_octant` saves every
  byte it is about to modify (`$0780`/`$07A8`/`$07D0`), so `undraw_plot_lines` (`$511E`) restores
  the exact background and nothing repaints the dial faces.
- **`tick_wheel_spin` (`$52A4`)**, the band cycle's only game work, draws **the front wheels
  turning**: three symmetric left/right byte runs at cells 0/1 and 38/39, display lines 133-140,
  XORed at a rate proportional to `road_speed`.  Computing those six addresses into
  (row, cell, line) and matching them against a real-BBC frame (`tmp/bbcref/ref_000211.png`) put
  them exactly on the dither at the top of each front-wheel arch.  It had been carried as
  `body_tick_xor_anim`, "identity of the element unknown", for a month.

### ⭐⭐ The twenty subsystems (named 2026-08-17, closing `docs/rename.md`'s biggest item)

The 24-call body is a **flat sequence**, so slot *n* of our list is subsystem *n* of anyone else's,
and that made the whole pass one sitting.  Each name below is derived from the routine's own body;
`disasm/symbols.csv` carries the evidence per address.  Slot alignment was cross-checked against
`revs.bbcelite.com`'s account of the same body — one-for-one, including all five slots this project
had already named independently, which is what makes the alignment trustworthy.

| slot | addr | name | what it is |
|---|---|---|---|
| 1 | `$5052` | `tick_race_timers` | the clocks and the main-loop counter |
| 2 | `$7B4A` | `draw_starting_lights` | in the `$7B00` overlay; paints column 37 |
| 3 | `$1579` | `read_driving_controls` | steering (incl. the assist), throttle/brake, gears |
| 4 | `$46A1` | `apply_driving_model` | the physics; writes `road_speed`, `wheel_spin_rate` |
| 5 | `$24F6` | `build_track_geometry` | the edge lists **and** the corner-marker list |
| 6 | `$4626` | `place_player_in_section` | position within the current section |
| 7 | `$24B9` | `advance_player_section` | moves the section cursor `$24` |
| 8 | `$0FFE` | `update_lap_timers` | lap/session limits, the top-of-screen messages |
| 9,12,20 | `$0E74` | `engine_sound_update` | (already named) |
| 10 | `$66B6` | `clear_surface_buffers` | (already named) |
| 11 | `$1A20` | `draw_road` | the road rasteriser — into the source blocks |
| 13 | `$18BC` | `fill_line_surface` | (already named) |
| 14 | `$4CA4` | `build_road_sign` | assembles object slot `$17` |
| 15 | `$2AD1` | `draw_track_object` | draws slot X — a car or a sign |
| 16 | `$1B12` | `draw_corner_markers` | consumes slot 5's marker list |
| 17 | `$2637` | `move_and_draw_cars` | the other cars; at most five drawn |
| 18 | `$1E15` | `fill_dash_edge_columns` | the tyre/dash seam, and it PRODUCES `$0504`/`$4400` |
| 19 | `$7FB6`… | `mirrors_update` | (already named) |
| 21 | `$4F44` | `update_horizon_band` | writes `band1_duration` — the only "horizon" there is |
| 22 | `$1BB9` | `process_car_contact` | collisions, and the skid-noise cells |
| 23 | `$111E` | `check_crash` | the fence; sets `crash_flag` for one frame |
| 24 | `$7BE2` | `view_paint_lines` | (already named) |
| — | `$1805` | `reset_driving_variables` | the session reset (`RESTART_MID`) |
| — | `$11CE` | `build_player_car` | `RESTART_LATE` |
| — | `$0B77` | `scale_wing_settings` | every (re)start's last step |
| — | `$17FC` | `print_message_pair` | two status rows, token X and token `$2D` |
| — | `$1163` | `finish_race` | races the remaining drivers so the results are real |

Two things the pass **corrected** rather than merely filled in:

- **`$24F6` was `build_road_edge_lists`, and the name was too narrow** — not one slot off, which was
  the worry.  The same track walk that emits the two 40-point edge lists also appends the frame's
  corner markers (`$25D9`-`$25FB` → `marker_edge_index`/`marker_flags`/`marker_offset_lo/hi`, count
  in `marker_count`), which slot 16 then consumes and zeroes.  One producer, two products.
- **`$1E15` is a producer of `view_paint_lines`' control input, not just a cosmetic fill.**  Its two
  passes point `plot_ptr2` at `$0504` and `$4400` — the per-line boundary source bytes the view
  sweep composes the two runs' edge cells from.  That link was invisible while the tables were named
  after their operator.

And the clock the whole pass hangs off: **`add_frame_time` (`$17C3`) adds 9 BCD hundredths of a
second per frame, and `$18` on one frame in 25** — an average of 9.36, i.e. the BBC's own ≈10.7
frames a second, written into the game's own time base.  `time_tick_period` (`$5A19`) is per
circuit, so the calibration is track data.

And a name that was wrong, corrected: **`menu_key_tbl` at `$39E0` has exactly four entries.**  The
two bytes after it are not a fifth and sixth key binding — `$39E4` is the start of `car_lap_mid`, a
20-entry per-car array, which is why they change at runtime.  Finding `sort_cars_by_key` using
`$39E4` as a car array is what settled it.  The earlier reading ("six-entry, user-rebindable") was
a guess dressed as a fact, and it is the exact failure mode `docs/postmortem.md` is about.

### ⭐ Three of those slots' DATA STRUCTURES (named 2026-08-17 with twins #6/#7/#8)

Naming a subsystem's routine and naming the cells it works on are two different passes; slots 4, 15
and 18 got the second one when they became twins.  What came out:

- ⭐⭐ **THE DRIVING MODEL IS A 15-ELEMENT 16-BIT VECTOR**, `model_state_lo` (`$62D0`) /
  `model_state_hi` (`$62E0`), element *i* being `+i` in each half.  Every access in the engine is
  `base+offset,X` or `,Y`, and `model_integrate_element` (`$47E5`) is the generic integrator —
  `model_state[X] += model_state[14]` on both halves — called with X=8 and X=`$0A`.  What is
  identified: **2** is the angular rate `integrate_car_position` (`$48EF`) adds into
  `car_heading_lo/hi`; **3/4/5** are the rates of **0/1/2**, applied by `integrate_state_rates`
  (`$4937`) through the extra fraction byte `model_state_frac`; **8** is `model_accum`, the one
  element `apply_driving_model` integrates by hand, and `rotate_state_0_into_8` (`$48B9`) is what
  resolves the pair **0/1** into **8/9**; **9** is `car_speed_lo/hi`, signed, whose magnitude the
  routine splits into `road_speed` (integer) and `road_speed_frac`; **$0A..$0D** are what
  `damp_and_derive_loads` (`$47F9`) halves twice a frame and `check_wheel_slip` (`$4A91`) writes.
  Elements 5..7 are forced to zero once `drive_state` reaches 2.  ⚠ What those elements are
  PHYSICALLY is still open — `docs/rename.md`.
  ⚠ `$62DF` (`loop_counter_hi`) and `$62EF` sit inside those address ranges and are **not** members
  — the vector stops at element 14, which is why it is 15 long and not 16.
- **`model_accum`'s integration is a midpoint step, and it reads as a bug until you read it twice**:
  the entry value is saved into `model_accum_entry_lo/hi`, `$4729` *subtracts* a scaled velocity so
  the next four sub-models run against the offset value, and only then is the entry value restored
  and `model_accum_delta_lo/hi` (1.5x what was removed) added.
- **THE OBJECT PLOTTER HAS A FOUR-CELL ARGUMENT BLOCK**, and slots 15 and 16 are its only two
  producers: `plot_x` (`$35`, always `4 * an azimuth + $50`, in 2-pixel units), `plot_line` (`$36`,
  a scan line), `proj_width` (`$2A`) and `plot_shape` (`$37`).  ⚠⚠ The first two were called
  `plot_row`/`plot_column` until 2026-08-17 and **the axes were the wrong way round** — the
  producers feed `$35` from an AZIMUTH and `$36` from `edge_y`, and `plot_object` clamps `$36`
  against `#$50` = 80 lines, which no 40-cell column can be.  `$018C` `car_flags_shape`'s low
  NIBBLE is the shape index — one byte carrying two unrelated things.
- **THE 24 OBJECT SLOTS** are four parallel arrays: `object_bearing_lo`/`object_bearing_hi`
  (`$0380`/`$0398`, the 16-bit BEARING from the camera, written straight out of
  `bearing_to_section`), `object_line` (`$03B0`) and `object_width` (`$03C8`).  Slot `$17` is the
  road sign; the rest are cars.  `car_heading_lo/hi` (`$0A`/`$0B`) is the player's own entry, copied
  out at `$11FB` with the high byte EORed with the direction flag `$25`, and every bearing in the
  frame is measured from it — ⚠ it was `player_pos_lo/hi` until 2026-08-17, and it is an ANGLE.
- ⚠⚠ **`plot_object` (`$1FB4`) CAN LOOP FOREVER, and it is not a self-modifying site.**  Its outer
  loop `$2002`-`$2027` repeats while `$62F3` reads 9 and `mem[$0025]` is positive, and `$62F3` is
  re-stored from `plot_shape` at the top of every pass — so with `plot_shape == 9` every pass is
  identical and the only exit is `FUN_202a` returning carry set.  The real shape tables at
  `$3CD0`/`$3CDD`/`$3CDE` are what guarantee that; nothing structural does.  Anything that feeds
  this routine synthetic data has to know it (`docs/validation-harness.md` §ELEVENTH).

### ⭐ The road pass's OWN data — the two tables and the array that shares bytes (2026-08-17)

Settled while clearing the rename queue; all three were open questions in it.

- **The whole `$6100` page is ONE 256-byte `arctan_table`**, entry *i* = `atan(i/256)` scaled so 45°
  reads `$FF` (curve-fitted; `i=$40`→`$50`, `i=$80`→`$97`).  What read like a second table at `$6180`
  is `LDA $6180,Y` with **Y already normalised to `$80..$FF`**, which addresses `$6200-$627F`:
  `reciprocal_table`, entry = `$8000 / (i + $80)`.  So the engine has exactly two curves in ROM — an
  angle from a ratio, and a reciprocal for the perspective width — and one restoring divide
  (`div16by8`) for everything else.
- **`point_distance_hypot` (`$0CA5`) is a two-segment alpha-max-plus-beta-min**, and the segment is
  chosen on the ARCTAN byte (`shared_temp_7e` against `#$67` ≈ 19°), not on the magnitudes: near the
  axis `larger + smaller/8`, off it `larger*7/8 + smaller/2`.  Every edge point and every object goes
  through it, and the `HookFieldOfView` the expansion circuits carry does **not** patch it —
  `disasm/track_smc.txt` has no extent in `$0C00-$0CFF`.
- ⚠⚠ **`edge_opp_x_lo/hi` (`$5E50`/`$5EA0`) deliberately shares bytes with `edge_x_lo/hi`.**  Base is
  `edge_x` + `$10`; the walk is capped at 18 points from cursor 6 / `$2E` (`$2498 CPY #$12`) and
  `emit_edge_width_offset` skips the first three, so each half only ever fills its own 25..39 slack.
  Highest byte written is `$5E8F`/`$5EDF` — nothing reaches `$5EE0`.  This is the one aliasing fact
  a buffer rearrangement must carry forward (`docs/direct-bitplane-plan.md` §5a).

## ⭐ Phase 5 addition — the display is fully derived, and the sky hides live code

`src/platform/bbc_screen.h` is now the reference for the screen: the CRTC table at `$4F0F` gives
40×26 cells of 8 lines at `$5A80` (208 lines, `$5A80 + 8320 = $7B00` exactly), the ULA's shift
register gives the pixel format (a pixel's two bits land in palette-index bits 3 and 1; bits 2 and
0 hold the following pixels' bits and are don't-care — which is why every palette table in the
game comes in groups of four), and the five raster bands are recorded live from what
`irq1v_band_schedule` writes rather than hard-coded, because band 2's duration is the horizon.

⭐⭐ **`$5E40-$66FF` — 5.5 KB of engine variables AND executable code — is inside the frame
buffer, on display.**  It is invisible because all sixteen of band 1's palette entries are the
same blue; nothing clears it and nothing can, because it is the program.  Measured here first (the
frame-buffer rows over that range are byte-identical to the boot image except where live variables
churn, and `listing.txt` disassembles 462 instructions inside them), then cross-checked against
the reference's write-up of the custom mode.  Two consequences: the raster phase matters to the
pixel, and any future "clear the screen" optimisation would erase the game.

Band boundaries, in display lines: band 0 (MODE 4, the two text rows) −26.4..18, band 1 (the sky
and the code) 18..81.1, band 2 (horizon + rear wings) 81.1..100.5, band 3 (the track, blue→red)
100.5..166.1, band 4 (the dashboard, green→cyan) 166.1..286.1.  ⚠ The latch is PIPELINED: the T1
value written during band n is band n+1's duration.

## Open items — what Phase 2 still owes

1. ✅ ~~Classify the residual 685 bytes~~ — `$6C00-$6E84` and `$6F8A-$6FB1` (down from 5327; see
   §Static coverage).  **Answered: it is dashboard bitmap, not code and not a buffer.**  It is
   one piece of the jigsaw that the unpack leaves alone (`$6C00-$6FFF` is "unchanged" in the
   relocation), and it is the top of the ~2.8 KB dashboard image block that already sits in the
   custom mode's screen memory when the loader finishes — the part `copy_dash_data` does *not*
   have to move because it is already where it belongs.  Consistent with its ~85%-zero shape and
   with `$3A5C`/`$3A62` poking `$7C79,X` nearby.  Cross-checked against the reference's memory
   map (`docs/reference-sources.md` — a map, never a source).
2. ✅ ~~Disassemble the track programs.~~ **Done** — `ModifyGameCode` is read out above, the code
   extents are measured, and the patcher now cross-checks against the differential with zero
   discrepancies.  Still owed: naming the six hook *bodies* by behaviour (they are the per-track
   field-of-view / hill-flattening / horizon variations the reference describes).
3. ~~Read out the OSBYTE/OSWORD reason codes.~~ **Done** — all 17 sites resolved except `$50F6`
   (A from a variable).  Still owed: confirm the heuristic against a trace, and read out the
   *parameters* (X/Y) at the ADC and buffer-flush sites, which is what the implementation needs.
4. ~~Decode the key table at `$39E0`.~~ **Done** (§The front end).  It is SPACE / 1 / 2 / 3 at
   runtime.  **Driving to a real race is still open, and is now a different question than it
   looked:** the front end blocks on `BIT $05F4 / BVS` at `$6560`, waiting for bit 6 to be cleared
   by something other than the keyboard.  Find what clears it and the trace becomes strong.
5. 🔧 **Extend the naming pass.**  **365 rows in `disasm/symbols.csv`** as of 2026-08-17; the
   figures below are from the original pass, when there were 129 and they were applied to the
   Ghidra project.  That was 42 of
   222 call targets (19%) but **45% of all call sites** — the difference is the point: the pass
   worked down `--functions` by caller count, so the routines everything funnels through are named
   first.  13 symbols carry **[PROVISIONAL]** and say so.
   Still unnamed: the interior of the 3D pipeline below `project_point`, the front end's prompt
   chain, and the other cars' AI.  ⭐ **The physics is no longer on that list** — `apply_driving_model`
   and all fifteen of its sub-models have names as of 2026-08-17 (see §Three of those slots' DATA
   STRUCTURES), though what the fifteen state ELEMENTS mean physically is still open in
   `docs/rename.md`.  ⚠ This does **not**
   gate Phase 3: `disasm/symbols.csv` feeds the transpiler, so a name learned later propagates
   through the whole corpus on the next `make gen` — which is exactly why `docs/toolchain.md` says
   the cost of being only roughly right early is near zero.
6. ✅ **Seven calls into `$7B00-$7BFF`, ~~a page nothing ever loads~~ — a page the game BUILDS.**
   *Answered; see the RESOLVED block at the end of this item.  The history is kept because each
   step was a correct measurement of the wrong question.*  Found in Phase 3 by
   generating the corpus: `JSR $7B00` (`$1739`), `$7B4A` (`$1704`), `$7B9C` (`$502A`, `$503B`,
   `$6612`), `$7BE2` (`$16E6`, `$1748`).  REVS2 covers `$1200-$6FFF` and a track file
   `$70DB-$7814` — on this disc **and** on the Nürburgring hack disc — so the page is zero in
   both `revs_mem.bin` and `revs_runtime.bin`, and a real BBC has leftover front-end text there
   (`$7BE2` = "…ornso", the tail of "Acornsoft").  A `JSR` there executes garbage.

   Not obviously dead code: `$16E6` is four instructions into the routine `$6563` calls when the
   front end releases, i.e. race init.  Measured (`tools/bbc_probe_unmapped_calls.mjs`): across
   boot, the track menu and 20M cycles of front end, **zero executions of all seven sites and
   zero writes into `$7A00-$7C00`**.  Consistent with unreachable — but the same run never gets
   past the `$6560` gate (open item 4), which is precisely what stands between it and the call
   sites, so it does not settle it.

   The port's answer is a trap, not a verdict: these emit `platform_brk()`, so the first run that
   reaches one reports it (`g_brkPC`/`g_brkCount`) instead of silently returning.  ⚠ Whatever the
   resolution, it is **not** "emit an empty function" — that makes calling into nothing look
   exactly like working.

   ### ⭐ Phase 4 update — REACHABILITY IS SETTLED, CONTENT IS NOT

   **They are reached, and three of them are in the MAIN LOOP.**  Running the corpus (host and
   Amiga) trips the trap within seconds: `g_brkCount` climbs by ~3 per game frame and `g_brkPC`
   cycles through `$7B4A`, `$7B00`, `$7BE2`, with `$7B9C` on the init path.  The engine's
   per-frame body is `$1701-$1763` — label range inside **`race_main_loop` (`$16DC`)**, which is
   the C function to look for; `$1704`/`$1739`/`$1748` are three of its 24 top-level calls.  Whatever lives in that page runs **fifty times a second**.  "Consistent with
   unreachable" is dead.

   **What is there is still unknown**, and the earlier "zero executions" measurements were never
   evidence about the page — they were evidence that the scripted BBC run never got that far, and
   nobody checked which.  Now measured (`tools/bbc_probe_frontend.mjs`,
   `tools/bbc_probe_pchist.mjs`): a real BBC driven through the menus stops in **`console_io`
   (`$6300-$6342`)**, the blocking OSRDCH line editor, whose caller `FUN_3EE0` (`$3EE0`) asks for
   a **two-digit number** with `X=2` and re-prompts until `FUN_32D0` accepts.  `FUN_3C50` asks two
   of them, then `FUN_34D0` waits for SPACE.  The port skips all of this because its `rdch()`
   returns CR immediately — which is exactly why the port reaches the main loop and the reference
   machine does not.

   **The next step is therefore a reference-loop step, not a disassembly step**: get jsbeeb past
   the line editor and dump `$7B00-$7BFF` at the first `$16E6`.  `tools/bbc_drive.mjs` is the
   feedback-driven driver written for it — it watches `$6581` (a menu is polling) and `$6316`
   (the line editor is reading) and answers whichever fired.

   ⭐ **The blocker is now bisected, and it is the harness, not the game.**  Counting `$32D0`
   (`FUN_32D0`, which runs only once `console_io` RETURNS a completed line) and `$3EEE`
   (`FUN_3EE0`'s reject-and-re-ask arm) *separately* separates the two explanations that need
   opposite fixes.  Both read **zero** while `$6316` climbs steadily: **`console_io` never
   returns, because no `$0D` ever reaches the MOS.**  It is not "the value was rejected" —
   nothing was ever entered.

   Two harness faults found and fixed on the way, both of which failed silently:
   - `utils.keyCodes.RETURN` **does not exist** in jsbeeb (that table calls it `ENTER`), so every
     RETURN press in every probe — including the pre-existing
     `tools/bbc_probe_unmapped_calls.mjs` — was `keyDown(undefined)`, a no-op.
   - Speculative driving-key presses **jammed the editor's own buffer**: `$74`/`$75` read
     `$54,$53` = `'T','S'`, and the field is two characters, so `$6334` answered every later key
     with a bell instead of storing it.  The run was wedged by its own input.

   Still failing after both fixes and after switching to raw matrix positions
   (`sysvia.keyDownRaw(utils.BBC.RETURN)`, which bypasses the browser-keycode layout entirely).
   ⚠ Also note the buffer then read `$10,$02` — **not** the `$0074` two-digit field — so the
   active prompt may be `FUN_66D4`'s **twelve-character** `console_io` call rather than
   `FUN_3EE0`'s numeric one; the "two-digit prompt" reading above is inferred from `FUN_3EE0`
   and is not yet confirmed to be the one that is blocking.

   **Next experiment, and it is self-verifying:** press RETURN at the BASIC prompt, where a
   working RETURN is directly observable in `tm.drainText()`.  That says in one cheap run whether
   RETURN injection works at all, instead of testing it through a game whose state is invisible.
   Only once that passes is it worth driving the front end again.

   ### ⭐⭐ RESOLVED — the page is built at runtime by `copy_dash_data` ($18EA)

   **There is a SECOND unpack, and nothing in this document knew about it.**  `tools/relocate.py`
   replays the startup relocation and gets the right answer; it is just not the last thing that
   moves code.  `$18EA` assembles `$7B00-$7FFF` — 1280 bytes — out of the **tails of 41 "dashData"
   blocks spaced `$80` apart from `$3000`**, and the destination *descends* from `$7FFF`:

       $18EA  STA $74                       ; bit 7 of A = direction
       $18EC  $192F-$1932 -> $70-$73        ; = 00 30 B0 7F: src $3000, dst base $7FB0
       $18F8  LDY #$4F                      ; each block's data ENDS at offset $4F
              LDA ($70),Y / STA ($72),Y     ; forward  (skipped when bit 7 of $74 is set)
              LDA ($72),Y / STA ($70),Y     ; reverse  (a harmless read-back when unpacking)
              INC $76 / DEY / TYA / CMP $3900,X / BNE     ; $3900,X = block X's START offset
              $72/$73 -= $76                ; destination descends
              $70/$71 += $80                ; next block
              INX / CPX #$29 / BNE          ; 41 blocks

   Blocks 0-25 carry **code** and bottom out on exactly `$7B00`; blocks 26-40 carry dashboard
   **image** and continue down to exactly `$7768`.  With bit 7 of A set the whole thing runs
   backwards and **stows the code back into the block tails** before the game returns to MODE 7 —
   which is precisely why the page is empty in `revs_mem.bin`, in `revs_runtime.bin`, and in every
   RAM dump taken outside a race.  Two phases of "a page nothing ever loads" was the wrong
   question: nothing ever *loads* it, something *builds* it.

   ⭐ **`$16E3` is `JSR $18EA` and `$16E6` is `JSR $7BE2`.**  The page is built one instruction
   before it is first called.  That single adjacency is the whole answer, and it was sitting in
   `listing.txt` the entire time — see the method note below.

   **Replay it with `tools/dashdata.py`.**  Four things agree, none of them copied from anywhere:

   - the 26 code blocks sum to **exactly 1280** bytes and bottom out on **exactly `$7B00`**;
   - the image blocks bottom out on **exactly `$7768`**, which is a named boundary in the
     reference's memory map;
   - block 0 lands at `$7FCC-$7FFF` and decodes as the mirror plotter, engine-shudder idiom and
     all — `LDX $FE68 / AND $2000,X / AND $61` (User VIA T2 counter-low ∧ game code as a decorrelator ∧
     engine status), then `SBC #$38 / SBC #$01` to subtract `$138` when a span crosses a
     character row;
   - `$7B00` decodes as the mirror update — `LDA $03C8,X / LSR / LSR / LSR` (object size ÷ 8) and
     then `ADC $B6` / `LDA $B6 / SEC` around the mirror centre line.

   So the three main-loop calls are **the wing mirrors and the dashboard**, at 50 Hz.  Named:

   | Addr | What it is |
   |---|---|
   | `$7B00` | update the wing mirrors — called from the main loop body (`$1739`) |
   | `$7B4A` | called from `$1704`; keys off `$61` (engine status) and `$6C`/`$6D` |
   | `$7B9C` | lap/best-time readout — `$06A0`/`$06B8`/`$06D0` through `$37D6` and `$5092` |
   | `$7BE2` | seeds `$70-$73` as screen row pointers, `JSR $7EF3`, `JMP $7D13` |
   | `$7FCC` | draw one car reflection in one mirror segment |

   ⚠ **Confidence: DERIVED and self-checking, NOT yet measured on a BBC.**  Reaching `$16E3` on
   real hardware is still behind the front-end line-editor blocker above.  Dumping `$7B00-$7FFF`
   at `$16E6` is a one-line addition to a probe the moment that clears, and it is worth doing —
   this is exactly the kind of confident-and-unverified reading the postmortem is about.

   ⚠ ~~**What this costs the port, and it is not small.**~~  ✅ **Closed 2026-08-12.**  The
   transpiler used to emit `platform_brk()` for these seven sites because the page is empty in the
   image it reads: `copy_dash_data` transpiles fine and moves the bytes correctly in `mem[]`, but
   transpiled C cannot *execute* bytes assembled at runtime.  The bytes are nevertheless
   **statically known** — a deterministic function of the static image — so `tools/dashdata.py
   --listing` produces a second listing and `tools/transpile.py` ingests it alongside
   `listing.txt` (`make gen`, `DASHCODE=0` to opt out).  Two further defects had to be fixed
   before the emitted C would run: the tail-call-cycle defect (item 9) and the overlay's
   self-modifying code (item 10).

   ⭐ **All four `$7Bxx` call sites are now live and `g_brkCount` reads 0.**  The baseline
   accordingly **fell from ≈2.2 to ≈1.4 FPS** — those three main-loop calls are ~36% of the
   frame, and they are mirror and dashboard work, i.e. **rasterisation**.  So the Phase 4
   headline "the hot path is physics, not rasterisation" is now measured with them in rather than
   shifted by an unmeasured amount; see `docs/perf-method.md` §THE BASELINE.

   ### Method note — why this took two phases

   Every measurement taken against this page was sound and every conclusion drawn from it was
   wrong, because they all answered *"what loads `$7B00`?"* and nothing ever does.  The reference's
   memory map named `$7B00-$7FFF` as game code in one line, which was enough to make the question
   change shape; from there the copier fell out of `grep` in minutes.  The postmortem's lesson
   applies literally: **a static walk finds what is reachable from the bytes on disc, and code the
   program assembles for itself is invisible to it** — the same standing limitation recorded in
   item 7b for the runtime-computed branch, one order of magnitude larger.

   ✅ ~~Until the port executes them, **every measurement of the main loop is missing three
   routines**~~ — it executes them as of 2026-08-12 (item 10).  What is still owed is the
   **BBC cross-check**: the overlay's bytes are DERIVED from a replay of `$18EA`, never compared
   against a dump from real hardware, and that is still blocked on the front-end line editor
   above.  Dumping `$7B00-$7FFF` at the first `$16E6` remains a one-line addition to a probe.

7. ⭐ **Phase 4 additions to the static map — all three found by RUNNING the corpus.**
   Recorded here because each one is a place where reading the binary gave a confident wrong
   answer, which is the failure mode the postmortem is about.

   a. **The rasteriser's NOP/INY opcode slots have a THIRD value: `$88` = `DEY`.**
      `$2BFB` is a literal `LDA #$88` on the arm the operand scan walked past:
      `$2BF9 BPL $2BFF / $2BFB LDA #$88 / $2BFD BNE $2C01 / $2BFF LDA #$C8`, and `$2C01` stores
      whichever landed.  So `$2F60`/`$2FA2` take `$88` when `$87` is negative, and `$2F47`/`$2F89`
      inherit it via `LDA $2F60`, and `$2F18` via `LDA $2F47`.  **The span loop can walk its
      destination in either direction** — which is what a road rasteriser drawing left-to-right
      or right-to-left needs.  Reported by `g_smcUnhandled` as `site $2F89 holds $0088`; fixed in
      `tools/transpile.py` SMC_SITES.

   b. **13 bytes of real code at `$1DC5` that no static walk can reach.**  The patched branch at
      `$1DD4` (offset written by `STY $1DD5` at `$1DA9`, so the target is a *register value*)
      jumps there; `$1DC2` is `JMP $1DE0`, so recursive descent leaves `$1DC5-$1DD1` an orphan and
      both Ghidra and `tools/sweep_entrypoints.py` stopped.  The bytes decode cleanly as the same
      span loop as `$1DD2`'s, storing through **`($72),Y` instead of `($70),Y`**.  Seeded in
      `ghidra_scripts/entrypoints.csv` as `span_store_alt`; the listing went 236→237 functions and
      7083→7098 instructions.
      ⚠ **This is a standing limitation of the entry-point sweep, not a one-off.**  A
      runtime-computed branch target is invisible to any static walk, so the sweep's "zero
      undecodable bytes reached" is only true of statically-reachable code.  The SMC trap is what
      catches the rest, and it only fires on a code path that actually runs.

   c. **OSBYTE 0 (read OS version) is an 18th MOS call site.**  The Phase 2 inventory's reason
      codes came from a nearest-preceding-`LDA #imm` heuristic, so a site whose `A` is computed is
      invisible to it — `docs/bbc-hardware.md` marks them **[DERIVED, heuristic]** for exactly
      this reason.  Found by `g_mosUnknownCount`; answered as MOS 1.20 in `src/platform/mos.cpp`.
      Also confirmed at runtime: **OSWORD 10 (read a character definition) is called ~850 times in
      the first 900 vblanks**, so the MOS font is a real Phase 5 dependency, not a footnote.

9. ✅ **~~Three 6502 loops are compiled as unbounded mutual recursion~~ — FIXED.**  Two of them
   were in the corpus that produced the 2.2 FPS baseline.  Found while wiring in the `$7B00`
   overlay, by a check written for the overlay that immediately fired on code predating it.

   The transpiler splits a function at every mid-function entry point and joins the pieces
   with `name(); return;`.  That is exact for a *call*.  For a **loop** it is a disaster: if
   control falls through segments S1→S2→…→Sn and Sn jumps back to S1, the C form is mutual
   recursion, and GCC eliminates none of these tail calls on either target.  The stack then
   grows with the ITERATION COUNT, and every iteration pays call overhead.

   | Cycle | What it is |
   |---|---|
   | `$1DE8 → $1DE5 → $1DE8` | ⚠ the span store loop inside `plot_view_src_line` — **one nested frame pair per pixel of every span**, in the hot path the profile points at |
   | `$31D0 → $3D68 → $31D0` | the dashData block fill loop (the `$3900,X` start-offset table's other reader) |
   | `$7BF7 → $7C00 → $7D56 → $7E00 → $7EF3 → $7BF7` | the overlay's 41-block unrolled dashboard blit — overlay-only |

   Measured on the overlay before it was gated off: **300-1000 live frames** on the host and
   **0 painted frames in 20 s** on the target, versus 2.2 FPS without it.  The two engine
   cycles are smaller but have been in every build ever measured, so **`docs/perf-method.md`'s
   baseline includes them** — an unknown slice of the 13.1% charged to `$1E15` may be call
   overhead and stack traffic, not geometry.

   ### ✅ FIXED — the dispatch prologue (`tools/transpile.py build_regions`)

   Every cyclic set of segments is now emitted as ONE C function taking the 6502 entry
   address, with a `switch` prologue that `goto`s the right label.  Every transfer inside the
   region becomes a goto, so a loop is a loop.  Each absorbed name survives as a thin wrapper
   (`void FUN_1de5(void) { region_1de5(0x1DE5); }`), so callers, `symbols.csv` names and the
   main-loop phase brackets are all unchanged, and segments in no cycle are untouched.

   `check_split_cycles()` now re-runs the analysis with each region collapsed to one node and
   **hard-fails** if anything is still cyclic — it can, now that the defect is fixed rather
   than merely revealed.  `platform_bad_region_entry()` reports an entry the switch does not
   cover; unreachable by construction, and reported anyway, because "unreachable by
   construction" is the assumption this project keeps being wrong about.

   Two things worth keeping:

   - ⭐ **The performance guess was WRONG, and that is the useful part.**  Re-measured after
     the fix: **2.3-2.5 FPS against a 2.2 baseline — no change.**  The recursion was a genuine
     stack-growth hazard (300-1000 live frames on the overlay) and cost no measurable
     framerate.  `docs/perf-method.md` now says so where it used to speculate the opposite.
   - It also silently fixed a latent SMC bug: the runtime-computed-branch dispatch set was
     truncated at the container's first split point, so a patched branch into a later segment
     had no `case`.  A region has no splits, so the set is now complete.

   The rejected alternative was **full slices** — stop cutting a split function at the next
   split point and emit its whole slice with local labels.  Smaller change, but it duplicates
   code per entry, which costs binary size on a 512 KB machine for no correctness gain.

10. ✅ **The `$7B00` overlay is HEAVILY SELF-MODIFYING — read out, emitted, and RUNNING
    (2026-08-12).**  Found by fixing item 9 and watching what broke next: the region loop spun
    forever on the host with a stable stack, because `$7D13` is `LDA #$60 / STA $7EEE` — it
    writes an **RTS over the `CPX #$2C` that terminates the loop**, and the transpiler was
    emitting the unpatched `CPX`/`BEQ`.

    `make sweep` reports **17 target addresses from 23 stores, and zero stores out into engine
    space**.  ⚠ **That count is the static scan's answer and it is NOT the shape of the
    mechanism.**  Five of the 17 (`$7C00` `$7C0F` `$7E00` `$7E0F`, plus `$7EEE`'s
    neighbours) are only the *static operand base* of a store whose own operand is patched, so
    the address actually written is computed.  Read out instruction by instruction it is **ONE
    idiom used three ways**, and the real site count is **42**, not 17.

    **The idiom.**  `$7C00-$7CFF` and `$7D56-$7EDD` are two **fully unrolled chains** of a
    17-byte column unit:

        LDY table,X ; BEQ +8 ; LDA #0 ; STA table,X ; LDA $6000,Y ; LDY #<row> ; STA (zp),Y

    16 units in chain A (tables `$3000,$3080…$3780`) and 24 in chain B (`$3800…$4380`) — the 40
    `$80`-spaced blocks `copy_dash_data` works from, i.e. **one unit per dashboard/mirror
    column**.  Chain A's last unit does `JMP $7D56`, so the two run as one 40-column sweep;
    the tail at `$7EEE` is `CPX #$2C / BEQ $7F17 / DEX`.  The overlay then steers that
    straight-line code entirely by writing bytes into it:

    | Way | What is patched | Sites |
    |---|---|---|
    | **COUNT** — stop the sweep after column *k* | plant `RTS` (`$60`) over the `STA (zp),Y` at unit+`$0F`, and put `STA` (`$91`) back over the previously planted one | **29** opcode slots + the 9 stores that plant them |
    | **START** — enter the sweep at column *k* | `JSR $7C00` / `JSR $7E00` with the low operand byte patched (`$7F68`, `$7D4D`, `$7F9B`) — a **computed call into the middle of a chain** | **3** |
    | **TERMINATE** | `$7EEE` toggles `CPX #$2C` (`$E0`) ↔ `RTS` (`$60`); `$7D13` writes the RTS on entry, `$7BBF` restores the CPX on exit | **1** |

    ⭐ **The fixed HIGH operand byte is what makes this tractable.** Every writer patches only
    the LOW byte, so a writer based at `$7C0F` can reach page `$7C` and nothing else. The
    patchable set is therefore **derived from the operand encoding, not guessed**: chain A's
    slot at `$7D0E` and chain B's first ten (`$7D65..$7DFE`) are unreachable by construction
    and stay plain `STA (zp),Y`.

    **Two entry offsets within a unit**, and the second one was found by RUNNING the corpus,
    exactly as `$2F89`/`$88` was in Phase 4 — the trap reported `site $7D4C holds $7E05` on
    the first frame that reached the overlay:

    - **+`$00`** the unit start.  It is the operand the image holds, and `$7D13`'s
      `BEQ $7D37` skips the pair that patches `$7D4D`, so the first iteration really does
      execute `JSR $7E00` unpatched.
    - **+`$05`** skips the `LDY table,X / BEQ` dirty test and drops into
      `LDA #0 / STA table,X / LDA $6000,Y` — "force this column, and index the pattern table
      with the Y I am handing you".  All three call sites do `TAY` immediately before the
      `JSR`, which is dead code for any other entry offset; that liveness argument is what
      makes +`$05` the only other offset the code can mean.

    **What was needed in the transpiler** (`tools/transpile.py`, `DASH_CHAINS`):

    - a new SMC class **`'call'`** — a `JSR` whose operand bytes are patched.  Unlike
      `'branch'` it must *return*, so it cannot be a `goto`: it dispatches over the legal
      target set and each case is a real call.  All three targets land inside `region_7bf7`,
      so each case is `region_7bf7(0xTTTT)` — which is what item 9's dispatch prologue is
      for.  The region now carries **59 extra dispatch entries** on top of its 5 segment
      entries, because a computed call reaches mid-unit addresses `build_segments` would
      never produce.
    - a **`'targets'` guard on the `'operand'` class**.  These stores patch an *opcode slot*;
      a low byte that is not a slot boundary would land mid-instruction, the opcode dispatch
      at the real slot would still read `$91`, and the C would diverge from the 6502
      **silently**.  The guard turns that into a `platform_smc_unhandled` trap.

    **Confirmation** (host, one full `$7BE2` call): `mem[$7EEE]` is back to `$E0`, i.e. the
    40-column sweep completed and `$7BBF` restored the page; every recorded operand is exactly
    `$05+$11k` (`$7D4D`=`$16`, `$7F68`=`$5A`, `$7F9B`=`$7C`) or `$0F+$11k` (`$7D24`=`$DB`,
    `$7F24`=`$75`, `$7F7D`=`$97`); `mem[$7C0F]`/`mem[$7E0F]` are `$91`.  45 s of host run and
    a 200 s FS-UAE run: **zero** `platform_smc_unhandled`, `platform_bad_region_entry`,
    `platform_brk`, and a stack that stays 11 frames deep.

    ⚠⚠ **THE THREE ENGINE STORES INTO THIS PAGE ARE NOT SELF-MODIFYING CODE.**  `$3A5C`/`$3A62`
    (`STA $7C79,X`), `$65B7` (`STA $7E85,X`) and `$6597` (`STA $7FC5`) write teletext codes
    (`$97` white-graphics, `$E2`/`$E6` mosaics, `$84`/`$81`/`$98`) — **`$7C00-$7FFF` is the
    MODE 7 SCREEN**.  The overlay and the teletext screen are the *same 1 KB, time-multiplexed*,
    which is exactly why `copy_dash_data` stows the page back before returning to MODE 7
    (item 6).  `$7C79` genuinely is operand byte 2 of the `LDY $3380,X` at `$7C77`, so a
    static scan cannot tell the two apart — only the values can.

    ⭐ **The overlay is now ON BY DEFAULT** (`make gen`; `make gen DASHCODE=0` opts out and
    restores the `platform_brk()` traps).  The four `$7Bxx` call sites are live, so the
    baseline finally includes the mirrors and the dashboard — and it **fell from ≈2.2 to
    ≈1.4 FPS**, i.e. those three main-loop calls are ~36% of the frame.  See
    `docs/perf-method.md` §THE BASELINE.

11. **`DumpHwAccesses.java` still carries Atari ranges.**  The sweep's hardware table above
   supersedes it for now; retool or retire the script rather than leaving a tool that reports
   GTIA registers for a BBC binary.

## Notes on the toolchain

Ghidra 12.1 on macOS arm64 ships **no prebuilt decompiler native** (`os/mac_arm_64/decompile does
not exist`).  Disassembly, analysis and listing export all work; only the decompiler view is
unavailable, which this pipeline does not use — the transpiler reads `listing.txt`.  Not worth
building from source.
