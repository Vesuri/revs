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
vector page — `STA $013B,X`, `STA $018C,Y`, and so on.  All have bases in `$0114-$01A4`, which is
page 1 used as per-car object arrays; the highest (`$01A4`) would need an index of `$5E` to touch
`$0202`, far beyond the field size.  **[DERIVED, not proven]** — the index ranges have not been
bounded from the code, only judged.  Listed rather than silently dropped, because a computed
vector write is exactly the thing that is invisible until it bites.

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

| Addr | Device | RW | Sites |
|---|---|---|---|
| `$FE00` `$FE01` | **6845 CRTC** address/data | W | `$4DE0` `$4DE6` |
| `$FE21` | **Video ULA** palette | W | `$4DFB` |
| `$FE45` `$FE46` `$FE47` | System VIA T1 latch/counter | W | `$4E3A` / `$4E35` `$4E3F` / `$4E47` |
| `$FE4B` | System VIA ACR | R+W | `$4E1B` `$4E1E` |
| `$FE4D` | System VIA IFR | R | `$4E11` |
| `$FE4E` | System VIA IER | W | `$4E26` |
| `$FE64` `$FE65` | User VIA T1 latch | W | `$4E2B` `$4E30` |
| `$FE66` `$FE67` | User VIA T1 counter | W | `$4E42` `$4E4A` |
| `$FE6B` | User VIA ACR | W | `$4E18` |
| `$FE6E` | User VIA IER | W | `$4E23` `$4F32` |
| `$FE68` | User VIA T1 counter-low (read = timer value) | R | `$0E7C` `$274E` `$498C` `$49BD` `$4C06` `$635F` |

Two things fall out of this table immediately:

- **Almost every hardware write is in `$4DE0-$4E4A`** — one contiguous "take over the machine"
  routine covering CRTC, ULA and both VIAs.  That is the platform boundary, and it is one place,
  not scattered.
- **The User VIA timer is read from six sites all over the engine** (`$FE68`), including the
  keyboard helper at `$0E7C`.  This is a free-running timer used as a clock/entropy source, not
  a printer port — the Amiga backend needs a real answer for it, not a stub returning 0.

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

### The measured cross-check

A static tool cannot report its own blind spots, so `tools/bbc_trace.mjs` traces real execution
under jsbeeb and `make sweep TRACE=…` diffs the two.  Of 310 engine addresses executed, **the only
two the static walk did not reach are `$1202` and `$1209`** — inside the pre-relocation loader
stub, which by construction is not in the post-relocation image.  No missed entry points.

⚠ **This trace is weak and must not be over-read.**  The engine parks in a key-configuration wait
loop at `$657C` (polling `$0E50` against a key table at `$39E0`), so only 310 of ~7000 known
instructions ever ran.  It proves the walk covers what executed; it proves nothing about the rest.
Driving Revs to real coverage needs the key table decoded — open item.

## Static coverage, and what is still unclassified

Of `$0B00-$78FF`: **14669 bytes decode as instructions**, 10665 are referenced as data by a
decoded instruction, and **5327 are neither**.  Runs of 16+ bytes:

| Run | Size | Content | Reading |
|---|---|---|---|
| `$3000-$306B` | 108 | binary | |
| `$3200-$324F`, `$327E-$32CF` | 80, 82 | binary | |
| `$41FB-$42CF`, `$42F5-$43CF` | 213, 219 | binary | |
| `$5B14-$5E3F` | 812 | **all zero** | inside the X=0 zero-filled block `$5A80-$5E40` — workspace, explained |
| `$66FF-$6E84` | 1926 | binary | overlaps the X=1 move's **dead source** `$64D0-$6C00` |
| `$6F8A-$6FB1` | 40 | binary | |
| `$7206-$77DA`, `$77E5-$78FF` | 1493, 283 | binary | the region the swap moved engine data *into* (`$70DB-$7800`) |

"Unclassified" here means *no decoded instruction names the address directly*.  Most of it is
expected to be data reached through zero-page pointers, which no absolute-operand scan can see —
so this list is a **work queue, not a defect list**.  `docs/entrypoint-sweep.md`'s definition of
done requires every run classified before C is generated.

## Open items — what Phase 2 still owes

1. **Classify the seven unclassified runs above.**  Zero-page-pointer data or unreachable code?
   The lever is an indirect-addressing pass: resolve `LDA ($70),Y`-style accesses by finding what
   writes the pointer.
2. **Re-run the sweep per track.**  Every finding here is Silverstone's runtime image.  The four
   expansion tracks are *executable* and patch the engine (`docs/reference-sources.md`), so the
   dispatch structure, the SMC inventory and possibly the entry set differ per track.  The
   before/after dumps already exist in `tmp/`; `tools/relocate.py` needs a per-track pass.
   ⚠ **Correct an earlier assumption while doing it:** `tools/bbc_refloop_track_diff.mjs` reads
   its `$1200-$6FFF` diff as "the per-track hook patch surface", and its header predicts 0 diffs
   for Silverstone.  Silverstone shows **5805**.  The bulk of that diff is the engine's own
   unpack, which happens for *every* track — so the script measures unpack + patch together, and
   the hook surface is whatever remains after `relocate.py` is subtracted.
3. ~~Read out the OSBYTE/OSWORD reason codes.~~ **Done** — all 17 sites resolved except `$50F6`
   (A from a variable).  Still owed: confirm the heuristic against a trace, and read out the
   *parameters* (X/Y) at the ADC and buffer-flush sites, which is what the implementation needs.
4. **Decode the key table at `$39E0`** (`9d cf ce ee dd ee`, negative-INKEY codes, and `$39E4-$39E5`
   are written at runtime).  Needed to drive the tracer into an actual race — which is what turns
   the coverage cross-check from weak into strong.
5. **The naming pass** (`disasm/symbols.csv`).
6. **`DumpHwAccesses.java` still carries Atari ranges.**  The sweep's hardware table above
   supersedes it for now; retool or retire the script rather than leaving a tool that reports
   GTIA registers for a BBC binary.

## Notes on the toolchain

Ghidra 12.1 on macOS arm64 ships **no prebuilt decompiler native** (`os/mac_arm_64/decompile does
not exist`).  Disassembly, analysis and listing export all work; only the decompiler view is
unavailable, which this pipeline does not use — the transpiler reads `listing.txt`.  Not worth
building from source.
