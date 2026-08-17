# The rename QUEUE — open items only

Names and cells that contradict what they do, or have no name yet.  Applied entries are DELETED
from this file in the same commit that applies them; `disasm/symbols.csv` carries the name and its
evidence afterwards.  The conventions that govern this queue are in `CLAUDE.md` §Working
conventions, not here.

---

## 1. The per-line buffer family at `$0400`/`$0450`/`$0554`/`$05A4`/`$0600`/`$0650`

`view_left_start_src` (`$0504`) turned out to be indexed by the SCAN LINE, and it sits in the
middle of this family — whose members are named as if they were per-COLUMN.  Six buffers, all in
page 4/5/6 at $50-byte spacing, so they are almost certainly one family with one index; if that
index is the line, every one of those names is inverted.  Settle it from the writers (start with
`$1DA6`, which fills `$0504` per line, and with whatever writes `$0450`) before renaming any of
them, because they are read from the plotters and a wrong index in a name is worse than none.

## 2. `$0164,X` / `$0178,X` — the two per-driver quantities `place_player_in_section` computes

Named-by-address only, and one of the two is the car's position *along* its track section and the
other is *across* it — `[INFERRED]`, which of them is which is not settled.  The evidence to use:
`$0178,X` is compared BETWEEN cars at `$271E`-`$2762`, stepped by ±1 by the other-car AI at
`$28A5`-`$28DF`, seeded to `$50 EOR $FF` for all twenty drivers at `$1100`, and masked with `$7F`
into `$0114,X` when `$11AB` spins a car out; `$0164,X` is differenced between two cars at `$27A4`
and accumulated at `$2887`, and `reset_driving_variables` zeroes both.  A ±1 step per frame reads
like a discrete lane/segment index, which would make `$0178` the "across" one — the opposite of the
first guess.  Cheap way to settle it: park the car and drive it straight, and watch which of the
two moves.

