| Asset embedding for the Revs Amiga build (the equivalent of Bin2Hunk).
|
| Paths are resolved relative to the amiga/ build directory (ASFLAGS -I"$(CURDIR)").
|
| ⚠ .incbin dependencies are INVISIBLE to `gcc -MMD`, so every file listed here must
| also be listed in the Makefile's INCBIN_DEPS.  Without that, editing or regenerating
| an embedded blob leaves a STALE copy in the binary while the on-disk file looks
| correct.  On the Atari port this exact trap shifted an embedded data block by one byte
| and cost a long debugging session.

	.section .rodata
	.align 4

| revs_runtime.bin — the 6502 memory image AFTER REVS2's own startup unpack, built by
| tools/relocate.py (`make runtime`).  Embedding it (rather than loading from disc at
| runtime) means every build boots the SAME initial state and the SAME code path, which
| is what makes a cross-build comparison mean anything at all.
|
| ⚠⚠ This must be the RUNTIME image, not revs_mem.bin.  REVS2 relocates itself before
| running, and src/gen/revs_gen.c is generated from a disassembly of the relocated
| layout — so booting the pre-unpack image would run correct code against a memory map
| where every address means something else.  The transliteration REPLACES the unpack
| stub; it does not execute it.  docs/static-map.md.
	.global revs_runtime_bin
	.global revs_runtime_bin_end
revs_runtime_bin:
	.incbin "../disasm/revs_runtime.bin"
revs_runtime_bin_end:
