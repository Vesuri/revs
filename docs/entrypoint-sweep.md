# The entry-point sweep — do this BEFORE generating any C

> ⭐ **Postmortem finding #1.1 — the single highest-leverage item on this project.**  On the
> Atari port, code reachable only through indirect jumps was discovered *piecemeal over the
> life of the project*: interrupt handlers, an event dispatcher, RTS-trick dispatch tables.
> Two whole conventions there (`entrypoints.csv`, "record a newly-found handler the moment you
> find it") are scars from getting burned repeatedly.  The failure mode is specific and
> expensive: **you reason statically about a handler Ghidra never disassembled**, and every
> conclusion drawn from that reasoning is wrong.
>
> The postmortem also says this bites *harder* here: the BBC MOS leans on indirect vectors, and
> Revs is known for self-modifying code and dispatch tricks.

## The rule

**`disasm/listing.txt` must be COMPLETE before the first line of C is generated.**  Not
"complete enough to start" — complete.  The set of dispatch patterns in a 6502 binary is finite
and enumerable, so this is a bounded, one-time job, and doing it up front costs a fraction of
what discovering each site reactively costs.

## What to enumerate and seed as a Ghidra entry point

Record every hit in `ghidra_scripts/entrypoints.csv` (`addr,name,note`) as it is found — that
file is what makes the seeding reproducible across a re-import, which is the whole point.

### 1. Indirect and computed jumps
- `JMP ($xxxx)` — and the word table it reads.  Seed EVERY entry of the table, not just the
  ones you can see being taken.
- `JMP (addr,X)` style computed jumps built from a table + an index.
- **RTS-trick dispatch**: `PHA` of a hi byte, `PHA` of a lo byte, `RTS`.  Grep for `PHA`
  pairs followed by `RTS`; the pushed address is `target-1`.
- `JSR` through a RAM pointer that some other routine writes (a hand-rolled vtable).
- Jump tables consisting of `JMP abs` instructions, indexed by a multiply-by-3.

### 2. BBC MOS / OS vectors — all of them, as roots
The BBC's vector page is where the MOS lets a program interpose.  Any of these that Revs
writes is an entry point into Revs's own code:

| Addr | Vector | Why it matters |
|---|---|---|
| `$0202/03` | **BRKV**  | error handler |
| `$0204/05` | **IRQ1V** | the primary IRQ claim — where a 50 Hz game body would live |
| `$0206/07` | **IRQ2V** | unrecognised-interrupt chain |
| `$0208/09` | CLIV | `*` command |
| `$020A/0B` | BYTEV | OSBYTE |
| `$020C/0D` | WORDV | OSWORD |
| `$020E/0F` | WRCHV | OSWRCH |
| `$0210/11` | RDCHV | OSRDCH |
| `$0212`… | FILEV, ARGSV, BGETV, BPUTV, GBPBV, FINDV, FSCV | filing system |
| `$0220/21` | **EVNTV** | the event vector — vsync/timer events |
| `$0222/23` | UPTV, NETV, VDUV, KEYV, INSV, REMV, CNPV, IND1-3V | keyboard/buffer hooks |

Plus the 6502's own hardware vectors at the top of memory: **NMI `$FFFA/FB`, RESET
`$FFFC/FD`, IRQ/BRK `$FFFE/FF`** — and, on the BBC specifically, the RAM NMI area at
`$0D00`, which the disc filing system uses.

### 3. Everything that is jumped to but never called
After a first pass, cross-check: any address referenced by a `.word` in a data region and
otherwise unreferenced is almost certainly a dispatch target.

## The self-modifying-code corollary

Revs is expected to use self-modifying code.  Two consequences, both learned on the Atari port:

- **`listing.txt` is the FINAL image, not the running image.**  A routine's bytes at address X
  during one phase may be a *different routine* at the same address during another.  Never
  assume the listing shows what executes at a given moment.
- **Scan the raw binary, not just `listing.txt`**, when hunting for a pattern.  A byte sequence
  Ghidra classified as data will not appear in the listing at all.

A routine that writes to its own instruction stream cannot be transliterated faithfully by the
transpiler; it gets a hand-written stub in `src/gen/revs_manual.c` (the same seam the Atari port
used for its self-modifying handlers).  Identify these during this sweep, not later.

## Definition of done

- [ ] Every `JMP ($..)` / computed jump / RTS-dispatch site found, and its table fully seeded.
- [ ] Every OS vector Revs writes identified, with the handler seeded.
- [ ] Hardware vectors seeded.
- [ ] Every self-modifying routine identified and listed (destined for `revs_manual.c`).
- [ ] `ghidra_scripts/entrypoints.csv` reproduces the whole set on a fresh import.
- [ ] A re-export of `listing.txt` shows no remaining "referenced but not disassembled" address.

Only then generate C.
