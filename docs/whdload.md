# The release — WHDLoad slave, install package, `make dist`

Read before touching `whdload/`, `tools/package_whdload.py`, the release build or the memory
sizes. The user's decisions behind all of it are `docs/phases.md` §Phase 7.

| Path | What |
|---|---|
| `whdload/RevsSlave.s` | the slave — a `kick13.s` kickemu, cross-assembled with vasm (`make -C whdload`) |
| `whdload/whdload/` | WHDLoad's own `kick13.s` / `kickfs.s` / `segtracker.s`, vendored from the Rescue on Fractalus port |
| `whdload/Revs Install/` | the install package, derived from WHDLoad's Install Template through RoF's (Install, ReadMe, icons) |
| `tools/package_whdload.py` | stages and archives `dist/Revs-<version>.lha`; refuses any member carrying REVS2 bytes |
| `tools/test_whdload.py` | the game under WHDLoad in FS-UAE, judged from WHDLoad's core dump |
| `tools/test_installer.py` | the Install script under the real Installer 43.3 in FS-UAE, six disc-image scenarios |
| `make dist` (repo root) | `engine-image` → clean `DIST=1` exe → slave → package |

## What ships and what does not

The archive (0.90: 173 427 bytes; the DIST exe 315 412 — no symbol hunk, `NATIVE_OPT` without
`-funroll-loops`, both in `amiga/Makefile`) holds the exe, the slave, the Install script, the
ReadMe and four icons. **No original engine byte**: the exe reads REVS2 off the player's `.ssd` at startup (`src/platform/engine_image.h`,
proved against `disasm/revs_runtime.bin` by `make engine-image`), and `package_whdload.py` scans every
member for any of REVS2's 256-byte blocks and refuses to build on a hit. The circuit data, the title
page and both fonts DO ship in the exe (user decision).

Installed layout — the slave's `ws_CurrentDir` is `data`, mounted as `DH0:` by `HDINIT`:

```
Revs/Revs.info  Revs.slave  ReadMe  ReadMe.info
Revs/data/Revs  revs.ssd
```

The exe finds the disc as `revs.ssd` (or under either bbcmicro.co.uk file name) in `PROGDIR:`,
`PROGDIR:data/`, then the current directory and `data/` — PROGDIR: only on V36+, so on 1.3 it is
the current directory, which `main()` sets to the icon's drawer on a Workbench launch and which the
slave's `data` directory already is.

## The slave

A **kickemu**, for the reason RoF's `docs/whdload-slave.md` §0 records: the exe is an ordinary
AmigaDOS program whose takeover is OS calls, and it now also reads a file with dos.library at
startup. `kick13.s` boots a real Kickstart 1.3 inside WHDLoad's memory; `_bootdos` LoadSegs `Revs`
and calls it as a CLI process. Unlike RoF there is nothing to patch (no save file, no options), so
the slave is only `_bootdos`. A return of 20 is the exe refusing the disc image, turned into a
`TDREASON_FAILMSG`; anything else aborts `TDREASON_OK`.

**Quit**: CTRL-Q (the game). The WHDLoad QuitKey is keypad `*` — **not F10, which is the BBC's f0**
(SHIFT+F10 returns to the pits) — and WHDLoad sees it only through a moved VBR, i.e. a 68010+.

## Memory

Measured with `amiga/mem_target.gdb` (exec's free lists walked from gdb once the race runs), 2026-09-30:

| Machine | chip free | other free |
|---|---|---|
| 512 KB chip + 512 KB slow, Kickstart 3.1, race running | 185 KB | 116 KB (slow) |

⇒ the plain exe runs on the user's target, a 1 MB machine (decision 3), with ~300 KB spare — and
3.1 takes more of both pools than 1.3 does, so the 1.3 margin is larger. ⚠ FS-UAE cannot boot 1.3
from a directory drive, which is why that row is 3.1; the Kickstart 1.3 run is the WHDLoad one.

The slave asks for `CHIPMEMSIZE` 400 KB + `FASTMEMSIZE` 512 KB + the 256 KB 1.3 image, so like
RoF's the WHDLoad install needs a **2 MB** machine; the plain exe does not.

## The WHDLoad tests (`tools/test_whdload.py`, WHDLoad 19.2, 2 MB A500, 2026-09-30)

| Mode | CPU | Result |
|---|---|---|
| `run` — the DIST exe, WHDLoad `TIMEOUT` then core dump | 68010 | PASS: 65 of 65 engine code blocks verbatim in the dump — REVS2 read off `data/revs.ssd` and rebuilt on the 1.3 kickemu |
| `run` and `nodisc` — the exe and slave EXTRACTED FROM `Revs-0.90.lha` | 68010 / 68000 | PASS, same |
| `quit` — a `QUITTEST=1500` exe (CTRL-Q held from field 1500) | 68000 | PASS: "Return OK." |
| `nodisc` — no `revs.ssd` | 68000 | PASS: WHDLoad shows the slave's "Revs could not start…" |

⚠ `run` needs a **68010**: WHDLoad's `TIMEOUT`, like its QuitKey, is only seen through a moved VBR,
and the game owns the VERTB vector — on a 68000 the run simply never ends (measured: no dump in
400 s). ⚠ And without `FILELOG`: logging every one of LoadSeg's small reads on a 68000 outlasted a
150 s ceiling before the game started.

## The Install script (`tools/test_installer.py`)

✅ **The user ran the install on their own setup and it works as expected** (2026-09-30, before the
disc-image reuse below existed).

**Reusing an installed disc image.** When `data/revs.ssd` is already in the target drawer (and is
204800 bytes), the script asks "Use existing" / "Select again" *before* the drawer question, and
"Use existing" skips the file requester. The drawer question defaults to **Delete**, so a kept image
is parked beside the drawer as `Revs.revs-ssd` while the drawer is deleted and moved back into the
new `data/`; a cancelled install that leaves it parked gets it offered next time. Modelled on the
Vette port's "Reinstall / Use existing".

`tools/test_installer.py` runs Commodore's Installer 43.3 on the real script in FS-UAE (an A1200,
Kickstart 3.1, the Workbench 2.04 floppy), with the requesters answered from a scenario and every
`message` logged instead of shown. Six scenarios — `fresh`, `keep-delete`, `keep-skip`,
`again-delete`, `stash`, `bad-size` — each check which questions were asked, which `revs.ssd` was
installed, whether the old drawer's contents survived and that the exe, slave and icon are the new
ones. ✅ all six PASS; the pre-change script FAILS `keep-delete` and `keep-skip` (sabotage).
Two traps it hit:

- ⚠ **A boot without `ENV:` hangs the Installer on "Please insert volume ENV:"** at the script's
  first `getenv` — a hand-written startup-sequence must make `ENV:` and `ENVARC:`.
- ⚠ **Installer's `textfile` fails when its file already exists** (a requester, so another hang):
  every logged form writes a file of its own.

The interactive half — the requesters' wording and layout — is still checked by eye: re-run the
install by hand after changing a prompt.
