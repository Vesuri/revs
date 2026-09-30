# Engine source ownership

This directory contains both generated translation output and maintained native
code. Generation requires the local disc-derived listings; see
[the toolchain guide](../../docs/toolchain.md).

| Files | Owner | Tracked |
|---|---|---|
| `revs_native.c` | Handwritten typed engine cores | Yes |
| `revs_native_seam.c`, `revs_native_seam.h` | Maintained ABI seams and shared declarations | Yes |
| `revs_native_abi.c` | Oracle-only 6502 ABI shims | Yes |
| `revs_gen.c`, `revs_decl.h` | Transpiler output | No |
| `mem.h` | Names generated from `disasm/symbols.csv` | No |
| `revs_validate_list.h`, `revs_smc_bytes.h` | Validation and SMC metadata | No |
| `revs_tracks.c`, `revs_tracks.h` | Generated circuit data | No |
| `revs_track_hooks.c`, `revs_track_hooks.h` | Generated circuit hook code | No |

Run `make gen` from the repository root. Never edit generated output by hand.
The native seam files are maintained source, not regeneration targets.
The build also generates `src/platform/titlescreen.h` from the disc.

See [the fidelity boundary](../../docs/faithfulness-seam.md) before changing
which implementation owns a routine.
