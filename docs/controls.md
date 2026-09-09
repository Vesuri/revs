# Controls — the BBC original and their Amiga counterparts

**Sources, in order of authority:**

1. **The printed handbook** — *Acornsoft Revs: Formula 3 Driver's Handbook* (the Revs + 4 Tracks
   edition; a scan is at `tmp/Acornsoft_Revs_Formula_3_Drivers_Handbook.pdf`, git-ignored).
   Chapter 3 *The Controls* and chapter 4 *Practice Laps and Qualifying Times*.
2. **`REVINST`** — the instructions program on the disc itself, which carries the full SHIFT-key
   list the printed handbook does not (CAS and the volume keys are 1986 additions). Detokenise it
   with the loader: the file is at disc offset `$300`, length `$2C40`.
3. **`shift_key_tbl` (`$3DE2`) + `shift_key_action_tbl` (`$39D4`)** — the engine's own table, which
   is what actually runs.
4. **Measured on a real BBC** with `tools/bbc_refloop_race.mjs --press=` (see §Measured, below).
   Anything tagged ⭐ MEASURED was run, not read.

⚠ The three written sources agree everywhere they overlap, once one OCR trap is cleared: the
handbook's "SHIFT/→" is printed in `REVINST` as `SHIFT+]`, because on the SAA5050 (MODE 7) the
character `]` **displays as →**. It is the right-arrow key, and nothing to do with a bracket.

## How a BBC key becomes an Amiga key

The game never reads ASCII: every control is `OSBYTE 129` with a negative INKEY number, i.e.
`kbd_test_key` (`$0E50`) with the byte `256-n` in X. A BBC internal key number is
`(row << 4) | col`, and the negative-INKEY byte is `255 - internal`, so the map is derived by
inverting jsbeeb's own key matrix (`tools/jsbeeb/src/utils.js`, the `BBC` table) — never guessed.

⚠⚠ **The function keys are not contiguous.** f0, f4 and f7 share their columns with other keys and
sit outside row 7:

| key | f0 | f1 | f2 | f3 | f4 | f5 | f6 | f7 | f8 | f9 | ESCAPE | COPY | DELETE | → |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| code | `$DF` | `$8E` | `$8D` | `$8C` | `$EB` | `$8B` | `$8A` | `$E9` | `$89` | `$88` | `$8F` | `$96` | `$A6` | `$86` |

An earlier version of `src/platform/amiga/RevsInput.cpp` assumed "row 7 is f0..f9" and every single
function-key row was bound to the wrong Amiga key; SHIFT+f0 ("return to pits") was on Amiga **F6**,
and two BBC codes collided on that one key.

## Driving

| What | BBC key | code | Amiga key |
|---|---|---|---|
| Steer left | `L` | `$A9` | `L`, or ← |
| Steer right | `+` (`;/+`) | `$A8` | `;`, or → |
| Amplify steering (fine lock) | `SPACE` | `$9D` | `Space` |
| Throttle | `S` | `$AE` | `S`, or ↑ |
| Brake | `A` | `$BE` | `A`, or ↓ |
| Change up / draw the clutch | `Q` | `$EF` | `Q` |
| Change down / draw the clutch | `TAB` | `$9F` | `Tab` |
| Starter motor | `T` | `$DC` | `T` |

Holding both steering keys at once holds the current lock; releasing both lets the wheel
self-centre at a rate set by road speed. Holding `Q` or `TAB` draws in the clutch — releasing it is
what transmits power, which is the whole racing start (hold `T`, `S` and `Q` together to start the
engine in gear).

**The arrow keys are this port's addition**, not the BBC's; the BBC's own steering keys are mapped
on the same letters, because a player following the game's own instructions was pressing keys the
map did not carry. ⚠ → therefore has two jobs on the Amiga (steer right, and — with SHIFT — quit).
The BBC had no such overlap. Quitting wins, which is the harmless order.

**Steering with the mouse** replaces the BBC's uPD7002 analogue joystick (user decision). Select it
the same way the BBC selects a joystick: **SHIFT+F2**.

## The SHIFT commands

`shift_key_commands` (`$0EE5`) runs once per race frame, does nothing unless SHIFT is held, and
scans `shift_key_tbl` from index `$0B` **down**, so where a code appears twice the higher index
wins. Each key's action byte names a byte of the `state_flags` block (`$05F4+n`) and the value to
store there.

| Command | BBC | code | writes | Amiga |
|---|---|---|---|---|
| Return to the pits and re-set the wings (**only while stationary**) | SHIFT+f0 | `$DF` | `$05F4` = `$C0` | SHIFT+**F10** |
| Select KEYBOARD steering | SHIFT+f1 | `$8E` | `$05F5` = `$00` | SHIFT+**F1** |
| Select JOYSTICK steering — **the mouse**, on the Amiga | SHIFT+f2 | `$8D` | `$05F5` = `$80` | SHIFT+**F2** |
| Disengage Computer Assisted Steering | SHIFT+f3 | `$8C` | `$05F8` = `$00` | SHIFT+**F3** |
| Volume down | SHIFT+f4 | `$EB` | `$05F6` = `$C0` | SHIFT+**F4** |
| Volume up | SHIFT+f5 | `$8B` | `$05F6` = `$40` | SHIFT+**F5** |
| Engage Computer Assisted Steering | SHIFT+f6 | `$8A` | `$05F8` = `$80` | SHIFT+**F6** |
| Retire from the race / leave a qualifying period | SHIFT+f7 | `$E9` | `$05F4` = `$20` | SHIFT+**F7** |
| Freeze the game | SHIFT+COPY | `$96` | `$05F7` = `$80` | SHIFT+**Help** |
| Unfreeze | `DELETE` (no SHIFT — the pause spin polls it directly at `$0F1E`) | `$A6` | `$05F7` = `$40` | **Backspace** |
| **Quit the session and go back to the menu** | SHIFT+→ | `$86` | `$05F4` = `$80` | SHIFT+**→** |

Index 2 of the table is a second `$8D` writing `$05F5` = `$C0`; the downward scan reaches index 3
first, so it is unreachable.

### How the four exits differ

`race_main_loop`'s tail (`$1794`) dispatches on `state_flags`, and `enter_session` (`$655C`) on what
is left when the loop returns:

- `$C0` (SHIFT+f0) — leave the driving loop **only if the car is stopped** (`$179F` tests `$0000`);
  otherwise the request is cancelled and you keep driving. Bit 6 is still set, so `enter_session`
  loops and re-asks the wing settings: you are in the pits, still in the same session.
- `$20` (SHIFT+f7) — run `finish_race` (`$1163`), which races the remaining drivers home with the
  player parked, and then leave. Nothing is drawn while it runs, so **the screen holds the last
  painted frame**; with a full field that is a long freeze, and it is not a hang.
- `$80` (SHIFT+→) — leave at once, bit 6 clear, so `enter_session` exits through
  `abort_to_front_end` (`$3273`): a non-local exit straight to the front-end menus. No results.
- `abort_if_quit_keys` (`$3261`) polls SHIFT+→ *directly*, outside the driving loop — from the
  "press SPACE to continue" prompts and the front-end paint loop — so the same combination gets you
  out of those too.

### ⭐ MEASURED on a real BBC (2026-09-08)

`tools/bbc_refloop_race.mjs --press=<codes>@<sec>[:<hold>]` holds negative-INKEY codes down during
a real drive under jsbeeb and samples `state_flags` every second. From a Silverstone practice
session with the throttle held:

```
cd tools/jsbeeb && volta run --node 24.15.0 -- node ../bbc_refloop_race.mjs \
    --drive --frames=200 --press=ff+86@4:2        # SHIFT + →
```

| pressed | what happened |
|---|---|
| `ff` (SHIFT alone) | nothing; frames keep coming — **the control** |
| `ff+86` SHIFT+→ | frames **stop dead at the press and never resume**; the engine drops to ~86 k instructions/s (the MODE 7 front end). The session is over |
| `ff+e9` SHIFT+f7 | `state_flags` → `$20` and drawing stops. In a **field** race it never resumes: `finish_race` is racing the others home. Solo (nobody to race), `finish_race` returns in ~2 s and `$1776`'s `LDA $5F3B / BMI` puts the practice loop back on the road with `state_flags` cleared |
| `ff+df` SHIFT+f0 | nothing visible **while moving** — exactly the stationary gate the handbook describes |
| `ff+a7` SHIFT+`]` | nothing. `]` is a real bracket key; the handbook's `]` is the SAA5050 glyph for → |

## Menus, prompts and text entry

| What | BBC | Amiga |
|---|---|---|
| Confirm / continue at a prompt | `SPACE` (`$9D`) | `Space` |
| Menu option 1 / 2 / 3 (`menu_key_tbl`, `$39E0` — exactly four entries) | `1` `2` `3` | `1` `2` `3` |
| Dismiss the standings tables (see them again) | `RETURN` (`$B6`) | `Return` |
| Circuit menu options 4 / 5 / 6 | — (the BBC's menu is `REVSMEN`, a separate BASIC program) | `4` `5` `6` — **this port's own menu**, `src/platform/trackmenu.c` |
| Wing settings, qualifying minutes, driver name | typed at a console prompt through `OSRDCH` | ⚠ **not implemented** — see below |
| Load the game | `SHIFT+BREAK` | n/a |

⚠⚠ **Text and number entry does not work on the Amiga yet.** `console_read_two_digits` (`$3EE0`)
and the driver-name editor both read through `OSRDCH` (`$FFE0`), and the Amiga backend does not
override `Platform::rdch()` — the default returns CR, so every such prompt answers itself with an
empty line and takes the game's own default. Wing settings therefore cannot be chosen, which is a
real gap, not a design decision. `src/platform/autorun.cpp` documents the same fact from the other
side.

## Fast taps, and why the port lost them

⚠⚠ **The port polls the keyboard once per rendered front-end frame; the BBC polls it at 6502
speed.** `menu_wait_key` (`$6577`) calls the render/tick/poll hooks every iteration before it scans
`menu_key_tbl`, and on the Amiga `PlatformAmiga::renderFrame()` ends in a full PAL-field wait — a
hook the port added (`docs/amiga-arch.md`), not something the 6502 ever did. So one loop pass costs
at least one field.

**Measured** (`amiga/spacerate.gdb`, `make PROBES=1 NOAUTORUN=1` — the `NOAUTORUN` matters, every
probe flag otherwise implies `REVS_AUTORUN_BUILD` and the scripted keyboard walks past the front
end, averaging the race into the figure): **~27 SPACE polls a second, one per ~37 ms**, and one
observed iteration spanned 15 fields (300 ms) — a full-page MODE 7 decode when the SAA5050 flash
phase dirties every row. A *level* poll cannot see a press shorter than its own period, so every
tap under ~37 ms was simply not there when the game looked. That is the whole mechanism behind
"tapping 1-6 or SPACE quickly does nothing".

⭐ **The fix is at the seam, not in either twin.** The CIA-A handler already sees every edge, so
`RevsInput.cpp` latches the down-edge and `keyDown()` answers from the latch when the live level is
clear. Three bounds keep it a fidelity fix rather than a convenience:

- **Front end only** (`g_screenMode7`). In the race both machines poll once per frame, so there is
  no gap to close and no licence to invent one.
- **Consumed on the answer.** One tap answers exactly one poll — otherwise a single tap walks two
  menu rows.
- **Expired after `KEY_LATCH_FIELDS` (4 fields, ~80 ms)** — one poll interval plus margin. A latch
  that outlived the page would surface as a phantom press on the next one.

**Proof** (`amiga/taplatch.gdb`, `make PROBES=1 NOAUTORUN=1 TAPTEST=1`): `REVS_TAPTEST` injects
taps of *zero length* — it writes the latch and never touches `g_keyDown`, so a level poll can
provably not see them. With the latch on, the circuit menu reaches `TM_FINISHED` and the game's
practice menu answers (`session_is_race=28`). The sabotage arm `NOLATCH=1` injects 569 of the same
taps and stalls at `TM_SELECT` with `g_keyLatchHits` 0.

⚠⚠ **gdb CANNOT WRITE this target's memory through the FS-UAE stub.** `set var g_keyLatchHits =
12345`, `set var g_keyDown[0x40] = 1` and `set var s_dwell = 4000` all read back unchanged across a
`continue`. An earlier harness "pressed" keys this way and reported, coherently and wrongly, that
nothing registered. Any input injection must live *inside* the program. `docs/method-lessons.md`.

## The double-press (still open)

⚠ **After choosing practice/competition with 1 or 2 and then SPACE, one more SPACE is needed.**
Distinct from the tap loss above and not fixed by the latch. The leading candidate is
`prompt_wing_settings_core` (`$3C50`), which ends in `wait_dismiss_space_core()` at `$3C6B`; it is
made invisible to the player because the Amiga backend does not override `Platform::rdch()`, so the
two `console_read_two_digits` wing prompts auto-answer instantly and only the SPACE wait shows.
Two engine mechanisms can also swallow a press and both are the 6502's own, so neither is a port
bug by itself:

1. **`menu_wait_key` (`$6577`) scans `menu_key_tbl` DOWNWARD from the option count and stops at
   the first key held — SPACE is index 0, scanned LAST.** Hold the number you just chose and press
   SPACE, and the scan matches the *number* every pass. The number must come up first.
2. **`wait_dismiss` (`$34D2`) debounces: `$34D9` spins while SPACE is DOWN, then `$34E0` waits for
   it to come down again.** A press still held from the previous page satisfies only the up-wait.

⭐ **What settles it:** `make refloop --press=<codes>@<sec>[:<hold>]` gives a real BBC a single
SPACE of realistic hold time at the same page boundary. If the real machine advances on one press
and the port needs two, the fix belongs at the seam, not in either routine.

⚠ Do NOT take `src/platform/autorun.cpp`'s paired SPACE steps as evidence the double-press is
faithful. A script holds a key until its step ends, so it genuinely needs a release step and then
a press; a player's finger is not a script.

## Keys deliberately left unbound

- **Amiga `Del`** is reserved for **BBC BREAK**. BREAK is a reset line, not a keyboard-matrix key,
  so it has no negative-INKEY code and nothing in the map can carry it. The BBC's `DELETE` key is
  Amiga **Backspace**.
- **Amiga `Esc`** carries BBC **ESCAPE** (`$8F`). No engine site polls it today; it is bound so the
  one key whose Amiga equivalent is unambiguous never shows up in `g_keyUnmapped`.
- BBC f8 (`$89`) and f9 (`$88`) are in no table the engine reads, and are not bound.

## Where this lives in the port

`src/platform/amiga/RevsInput.cpp` holds the single BBC-code → Amiga-rawkey table, and it is the
only place the two vocabularies meet. A code the game asks about that the table does not carry is
**counted** (`g_keyUnmapped`, `g_keyUnmappedCode`) rather than silently answered "not held" — a
missing mapping and a wrong mapping look identical from the game's side, and one of them is
otherwise silent forever. `amiga/keytest.gdb` drives the whole path (`g_keyDown` → `keyDown()` →
`OSBYTE 129` → the game's own menu logic) on a headless target with no keyboard.
