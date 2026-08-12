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
| `$FE68` | User VIA T2 counter-low (read = timer value) | R | `$0E7C` `$274E` `$498C` `$49BD` `$4C06` `$635F` |
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

## Open items — what Phase 2 still owes

1. 🔧 **Classify the residual 685 bytes** — `$6C00-$6E84` and `$6F8A-$6FB1` (down from 5327; see
   §Static coverage).  The indirect-addressing pass is done; these two need reading, not tooling.
2. **Disassemble the track programs.**  The hook *inventory* is done (above); the hook *bodies*
   are not.  `$5300-$5A25` holds a program for four of the five circuits, and nothing has
   disassembled it yet.  Sweep it per track with the hook targets as roots.
3. ~~Read out the OSBYTE/OSWORD reason codes.~~ **Done** — all 17 sites resolved except `$50F6`
   (A from a variable).  Still owed: confirm the heuristic against a trace, and read out the
   *parameters* (X/Y) at the ADC and buffer-flush sites, which is what the implementation needs.
4. ~~Decode the key table at `$39E0`.~~ **Done** (§The front end).  It is SPACE / 1 / 2 / 3 at
   runtime.  **Driving to a real race is still open, and is now a different question than it
   looked:** the front end blocks on `BIT $05F4 / BVS` at `$6560`, waiting for bit 6 to be cleared
   by something other than the keyboard.  Find what clears it and the trace becomes strong.
5. **Extend the naming pass.** 52 symbols are in `disasm/symbols.csv` and applied to the Ghidra
   project (`ApplyNames`), covering the entry seam, the hardware registers, the math primitives,
   input, sound, the text interpreter and the rasteriser's outer shape.  **That is ~20% of the 236
   functions.**  The unnamed remainder is the physics and the 3D pipeline — the parts that need
   real reading, not profiling.  `--functions` gives the evidence; work down it by caller count.
6. **`DumpHwAccesses.java` still carries Atari ranges.**  The sweep's hardware table above
   supersedes it for now; retool or retire the script rather than leaving a tool that reports
   GTIA registers for a BBC binary.

## Notes on the toolchain

Ghidra 12.1 on macOS arm64 ships **no prebuilt decompiler native** (`os/mac_arm_64/decompile does
not exist`).  Disassembly, analysis and listing export all work; only the decompiler view is
unavailable, which this pipeline does not use — the transpiler reads `listing.txt`.  Not worth
building from source.
