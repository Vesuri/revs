# BBC Micro hardware — the abstraction boundary

> The Atari port had `docs/atari-hardware.md`: a primer plus the Atari→host→Amiga mapping, and it
> *was* the definition of what `platform.h` had to expose.  This is that file for the BBC, and it
> is deliberately marked up with what is **derived** vs what is still **assumed** — the postmortem's
> whole theme is not letting an assumption calcify into a documented fact.
>
> ✅ **Phase 2 replaced every [ASSUMED] row here with a [DERIVED] one.**  The hardware-access map
> now exists twice over, from two independent tools (`tools/sweep_entrypoints.py` and
> `ghidra_scripts/DumpHwAccesses.java`), and it answered the question this file was written to
> ask: *which registers does Revs actually touch, and from where.*  Site-level evidence and what
> each finding implies for the port live in **`docs/static-map.md`**.
>
> Three of those answers contradicted the assumption they replaced — the interrupt source, the
> ADC, and the sound chip.  Each is called out inline with a ⚠.  Anything still resting on general
> BBC knowledge rather than this binary is marked **[ASSUMED]** and is a to-do, not a fact.

## The machine

BBC Micro B / B+ / Master: **6502 @ 2 MHz**, 32 KB RAM, MOS in ROM at `$C000-$FFFF`, a paged
ROM slot at `$8000-$BFFF` (BASIC lives there).  So a game's RAM is roughly `$0000-$7FFF`, and
screen memory is carved out of the top of it.

Revs's own footprint, **[DERIVED from `revs.ssd`]** — *Revs Plus Revs 4 Tracks*, ©
Superior/Acornsoft 1986; 200 KB single-sided 80-track DFS, title "REVINST", `*OPT 4,3`:

| File | Load | Exec | Length | What |
|---|---|---|---|---|
| `!BOOT` | — | — | `$30` | `*BASIC` / `PAGE=&1900` / `*FX21` / `CHAIN "REVINST"` |
| `REVINST` | `$1900` | `$8023` | `$2C40` | BASIC — instructions + banner |
| `REVSMEN` | `$1900` | `$1900` | `$433` | track menu; `*LO.<TRACK>` then `*/REVS2` |
| `5TRSCRN` | `$7C00` | `$7C00` | `$400` | **MODE 7 teletext title screen** (`$7C00` is MODE 7 screen RAM) |
| **`REVS2`** | `$1200` | `$1200` | `$5E00` | **the 24 KB machine-code engine** — one binary, five tracks |
| `SILVER` | `$70DB` | **`$0000`** | `$739` | Silverstone (1985) — **passive data** |
| `BRANDS` `DONING` `OULTON` `SNETTER` | `$70DB` | **`$70DB`** | `$738`–`$73C` | the expansion circuits — **executable** |

So the engine occupies `$1200-$7000`, track data `$70DB-$7817`, and the MODE 7 screen sits at
`$7C00-$8000` — i.e. RAM is essentially full from `$1200` up.

⚠⚠ **The expansion track files patch the engine as it starts** (`ModifyGameCode`, `CallTrackHook`,
per-track hooks), so the executing bytes differ per track.  The exec addresses above are the
binary's own evidence for it — Silverstone's data is not executable; every expansion track is.
See `docs/reference-sources.md`.

## Memory-mapped I/O — the platform boundary

The whole I/O window is the contiguous `$FC00-$FEFF`; `src/cpu/bus.h` routes it to
`Platform::hwRead`/`hwWrite` and everything else to `mem[]`.

⭐ **MEASURED.**  The table below is now the sweep's output, not general BBC knowledge — 19
registers across 4 devices, agreed independently by `tools/sweep_entrypoints.py` and
`ghidra_scripts/DumpHwAccesses.java`.  Full site lists and what each one implies:
**`docs/static-map.md`**; regenerate with `make sweep` / `disasm/hw-access.md`.

| Range | Device | Revs's actual use |
|---|---|---|
| `$FC00-$FCFF` | FRED (1 MHz bus) | **[DERIVED]** never touched |
| `$FD00-$FDFF` | JIM (1 MHz bus, paged) | **[DERIVED]** never touched |
| `$FE00-$FE01` | **6845 CRTC** | **[DERIVED]** written from one place, `$4DE0`/`$4DE6`, during setup |
| `$FE08-$FE1F` | 6850 ACIA + serial ULA | **[DERIVED]** never touched |
| `$FE20-$FE21` | **Video ULA** | **[DERIVED]** ⭐ control at `$FE20` and the 16-entry palette at `$FE21` are rewritten **mid-frame, per raster band, from the IRQ handler** — this is where the visual character lives |
| `$FE30-$FE3F` | ROM select latch | **[DERIVED]** never touched |
| `$FE40-$FE5F` | **System VIA** | **[DERIVED]** T1 latch/counter, ACR, IFR, IER — all written once, during setup at `$4E11-$4E47`.  ⚠ *Not* the game's interrupt source |
| `$FE60-$FE7F` | **User VIA** | **[DERIVED]** ⭐ the game's real interrupt source: **T1 timeout drives the 50 Hz body**, and T2 is read as a free-running clock from six sites |
| `$FE80-$FE9F` | 1770/8271 FDC | **[DERIVED]** never touched — the BASIC front end did all the loading |
| `$FEC0-$FEDF` | uPD7002 ADC | **[DERIVED]** ⭐ **never addressed directly.**  The steering is read via `OSBYTE 128`, configured via `OSBYTE 190` |
| `$FEE0-$FEFF` | Tube | **[DERIVED]** never touched |

### The three that actually matter — and one correction

- **Video ULA + 6845** define the screen composition.  This is the piece with **no Atari
  counterpart in the tooling**: the display-list analyser skill has to be re-tooled for
  CRTC+ULA (`docs/bbc-reference-loop.md` step 5).  The guess that "palette tricks and mid-frame
  register changes are where the visual character lives" was right, and it is now measured: the
  IRQ handler runs a **5-state raster-band machine** (`$4F43`), each band setting a ULA mode plus
  a full 16-entry palette — one band's palette read from a table at `$3468`, another *computed* by
  stepping the high nibble.  `docs/static-map.md` has the code.
- ⚠ **CORRECTION — the 50 Hz body is a USER VIA T1 timer interrupt, not System VIA vsync.**  The
  handler at `$4E5C` (claimed via `IRQ1V`) opens with `LDA $FE6D / AND #$40` — the User VIA T1
  timeout flag — acknowledges it, and chains to the saved previous handler otherwise.  The System
  VIA registers are written once at setup and never again.  So "System VIA 50 Hz vsync IRQ →
  `INTB_VERTB`" in the mapping table below, and in `docs/amiga-arch.md`, is **too simple**: this is
  a raster-timed timer chain that repaints the palette mid-frame, which is copper work with only
  the band-0 boundary belonging in VERTB.  Resolve this before Phase 5 designs the copper list.

  ⭐ **Phase 4's interim answer, and the line Phase 5 has to change.**  One Amiga VERTB drives the
  WHOLE band cycle — `Revs::vbi()` calls `Platform::fireIrq1v()` until `$4F43` wraps to 0 — because
  dispatching one band per interrupt would tick the simulation at 10 Hz while everything else
  looked healthy.  The bands therefore all fire at the top of the frame instead of at their
  scheduled raster positions, collapsing the mid-screen splits into one; invisible while nothing
  is drawn.  `g_userT1LatchLo`/`g_userT1LatchHi` (`src/platform/bbc_hw.cpp`) capture the T1 reload
  the handler writes at the end of each band — **that sequence IS the raster schedule**, and it is
  what the copper WAITs get built from.

  ⚠ Two things the port had to get right here, both of which read as something else when wrong:
  - **`fireIrq1v()` must not dispatch until the game has claimed IRQ1V** (`$0204/$0205` = `$4E5C`).
    The backend installs its vblank before `engine_main()` runs, and `$4F43` starts at 0 in the
    image — a valid band — so the handler cheerfully steps a state machine whose timers and
    palette tables do not exist yet.  Measured: the counter ended on `$FE`, a state the dispatch
    has no arm for, and the 50 Hz body then never ran again.
  - **The handler ends in `RTI`, emitted as `PLP(); return;`** — so the caller must push a P byte
    first, exactly as the 6502's IRQ sequence would.  Without it `cpu.S` walks back one byte per
    frame and page 1 is corrupted 256 frames after the code that caused it.
- ⭐⭐ **User VIA T2 (`$FE68`) is the game's ONLY source of entropy, and it is a CLOCK.**  Revs has
  no PRNG.  Every random-looking decision it makes reads the low byte of the User VIA's T2 counter
  and masks it: the starter's catch delay (`$498C`, `AND $09` — so the crank succeeds about one
  poll in eight, not on the first), the idle-rev jitter (`$49BD`, `AND #7`, which is why a real BBC
  idles at `$2C` and not a round number), the gravel/skid trigger (`$0E7C`, `CMP #$3F`), `$274E`,
  `$4C06`, `$635F`, and the mirrors' engine shudder in the `$7B00` overlay (`$7FB6`, where it is
  ANDed with game code at `$2000,X` as a second decorrelator).
  **[MEASURED]** with `make refloop --park --via-t2`, which samples what the real 6502 got at each
  site — one instruction *after* each read, because reading T2C-L clears the T2 interrupt flag and a
  probe that reads the register perturbs the machine it is measuring.  At `$635F` (32 reads in one
  loop): 28 distinct values over `$18..$CC`, and **22 of 31 successive samples land exactly on
  "previous value minus the microseconds that elapsed"**, 24 of 31 within ±2.  T2 free-runs *down*
  at 1 MHz and keeps counting past its timeout, so the observable is the elapsed-time low byte,
  negated — decorrelated from game code because it is a clock, not because it is random.
  ✅ Modelled in `src/platform/bbc_hw.cpp` behind `Platform::hwMicros()`: `steady_clock` on the
  host, `VHPOSR` + the field count on the Amiga (one `move.w`), and the deterministic field-counted
  base under `REVS_FIXED_RNG` so a perf run stays pinned.  ⚠ The port answered a constant `$0`
  here until 2026-08-16, which is not a harmless stub — it removed the crank delay, pinned the idle
  at `$28`, and made the gravel trigger fire on every call.
- **uPD7002 ADC** is the steering.  A racing sim reading an analogue axis is a *different* input
  problem from a digital joystick, and it is worth getting exactly right early: the feel of the
  game is in it.
  ⚠ **But Revs never touches the ADC registers.**  It reads the axis through **`OSBYTE 128`**
  (`$168E`, `$5041`) and configures resolution through **`OSBYTE 190`** (`$3879`).  So the steering
  seam is in `Platform::mosCall`, and `bus.h` has nothing to intercept — the opposite of what the
  mapping table below implied.
  ✅ **IMPLEMENTED in Phase 5** (`src/platform/amiga/RevsInput.*`): the flag that picks analogue
  vs keys is `$05F5` bit 7, `engine_init` zeroes it (so the game boots in KEYBOARD mode), and the
  player switches with the game's own SHIFT+f1 / SHIFT+f2 — decoded from the key table at `$3DE2`
  paired with the action bytes at `$39D4`.  That decode also settles the BBC internal key-number
  layout: `(row = n>>4, col = n&15)`, row 7 = f0..f9, so -114 is f1 and -115 is f2.

  **Decision: the Amiga port uses mouse + keyboard** (user, 2026-08-12).  The BBC's own
  `SHIFT+f1` keyboard mode is the faithful precedent for digital steering; the mouse stands in for
  the analogue axis.  Note the game has a `HookJoystick` per track and a "SPACE — amplify
  steering" key, so the analogue response curve is real logic to be reproduced, not a range to be
  invented — read it out of the binary rather than tuning by feel.

  ⭐ **The steering DIRECTION is [DERIVED], and the one link that was not cost a play-test**
  (2026-08-16).  The chain from the mouse to the wheel is derivable end to end: `kbd_test_key`
  returns **Z set when held**, so `$15B3`'s fall-through arm is the held arm and L gives `$76 = 2`
  while `;/+` gives 1; `$15E7` EORs `$76` with `$62A2` and ANDs #1, which proves `$76` bit 0 *is*
  the direction bit; `adc_read`'s `TYA / LDX #1 / ADC #$80 / BPL` (`$5044`) leaves X = 1 for an ADC
  high byte ≥ `$80`, and `$15A7`'s `TXA / ORA $74` makes that the same bit (`mul8` at `$0C02` is
  fully unrolled and never touches X, so it survives the intervening call).  The disc's own
  `REVINST` prints "L - Steer left" / "+ - Steer right", so **direction bit 0 = LEFT** with nothing
  assumed.  ⚠ The single link *not* in that chain was the Amiga mouse's own counter direction, and
  it was assumed — wrongly.  It steered backwards on the target while every derived step was right,
  which is why `MOUSE_X_SIGN` in `RevsInput.cpp` is one named constant carrying the empirical sign.

  ⭐ **The mouse BUTTONS are a deliberate divergence** (user, 2026-08-16): **right = throttle, left
  = brake**, driving ADC channel 2 the way the S/A keys already do.  There is no BBC precedent to be
  faithful to — analogue mode has exactly **one** button and spends it on the gearbox (`$168E` reads
  it, `$1696` picks up-vs-down itself from `$3E`/`$3F`; the player never chooses a direction), so
  that faithful meaning goes on the **middle** button and nothing is lost.  ⚠ Left/right/middle come
  from two different places — left is `CIAAPRA` bit 6, right and middle are `POTINP` bits 10 and 8 —
  and the `POTINP` pair only report while the pot pins are **inputs**, hence the one-off `POTGO`
  clear in `initialize()`.  ⚠⚠ **FS-UAE does not model that dependency**: removing the clear
  entirely still reads both buttons correctly (measured by sabotage), so this is a real-hardware-only
  requirement that no host run can verify.  `amiga/mousebtn.gdb` proves the released state reads
  released and says so in its own header.

## MOS calls — genuinely new vs the Atari port

⚠ **This is the structural difference from RoF, and it needs a design decision, not just code.**

The Atari port simply *replaced* the Atari OS: the game's own code was self-contained enough that
the OS ROM was mostly a source of shadow registers.  **Revs runs under the MOS.**  It reaches the
keyboard, the ADC, the disc and the screen through OS entry points:

⭐ **MEASURED — the list is complete: four entries, 17 call sites.**  `docs/static-map.md` has
every site with its reason code.  **Revs calls no filing-system entry at all** (no OSFILE, OSARGS,
OSBGET, OSBPUT, OSGBPB, OSFIND) — the BASIC front end did all the loading, so the engine never
touches the disc.

| Entry | Call | Sites | Reason codes Revs uses |
|---|---|---|---|
| `$FFF4` | **OSBYTE** | 10 | `129` negative INKEY (the keyboard); **`128` read ADC — the steering**; `190` ADC conversion type; `154` write the ULA control register via its OS copy; `21` flush a buffer; `4` cursor-key behaviour; `2` select input stream; `126` acknowledge ESCAPE; **`0` read the OS version** ⭐ |
| `$FFF1` | **OSWORD** | 2 | **`8` define a sound ENVELOPE** — so sound goes through the MOS's scheduler, not the SN76489 directly; `10` read a character definition |
| `$FFEE` | OSWRCH | 4 | VDU output (`127`, `7`, `156`, one from a variable) |
| `$FFE0` | OSRDCH | 1 | read a character |
| — | filing system | **0** | **[DERIVED]** never called |

Plus, in the loader stub only: `OSBYTE 200,3` and `OSBYTE 140,0`.

These are **not** bus addresses — they are `JSR` targets into ROM, so `bus.h` never sees them.
They are intercepted as MOS calls in the transpiler / native layer and serviced by
`Platform::mosCall(entry)`.

**The discipline: enumerate every MOS call Revs makes, from the disassembly, BEFORE implementing
any of them.**  Same reasoning as the entry-point sweep — a MOS call you did not know about is a
behaviour you will reason about wrongly.  ✅ **Done** (Phase 2, table above).  ⚠ The reason codes
are recovered by a nearest-preceding-`LDA #imm` heuristic, so they are **[DERIVED, heuristic]** —
and the *parameters* (X/Y) at the ADC and buffer-flush sites are not read out yet, which is what
an implementation actually needs.

⭐⭐ **Phase 5 added TWO more, and one of them is a whole subsystem.**  Measured on the target
with `amiga/mos.gdb` (`g_mosUnknownCount` + entry + A):

- **`OSWORD 0` (read a line)** — caught in the front end.  So `console_io` reaches the OS through
  OSWORD 0, not only the OSRDCH site `docs/static-map.md` §Open items 6 assumes.
- **`OSWORD 7` (SOUND)** — was not caught, and that absence was the measurement: the engine sound
  is built from OSWORD 7 command blocks, so a single sound would have shown up as an unhandled MOS
  call.  None ever did, which is how we knew **the scripted run never started the engine**.
  ✅ **IMPLEMENTED AND MEASURED, 2026-08-15** — see §Sound below.  A `STRAIGHT_TO_RACE` run with the
  throttle held now counts 298 of them.

Both were invisible to the static pass for the same reason as OSBYTE 0: the reason codes come from
a nearest-preceding-`LDA #imm` heuristic and a *computed* `A` defeats it.  **Treat the table above
as a floor, not a closed set** — three of its rows were found by running the game, not reading it.

⭐ **Phase 4 added reason code `0` (read the OS version) at runtime.**  It is not a tenth static
site — it is one of the ten whose `A` is *computed*, so the nearest-preceding-`LDA #imm` heuristic
attributed it to something else.  That is precisely why the rows above are **[DERIVED,
heuristic]**, and why `src/platform/mos.cpp` counts an unhandled call instead of returning a
plausible zero: the counter is what found it.  Also measured on the target: **OSWORD 10 (read a
character definition) fires ~850 times in the first 900 vblanks**, so reproducing the MOS font is
a genuine Phase 5 dependency, not a footnote — the port currently returns blank glyphs and counts
them in `g_mosCharDefCount`.

✅ **`OSWORD 10` is IMPLEMENTED as of 2026-08-15** — `src/platform/mos_font.h`, 96 printable glyphs,
**drawn** rather than extracted (the MOS software font is Acorn's ROM with no published spec behind
it, unlike the SAA5050 set).  Measured need: 88 calls / 15 codes / all `$20..$74` in one practice
race (`make refloop-charset`); the whole printable range is drawn anyway and anything outside it is
counted in `g_mosCharDefOutOfRange`, because one session's vocabulary is a floor.

⭐⭐ **And there is a THIRD entry to `vdu_char_def`: `$508C`, which is DOUBLE WIDTH.**  It stores the
code and `JMP $509D`, skipping `$509B`'s `LDA #0 / STA $77`, so the caller's `$77` survives to reach
the arm at `$50AE` — `AND #$F0` keeps the glyph's left four columns, `ASL A` x4 lifts its right four
into place, and each half becomes one MODE 5 cell.  Its only caller is the gearstick readout
`$42D0` (`$77` = `$22` then `$FF`; codes 'N' and '1').  **Reading the code says that arm is dead**,
because `$509B` zeroes the flag unconditionally — it was found by counting entries on real hardware
(92 reads at `$50AA` vs 88 at `$5096`).  It also constrains the font: the glyph body must be CENTRED
in the cell or narrow characters lose their second half.

⚠⚠ **THOSE `OSWORD 10` CALLS ARE THE RACE VIEW, NOT THE FRONT END — do not read them as a MODE 7
dependency** (they were, for two phases).  `vdu_char_def` (`$5092`) has two arms selected by `$64`
bit 7 and they use two *different fonts*: the bitmap arm asks `OSWORD 10` for a **MOS ROM** glyph
and plots it into the frame buffer, while the MODE 7 arm calls **OSWRCH** and the **SAA5050**
teletext chip — which has its own character ROM and is not addressable by the CPU at all — draws
the cell.  Measured on a real BBC in the front end (`tools/bbc_probe_mode7.mjs`): **310 OSWRCH
calls and ZERO OSWORD 10 calls.**  So MODE 7 needed a VDU driver, not a font (done —
`src/platform/teletext.h`), and `OSWORD 10` belongs to the race view's own text — now implemented
above, with its own drawn font.

⚠ The implementation is `src/platform/mos.cpp`, shared by BOTH backends rather than overridden per
platform.  OSBYTE 129's contract (X=Y=$FF when the key is held) is the 6502's ABI and identical
everywhere; only *where the input comes from* differs, and that is five virtual hooks
(`keyDown` / `adcAxis` / `adcButtons` / `rdch` / `wrch`).  Duplicating the dispatch per backend
would let the host and the target disagree about the OS itself.

Also relevant, and already visible in the BASIC front end: `*FX21` in `!BOOT` is `OSBYTE 21`
(flush a buffer); `*FX200,3` in `REVINST`/`REVSMEN` disables ESCAPE and clears memory on BREAK;
`*FX15` flushes input; and `REVINST` calls `A%=114:X%=1:CALL &FFF4` — an explicit **OSBYTE 114**
(select the shadow/main screen bank on a B+/Master).  So the game is using OSBYTE well before the
engine starts, and `$FFF4` appears in the BASIC as a literal.

## OS vectors — the interrupt seam

`$0200-$0235` holds the MOS vectors.  These stay in `mem[]`, and the platform is *notified* of
writes there (the analogue of the Atari port tracking VVBLKI/VDSLST).  The ones that make code
reachable are enumerated in `docs/entrypoint-sweep.md` §2 — **IRQ1V (`$0204`)** and **EVNTV
(`$0220`)** are the two most likely to carry Revs's own 50 Hz body.

## Screen modes

**[DERIVED]** Revs uses a multi-band display built by **mid-frame register changes**, not two
independent regions — as guessed, and now measured.  The IRQ handler at `$4E5C` dispatches on a
band counter at `$4F43` with five states, and each band writes the ULA control register (`$88` for
one band, `$C4` for another) followed by all 16 palette entries.  So the viewport needs **at least
four copper bands**, one of them with a *computed* palette.  `docs/static-map.md` has the
disassembly.  ⚠ Still to derive before the copper list is designed: which scanlines the bands
start on (the T1 reload values at `$4F01`/`$4F04` and the CRTC setup at `$4DE0`), and what `$3468`
holds.

The Amiga side of the mapping (region splits, pointers-before-colours, band rules) is in
`docs/amiga-lessons.md`.

## What the port replaces, and with what

| BBC | Amiga |
|---|---|
| 6845 CRTC + Video ULA | copper list + bitplanes |
| Video ULA palette writes (incl. mid-frame) | copper `COLORxx` MOVEs at an end-of-previous-line WAIT |
| ⚠ **User VIA T1 timer IRQ** (not System VIA vsync) — a raster-timed chain | `INTB_VERTB` for the frame boundary **plus copper MOVEs for the mid-frame band changes**. The one-to-one "vsync → VERTB" mapping does not hold; see the correction above |
| ⚠ SN76489 **via the MOS sound scheduler** (`OSWORD 7`/`8`; the chip is never addressed) | `src/platform/sound.c` reproduces the SCHEDULER (validated: `make sound`), `RevsAudio.cpp` maps the chip state onto Paula — §Sound below |
| ⚠ ADC steering **via `OSBYTE 128`**, never the `$FEC0` registers | **mouse + keyboard** (decided), serviced in `Platform::mosCall` — there is nothing for `bus.h` to intercept |
| Keyboard via OSBYTE | CIA-A serial-port keyboard handler |
| Disc-loaded track data (the engine itself never calls the filing system) | embedded in the binary (see `incbin.s`) |
| MOS `$C000-$FFFF` | `Platform::mosCall` for the calls Revs actually makes |

## Sound — ⭐ MEASURED, and the model is validated tick-for-tick (2026-08-15)

⚠ **THE ENGINE NOTE RAMPS ~50x TOO SLOWLY ON THE TARGET, AND THAT IS THE FRAMERATE, NOT AUDIO.**
`sfx_trigger_random` is a MAIN-LOOP call, so the note-change rate is the painted-frame rate; at
~2.6 FPS against 50 it crawls, and no amount of work in `sound.*` or `RevsAudio.*` will change it.
Verify with `GDBSCRIPT=sound.gdb ./diag_run.sh 70` on a `STRAIGHT_TO_RACE` build — a run that never
starts the engine measures silence.

**Revs never addresses the SN76489.**  Every note is an `OSWORD 7` (SOUND) control block and one
`OSWORD 8` (ENVELOPE) definition, through `sound_queue` (`$0B4A`), `sound_queue_default` (`$0B47`)
and `sound_envelope` (`$0B65`) — one `JSR $FFF1` at `$0B70` serving two reason codes, which is
exactly why the Phase 2 nearest-`LDA #imm` scan never attributed reason 7 to it.  So what the port
had to reproduce is **the OS's scheduler**, and none of that is in the game binary.

**The model and every number in it are in `src/platform/sound.h`** — read that, not this.  The
Amiga mapping onto Paula is in `src/platform/amiga/RevsAudio.h`.  What belongs here is the
instrument and the shape of the evidence:

| Command | What it does |
|---|---|
| `make sound-fixture` | sweep a real MOS 1.20 under jsbeeb: all 256 pitches, every amplitude, the same pitch on all three tone channels, all 8 noise modes, Revs's own envelope, duration expiry, the release phase, and an `OSBYTE 21` flush |
| `make sound-fixture-race` | capture **Revs's own** command + chip stream out of a real driving race (`--drive`; a parked car records silence) |
| `make sound` | replay both through `src/platform/sound.c` and diff the chip state **tick by tick** — currently 0 of 8918 and 0 of 8083 |
| `GDBSCRIPT=sound.gdb ./diag_run.sh 70` | on the target: OS calls → scheduler ticks → chip writes → Paula updates, so a zero says *which* link is broken |

Five things the measurement settled that no amount of reading the game could:

1. **pitch → divider** is one octave of 48 dividers shifted right by the octave, **plus the channel
   index** — the same pitch on channels 1/2/3 comes out one divider count apart.  The MOS detunes
   its own channels, and Revs's two engine tones (a fixed 28 pitch units apart) beat accordingly.
2. **attenuation = 15 - (level >> 3)** for static amplitudes and envelopes alike, and **only the low
   byte of the amplitude word counts**.  Revs writes just that byte and leaves the template's `$FF`
   high byte, so the real machine sees `$FF00` and plays silence — read as a signed word it is −256.
3. **the pitch envelope repeats** unless byte 1 bit 7 is set, and the accumulated pitch is *not*
   reset at the repeat.  That is why Revs's sections are net-zero (+2 ×4, −2, −6).
4. **`OSBYTE 21` on buffer 4+channel really silences a playing sound** (and resets its divider).
   Load-bearing: it is the only way Revs stops anything, since every sound has duration 255.
5. **the engine sound is one mechanism with two halves.**  Below rev `$5C` the NOISE channel plays
   and channel 1 is muted *purely to supply its divider* (noise pitch 3 = "use tone 1's frequency");
   above it the noise channel is flushed off and the two tones sound.  A port that ignored a muted
   channel's pitch would play a fixed-pitch buzz.

⚠ **The engine note ramps ~50× too slowly, and that is the FRAMERATE, not the audio.**
`sfx_trigger_random` (`$0E74`) steps the rev counter `$0060` by one toward `$005F` per call and is
called from the main loop, which paints at ~1 FPS instead of 50 — so the pitch crawls to where a
real BBC would arrive in a fifth of a second.  The 100 Hz scheduler tick itself is *not* affected:
it runs off the VERTB, two per field (`RevsAudio.h`).

⚠ **Untested paths, deliberately counted rather than guessed:** the channel byte's SYNC and HOLD
nibbles and any *queued* (non-flush) sound.  Revs has never issued one — all five of its blocks are
`$10..$13`, i.e. flush — so `g_sndSyncRequests`/`g_sndHoldRequests`/`g_sndQueued` exist to say if it
ever does.  They must stay 0.
