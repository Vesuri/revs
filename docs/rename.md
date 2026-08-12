# Misnamed functions — the rename backlog

**Convention:** whenever you encounter a function whose name clearly contradicts what it does,
**append a row here immediately.**  Do not rename piecemeal in generated files —
`disasm/symbols.csv` is the source of truth, and a batch rename via the transpiler is cheap.

Why immediately, and why this file exists before there is anything in it: on the Atari port this
backlog accumulated because renames were deferred, and **on a binary-only project the function
names are your map** — every wrong name taxes every later reasoning step (postmortem #1.2).  The
counter-measure is one concentrated naming pass up front (`docs/phases.md` Phase 2.4) plus this
file for everything found afterwards.

| Addr | Current name | What it actually does | Suggested name |
|---|---|---|---|
| — | — | *(empty — the binary has not been disassembled yet)* | — |
